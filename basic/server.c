#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <liburing.h>

// 引入日志和监控模块
#include "logger.h"
#include "monitor.h"

#ifdef ENABLE_EBPF
#include "sockmap_loader.h"
#endif

#define PORT 8888
#define QUEUE_DEPTH 4096  // io_uring 队列深度
#define BUFFER_SIZE 4096
#define BACKLOG 4096
#define CONTROL_SOCKET "/tmp/tcp_echo_server.sock"

// ==========================================
// io_uring 上下文定义
// ==========================================

typedef enum { EVENT_ACCEPT, EVENT_READ, EVENT_WRITE } EventType;

// 每个 I/O 操作的上下文
typedef struct {
    int fd;
    EventType type;
    char buffer[BUFFER_SIZE];
    struct iovec iov;
    struct msghdr msg;  // 用于 sendmsg/recvmsg (可选，这里用 readv/writev 简化)
} IoContext;

// ==========================================
// 配置与统计结构
// ==========================================

static int g_worker_count = 0;

typedef struct {
    long long total_connections;
    long long active_connections;
    long long total_requests;
    long long total_bytes_recv;
    long long total_bytes_sent;
    char padding[64];
} __attribute__((aligned(64))) ThreadStats;

typedef struct {
    int thread_id;
    struct io_uring ring;  // 每个线程一个 io_uring 实例
    pthread_t thread_handle;
    ThreadStats stats;
} WorkerContext;

static volatile int running = 1;
static Logger *g_logger = NULL;
static Monitor *g_monitor = NULL;
static WorkerContext *g_workers = NULL;
static long long g_start_time_us = 0;

#ifdef ENABLE_EBPF
static sockmap_loader_t *g_sockmap = NULL;
#define EBPF_OBJ_PATH "./out/ebpf/sockmap.bpf.o"
#endif

// ==========================================
// 工具函数
// ==========================================
int ensure_directory_exists(const char *path) {
    char temp[256];
    char *p = NULL;
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);
    if (temp[len - 1] == '/')
        temp[len - 1] = 0;
    for (p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(temp, S_IRWXU) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

void signal_handler(int signum) {
    if (g_logger)
        LOG_INFO(g_logger, "收到信号 %d，正在关闭服务器...", signum);
    running = 0;
}

int set_reuseport(int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        return -1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

// ==========================================
// io_uring 辅助函数
// ==========================================

// 准备 Accept 请求
void add_accept_request(struct io_uring *ring, int server_fd, struct sockaddr *client_addr, socklen_t *client_len,
                        IoContext *ctx) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // 如果 SQ 满了，sqe 可能为 NULL，生产环境需要处理这种情况（通常是提交并重试）
    if (!sqe)
        return;

    io_uring_prep_accept(sqe, server_fd, client_addr, client_len, 0);
    ctx->fd = server_fd;
    ctx->type = EVENT_ACCEPT;
    io_uring_sqe_set_data(sqe, ctx);
}

// 准备 Read 请求
void add_read_request(struct io_uring *ring, int client_fd, IoContext *ctx) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return;

    ctx->iov.iov_base = ctx->buffer;
    ctx->iov.iov_len = BUFFER_SIZE;
    ctx->fd = client_fd;
    ctx->type = EVENT_READ;

    io_uring_prep_readv(sqe, client_fd, &ctx->iov, 1, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

// 准备 Write 请求
void add_write_request(struct io_uring *ring, int client_fd, IoContext *ctx, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return;

    ctx->iov.iov_base = ctx->buffer;
    ctx->iov.iov_len = len;
    ctx->fd = client_fd;
    ctx->type = EVENT_WRITE;

    io_uring_prep_writev(sqe, client_fd, &ctx->iov, 1, 0);
    io_uring_sqe_set_data(sqe, ctx);
}

// ==========================================
// Socket 创建
// ==========================================
int create_listener() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (set_reuseport(fd) < 0) {
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, BACKLOG) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// ==========================================
// 工作线程 (Worker) - io_uring 核心循环
// ==========================================
void *worker_routine(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;
    int thread_id = ctx->thread_id;

    // 1. CPU 亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int cpu_count = monitor_get_cpu_count();
    int cpu_id = thread_id % cpu_count;
    CPU_SET(cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    LOG_INFO(g_logger, "[Worker %d] 绑定 CPU %d", thread_id, cpu_id);

    // 2. 初始化 io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ctx->ring, 0) < 0) {
        LOG_ERROR(g_logger, "[Worker %d] io_uring_queue_init 失败", thread_id);
        return NULL;
    }

    // 3. 创建监听 Socket
    int listen_fd = create_listener();
    if (listen_fd < 0) {
        LOG_ERROR(g_logger, "[Worker %d] 创建监听 Socket 失败: %s", thread_id, strerror(errno));
        return NULL;
    }

    // 4. 提交第一个 Accept 请求
    IoContext *listener_ctx = malloc(sizeof(IoContext));
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    add_accept_request(&ctx->ring, listen_fd, (struct sockaddr *)&client_addr, &client_len, listener_ctx);
    io_uring_submit(&ctx->ring);

    struct io_uring_cqe *cqe;

    // 5. 事件循环
    while (running) {
        struct __kernel_timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;

        int ret = io_uring_wait_cqe_timeout(&ctx->ring, &cqe, &ts);

        if (ret == -ETIME) {
            continue;
        }

        if (ret < 0) {
            if (ret == -EINTR)
                continue;
            LOG_ERROR(g_logger, "[Worker %d] io_uring_wait_cqe 错误: %d", thread_id, ret);
            break;
        }

        unsigned head;
        int count = 0;

        io_uring_for_each_cqe(&ctx->ring, head, cqe) {
            count++;
            IoContext *req_ctx = (IoContext *)io_uring_cqe_get_data(cqe);
            int res = cqe->res;

            if (res < 0 && res != -EAGAIN) {
                if (req_ctx->type != EVENT_ACCEPT) {
                    close(req_ctx->fd);
                    free(req_ctx);
                    ctx->stats.active_connections--;
                }
            } else {
                switch (req_ctx->type) {
                case EVENT_ACCEPT: {
                    int client_fd = res;
                    if (client_fd >= 0) {
                        ctx->stats.total_connections++;
                        ctx->stats.active_connections++;
                        IoContext *client_ctx = malloc(sizeof(IoContext));
                        if (client_ctx) {
                            add_read_request(&ctx->ring, client_fd, client_ctx);
#ifdef ENABLE_EBPF
                            if (g_sockmap)
                                sockmap_loader_add_socket(g_sockmap, client_fd);
#endif
                        } else {
                            close(client_fd);
                        }
                    }
                    client_len = sizeof(client_addr);
                    add_accept_request(&ctx->ring, listen_fd, (struct sockaddr *)&client_addr, &client_len,
                                       listener_ctx);
                    break;
                }
                case EVENT_READ: {
                    int bytes_read = res;
                    if (bytes_read <= 0) {
                        close(req_ctx->fd);
#ifdef ENABLE_EBPF
                        if (g_sockmap)
                            sockmap_loader_remove_socket(g_sockmap, req_ctx->fd);
#endif
                        free(req_ctx);
                        ctx->stats.active_connections--;
                    } else {
                        ctx->stats.total_bytes_recv += bytes_read;
                        ctx->stats.total_requests++;
                        add_write_request(&ctx->ring, req_ctx->fd, req_ctx, bytes_read);
                    }
                    break;
                }
                case EVENT_WRITE: {
                    int bytes_written = res;
                    if (bytes_written > 0) {
                        ctx->stats.total_bytes_sent += bytes_written;
                    }
                    add_read_request(&ctx->ring, req_ctx->fd, req_ctx);
                    break;
                }
                }
            }
        }

        io_uring_cq_advance(&ctx->ring, count);
        io_uring_submit(&ctx->ring);
    }

    free(listener_ctx);
    close(listen_fd);
    io_uring_queue_exit(&ctx->ring);
    return NULL;
}

// ==========================================
// 控制线程
// ==========================================
void *control_thread(void *arg) {
    (void)arg;
    unlink(CONTROL_SOCKET);
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCKET, sizeof(addr.sun_path) - 1);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return NULL;
    listen(sock, 5);

    while (running) {
        int client = accept(sock, NULL, NULL);
        if (client < 0)
            continue;

        char cmd[256] = {0};
        if (read(client, cmd, sizeof(cmd) - 1) > 0) {
            cmd[strcspn(cmd, "\r\n")] = 0;
            char response[4096];

            if (strcmp(cmd, "stats") == 0) {
                long long total_conn = 0, active_conn = 0, total_req = 0, rx = 0, tx = 0;
                for (int i = 0; i < g_worker_count; i++) {
                    total_conn += g_workers[i].stats.total_connections;
                    active_conn += g_workers[i].stats.active_connections;
                    total_req += g_workers[i].stats.total_requests;
                    rx += g_workers[i].stats.total_bytes_recv;
                    tx += g_workers[i].stats.total_bytes_sent;
                }

                SystemStats sys_stats;
                monitor_collect(g_monitor, &sys_stats);
                long long uptime = (monitor_get_time_us() - g_start_time_us) / 1000000;

                snprintf(response, sizeof(response),
                         "{\"status\":\"running\",\"mode\":\"io_uring\",\"uptime\":%lld,"
                         "\"connections\":{\"total\":%lld,\"active\":%lld},"
                         "\"traffic\":{\"requests\":%lld,\"rx\":%lld,\"tx\":%lld},"
                         "\"system\":{\"cpu\":%.2f,\"mem_mb\":%.2f,\"threads\":%d}}\n",
                         uptime, total_conn, active_conn, total_req, rx, tx, sys_stats.cpu_usage_percent,
                         sys_stats.memory_rss_kb / 1024.0, g_worker_count);
            } else if (strcmp(cmd, "shutdown") == 0) {
                snprintf(response, sizeof(response), "{\"status\":\"shutting_down\"}\n");
                write(client, response, strlen(response));
                running = 0;
                break;
            } else {
                snprintf(response, sizeof(response), "{\"error\":\"unknown_command\"}\n");
            }
            write(client, response, strlen(response));
        }
        close(client);
    }
    close(sock);
    unlink(CONTROL_SOCKET);
    return NULL;
}

// ==========================================
// 主函数
// ==========================================
int main(int argc, char *argv[]) {
    const char *log_dir = "test/logs";
    if (ensure_directory_exists(log_dir) != 0) {
        fprintf(stderr, "无法创建日志目录 %s: %s\n", log_dir, strerror(errno));
        return 1;
    }

    char log_filename[256];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(log_filename, sizeof(log_filename), "%s/server_uring_%04d%02d%02d_%02d%02d%02d.log", log_dir,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min,
             tm_info->tm_sec);

    g_logger = logger_init(log_filename, LOG_INFO, 1, "server");
    if (!g_logger)
        return 1;

    g_monitor = monitor_init();
    g_start_time_us = monitor_get_time_us();

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    int num_cpus = monitor_get_cpu_count();
    if (argc > 1)
        g_worker_count = atoi(argv[1]);
    if (g_worker_count <= 0)
        g_worker_count = num_cpus;

    LOG_INFO(g_logger, "启动 io_uring 服务器 | CPU: %d | Workers: %d | Port: %d", num_cpus, g_worker_count, PORT);

#ifdef ENABLE_EBPF
    g_sockmap = sockmap_loader_init(EBPF_OBJ_PATH);
    if (g_sockmap)
        LOG_INFO(g_logger, "eBPF Sockmap 加载成功");
#endif

    g_workers = calloc(g_worker_count, sizeof(WorkerContext));
    for (int i = 0; i < g_worker_count; i++) {
        g_workers[i].thread_id = i;
        if (pthread_create(&g_workers[i].thread_handle, NULL, worker_routine, &g_workers[i]) != 0) {
            LOG_ERROR(g_logger, "无法创建线程 %d", i);
            exit(1);
        }
    }

    pthread_t ctrl_thread_id;
    pthread_create(&ctrl_thread_id, NULL, control_thread, NULL);
    pthread_detach(ctrl_thread_id);

    for (int i = 0; i < g_worker_count; i++) {
        pthread_join(g_workers[i].thread_handle, NULL);
    }

#ifdef ENABLE_EBPF
    if (g_sockmap)
        sockmap_loader_destroy(g_sockmap);
#endif
    free(g_workers);
    monitor_destroy(g_monitor);
    logger_close(g_logger);
    return 0;
}