# Git 提交准备指南

## 推荐的提交策略

### 选项 1: 单次提交（适合小型项目）

```bash
git init
git add .
git commit -m "feat: implement eBPF Sockmap accelerated TCP Echo benchmark

- Implemented high-performance epoll-based TCP Echo Server
- Added flexible client with command-line arguments support
- Integrated eBPF Sockmap for kernel-level socket redirection
- Added comprehensive logging and performance monitoring system
- Created super_client.py for server control interface
- Implemented automated benchmark comparison script
- Added environment setup script for eBPF dependencies

Performance Results:
- QPS improvement: +3.29% (135,399 → 139,852 req/s)
- Latency reduction: -3.25% (7.39 → 7.15 μs)
- CPU reduction: -0.92% (56.45% → 55.93%)

Test environment: Ubuntu 20.04, Linux 5.15.0-139"
```

### 选项 2: 分阶段提交（推荐，便于追溯）

#### 第一次提交：基础架构
```bash
git init
git add basic/ common/ Makefile
git commit -m "feat: add basic TCP Echo Server with epoll

- Implemented epoll-based server with edge-triggered mode
- Added client with pressure testing capabilities
- Integrated logging and monitoring systems
- Support TCP_NODELAY and non-blocking I/O"
```

#### 第二次提交：命令行支持
```bash
git add basic/client.c
git commit -m "feat: add command-line arguments support for client

- Added getopt_long() for flexible configuration
- Support -c (connections), -r (rounds), -s (size)
- Support -q (QPS limit), -d (duration)
- Dynamic buffer allocation based on parameters"
```

#### 第三次提交：控制工具
```bash
git add super_client.py
git commit -m "feat: add super_client.py for server control

- Unix socket-based control interface
- Commands: start/stop/restart/status/stats/watch
- JSON-based statistics display
- Integrated with Makefile targets"
```

#### 第四次提交：eBPF 实现
```bash
git add ebpf/ benchmark.sh
git commit -m "feat: implement eBPF Sockmap acceleration

- Added sockmap.bpf.c with SK_MSG, SK_SKB programs
- Implemented sockmap_loader for userspace integration
- Support conditional compilation with ENABLE_EBPF
- Added automated benchmark comparison script

Performance improvements:
- QPS: +3.29%, Latency: -3.25%, CPU: -0.92%"
```

#### 第五次提交：文档和工具
```bash
git add README.md setup_env.sh .gitignore
git commit -m "docs: add comprehensive documentation and setup script

- Updated README.md with benchmark results
- Added setup_env.sh for automated environment configuration
- Created .gitignore for project cleanup
- Merged QUICKSTART.md content into README.md"
```

## 常用 Git 命令

```bash
# 初始化仓库
git init

# 查看状态
git status

# 添加所有文件
git add .

# 添加特定文件/目录
git add basic/ common/ ebpf/

# 提交
git commit -m "commit message"

# 查看提交历史
git log --oneline

# 添加远程仓库
git remote add origin <repository-url>

# 推送到远程
git push -u origin main

# 创建并切换分支
git checkout -b develop

# 查看差异
git diff
```

## 提交信息规范（推荐）

遵循 Conventional Commits 规范：

- `feat:` - 新功能
- `fix:` - Bug 修复
- `docs:` - 文档更新
- `style:` - 代码格式调整
- `refactor:` - 代码重构
- `perf:` - 性能优化
- `test:` - 测试相关
- `chore:` - 构建/工具配置

## 注意事项

1. **不要提交编译产物**：确保 .gitignore 正确配置
2. **检查敏感信息**：确保没有提交密码、密钥等
3. **测试后提交**：确保代码可以正常编译和运行
4. **描述清晰**：提交信息应该清晰描述做了什么改动

## 检查清单

- [ ] 代码可以正常编译（`make clean && make all-ebpf`）
- [ ] 基础版本可以正常运行
- [ ] eBPF 版本可以正常运行（需要 root 权限）
- [ ] benchmark.sh 可以正常执行
- [ ] 没有提交临时文件和编译产物
- [ ] README.md 信息准确完整
- [ ] .gitignore 配置正确
