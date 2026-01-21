#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "sockmap_loader.h"
#include <netinet/in.h>
#include <arpa/inet.h>

struct sockmap_loader {
    struct bpf_object *obj;
    struct bpf_program *prog_msg;
    struct bpf_program *prog_parser;
    struct bpf_program *prog_verdict;
    int map_sock_fd;   // sockmap fd
    int map_hash_fd;   // sockhash fd
    int map_stats_fd;  // stats fd
    int prog_msg_fd;
    int prog_parser_fd;
    int prog_verdict_fd;
};

sockmap_loader_t *sockmap_loader_init(const char *bpf_obj_path) {
    struct bpf_object *obj;
    int err;

    // 分配句柄
    sockmap_loader_t *loader = calloc(1, sizeof(*loader));
    if (!loader) {
        fprintf(stderr, "Failed to allocate loader\n");
        return NULL;
    }

    // 打开 BPF 对象文件
    obj = bpf_object__open(bpf_obj_path);
    if (!obj) {
        fprintf(stderr, "Failed to open BPF object: %s\n", bpf_obj_path);
        free(loader);
        return NULL;
    }
    loader->obj = obj;

    // 加载 BPF 程序到内核
    err = bpf_object__load(obj);
    if (err) {
        fprintf(stderr, "Failed to load BPF object: %d\n", err);
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }

    // 查找 BPF 程序
    loader->prog_msg = bpf_object__find_program_by_name(obj, "bpf_prog_msg");
    loader->prog_parser = bpf_object__find_program_by_name(obj, "bpf_prog_parser");
    loader->prog_verdict = bpf_object__find_program_by_name(obj, "bpf_prog_verdict");

    if (!loader->prog_msg || !loader->prog_parser || !loader->prog_verdict) {
        fprintf(stderr, "Failed to find BPF programs\n");
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }

    // 获取程序 fd
    loader->prog_msg_fd = bpf_program__fd(loader->prog_msg);
    loader->prog_parser_fd = bpf_program__fd(loader->prog_parser);
    loader->prog_verdict_fd = bpf_program__fd(loader->prog_verdict);

    // 查找 BPF maps
    struct bpf_map *map;

    map = bpf_object__find_map_by_name(obj, "sock_map");
    if (!map) {
        fprintf(stderr, "Failed to find sock_map\n");
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }
    loader->map_sock_fd = bpf_map__fd(map);

    map = bpf_object__find_map_by_name(obj, "sock_hash");
    if (!map) {
        fprintf(stderr, "Failed to find sock_hash\n");
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }
    loader->map_hash_fd = bpf_map__fd(map);

    map = bpf_object__find_map_by_name(obj, "stats");
    if (!map) {
        fprintf(stderr, "Failed to find stats map\n");
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }
    loader->map_stats_fd = bpf_map__fd(map);

    // 附加 parser 和 verdict 程序到 sockmap
    err = bpf_prog_attach(loader->prog_parser_fd, loader->map_sock_fd, BPF_SK_SKB_STREAM_PARSER, 0);
    if (err) {
        fprintf(stderr, "Failed to attach parser program: %d\n", err);
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }

    err = bpf_prog_attach(loader->prog_verdict_fd, loader->map_sock_fd, BPF_SK_SKB_STREAM_VERDICT, 0);
    if (err) {
        fprintf(stderr, "Failed to attach verdict program: %d\n", err);
        bpf_object__close(obj);
        free(loader);
        return NULL;
    }

    printf("eBPF Sockmap loaded successfully\n");
    printf("  - sockmap fd: %d\n", loader->map_sock_fd);
    printf("  - sockhash fd: %d\n", loader->map_hash_fd);
    printf("  - parser fd: %d\n", loader->prog_parser_fd);
    printf("  - verdict fd: %d\n", loader->prog_verdict_fd);
    printf("  - msg fd: %d\n", loader->prog_msg_fd);

    return loader;
}

static int get_socket_key(int fd, __u64 *key) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &len) < 0) {
        return -1;
    }

    __u32 ip = addr.sin_addr.s_addr;
    __u32 port = htons(addr.sin_port);

    *key = ((__u64)ip << 32) | port;
    return 0;
}

int sockmap_loader_add_socket(sockmap_loader_t *loader, int sock_fd) {
    if (!loader)
        return -1;
    int err;

    // 1. 添加到 sock_map (Array)
    __u32 idx = sock_fd;
    err = bpf_map_update_elem(loader->map_sock_fd, &idx, &sock_fd, BPF_ANY);
    if (err) {
        fprintf(stderr, "Failed to add socket %d to sock_map: %d\n", sock_fd, err);
        return -1;
    }

    // 2. 添加到 sock_hash (Hash)
    __u64 key;
    if (get_socket_key(sock_fd, &key) == 0) {
        err = bpf_map_update_elem(loader->map_hash_fd, &key, &sock_fd, BPF_ANY);
        if (err) {
            fprintf(stderr, "Failed to add socket %d to sock_hash: %d\n", sock_fd, err);
        }
    } else {
        fprintf(stderr, "Failed to get peer name for socket %d\n", sock_fd);
    }

    return 0;
}

int sockmap_loader_remove_socket(sockmap_loader_t *loader, int sock_fd) {
    if (!loader)
        return -1;

    // 1. 从 sock_map 移除
    __u32 idx = sock_fd;
    bpf_map_delete_elem(loader->map_sock_fd, &idx);

    // 2. 从 sock_hash 移除
    __u64 key;
    if (get_socket_key(sock_fd, &key) == 0) {
        bpf_map_delete_elem(loader->map_hash_fd, &key);
    }

    return 0;
}

int sockmap_loader_get_stats(sockmap_loader_t *loader, unsigned long long *redirected, unsigned long long *redirect_err,
                             unsigned long long *parsed, unsigned long long *parse_err) {
    if (!loader) {
        return -1;
    }

    __u32 key;
    __u64 val;

    // 读取重定向成功次数
    key = 0;  // STAT_REDIRECTED
    if (bpf_map_lookup_elem(loader->map_stats_fd, &key, &val) == 0) {
        if (redirected)
            *redirected = val;
    }

    // 读取重定向失败次数
    key = 1;  // STAT_REDIRECT_ERR
    if (bpf_map_lookup_elem(loader->map_stats_fd, &key, &val) == 0) {
        if (redirect_err)
            *redirect_err = val;
    }

    // 读取解析成功次数
    key = 2;  // STAT_PARSED
    if (bpf_map_lookup_elem(loader->map_stats_fd, &key, &val) == 0) {
        if (parsed)
            *parsed = val;
    }

    // 读取解析失败次数
    key = 3;  // STAT_PARSE_ERR
    if (bpf_map_lookup_elem(loader->map_stats_fd, &key, &val) == 0) {
        if (parse_err)
            *parse_err = val;
    }

    return 0;
}

void sockmap_loader_destroy(sockmap_loader_t *loader) {
    if (!loader) {
        return;
    }

    if (loader->obj) {
        bpf_object__close(loader->obj);
    }

    free(loader);
}
