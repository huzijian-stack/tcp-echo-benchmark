# 🚀 快速使用指南

## 编译

```bash
make clean && make
```

## 方式一：使用 Super Client（推荐）

### 1. 启动 Server
```bash
make server-start
# 或
./super_client.py start
```

### 2. 查看 Server 状态
```bash
make server-status
# 或
./super_client.py status
```

### 3. 运行 Client 测试
```bash
make run-client
```

### 4. 实时监控 Server（另开一个终端）
```bash
make server-watch
# 或
./super_client.py watch
```

### 5. 获取统计信息
```bash
make server-stats
# 或
./super_client.py stats
```

### 6. 停止 Server
```bash
make server-stop
# 或
./super_client.py stop
```

---

## 方式二：传统方式

### 1. 启动 Server
```bash
make run-server
```

### 2. 运行 Client
```bash
make run-client
```

### 3. 停止 Server
```bash
make stop-server
```

---

## 方式三：一键测试

```bash
make test
```

这会自动完成：编译 → 启动 Server → 运行 Client → 停止 Server

---

## 📊 查看结果

### 查看最近的日志
```bash
make logs
```

### 列出所有测试日志
```bash
make list-logs
```

### 查看日志文件
```bash
ls -lh test/logs/
```

---

## 💡 Super Client 命令大全

```bash
# 启动
./super_client.py start

# 停止
./super_client.py stop

# 重启
./super_client.py restart

# 查看状态
./super_client.py status

# 获取统计信息
./super_client.py stats

# 实时监控（每 1 秒刷新）
./super_client.py watch 1

# 帮助
./super_client.py help
```

---

## 📈 性能测试示例

### 测试 1：基准测试（默认配置）
```bash
# 10 连接 × 100,000 轮 = 1,000,000 次请求
make server-start
./out/client
make server-stop
```

### 测试 2：自定义并发和轮次
```bash
# 20 连接，每连接 20 万轮
make server-start
./out/client -c 20 -r 200000
make server-stop
```

### 测试 3：QPS 限制测试
```bash
# 限制 5 万 QPS，运行 60 秒
make server-start
./out/client -q 50000 -d 60
make server-stop
```

### 测试 4：综合配置
```bash
# 10 连接，限制 3 万 QPS，运行 2 分钟
make server-start
./out/client -c 10 -q 30000 -d 120
make server-stop
```

### 测试 5：大包测试
```bash
# 1KB 数据包，10 连接，10 万轮
make server-start
./out/client -c 10 -r 100000 -s 1024
make server-stop
```

### Client 命令行选项

```bash
./out/client --help

选项:
  -c, --connections NUM   并发连接数 (默认: 10)
  -r, --rounds NUM        测试轮次 (默认: 100000, 0=基于时长)
  -s, --size NUM          发送数据大小(字节) (默认: 64)
  -q, --qps NUM           QPS 限制 (默认: 0, 0=不限制)
  -d, --duration SEC      测试时长(秒) (默认: 0, 0=基于轮次)
  -h, --help              显示此帮助信息
```

---

## 🔍 监控服务器

在运行 client 的同时，另开一个终端：
```bash
# 实时监控（每 2 秒刷新）
make server-watch

# 或者手动查询
watch -n 1 "python3 super_client.py stats"
```

---

## 📝 日志文件说明

- `test/logs/server_时间戳.log` - Server 日志
- `test/logs/client_时间戳.log` - Client 日志

日志包含：
- 详细的运行信息
- 性能统计
- 系统资源使用
- JSON 格式的测试结果

---

## 🎯 下一步

1. **记录基准数据**
   ```bash
   make server-start
   ./out/client > baseline.txt
   make server-stop
   ```

2. **开始 eBPF 开发**
   - 实现 eBPF Sockmap 加速
   - 对比加速前后的性能差异

3. **性能调优测试**
   ```bash
   # 测试不同并发数
   ./out/client -c 5
   ./out/client -c 10
   ./out/client -c 20
   ./out/client -c 50

   # 测试不同数据包大小
   ./out/client -s 64
   ./out/client -s 256
   ./out/client -s 1024
   ./out/client -s 4096

   # QPS 压测
   ./out/client -q 10000 -d 30
   ./out/client -q 50000 -d 30
   ./out/client -q 100000 -d 30
   ```

---

## ❓ 常见问题

### Q: Server 启动失败？
```bash
# 检查端口占用
netstat -tuln | grep 8888

# 检查 socket 文件
ls -l /tmp/tcp_echo_server.sock

# 清理残留
rm /tmp/tcp_echo_server.sock
```

### Q: Client 连接失败？
```bash
# 确保 server 正在运行
make server-status

# 查看 server 日志
make tail-server
```

### Q: 如何清理所有数据？
```bash
make distclean  # 清理编译产物和测试数据
```
