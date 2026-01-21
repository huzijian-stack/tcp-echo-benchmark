#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

// 初始化日志系统
Logger* logger_init(const char *filename, LogLevel min_level,
                   int console_enabled, const char *program_name) {
    Logger *logger = (Logger*)malloc(sizeof(Logger));
    if (!logger) {
        fprintf(stderr, "Failed to allocate memory for logger\n");
        return NULL;
    }

    // 初始化配置
    logger->file = NULL;
    logger->min_level = min_level;
    logger->console_enabled = console_enabled;
    logger->color_enabled = isatty(STDOUT_FILENO);  // 检测是否是终端
    strncpy(logger->program_name, program_name ? program_name : "app",
            sizeof(logger->program_name) - 1);
    logger->program_name[sizeof(logger->program_name) - 1] = '\0';

    // 打开日志文件
    if (filename) {
        logger->file = fopen(filename, "a");
        if (!logger->file) {
            fprintf(stderr, "Failed to open log file: %s\n", filename);
            free(logger);
            return NULL;
        }
        // 设置行缓冲
        setvbuf(logger->file, NULL, _IOLBF, 0);
    }

    // 写入启动标记
    logger_log(logger, LOG_INFO, "========== Logger initialized ==========");

    return logger;
}

// 获取当前时间字符串（格式: 2026-01-20 18:30:45.123）
static void get_timestamp(char *buf, size_t size) {
    struct timeval tv;
    struct tm *tm_info;

    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);

    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
}

// 写日志
void logger_log(Logger *logger, LogLevel level, const char *fmt, ...) {
    if (!logger || level < logger->min_level) {
        return;
    }

    char timestamp[32];
    char message[4096];
    va_list args;

    // 获取时间戳
    get_timestamp(timestamp, sizeof(timestamp));

    // 格式化用户消息
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // 写入文件（无颜色）
    if (logger->file) {
        fprintf(logger->file, "[%s] [%s] [%s] %s\n",
                timestamp,
                logger->program_name,
                LOG_LEVEL_NAMES[level],
                message);
    }

    // 写入控制台（带颜色）
    if (logger->console_enabled) {
        if (logger->color_enabled) {
            fprintf(stdout, "[%s] [%s%s%s] %s\n",
                    timestamp,
                    LOG_LEVEL_COLORS[level],
                    LOG_LEVEL_NAMES[level],
                    COLOR_RESET,
                    message);
        } else {
            fprintf(stdout, "[%s] [%s] %s\n",
                    timestamp,
                    LOG_LEVEL_NAMES[level],
                    message);
        }
    }
}

// 刷新日志缓冲区
void logger_flush(Logger *logger) {
    if (logger && logger->file) {
        fflush(logger->file);
    }
}

// 关闭日志系统
void logger_close(Logger *logger) {
    if (!logger) {
        return;
    }

    logger_log(logger, LOG_INFO, "========== Logger shutdown ==========");
    logger_flush(logger);

    if (logger->file) {
        fclose(logger->file);
    }

    free(logger);
}
