# TCP Echo Benchmark 项目总结

## 项目概述

这是一个用于测试 eBPF Sockmap 性能优化的 TCP Echo Server 基准测试工具，实现了内核态零拷贝数据转发，并提供了完整的性能对比测试框架。

## 核心特性

### 1. 高性能网络服务器
- **Epoll 事件驱动**：边缘触发（EPOLLET）+ 非阻塞 I/O
- **TCP 优化**：TCP_NODELAY, SO_REUSEADDR
- **多连接支持**：支持数千并发连接

### 2. eBPF Sockmap 加速
- **零拷贝转发**：数据在内核态直接转发，无需用户态拷贝
- **内核程序**：SK_MSG, SK_SKB Parser, SK_SKB Verdict
- **统计支持**：实时统计重定向成功/失败次数

### 3. 灵活的压测客户端
- **命令行参数**：-c (连接数), -r (轮次), -s (包大小), -q (QPS), -d (时长)
- **QPS 限制**：支持精确的流量控制
- **实时统计**：QPS, 延迟, 吞吐量, CPU, 内存

### 4. 完整的监控系统
- **日志系统**：4 级日志（DEBUG/INFO/WARN/ERROR），带时间戳
- **性能监控**：CPU, 内存, 上下文切换, 页错误
- **JSON 输出**：便于分析和可视化

### 5. 服务器控制工具
- **super_client.py**：Unix socket 控制接口
- **命令支持**：start, stop, restart, status, stats, watch
- **实时监控**：周期性刷新服务器统计信息

### 6. 自动化测试
- **benchmark.sh**：自动对比基础版本和 eBPF 版本
- **性能对比**：QPS, 延迟, CPU 使用率
- **详细报告**：百分比提升和原始数据

## 项目结构

```
tcp-echo-benchmark/
├── basic/                      # 基础实现
│   ├── server.c               # TCP Echo Server（支持 eBPF）
│   └── client.c               # 压测客户端
├── common/                     # 公共模块
│   ├── include/               # 头文件
│   │   ├── logger.h           # 日志系统
│   │   └── monitor.h          # 性能监控
│   └── src/                   # 源文件
│       ├── logger.c
│       └── monitor.c
├── ebpf/                       # eBPF 实现
│   ├── include/
│   │   └── sockmap_loader.h   # eBPF 加载器接口
│   └── src/
│       ├── sockmap.bpf.c      # eBPF 内核程序
│       └── sockmap_loader.c   # 用户态加载器
├── out/                        # 编译输出
│   ├── server                 # 基础版本
│   ├── server_ebpf            # eBPF 加速版本
│   ├── client                 # 客户端
│   └── ebpf/
│       └── sockmap.bpf.o      # eBPF 对象文件
├── Makefile                    # 构建系统
├── super_client.py             # 服务器控制工具
├── benchmark.sh                # 性能对比脚本
├── setup_env.sh                # 环境配置脚本
├── README.md                   # 主文档
├── .gitignore                  # Git 忽略规则
└── COMMIT_TEMPLATE.md          # Git 提交指南
```

## 性能测试结果

### 测试配置
- **并发连接**：10
- **测试轮次**：100,000
- **数据包大小**：64 字节
- **测试环境**：Ubuntu 20.04, Linux 5.15.0-139, gcc 9.4.0, clang 18

### 性能对比

| 指标 | 基础版本 | eBPF 版本 | 提升幅度 |
|------|----------|-----------|----------|
| **QPS** | 135,399.88 请求/秒 | 139,852.79 请求/秒 | **+3.29%** |
| **平均延迟** | 7.39 微秒 | 7.15 微秒 | **-3.25%** |
| **CPU 使用率** | 56.45% | 55.93% | **-0.92%** |

### 性能分析

**为什么提升相对较小？**
1. **Echo Server 特性**：业务逻辑极简，大部分时间消耗在网络 I/O
2. **小包测试**：64 字节小包，内核开销占比相对较高
3. **低并发**：10 连接，未充分发挥 eBPF 在高并发下的优势
4. **Loopback 测试**：本地回环测试，网络延迟极低

**预期更高提升的场景**：
- 更高并发（50+ 连接）
- 更大数据包（1KB+）
- 真实网络环境
- 更复杂的数据处理逻辑

## 技术亮点

### 1. eBPF Sockmap 实现
- 手动定义 BPF 类型，避免内核头文件依赖冲突
- 使用 SOCKHASH 实现高效的 socket 查找
- 统计程序运行情况（重定向成功/失败）

### 2. 内存管理
- 动态分配缓冲区，根据命令行参数调整
- 避免内存泄漏，正确释放资源

### 3. 并发控制
- 使用 epoll 边缘触发模式，避免惊群效应
- 非阻塞 I/O，避免线程阻塞

### 4. 性能监控
- 实时采集 CPU、内存使用率
- 统计系统调用开销（上下文切换、页错误）

## 使用指南

### 快速开始

```bash
# 1. 配置环境
./setup_env.sh

# 2. 编译项目
make clean && make all-ebpf

# 3. 运行基准测试
./benchmark.sh

# 4. 查看结果
cat /tmp/baseline.txt  # 基础版本
cat /tmp/ebpf.txt      # eBPF 版本
```

### 自定义测试

```bash
# 启动 server
make server-start

# 运行不同配置的测试
./out/client -c 20 -r 200000        # 高并发
./out/client -s 1024 -r 100000      # 大包
./out/client -q 50000 -d 60         # QPS 限制

# 停止 server
make server-stop
```

### 监控和调试

```bash
# 实时监控
make server-watch

# 查看日志
make logs
make tail-server
make tail-client
```

## 文件说明

### 核心文件
- **basic/server.c** (1167 行)：TCP Echo Server，支持 eBPF 条件编译
- **basic/client.c** (709 行)：压测客户端，支持丰富的命令行参数
- **ebpf/src/sockmap.bpf.c** (195 行)：eBPF 内核程序
- **ebpf/src/sockmap_loader.c**：eBPF 用户态加载器

### 辅助文件
- **common/src/logger.c**：日志系统实现
- **common/src/monitor.c**：性能监控实现
- **super_client.py** (300+ 行)：Python 控制工具
- **benchmark.sh** (122 行)：自动化性能对比脚本
- **setup_env.sh** (378 行)：环境配置脚本

### 文档文件
- **README.md**：主文档，包含完整使用说明
- **QUICKSTART.md**：快速开始指南（可删除，已合并到 README）
- **EBPF_SETUP.md**：eBPF 完整配置文档
- **EBPF_QUICK_SETUP.md**：eBPF 快速配置指南
- **COMMIT_TEMPLATE.md**：Git 提交指南

## 开发历程

### 阶段 1：基础实现
- 实现 epoll 事件驱动的 TCP Echo Server
- 实现多连接压测客户端
- 集成日志和监控系统

### 阶段 2：功能增强
- 添加命令行参数支持
- 实现 QPS 限制功能
- 创建 super_client.py 控制工具

### 阶段 3：eBPF 集成
- 实现 eBPF Sockmap 内核程序
- 创建用户态加载器
- 集成条件编译支持

### 阶段 4：测试和优化
- 实现自动化基准测试
- 修复日志解析问题
- 完善性能统计

### 阶段 5：文档和发布
- 更新 README 并合并 QUICKSTART
- 创建环境配置脚本
- 准备 Git 提交材料

## 后续优化方向

### 性能优化
1. **多进程/多线程**：利用多核 CPU
2. **SO_REUSEPORT**：负载均衡到多个进程
3. **更复杂的 eBPF 逻辑**：如数据包过滤、修改

### 功能扩展
1. **支持 TLS/SSL**：加密通信
2. **协议扩展**：HTTP Echo, UDP Echo
3. **更多统计指标**：P99 延迟, 连接建立时间

### 测试增强
1. **压力测试**：支持更高并发（1000+）
2. **长时间测试**：稳定性验证
3. **网络模拟**：延迟、丢包场景

## 许可证

GPL-2.0

## 作者

Li Xiang
