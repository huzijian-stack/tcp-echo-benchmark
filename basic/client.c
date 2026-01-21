#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <limits.h>
#include <getopt.h>

// 引入日志和监控模块
#include "logger.h"
#include "monitor.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888

// 默认参数
#define DEFAULT_CONNECTIONS 10
#define DEFAULT_ROUNDS 100000
#define DEFAULT_SIZE 64
#define DEFAULT_QPS 0
#define DEFAULT_DURATION 0

// 全局 Logger 实例
static Logger *g_logger = NULL;

// 配置结构
typedef struct {
    int num_connections;
    int test_rounds;
    int send_size;
    int qps_limit;
    int duration_sec;
} ClientConfig;

void print_usage(const char *prog) {
    printf("用法: %s [选项]\n\n", prog);
    printf("选项:\n");
    printf("  -c, --connections NUM   并发连接数 (默认: %d)\n", DEFAULT_CONNECTIONS);
    printf("  -r, --rounds NUM        测试轮次 (默认: %d, 0=基于时长)\n", DEFAULT_ROUNDS);
    printf("  -s, --size NUM          发送数据大小(字节) (默认: %d)\n", DEFAULT_SIZE);
    printf("  -q, --qps NUM           QPS 限制 (默认: %d, 0=不限制)\n", DEFAULT_QPS);
    printf("  -d, --duration SEC      测试时长(秒) (默认: %d, 0=基于轮次)\n", DEFAULT_DURATION);
    printf("  -h, --help              显示此帮助信息\n\n");
    printf("示例:\n");
    printf("  %s                                    # 默认配置\n", prog);
    printf("  %s -c 20 -r 200000                    # 20连接, 20万轮\n", prog);
    printf("  %s -q 50000 -d 60                     # 限制5万QPS, 运行60秒\n", prog);
    printf("  %s -c 10 -q 30000 -d 120              # 10连接, 3万QPS, 2分钟\n", prog);
    printf("\n");
}

struct connection {
    int fd;          // socket 文件描述符
    char *send_buf;  // 发送缓冲区（动态分配）
    char *recv_buf;  // 接收缓冲区（动态分配）
};

// 设置 TCP_NODELAY（禁用 Nagle 算法，减少延迟）
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

// 功能：发送数据，接收回显，验证正确性
// 返回：成功返回 0，失败返回 -1
int do_echo_test(int fd, char *send_buf, char *recv_buf, size_t size) {
    // 1. 发送数据（循环写，确保全部发送）
    ssize_t written = 0;
    while (written < (ssize_t)size) {
        ssize_t n = write(fd, send_buf + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // 被信号中断，重试
            }
            if (g_logger) {
                LOG_ERROR(g_logger, "write 失败: %s", strerror(errno));
            }
            return -1;
        }
        written += n;
    }

    // 2. 接收数据（循环读，确保读满）
    ssize_t total_read = 0;
    while (total_read < (ssize_t)size) {
        ssize_t n = read(fd, recv_buf + total_read, size - total_read);
        if (n < 0) {
            if (g_logger) {
                LOG_ERROR(g_logger, "read 失败: %s", strerror(errno));
            }
            return -1;
        } else if (n == 0) {
            if (g_logger) {
                LOG_ERROR(g_logger, "连接被服务器关闭");
            }
            return -1;
        }
        total_read += n;
    }

    // 3. 验证数据一致性
    if (memcmp(send_buf, recv_buf, size) != 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "数据不一致！");
        }
        return -1;
    }

    return 0;
}

// 功能：创建一个到服务器的连接
// 返回：成功返回 socket fd，失败返回 -1
int connect_to_server() {
    // 1. 创建 socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "socket 创建失败: %s", strerror(errno));
        }
        return -1;
    }

    // 2. 填充服务器地址结构体
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // 将 IP 地址从字符串转换为网络字节序
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "inet_pton 失败: %s", strerror(errno));
        }
        close(fd);
        return -1;
    }

    // 3. 连接到服务器
    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (g_logger) {
            LOG_ERROR(g_logger, "connect 失败: %s", strerror(errno));
        }
        close(fd);
        return -1;
    }

    // 4. 设置 TCP_NODELAY（减少延迟）
    if (set_nodelay(fd) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[]) {
    // ========================================
    // 1. 解析命令行参数
    // ========================================
    ClientConfig config = {.num_connections = DEFAULT_CONNECTIONS,
                           .test_rounds = DEFAULT_ROUNDS,
                           .send_size = DEFAULT_SIZE,
                           .qps_limit = DEFAULT_QPS,
                           .duration_sec = DEFAULT_DURATION};

    static struct option long_options[] = {{"connections", required_argument, 0, 'c'},
                                           {"rounds", required_argument, 0, 'r'},
                                           {"size", required_argument, 0, 's'},
                                           {"qps", required_argument, 0, 'q'},
                                           {"duration", required_argument, 0, 'd'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "c:r:s:q:d:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            config.num_connections = atoi(optarg);
            if (config.num_connections <= 0 || config.num_connections > 10000) {
                fprintf(stderr, "错误: 连接数必须在 1-10000 之间\n");
                return 1;
            }
            break;
        case 'r':
            config.test_rounds = atoi(optarg);
            if (config.test_rounds < 0) {
                fprintf(stderr, "错误: 轮次必须 >= 0\n");
                return 1;
            }
            break;
        case 's':
            config.send_size = atoi(optarg);
            if (config.send_size <= 0 || config.send_size > 65536) {
                fprintf(stderr, "错误: 数据大小必须在 1-65536 字节之间\n");
                return 1;
            }
            break;
        case 'q':
            config.qps_limit = atoi(optarg);
            if (config.qps_limit < 0) {
                fprintf(stderr, "错误: QPS 限制必须 >= 0\n");
                return 1;
            }
            break;
        case 'd':
            config.duration_sec = atoi(optarg);
            if (config.duration_sec < 0) {
                fprintf(stderr, "错误: 时长必须 >= 0\n");
                return 1;
            }
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    // ========================================
    // 2. 初始化日志系统
    // ========================================
    char log_filename[256];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(log_filename, sizeof(log_filename), "test/logs/client_%04d%02d%02d_%02d%02d%02d.log",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min,
             tm_info->tm_sec);

    g_logger = logger_init(log_filename, LOG_INFO, 1, "client");
    if (!g_logger) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    // ========================================
    // 3. 初始化性能监控
    // ========================================
    Monitor *monitor = monitor_init();
    if (!monitor) {
        LOG_ERROR(g_logger, "Failed to initialize monitor");
        logger_close(g_logger);
        return 1;
    }

    // 采集初始系统状态
    SystemStats stats_before;
    monitor_collect(monitor, &stats_before);

    // ========================================
    // 4. 打印测试配置
    // ========================================
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "    TCP Echo 客户端压测工具");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "服务器: %s:%d", SERVER_IP, SERVER_PORT);
    LOG_INFO(g_logger, "并发连接数: %d", config.num_connections);
    LOG_INFO(g_logger, "每连接请求数: %d", config.test_rounds);
    LOG_INFO(g_logger, "发送数据大小: %d 字节", config.send_size);
    LOG_INFO(g_logger, "日志文件: %s", log_filename);

    // ========================================
    // 5. 创建连接
    // ========================================
    struct connection *conns = malloc(config.num_connections * sizeof(struct connection));
    if (!conns) {
        LOG_ERROR(g_logger, "内存分配失败");
        monitor_destroy(monitor);
        logger_close(g_logger);
        return 1;
    }

    LOG_INFO(g_logger, "正在建立连接...");

    for (int i = 0; i < config.num_connections; i++) {
        conns[i].send_buf = malloc(config.send_size);
        conns[i].recv_buf = malloc(config.send_size);

        if (!conns[i].send_buf || !conns[i].recv_buf) {
            LOG_ERROR(g_logger, "缓冲区内存分配失败");
            // 清理
            for (int j = 0; j <= i; j++) {
                if (conns[j].send_buf)
                    free(conns[j].send_buf);
                if (conns[j].recv_buf)
                    free(conns[j].recv_buf);
            }
            free(conns);
            monitor_destroy(monitor);
            logger_close(g_logger);
            return 1;
        }

        conns[i].fd = connect_to_server();
        if (conns[i].fd < 0) {
            LOG_ERROR(g_logger, "连接 %d 创建失败", i);
            // 关闭已创建的连接
            for (int j = 0; j < i; j++) {
                close(conns[j].fd);
                free(conns[j].send_buf);
                free(conns[j].recv_buf);
            }
            free(conns[i].send_buf);
            free(conns[i].recv_buf);
            free(conns);
            monitor_destroy(monitor);
            logger_close(g_logger);
            return 1;
        }
        // 初始化 send_buf（填充测试数据，每个连接不同）
        memset(conns[i].send_buf, 'A' + (i % 26), config.send_size);
        LOG_DEBUG(g_logger, "连接 %d 建立成功 (fd=%d)", i, conns[i].fd);
    }

    LOG_INFO(g_logger, "所有连接建立成功");

    // ========================================
    // 6. 开始性能测试
    // ========================================
    if (config.duration_sec > 0) {
        LOG_INFO(g_logger, "开始性能测试（时长: %d 秒）...", config.duration_sec);
    } else {
        LOG_INFO(g_logger, "开始性能测试（轮次: %d）...", config.test_rounds);
    }

    if (config.qps_limit > 0) {
        LOG_INFO(g_logger, "QPS 限制: %d 请求/秒", config.qps_limit);
    }

    long long start_time = monitor_get_time_us();
    long long success_count = 0;
    long long fail_count = 0;

    // 计算结束时间
    long long end_time_target = (config.duration_sec > 0) ? start_time + (config.duration_sec * 1000000LL) : LLONG_MAX;

    // QPS 限制相关
    long long sleep_interval_us = 0;
    if (config.qps_limit > 0) {
        // 每个连接的发送间隔 = 1秒 / (QPS限制 / 连接数)
        sleep_interval_us = (1000000LL * config.num_connections) / config.qps_limit;
        LOG_INFO(g_logger, "发送间隔: %lld 微秒", sleep_interval_us);
    }

    int round = 0;
    long long next_send_time = start_time;

    // 主测试循环
    while (1) {
        // 检查是否应该结束
        long long now = monitor_get_time_us();

        // 时长模式：检查是否超时
        if (config.duration_sec > 0 && now >= end_time_target) {
            break;
        }

        // 轮次模式：检查是否完成
        if (config.duration_sec == 0 && config.test_rounds > 0 && round >= config.test_rounds) {
            break;
        }

        // 执行测试
        for (int i = 0; i < config.num_connections; i++) {
            if (do_echo_test(conns[i].fd, conns[i].send_buf, conns[i].recv_buf, config.send_size) < 0) {
                LOG_ERROR(g_logger, "Echo 测试失败 (连接 %d, 轮次 %d)", i, round);
                fail_count++;
                // 关闭所有连接并退出
                for (int j = 0; j < config.num_connections; j++) {
                    close(conns[j].fd);
                    free(conns[j].send_buf);
                    free(conns[j].recv_buf);
                }
                free(conns);
                monitor_destroy(monitor);
                logger_close(g_logger);
                return 1;
            }
            success_count++;
        }

        round++;

        // 每 10000 轮打印一次进度
        if (round % 10000 == 0) {
            double current_elapsed = (monitor_get_time_us() - start_time) / 1000000.0;
            double current_qps = success_count / current_elapsed;
            if (config.duration_sec > 0) {
                LOG_INFO(g_logger, "[PROGRESS] 已运行 %.1f 秒, 当前 QPS: %.2f", current_elapsed, current_qps);
            } else {
                LOG_INFO(g_logger, "[PROGRESS] 已完成 %d/%d 轮, 当前 QPS: %.2f", round, config.test_rounds,
                         current_qps);
            }
        }

        // QPS 限制：控制发送速率
        if (config.qps_limit > 0) {
            next_send_time += sleep_interval_us;
            now = monitor_get_time_us();

            if (now < next_send_time) {
                long long sleep_time = next_send_time - now;
                if (sleep_time > 0) {
                    usleep(sleep_time);
                }
            } else {
                // 如果已经落后，重置下次发送时间
                next_send_time = now;
            }
        }
    }

    long long end_time = monitor_get_time_us();
    double elapsed_sec = (end_time - start_time) / 1000000.0;

    // ========================================
    // 7. 计算性能指标
    // ========================================
    long long total_requests = config.test_rounds * config.num_connections;
    double qps = success_count / elapsed_sec;
    double avg_latency_us = (elapsed_sec * 1000000) / success_count;
    double throughput_mbps = (success_count * config.send_size * 8) / (elapsed_sec * 1000000);

    // ========================================
    // 7. 采集最终系统状态
    // ========================================
    SystemStats stats_after;
    monitor_collect(monitor, &stats_after);

    // ========================================
    // 8. 打印性能结果
    // ========================================
    LOG_INFO(g_logger, "");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "         性能测试结果");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "连接数:           %d", config.num_connections);
    LOG_INFO(g_logger, "每连接请求数:     %d", config.test_rounds);
    LOG_INFO(g_logger, "总请求数:         %lld", total_requests);
    LOG_INFO(g_logger, "成功请求数:       %lld", success_count);
    LOG_INFO(g_logger, "失败请求数:       %lld", fail_count);
    LOG_INFO(g_logger, "----------------------------------------");
    LOG_INFO(g_logger, "总耗时:           %.2f 秒", elapsed_sec);
    LOG_INFO(g_logger, "QPS:              %.2f 请求/秒", qps);
    LOG_INFO(g_logger, "平均延迟:         %.2f 微秒", avg_latency_us);
    LOG_INFO(g_logger, "吞吐量:           %.2f Mbps", throughput_mbps);

    // ========================================
    // 9. 打印系统资源统计
    // ========================================
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "         系统资源统计");
    LOG_INFO(g_logger, "========================================");
    LOG_INFO(g_logger, "CPU 使用率:       %.2f%%", stats_after.cpu_usage_percent);
    LOG_INFO(g_logger, "RSS 内存:         %.2f MB", stats_after.memory_rss_kb / 1024.0);
    LOG_INFO(g_logger, "虚拟内存:         %.2f MB", stats_after.memory_vms_kb / 1024.0);
    LOG_INFO(g_logger, "线程数:           %ld", stats_after.num_threads);
    LOG_INFO(g_logger, "----------------------------------------");
    LOG_INFO(g_logger, "自愿上下文切换:   %ld",
             stats_after.ctx_switches_voluntary - stats_before.ctx_switches_voluntary);
    LOG_INFO(g_logger, "非自愿上下文切换: %ld",
             stats_after.ctx_switches_involuntary - stats_before.ctx_switches_involuntary);
    LOG_INFO(g_logger, "次要页面错误:     %ld", stats_after.minor_page_faults - stats_before.minor_page_faults);
    LOG_INFO(g_logger, "主要页面错误:     %ld", stats_after.major_page_faults - stats_before.major_page_faults);
    LOG_INFO(g_logger, "========================================");

    // ========================================
    // 10. 输出 JSON 格式（方便后续分析）
    // ========================================
    LOG_INFO(g_logger, "");
    LOG_INFO(g_logger, "=== JSON 格式输出 ===");
    printf("{\n");
    printf("  \"timestamp\": %lld,\n", stats_after.timestamp_us);
    printf("  \"test_config\": {\n");
    printf("    \"connections\": %d,\n", config.num_connections);
    printf("    \"rounds\": %d,\n", config.test_rounds);
    printf("    \"send_size\": %d\n", config.send_size);
    printf("  },\n");
    printf("  \"performance\": {\n");
    printf("    \"qps\": %.2f,\n", qps);
    printf("    \"latency_us\": %.2f,\n", avg_latency_us);
    printf("    \"throughput_mbps\": %.2f,\n", throughput_mbps);
    printf("    \"elapsed_sec\": %.2f\n", elapsed_sec);
    printf("  },\n");
    printf("  \"system\": {\n");
    printf("    \"cpu_usage_percent\": %.2f,\n", stats_after.cpu_usage_percent);
    printf("    \"memory_rss_mb\": %.2f,\n", stats_after.memory_rss_kb / 1024.0);
    printf("    \"memory_vms_mb\": %.2f,\n", stats_after.memory_vms_kb / 1024.0);
    printf("    \"ctx_switches_voluntary\": %ld,\n",
           stats_after.ctx_switches_voluntary - stats_before.ctx_switches_voluntary);
    printf("    \"ctx_switches_involuntary\": %ld,\n",
           stats_after.ctx_switches_involuntary - stats_before.ctx_switches_involuntary);
    printf("    \"page_faults_minor\": %ld,\n", stats_after.minor_page_faults - stats_before.minor_page_faults);
    printf("    \"page_faults_major\": %ld\n", stats_after.major_page_faults - stats_before.major_page_faults);
    printf("  }\n");
    printf("}\n");

    // ========================================
    // 11. 清理资源
    // ========================================
    LOG_INFO(g_logger, "");
    LOG_INFO(g_logger, "关闭连接...");
    for (int i = 0; i < config.num_connections; i++) {
        close(conns[i].fd);
        free(conns[i].send_buf);
        free(conns[i].recv_buf);
    }
    free(conns);

    LOG_INFO(g_logger, "测试完成！");

    monitor_destroy(monitor);
    logger_close(g_logger);

    return 0;
}
