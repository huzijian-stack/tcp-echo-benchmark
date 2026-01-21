#ifndef MONITOR_H
#define MONITOR_H

#include <sys/types.h>

// 系统资源统计结构
typedef struct {
    // CPU 相关
    double cpu_usage_percent;     // CPU 使用率 (%)
    unsigned long utime;          // 用户态 CPU 时间（jiffies）
    unsigned long stime;          // 内核态 CPU 时间（jiffies）

    // 内存相关
    long memory_rss_kb;           // 常驻内存大小（RSS, KB）
    long memory_vms_kb;           // 虚拟内存大小（VMS, KB）
    long memory_shared_kb;        // 共享内存大小（KB）

    // 上下文切换
    long ctx_switches_voluntary;  // 自愿上下文切换
    long ctx_switches_involuntary;// 非自愿上下文切换

    // 系统调用统计（通过 rusage）
    long minor_page_faults;       // 次要页面错误
    long major_page_faults;       // 主要页面错误

    // 进程信息
    pid_t pid;                    // 进程 ID
    long num_threads;             // 线程数

    // 采样时间戳
    long long timestamp_us;       // 微秒级时间戳
} SystemStats;

// 性能监控器（用于计算增量）
typedef struct {
    SystemStats last_stats;       // 上次采样数据
    long long last_sample_time;   // 上次采样时间（微秒）
    int initialized;              // 是否已初始化
} Monitor;

// ============================================
// 函数声明
// ============================================

// 初始化监控器
Monitor* monitor_init();

// 获取当前系统统计信息
// 如果 monitor 不为 NULL，会计算增量信息（如 CPU 使用率）
int monitor_collect(Monitor *monitor, SystemStats *stats);

// 打印统计信息（人类可读格式）
void monitor_print_stats(const SystemStats *stats);

// 打印统计信息（JSON 格式）
void monitor_print_stats_json(const SystemStats *stats);

// 销毁监控器
void monitor_destroy(Monitor *monitor);

// ============================================
// 工具函数
// ============================================

// 获取当前时间（微秒）
long long monitor_get_time_us();

// 获取系统 CPU 核心数
int monitor_get_cpu_count();

#endif // MONITOR_H
