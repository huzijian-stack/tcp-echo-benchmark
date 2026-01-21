#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

// 初始化监控器
Monitor* monitor_init() {
    Monitor *monitor = (Monitor*)malloc(sizeof(Monitor));
    if (!monitor) {
        return NULL;
    }

    memset(monitor, 0, sizeof(Monitor));
    monitor->initialized = 0;

    return monitor;
}

// 获取当前时间（微秒）
long long monitor_get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

// 获取系统 CPU 核心数
int monitor_get_cpu_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

// 从 /proc/self/stat 读取 CPU 时间
static int read_cpu_time(unsigned long *utime, unsigned long *stime) {
    FILE *fp = fopen("/proc/self/stat", "r");
    if (!fp) {
        return -1;
    }

    // /proc/self/stat 格式（第 14 和 15 列是 utime 和 stime）
    // 跳过前 13 个字段
    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return -1;
    }

    // 使用 sscanf 解析
    int pid;
    char comm[256];
    char state;
    // 格式: pid (comm) state ... utime stime ...
    int ret = sscanf(buffer,
                     "%d %s %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                     &pid, comm, &state, utime, stime);

    fclose(fp);
    return (ret == 5) ? 0 : -1;
}

// 从 /proc/self/status 读取内存信息
static int read_memory_info(long *rss_kb, long *vms_kb, long *shared_kb, long *threads) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) {
        return -1;
    }

    char line[256];
    *rss_kb = 0;
    *vms_kb = 0;
    *shared_kb = 0;
    *threads = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", rss_kb);
        } else if (strncmp(line, "VmSize:", 7) == 0) {
            sscanf(line + 7, "%ld", vms_kb);
        } else if (strncmp(line, "RssFile:", 8) == 0) {
            sscanf(line + 8, "%ld", shared_kb);
        } else if (strncmp(line, "Threads:", 8) == 0) {
            sscanf(line + 8, "%ld", threads);
        }
    }

    fclose(fp);
    return 0;
}

// 获取当前系统统计信息
int monitor_collect(Monitor *monitor, SystemStats *stats) {
    if (!stats) {
        return -1;
    }

    memset(stats, 0, sizeof(SystemStats));

    // 1. 记录时间戳
    stats->timestamp_us = monitor_get_time_us();
    stats->pid = getpid();

    // 2. 读取 CPU 时间
    if (read_cpu_time(&stats->utime, &stats->stime) < 0) {
        return -1;
    }

    // 3. 读取内存信息
    if (read_memory_info(&stats->memory_rss_kb,
                        &stats->memory_vms_kb,
                        &stats->memory_shared_kb,
                        &stats->num_threads) < 0) {
        return -1;
    }

    // 4. 使用 getrusage 获取额外信息
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        stats->ctx_switches_voluntary = usage.ru_nvcsw;
        stats->ctx_switches_involuntary = usage.ru_nivcsw;
        stats->minor_page_faults = usage.ru_minflt;
        stats->major_page_faults = usage.ru_majflt;
    }

    // 5. 计算 CPU 使用率（如果有历史数据）
    if (monitor && monitor->initialized) {
        // 计算时间差（秒）
        double time_delta_sec = (stats->timestamp_us - monitor->last_sample_time) / 1000000.0;

        if (time_delta_sec > 0) {
            // 计算 CPU 时间差（jiffies）
            unsigned long cpu_time_delta = (stats->utime - monitor->last_stats.utime) +
                                          (stats->stime - monitor->last_stats.stime);

            // jiffies 转秒（假设 HZ=100，即 1 jiffy = 0.01 秒）
            long hz = sysconf(_SC_CLK_TCK);
            double cpu_time_sec = (double)cpu_time_delta / hz;

            // CPU 使用率 = (CPU 时间 / 实际时间) * 100%
            // 对于多核系统，这个值可能超过 100%
            stats->cpu_usage_percent = (cpu_time_sec / time_delta_sec) * 100.0;
        }
    }

    // 6. 更新监控器状态
    if (monitor) {
        monitor->last_stats = *stats;
        monitor->last_sample_time = stats->timestamp_us;
        monitor->initialized = 1;
    }

    return 0;
}

// 打印统计信息（人类可读格式）
void monitor_print_stats(const SystemStats *stats) {
    printf("\n========================================\n");
    printf("         系统资源统计\n");
    printf("========================================\n");
    printf("进程 ID:          %d\n", stats->pid);
    printf("线程数:           %ld\n", stats->num_threads);
    printf("----------------------------------------\n");
    printf("CPU 使用率:       %.2f%%\n", stats->cpu_usage_percent);
    printf("用户态 CPU:       %lu jiffies\n", stats->utime);
    printf("内核态 CPU:       %lu jiffies\n", stats->stime);
    printf("----------------------------------------\n");
    printf("RSS 内存:         %.2f MB\n", stats->memory_rss_kb / 1024.0);
    printf("虚拟内存:         %.2f MB\n", stats->memory_vms_kb / 1024.0);
    printf("共享内存:         %.2f MB\n", stats->memory_shared_kb / 1024.0);
    printf("----------------------------------------\n");
    printf("自愿上下文切换:   %ld\n", stats->ctx_switches_voluntary);
    printf("非自愿上下文切换: %ld\n", stats->ctx_switches_involuntary);
    printf("次要页面错误:     %ld\n", stats->minor_page_faults);
    printf("主要页面错误:     %ld\n", stats->major_page_faults);
    printf("========================================\n\n");
}

// 打印统计信息（JSON 格式）
void monitor_print_stats_json(const SystemStats *stats) {
    printf("{\n");
    printf("  \"pid\": %d,\n", stats->pid);
    printf("  \"threads\": %ld,\n", stats->num_threads);
    printf("  \"cpu\": {\n");
    printf("    \"usage_percent\": %.2f,\n", stats->cpu_usage_percent);
    printf("    \"utime_jiffies\": %lu,\n", stats->utime);
    printf("    \"stime_jiffies\": %lu\n", stats->stime);
    printf("  },\n");
    printf("  \"memory\": {\n");
    printf("    \"rss_mb\": %.2f,\n", stats->memory_rss_kb / 1024.0);
    printf("    \"vms_mb\": %.2f,\n", stats->memory_vms_kb / 1024.0);
    printf("    \"shared_mb\": %.2f\n", stats->memory_shared_kb / 1024.0);
    printf("  },\n");
    printf("  \"context_switches\": {\n");
    printf("    \"voluntary\": %ld,\n", stats->ctx_switches_voluntary);
    printf("    \"involuntary\": %ld\n", stats->ctx_switches_involuntary);
    printf("  },\n");
    printf("  \"page_faults\": {\n");
    printf("    \"minor\": %ld,\n", stats->minor_page_faults);
    printf("    \"major\": %ld\n", stats->major_page_faults);
    printf("  },\n");
    printf("  \"timestamp_us\": %lld\n", stats->timestamp_us);
    printf("}\n");
}

// 销毁监控器
void monitor_destroy(Monitor *monitor) {
    if (monitor) {
        free(monitor);
    }
}
