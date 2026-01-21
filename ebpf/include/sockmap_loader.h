#ifndef SOCKMAP_LOADER_H
#define SOCKMAP_LOADER_H

#include <stdbool.h>

// Sockmap 加载器句柄
typedef struct sockmap_loader sockmap_loader_t;

// 初始化 sockmap 加载器
// 参数：bpf_obj_path - BPF 对象文件路径
// 返回：加载器句柄，失败返回 NULL
sockmap_loader_t* sockmap_loader_init(const char *bpf_obj_path);

// 将 socket 添加到 sockmap
// 参数：loader - 加载器句柄
//       sock_fd - socket 文件描述符
// 返回：0 成功，-1 失败
int sockmap_loader_add_socket(sockmap_loader_t *loader, int sock_fd);

// 从 sockmap 移除 socket
// 参数：loader - 加载器句柄
//       sock_fd - socket 文件描述符
// 返回：0 成功，-1 失败
int sockmap_loader_remove_socket(sockmap_loader_t *loader, int sock_fd);

// 获取统计信息
// 参数：loader - 加载器句柄
//       redirected - 重定向成功次数（输出）
//       redirect_err - 重定向失败次数（输出）
//       parsed - 解析成功次数（输出）
//       parse_err - 解析失败次数（输出）
// 返回：0 成功，-1 失败
int sockmap_loader_get_stats(sockmap_loader_t *loader,
                             unsigned long long *redirected,
                             unsigned long long *redirect_err,
                             unsigned long long *parsed,
                             unsigned long long *parse_err);

// 清理资源
void sockmap_loader_destroy(sockmap_loader_t *loader);

#endif // SOCKMAP_LOADER_H
