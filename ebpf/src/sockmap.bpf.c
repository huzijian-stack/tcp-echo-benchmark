// SPDX-License-Identifier: GPL-2.0
/* 使用 vmlinux.h 或 libbpf 提供的头文件，而不是直接包含内核头文件 */

/* BPF helper 定义 */
#define SEC(name) __attribute__((section(name), used))

/* BPF 类型定义 */
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;
typedef long long __s64;

/* BPF Map 类型 */
enum bpf_map_type {
    BPF_MAP_TYPE_UNSPEC = 0,
    BPF_MAP_TYPE_HASH = 1,
    BPF_MAP_TYPE_ARRAY = 2,
    BPF_MAP_TYPE_SOCKMAP = 15,
    BPF_MAP_TYPE_SOCKHASH = 18,
};

/* BPF 辅助函数声明 */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, __u64 flags) = (void *)2;
static long (*bpf_msg_redirect_hash)(void *msg, void *map, void *key, __u64 flags) = (void *)71;
static long (*bpf_sk_redirect_hash)(void *skb, void *map, void *key, __u64 flags) = (void *)72;
static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...) = (void *)6;

/* BPF 程序返回值 */
#define SK_PASS 1
#define BPF_F_INGRESS (1U << 0)
#define BPF_ANY 0

/* Map 定义宏 */
#define __uint(name, val) int(*name)[val]
#define __type(name, val) typeof(val) *name

#ifndef bpf_printk
#define bpf_printk(fmt, ...)                                       \
    ({                                                             \
        char ____fmt[] = fmt;                                      \
        bpf_trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__); \
    })
#endif

/* SK_MSG 上下文 */
struct sk_msg_md {
    void *data;
    void *data_end;
    __u32 family;
    __u32 remote_ip4;
    __u32 local_ip4;
    __u32 remote_ip6[4];
    __u32 local_ip6[4];
    __u32 remote_port;
    __u32 local_port;
    __u32 size;
};

/* SK_SKB 上下文 */
struct __sk_buff {
    __u32 len;
    __u32 pkt_type;
    __u32 mark;
    __u32 queue_mapping;
    __u32 protocol;
    __u32 vlan_present;
    __u32 vlan_tci;
    __u32 vlan_proto;
    __u32 priority;
    __u32 ingress_ifindex;
    __u32 ifindex;
    __u32 tc_index;
    __u32 cb[5];
    __u32 hash;
    __u32 tc_classid;
    __u32 data;
    __u32 data_end;
    __u32 napi_id;
    __u32 family;
    __u32 remote_ip4;
    __u32 local_ip4;
    __u32 remote_ip6[4];
    __u32 local_ip6[4];
    __u32 remote_port;
    __u32 local_port;
};

/* 原子操作 */
#define __sync_fetch_and_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)

// Sockmap：存储 socket 的映射
struct {
    __uint(type, BPF_MAP_TYPE_SOCKMAP);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, __u32);
} sock_map SEC(".maps");

// Socket Hash：用于快速查找对端 socket
struct {
    __uint(type, BPF_MAP_TYPE_SOCKHASH);
    __uint(max_entries, 65536);
    __type(key, __u64);
    __type(value, __u32);
} sock_hash SEC(".maps");

// 统计信息
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

// 统计索引
#define STAT_REDIRECTED 0    // 重定向成功次数
#define STAT_REDIRECT_ERR 1  // 重定向失败次数
#define STAT_PARSED 2        // 解析成功次数
#define STAT_PARSE_ERR 3     // 解析失败次数

// SK_MSG 程序：处理发送路径，实现 socket 间的直接数据转发
SEC("sk_msg")
int bpf_prog_msg(struct sk_msg_md *msg) {
    __u32 key = STAT_PARSED;
    __u64 *val;

    // 更新统计
    val = bpf_map_lookup_elem(&stats, &key);
    if (val)
        __sync_fetch_and_add(val, 1);

    // 获取对端 socket 的 key
    // 这里使用 msg->remote_ip4 和 msg->remote_port 构造 hash key
    __u64 hash_key = ((__u64)msg->remote_ip4 << 32) | msg->remote_port;

    // 尝试重定向到对端 socket
    long ret = bpf_msg_redirect_hash(msg, &sock_hash, &hash_key, BPF_F_INGRESS);

    if (ret == SK_PASS) {
        // 重定向成功
        key = STAT_REDIRECTED;
        val = bpf_map_lookup_elem(&stats, &key);
        if (val)
            __sync_fetch_and_add(val, 1);
        return SK_PASS;
    } else {
        // 重定向失败，走正常路径
        key = STAT_REDIRECT_ERR;
        val = bpf_map_lookup_elem(&stats, &key);
        if (val)
            __sync_fetch_and_add(val, 1);
        return SK_PASS;
    }
}

// SK_SKB 程序（Stream Parser）：解析流，决定消息边界
SEC("sk_skb/stream_parser")
int bpf_prog_parser(struct __sk_buff *skb) {
    __u32 key = STAT_PARSED;
    __u64 *val;

    val = bpf_map_lookup_elem(&stats, &key);
    if (val)
        __sync_fetch_and_add(val, 1);

    // 对于 Echo 服务器，我们希望立即转发所有数据
    // 返回数据长度表示这是一个完整的消息
    return skb->len;
}

SEC("sk_skb/stream_verdict")
int bpf_prog_verdict(struct __sk_buff *skb) {
    __u32 key;
    __u64 *val;

    __u32 port_net = __builtin_bswap32(skb->remote_port);

    __u64 hash_key = ((__u64)skb->remote_ip4 << 32) | port_net;

    // 尝试重定向 (保持 flags=0)
    long ret = bpf_sk_redirect_hash(skb, &sock_hash, &hash_key, 0);

    if (ret == SK_PASS) {
        key = STAT_REDIRECTED;
        val = bpf_map_lookup_elem(&stats, &key);
        if (val)
            __sync_fetch_and_add(val, 1);
        return SK_PASS;
    }

    // 重定向失败
    key = STAT_REDIRECT_ERR;
    val = bpf_map_lookup_elem(&stats, &key);
    if (val)
        __sync_fetch_and_add(val, 1);

    return SK_PASS;
}

char _license[] SEC("license") = "GPL";
