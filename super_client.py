#!/usr/bin/env python3
"""
Super Client - TCP Echo Server 控制工具
通过 Unix Socket 控制 server 的启动、停止和状态查询
"""

import socket
import sys
import json
import time
import subprocess
import os

CONTROL_SOCKET = "/tmp/tcp_echo_server.sock"
SERVER_BIN = "./out/server"

def send_command(cmd):
    """发送命令到 server 并获取响应"""
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(CONTROL_SOCKET)
        sock.sendall(cmd.encode() + b'\n')

        response = b''
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk

        sock.close()
        return response.decode()
    except FileNotFoundError:
        return None
    except ConnectionRefusedError:
        return None
    except Exception as e:
        return f"Error: {e}"

def is_server_running():
    """检查 server 是否正在运行"""
    return os.path.exists(CONTROL_SOCKET)

def start_server():
    """启动 server（后台运行）"""
    if is_server_running():
        print("❌ Server 已经在运行")
        return False

    print("🚀 启动 Server...")
    try:
        # 后台启动 server
        proc = subprocess.Popen(
            [SERVER_BIN],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True
        )

        # 等待 server 启动
        for i in range(10):
            time.sleep(0.5)
            if is_server_running():
                print(f"✅ Server 启动成功 (PID: {proc.pid})")
                print(f"   控制接口: {CONTROL_SOCKET}")
                return True

        print("⏱️  Server 启动超时")
        return False
    except Exception as e:
        print(f"❌ 启动失败: {e}")
        return False

def stop_server():
    """停止 server"""
    if not is_server_running():
        print("❌ Server 未运行")
        return False

    print("🛑 正在停止 Server...")
    response = send_command("shutdown")

    if response:
        try:
            data = json.loads(response)
            if data.get("status") == "shutting_down":
                # 等待 server 完全关闭
                for i in range(10):
                    time.sleep(0.5)
                    if not is_server_running():
                        print("✅ Server 已停止")
                        return True

                print("⏱️  Server 停止超时")
                return False
        except json.JSONDecodeError:
            pass

    print("❌ 停止失败")
    return False

def get_stats():
    """获取 server 统计信息"""
    if not is_server_running():
        print("❌ Server 未运行")
        return None

    response = send_command("stats")
    if not response:
        print("❌ 无法获取统计信息")
        return None

    try:
        return json.loads(response)
    except json.JSONDecodeError:
        print(f"❌ 解析响应失败: {response}")
        return None

def print_stats(stats):
    """打印统计信息"""
    if not stats:
        return

    uptime_sec = stats.get("uptime_sec", 0)
    hours = uptime_sec // 3600
    minutes = (uptime_sec % 3600) // 60
    seconds = uptime_sec % 60

    print("\n" + "="*50)
    print("          Server 统计信息")
    print("="*50)
    print(f"状态:           {stats.get('status', 'unknown')}")
    print(f"运行时间:       {hours:02d}:{minutes:02d}:{seconds:02d}")
    print("-"*50)

    conn = stats.get("connections", {})
    print(f"总连接数:       {conn.get('total', 0)}")
    print(f"当前活跃连接:   {conn.get('active', 0)}")
    print("-"*50)

    traffic = stats.get("traffic", {})
    print(f"总请求数:       {traffic.get('total_requests', 0)}")
    print(f"接收字节数:     {traffic.get('bytes_recv', 0)} ({traffic.get('bytes_recv', 0) / 1024 / 1024:.2f} MB)")
    print(f"发送字节数:     {traffic.get('bytes_sent', 0)} ({traffic.get('bytes_sent', 0) / 1024 / 1024:.2f} MB)")
    print("-"*50)

    sys = stats.get("system", {})
    print(f"CPU 使用率:     {sys.get('cpu_percent', 0):.2f}%")
    print(f"内存 (RSS):     {sys.get('memory_rss_mb', 0):.2f} MB")
    print(f"线程数:         {sys.get('threads', 0)}")
    print("="*50 + "\n")

def watch_stats(interval=2):
    """持续监控 server 统计信息"""
    print(f"📊 开始监控 (每 {interval} 秒更新，按 Ctrl+C 停止)\n")

    try:
        while True:
            # 清屏
            os.system('clear' if os.name == 'posix' else 'cls')

            if not is_server_running():
                print("❌ Server 未运行")
                break

            stats = get_stats()
            if stats:
                print_stats(stats)
            else:
                print("❌ 无法获取统计信息")
                break

            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n\n⏹️  停止监控")

def show_help():
    """显示帮助信息"""
    print("""
TCP Echo Server 控制工具 (Super Client)

用法:
    python3 super_client.py <command>

命令:
    start       启动 server（后台运行）
    stop        停止 server
    status      查看 server 状态
    stats       获取详细统计信息
    watch       实时监控统计信息（默认 2 秒更新）
    restart     重启 server

示例:
    python3 super_client.py start
    python3 super_client.py stats
    python3 super_client.py watch
    """)

def main():
    if len(sys.argv) < 2:
        show_help()
        sys.exit(1)

    command = sys.argv[1].lower()

    if command == "start":
        sys.exit(0 if start_server() else 1)

    elif command == "stop":
        sys.exit(0 if stop_server() else 1)

    elif command == "restart":
        stop_server()
        time.sleep(1)
        sys.exit(0 if start_server() else 1)

    elif command == "status":
        if is_server_running():
            print("✅ Server 正在运行")
            print(f"   控制接口: {CONTROL_SOCKET}")
            sys.exit(0)
        else:
            print("❌ Server 未运行")
            sys.exit(1)

    elif command == "stats":
        stats = get_stats()
        if stats:
            print_stats(stats)
            sys.exit(0)
        else:
            sys.exit(1)

    elif command == "watch":
        interval = int(sys.argv[2]) if len(sys.argv) > 2 else 2
        watch_stats(interval)
        sys.exit(0)

    elif command in ["help", "-h", "--help"]:
        show_help()
        sys.exit(0)

    else:
        print(f"❌ 未知命令: {command}")
        print("运行 'python3 super_client.py help' 查看帮助")
        sys.exit(1)

if __name__ == "__main__":
    main()
