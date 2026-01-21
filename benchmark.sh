#!/bin/bash
# eBPF Sockmap 性能对比测试

set -e

COLOR_RESET='\033[0m'
COLOR_GREEN='\033[32m'
COLOR_YELLOW='\033[33m'
COLOR_BLUE='\033[34m'
COLOR_RED='\033[31m'

echo -e "${COLOR_BLUE}========================================${COLOR_RESET}"
echo -e "${COLOR_BLUE}   eBPF Sockmap 性能对比测试${COLOR_RESET}"
echo -e "${COLOR_BLUE}========================================${COLOR_RESET}"
echo ""

# 测试参数
CONNECTIONS=${1:-10}
ROUNDS=${2:-100000}
SIZE=${3:-64}

echo -e "${COLOR_YELLOW}测试配置:${COLOR_RESET}"
echo "  并发连接数: $CONNECTIONS"
echo "  测试轮次:   $ROUNDS"
echo "  数据包大小: $SIZE 字节"
echo ""

# 确保 server 没有运行
cleanup() {
    # 1. 强杀进程
    sudo pkill -9 -f "out/server" 2>/dev/null || true
    sudo pkill -9 -f "out/client" 2>/dev/null || true

    sudo rm -f /tmp/tcp_echo_server.sock

    sleep 1
}
cleanup
# ============================================
# 测试 1: 基础版本（无 eBPF）
# ============================================
echo -e "${COLOR_GREEN}[1/2] 测试基础版本（无 eBPF）...${COLOR_RESET}"
echo ""

./out/server >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

# 运行 client
./out/client -c $CONNECTIONS -r $ROUNDS -s $SIZE > /tmp/baseline.txt 2>&1

# 停止 server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 1

cleanup
# 提取性能数据（跳过日志时间戳，排除 PROGRESS）
BASELINE_QPS=$(grep "QPS:" /tmp/baseline.txt | grep -v PROGRESS | awk '{print $5}')
BASELINE_LATENCY=$(grep "平均延迟:" /tmp/baseline.txt | awk '{print $5}')
BASELINE_CPU=$(grep "CPU 使用率:" /tmp/baseline.txt | awk '{print $6}' | sed 's/%//')

echo -e "${COLOR_BLUE}基础版本结果:${COLOR_RESET}"
echo "  QPS:         $BASELINE_QPS 请求/秒"
echo "  平均延迟:    $BASELINE_LATENCY 微秒"
echo "  CPU 使用率:  $BASELINE_CPU%"
echo ""

# ============================================
# 测试 2: eBPF 加速版本
# ============================================
echo -e "${COLOR_GREEN}[2/2] 测试 eBPF 加速版本...${COLOR_RESET}"
echo ""

# 需要 root 权限运行 eBPF 版本
sudo ./out/server_ebpf >/dev/null 2>&1 &
SERVER_EBPF_PID=$!
sleep 2

# 运行 client
./out/client -c $CONNECTIONS -r $ROUNDS -s $SIZE > /tmp/ebpf.txt 2>&1

# 停止 server
sudo kill $SERVER_EBPF_PID 2>/dev/null || true
sudo wait $SERVER_EBPF_PID 2>/dev/null || true
sleep 1

# 提取性能数据（跳过日志时间戳，排除 PROGRESS）
EBPF_QPS=$(grep "QPS:" /tmp/ebpf.txt | grep -v PROGRESS | awk '{print $5}')
EBPF_LATENCY=$(grep "平均延迟:" /tmp/ebpf.txt | awk '{print $5}')
EBPF_CPU=$(grep "CPU 使用率:" /tmp/ebpf.txt | awk '{print $6}' | sed 's/%//')

echo -e "${COLOR_BLUE}eBPF 版本结果:${COLOR_RESET}"
echo "  QPS:         $EBPF_QPS 请求/秒"
echo "  平均延迟:    $EBPF_LATENCY 微秒"
echo "  CPU 使用率:  $EBPF_CPU%"
echo ""

# ============================================
# 性能对比
# ============================================
echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}         性能对比分析${COLOR_RESET}"
echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
echo ""

# 计算提升百分比（使用 bc 或 awk）
QPS_IMPROVE=$(awk "BEGIN {printf \"%.2f\", ($EBPF_QPS - $BASELINE_QPS) / $BASELINE_QPS * 100}")
LATENCY_REDUCE=$(awk "BEGIN {printf \"%.2f\", ($BASELINE_LATENCY - $EBPF_LATENCY) / $BASELINE_LATENCY * 100}")
CPU_REDUCE=$(awk "BEGIN {printf \"%.2f\", ($BASELINE_CPU - $EBPF_CPU) / $BASELINE_CPU * 100}")

echo -e "${COLOR_BLUE}指标对比:${COLOR_RESET}"
printf "  %-15s %-20s %-20s %-15s\n" "指标" "基础版本" "eBPF 版本" "提升"
echo "  ---------------------------------------------------------------"
printf "  %-15s %-20s %-20s ${COLOR_GREEN}%+.2f%%${COLOR_RESET}\n" "QPS" "$BASELINE_QPS" "$EBPF_QPS" "$QPS_IMPROVE"
printf "  %-15s %-20s %-20s ${COLOR_GREEN}%+.2f%%${COLOR_RESET}\n" "平均延迟" "$BASELINE_LATENCY" "$EBPF_LATENCY" "$LATENCY_REDUCE"
printf "  %-15s %-20s %-20s ${COLOR_GREEN}%+.2f%%${COLOR_RESET}\n" "CPU 使用率" "$BASELINE_CPU%" "$EBPF_CPU%" "$CPU_REDUCE"
echo ""

# 保存结果
echo -e "${COLOR_YELLOW}详细结果已保存到:${COLOR_RESET}"
echo "  基础版本: /tmp/baseline.txt"
echo "  eBPF版本: /tmp/ebpf.txt"
echo ""

echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
echo -e "${COLOR_GREEN}         测试完成！${COLOR_RESET}"
echo -e "${COLOR_GREEN}========================================${COLOR_RESET}"
