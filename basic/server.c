#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

// 引入日志和监控模块
#include "logger.h"
#include "monitor.h"

#ifdef ENABLE_EBPF
#include "sockmap_loader.h"
#endif

#define PORT 8888                                   // 监听端口
#define MAX_EVENTS 1024                             // epoll 最大事件数
#define BUFFER_SIZE 4096                            // 缓冲区大小
#define BACKLOG 512                                 // listen 队列长度
#define CONTROL_SOCKET "/tmp/tcp_echo_server.sock"  // 控制 socket 路径

// 全局变量
static volatile int running = 1;   // 运行标志
static Logger *g_logger = NULL;    // 全局日志实例
static Monitor *g_monitor = NULL;  // 全局监控实例

#ifdef ENABLE_EBPF
static sockmap_loader_t *g_sockmap = NULL;  // eBPF Sockmap 加载器
#define EBPF_OBJ_PATH "./out/ebpf/sockmap.bpf.o"
#endif

// 服务器统计信息
typedef struct {
    long long total_connections;   // 总连接数
    long long active_connections;  // 当前活跃连接数
    long long total_requests;      // 总请求数
    long long total_bytes_recv;    // 总接收字节数
    long long total_bytes_sent;    // 总发送字节数
    long long start_time_us;       // 启动时间
} ServerStats;

static ServerStats g_stats = {0};  // 全局统计

// 信号处理函数
void signal_handler(int signum) {
    LOG_INFO(g_logger, "收到信号 %d，正在关闭服务器...", signum);
    running = 0;
}

// 将文件描述符设置为非阻塞模式
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        if (g_logger) {
            LOG_ERROR(g_logger, "fcntl F_GETFL 失败: %s", strerror(errno));
        }
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        if (g_logger) {
            LOG_ERROR(g_logger, "fcntl F_SETFL 失败: %s", strerror(errno));
        }
        return -1;
    }
    return 0;
}

// 设置 SO_REUSEADDR
int set_reuseaddr(int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "setsockopt SO_REUSEADDR 失败: %s", strerror(errno));
        }
        return -1;
    }
    return 0;
}

// 设置 TCP_NODELAY
int set_nodelay(int fd) {
    int opt = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "setsockopt TCP_NODELAY 失败: %s", strerror(errno));
        }
        return -1;
    }
    return 0;
}

// 创建、绑定 listening socket
int create_and_bind() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR(g_logger, "socket 创建失败: %s", strerror(errno));
        return -1;
    }

    if (set_reuseaddr(listen_fd) < 0) {
        close(listen_fd);
        return -1;
    }

    if (set_nonblocking(listen_fd) < 0) {
        close(listen_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR(g_logger, "bind 失败: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

// 处理新连接
int handle_accept(int listen_fd, int epoll_fd) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                LOG_ERROR(g_logger, "accept 失败: %s", strerror(errno));
                return -1;
            }
        }

        if (set_nonblocking(client_fd) < 0) {
            close(client_fd);
            continue;
        }

        if (set_nodelay(client_fd) < 0) {
            close(client_fd);
            continue;
        }

        struct epoll_event event;
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLET;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            LOG_ERROR(g_logger, "epoll_ctl 失败: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        g_stats.total_connections++;
        g_stats.active_connections++;

#ifdef ENABLE_EBPF
        // 将 socket 添加到 sockmap
        if (g_sockmap) {
            if (sockmap_loader_add_socket(g_sockmap, client_fd) < 0) {
                LOG_WARN(g_logger, "Failed to add socket %d to sockmap", client_fd);
            } else {
                LOG_DEBUG(g_logger, "Socket %d added to sockmap", client_fd);
            }
        }
#endif

        LOG_DEBUG(g_logger, "接受新连接：fd=%d (总连接=%lld, 活跃=%lld)", client_fd, g_stats.total_connections,
                  g_stats.active_connections);
    }
    return 0;
}

// 处理客户端数据（Echo 逻辑）
int handle_read(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t total_read = 0;

    while (1) {
        ssize_t n = read(client_fd, buffer, sizeof(buffer));

        if (n > 0) {
            total_read += n;
            g_stats.total_bytes_recv += n;
            g_stats.total_requests++;

            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(client_fd, buffer + written, n - written);
                if (w < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    LOG_ERROR(g_logger, "write 失败: %s", strerror(errno));
                    return -1;
                }
                written += w;
                g_stats.total_bytes_sent += w;
            }
        } else if (n == 0) {
            LOG_DEBUG(g_logger, "客户端关闭连接：fd=%d", client_fd);
            g_stats.active_connections--;
#ifdef ENABLE_EBPF
            // 从 sockmap 移除 socket
            if (g_sockmap) {
                sockmap_loader_remove_socket(g_sockmap, client_fd);
            }
#endif
            return -1;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                LOG_ERROR(g_logger, "read 失败: %s", strerror(errno));
                g_stats.active_connections--;
#ifdef ENABLE_EBPF
                // 从 sockmap 移除 socket
                if (g_sockmap) {
                    sockmap_loader_remove_socket(g_sockmap, client_fd);
                }
#endif
                return -1;
            }
        }
    }

    return 0;
}

// 控制接口处理函数
void *control_thread(void *arg) {
    (void)arg;

    // 删除旧的 socket 文件
    unlink(CONTROL_SOCKET);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR(g_logger, "控制 socket 创建失败: %s", strerror(errno));
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCKET, sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR(g_logger, "控制 socket bind 失败: %s", strerror(errno));
        close(sock);
        return NULL;
    }

    if (listen(sock, 5) < 0) {
        LOG_ERROR(g_logger, "控制 socket listen 失败: %s", strerror(errno));
        close(sock);
        return NULL;
    }

    LOG_INFO(g_logger, "控制接口启动: %s", CONTROL_SOCKET);

    while (running) {
        int client = accept(sock, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        char cmd[256] = {0};
        ssize_t n = read(client, cmd, sizeof(cmd) - 1);
        if (n <= 0) {
            close(client);
            continue;
        }

        // 移除换行符
        cmd[strcspn(cmd, "\r\n")] = 0;

        LOG_INFO(g_logger, "收到控制命令: %s", cmd);

        char response[4096];
        if (strcmp(cmd, "stats") == 0) {
            // 获取当前系统状态
            SystemStats sys_stats;
            monitor_collect(g_monitor, &sys_stats);

            long long uptime_sec = (monitor_get_time_us() - g_stats.start_time_us) / 1000000;

            snprintf(response, sizeof(response),
                     "{\n"
                     "  \"status\": \"running\",\n"
                     "  \"uptime_sec\": %lld,\n"
                     "  \"connections\": {\n"
                     "    \"total\": %lld,\n"
                     "    \"active\": %lld\n"
                     "  },\n"
                     "  \"traffic\": {\n"
                     "    \"total_requests\": %lld,\n"
                     "    \"bytes_recv\": %lld,\n"
                     "    \"bytes_sent\": %lld\n"
                     "  },\n"
                     "  \"system\": {\n"
                     "    \"cpu_percent\": %.2f,\n"
                     "    \"memory_rss_mb\": %.2f,\n"
                     "    \"threads\": %ld\n"
                     "  }\n"
                     "}\n",
                     uptime_sec, g_stats.total_connections, g_stats.active_connections, g_stats.total_requests,
                     g_stats.total_bytes_recv, g_stats.total_bytes_sent, sys_stats.cpu_usage_percent,
                     sys_stats.memory_rss_kb / 1024.0, sys_stats.num_threads);
        } else if (strcmp(cmd, "shutdown") == 0) {
            snprintf(response, sizeof(response), "{\"status\": \"shutting_down\"}\n");
            write(client, response, strlen(response));
            close(client);
            running = 0;
            break;
        } else {
            snprintf(response, sizeof(response), "{\"error\": \"unknown_command\", \"cmd\": \"%s\"}\n", cmd);
        }

        write(client, response, strlen(response));
        close(client);
    }

    close(sock);
    unlink(CONTROL_SOCKET);
    return NULL;
}

int main() {
    // 初始化日志系统
    char log_filename[256];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(log_filename, sizeof(log_filename), "test/logs/server_%04d%02d%02d_%02d%02d%02d.log",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min,
             tm_info->tm_sec);

    g_logger = logger_init(log_filename, LOG_INFO, 1, "server");
    if (!g_logger) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    // 初始化监控
    g_monitor = monitor_init();
    if (!g_monitor) {
        LOG_ERROR(g_logger, "Failed to initialize monitor");
        logger_close(g_logger);
        return 1;
    }

    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "    高性能 Epoll Echo Server");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "端口: %d", PORT);
    LOG_INFO(g_logger, "最大事件数: %d", MAX_EVENTS);
    LOG_INFO(g_logger, "缓冲区大小: %d 字节", BUFFER_SIZE);
    LOG_INFO(g_logger, "日志文件: %s", log_filename);
#ifdef ENABLE_EBPF
    LOG_INFO(g_logger, "eBPF Sockmap: 已启用");
#else
    LOG_INFO(g_logger, "eBPF Sockmap: 未启用");
#endif

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE

    // 创建并绑定 listening socket
    int listen_fd = create_and_bind();
    if (listen_fd < 0) {
        monitor_destroy(g_monitor);
        logger_close(g_logger);
        return 1;
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        LOG_ERROR(g_logger, "listen 失败: %s", strerror(errno));
        close(listen_fd);
        monitor_destroy(g_monitor);
        logger_close(g_logger);
        return 1;
    }

    LOG_INFO(g_logger, "服务器监听在 127.0.0.1:%d", PORT);

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR(g_logger, "epoll_create1 失败: %s", strerror(errno));
        close(listen_fd);
        monitor_destroy(g_monitor);
        logger_close(g_logger);
        return 1;
    }

    // 把 listening socket 加入 epoll
    struct epoll_event event;
    event.data.fd = listen_fd;
    event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        LOG_ERROR(g_logger, "epoll_ctl 失败: %s", strerror(errno));
        close(listen_fd);
        close(epoll_fd);
        monitor_destroy(g_monitor);
        logger_close(g_logger);
        return 1;
    }

    // 启动控制线程
    pthread_t ctrl_thread;
    if (pthread_create(&ctrl_thread, NULL, control_thread, NULL) != 0) {
        LOG_ERROR(g_logger, "创建控制线程失败: %s", strerror(errno));
    } else {
        pthread_detach(ctrl_thread);
    }

    // 记录启动时间
    g_stats.start_time_us = monitor_get_time_us();

#ifdef ENABLE_EBPF
    // 初始化 eBPF Sockmap
    LOG_INFO(g_logger, "正在加载 eBPF Sockmap...");
    g_sockmap = sockmap_loader_init(EBPF_OBJ_PATH);
    if (!g_sockmap) {
        LOG_ERROR(g_logger, "Failed to initialize eBPF sockmap");
        LOG_WARN(g_logger, "将使用传统方式运行（无 eBPF 加速）");
    } else {
        LOG_INFO(g_logger, "eBPF Sockmap 加载成功");
    }
#endif

    struct epoll_event events[MAX_EVENTS];
    LOG_INFO(g_logger, "Epoll 循环启动");

    // 事件循环
    while (running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR(g_logger, "epoll_wait 失败: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                if (handle_accept(listen_fd, epoll_fd) < 0) {
                    LOG_ERROR(g_logger, "accept 失败");
                }
            } else {
                if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
                    close(events[i].data.fd);
                    g_stats.active_connections--;
                    continue;
                }

                if (handle_read(events[i].data.fd) < 0) {
                    close(events[i].data.fd);
                }
            }
        }
    }

    // 清理
    LOG_INFO(g_logger, "");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "         服务器统计");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "总连接数:         %lld", g_stats.total_connections);
    LOG_INFO(g_logger, "总请求数:         %lld", g_stats.total_requests);
    LOG_INFO(g_logger, "总接收字节:       %lld", g_stats.total_bytes_recv);
    LOG_INFO(g_logger, "总发送字节:       %lld", g_stats.total_bytes_sent);

#ifdef ENABLE_EBPF
    // 打印 eBPF 统计信息
    if (g_sockmap) {
        unsigned long long redirected = 0, redirect_err = 0, parsed = 0, parse_err = 0;
        sockmap_loader_get_stats(g_sockmap, &redirected, &redirect_err, &parsed, &parse_err);
        LOG_INFO(g_logger, "----------------------------------------");
        LOG_INFO(g_logger, "eBPF Sockmap 统计:");
        LOG_INFO(g_logger, "  重定向成功:     %llu", redirected);
        LOG_INFO(g_logger, "  重定向失败:     %llu", redirect_err);
        LOG_INFO(g_logger, "  解析成功:       %llu", parsed);
        LOG_INFO(g_logger, "  解析失败:       %llu", parse_err);
        if (redirected + redirect_err > 0) {
            double success_rate = (redirected * 100.0) / (redirected + redirect_err);
            LOG_INFO(g_logger, "  重定向成功率:   %.2f%%", success_rate);
        }
    }
#endif

    // 获取最终系统状态
    SystemStats final_stats;
    monitor_collect(g_monitor, &final_stats);
    LOG_INFO(g_logger, "----------------------------------------");
    LOG_INFO(g_logger, "最终 CPU 使用率:  %.2f%%", final_stats.cpu_usage_percent);
    LOG_INFO(g_logger, "最终 RSS 内存:    %.2f MB", final_stats.memory_rss_kb / 1024.0);
    LOG_INFO(g_logger, "========================================");

#ifdef ENABLE_EBPF
    // 清理 eBPF 资源
    if (g_sockmap) {
        sockmap_loader_destroy(g_sockmap);
        LOG_INFO(g_logger, "eBPF Sockmap 已清理");
    }
#endif

    close(listen_fd);
    close(epoll_fd);
    monitor_destroy(g_monitor);
    logger_close(g_logger);

    return 0;
}
