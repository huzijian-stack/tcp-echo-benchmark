#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// 日志级别
typedef enum {
    LOG_DEBUG = 0,  // 调试信息
    LOG_INFO  = 1,  // 一般信息
    LOG_WARN  = 2,  // 警告
    LOG_ERROR = 3   // 错误
} LogLevel;

// 日志配置结构
typedef struct {
    FILE *file;              // 日志文件句柄
    LogLevel min_level;      // 最低输出级别
    int console_enabled;     // 是否同时输出到控制台
    int color_enabled;       // 是否启用彩色输出（仅控制台）
    char program_name[64];   // 程序名称
} Logger;

// 日志级别名称
static const char *LOG_LEVEL_NAMES[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

// 日志级别颜色（ANSI 转义码）
static const char *LOG_LEVEL_COLORS[] = {
    "\033[36m",  // DEBUG: 青色
    "\033[32m",  // INFO:  绿色
    "\033[33m",  // WARN:  黄色
    "\033[31m"   // ERROR: 红色
};

#define COLOR_RESET "\033[0m"

// ============================================
// 函数声明
// ============================================

// 初始化日志系统
// 参数:
//   filename: 日志文件路径（NULL 表示只输出到控制台）
//   min_level: 最低输出级别
//   console_enabled: 是否同时输出到控制台
//   program_name: 程序名称
// 返回: Logger 指针，失败返回 NULL
Logger* logger_init(const char *filename, LogLevel min_level,
                   int console_enabled, const char *program_name);

// 写日志
// 参数:
//   logger: Logger 实例
//   level: 日志级别
//   fmt: 格式化字符串（类似 printf）
void logger_log(Logger *logger, LogLevel level, const char *fmt, ...);

// 刷新日志缓冲区
void logger_flush(Logger *logger);

// 关闭日志系统
void logger_close(Logger *logger);

// ============================================
// 便捷宏定义
// ============================================

#define LOG_DEBUG(logger, ...) logger_log(logger, LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(logger, ...)  logger_log(logger, LOG_INFO, __VA_ARGS__)
#define LOG_WARN(logger, ...)  logger_log(logger, LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(logger, ...) logger_log(logger, LOG_ERROR, __VA_ARGS__)

#endif // LOGGER_H
