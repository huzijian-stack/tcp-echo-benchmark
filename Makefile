# ============================================
# TCP Echo Benchmark Makefile
# ============================================

# 编译器和编译选项
CC := gcc
CLANG := clang
CFLAGS := -Wall -Wextra -O2 -g -std=gnu11
LDFLAGS := -lpthread

# 内核版本和路径
KERNEL_VERSION := $(shell uname -r)
KERNEL_HEADERS := /usr/src/linux-headers-$(KERNEL_VERSION)

# libbpf submodule 路径
LIBBPF_DIR := third_party/libbpf
LIBBPF_SRC := $(LIBBPF_DIR)/src
LIBBPF_OBJ := $(LIBBPF_SRC)/libbpf.a
LIBBPF_INCLUDES := -I$(LIBBPF_SRC)

# BPF 编译选项（添加必要的 include 路径）
BPF_CFLAGS := -O2 -target bpf -D__TARGET_ARCH_x86 -g \
	-I$(KERNEL_HEADERS)/include \
	-I$(KERNEL_HEADERS)/include/uapi \
	-I$(KERNEL_HEADERS)/arch/x86/include \
	-I$(KERNEL_HEADERS)/arch/x86/include/uapi \
	-I$(KERNEL_HEADERS)/arch/x86/include/generated \
	-I$(KERNEL_HEADERS)/arch/x86/include/generated/uapi

# 目录结构
SRC_DIR := basic
COMMON_DIR := common
COMMON_INC := $(COMMON_DIR)/include
COMMON_SRC := $(COMMON_DIR)/src
OUT_DIR := out
TEST_DIR := test
LOG_DIR := $(TEST_DIR)/logs
EBPF_DIR := ebpf
EBPF_SRC := $(EBPF_DIR)/src
EBPF_INC := $(EBPF_DIR)/include
EBPF_OUT := $(OUT_DIR)/ebpf

# 目标可执行文件
SERVER_BIN := $(OUT_DIR)/server
SERVER_EBPF_BIN := $(OUT_DIR)/server_ebpf
CLIENT_BIN := $(OUT_DIR)/client

# eBPF 文件
EBPF_OBJ := $(EBPF_OUT)/sockmap.bpf.o
SOCKMAP_LOADER_SRC := $(EBPF_SRC)/sockmap_loader.c

# 源文件
SERVER_SRC := $(SRC_DIR)/server.c
CLIENT_SRC := $(SRC_DIR)/client.c
COMMON_SRCS := $(COMMON_SRC)/logger.c $(COMMON_SRC)/monitor.c

# 包含路径
INCLUDE_DIRS := -I$(COMMON_INC) -I$(EBPF_INC) $(LIBBPF_INCLUDES)

# 生成带时间戳的日志文件名
TIMESTAMP := $(shell date +%Y%m%d_%H%M%S)
SERVER_LOG := $(LOG_DIR)/server_$(TIMESTAMP).log
CLIENT_LOG := $(LOG_DIR)/client_$(TIMESTAMP).log

# 颜色输出（让编译信息更好看）
COLOR_RESET := \033[0m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m

# ============================================
# 默认目标
# ============================================
.PHONY: all
all: banner dirs $(SERVER_BIN) $(CLIENT_BIN) success

# eBPF 版本（可选）
.PHONY: all-ebpf
all-ebpf: banner dirs $(LIBBPF_OBJ) $(SERVER_BIN) $(CLIENT_BIN) $(EBPF_OBJ) $(SERVER_EBPF_BIN) success-ebpf

# ============================================
# 创建必要的目录
# ============================================
.PHONY: dirs
dirs:
	@mkdir -p $(OUT_DIR)
	@mkdir -p $(EBPF_OUT)
	@mkdir -p $(LOG_DIR)
	@mkdir -p $(TEST_DIR)
	@echo "$(COLOR_BLUE)[✓] 目录创建完成$(COLOR_RESET)"

# ============================================
# 编译目标
# ============================================

# 编译 libbpf（从 submodule）
$(LIBBPF_OBJ): .gitmodules
	@echo "$(COLOR_YELLOW)[→] 检查 libbpf submodule...$(COLOR_RESET)"
	@if [ ! -d "$(LIBBPF_DIR)/.git" ]; then \
		echo "$(COLOR_YELLOW)[→] 初始化 libbpf submodule...$(COLOR_RESET)"; \
		git submodule update --init --recursive; \
	fi
	@echo "$(COLOR_YELLOW)[→] 编译 libbpf...$(COLOR_RESET)"
	@$(MAKE) -C $(LIBBPF_SRC) > /dev/null 2>&1
	@echo "$(COLOR_GREEN)[✓] libbpf 编译完成: $@$(COLOR_RESET)"

# 编译 eBPF 程序
$(EBPF_OBJ): $(EBPF_SRC)/sockmap.bpf.c
	@echo "$(COLOR_YELLOW)[→] 编译 eBPF 程序...$(COLOR_RESET)"
	@$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	@echo "$(COLOR_GREEN)[✓] eBPF 程序编译完成: $@$(COLOR_RESET)"

# 编译 server
$(SERVER_BIN): $(SERVER_SRC) $(COMMON_SRCS)
	@echo "$(COLOR_YELLOW)[→] 编译 Server...$(COLOR_RESET)"
	@$(CC) $(CFLAGS) $(INCLUDE_DIRS) -o $@ $^ $(LDFLAGS)
	@echo "$(COLOR_GREEN)[✓] Server 编译完成: $@$(COLOR_RESET)"

# 编译 server (eBPF 版本) - 使用 submodule 中的 libbpf
$(SERVER_EBPF_BIN): $(SERVER_SRC) $(COMMON_SRCS) $(SOCKMAP_LOADER_SRC) $(LIBBPF_OBJ)
	@echo "$(COLOR_YELLOW)[→] 编译 Server (eBPF 版本)...$(COLOR_RESET)"
	@$(CC) $(CFLAGS) $(INCLUDE_DIRS) -DENABLE_EBPF -o $@ $(SERVER_SRC) $(COMMON_SRCS) $(SOCKMAP_LOADER_SRC) $(LDFLAGS) $(LIBBPF_OBJ) -lelf -lz
	@echo "$(COLOR_GREEN)[✓] Server (eBPF) 编译完成 (使用 submodule libbpf): $@$(COLOR_RESET)"

# 编译 client
$(CLIENT_BIN): $(CLIENT_SRC) $(COMMON_SRCS)
	@echo "$(COLOR_YELLOW)[→] 编译 Client...$(COLOR_RESET)"
	@$(CC) $(CFLAGS) $(INCLUDE_DIRS) -o $@ $^ $(LDFLAGS)
	@echo "$(COLOR_GREEN)[✓] Client 编译完成: $@$(COLOR_RESET)"

# ============================================
# 清理目标
# ============================================
.PHONY: clean
clean:
	@echo "$(COLOR_YELLOW)[→] 清理编译产物...$(COLOR_RESET)"
	@rm -rf $(OUT_DIR)
	@rm -f $(SRC_DIR)/server $(SRC_DIR)/client  # 清理旧的编译产物
	@echo "$(COLOR_GREEN)[✓] 清理完成$(COLOR_RESET)"

# 清理 libbpf
.PHONY: clean-libbpf
clean-libbpf:
	@echo "$(COLOR_YELLOW)[→] 清理 libbpf...$(COLOR_RESET)"
	@if [ -d "$(LIBBPF_SRC)" ]; then $(MAKE) -C $(LIBBPF_SRC) clean > /dev/null 2>&1; fi
	@echo "$(COLOR_GREEN)[✓] libbpf 清理完成$(COLOR_RESET)"

# 深度清理（包括日志和 libbpf）
.PHONY: distclean
distclean: clean clean-libbpf
	@echo "$(COLOR_YELLOW)[→] 清理测试数据...$(COLOR_RESET)"
	@rm -rf $(TEST_DIR)
	@echo "$(COLOR_GREEN)[✓] 深度清理完成$(COLOR_RESET)"

# ============================================
# 运行目标
# ============================================

# 运行 server（后台运行，输出到日志）
.PHONY: run-server
run-server: $(SERVER_BIN)
	@echo "$(COLOR_BLUE)[→] 启动 Server (日志: $(SERVER_LOG))$(COLOR_RESET)"
	@$(SERVER_BIN) > $(SERVER_LOG) 2>&1 &
	@echo "$(COLOR_GREEN)[✓] Server 已启动 (PID: $$!)$(COLOR_RESET)"
	@echo "使用 'make stop-server' 或 'make server-stop' 停止服务器"

# 使用 super_client 启动 server（推荐）
.PHONY: server-start
server-start: $(SERVER_BIN)
	@python3 super_client.py start

# 停止 server（传统方式）
.PHONY: stop-server
stop-server:
	@echo "$(COLOR_YELLOW)[→] 停止 Server...$(COLOR_RESET)"
	@pkill -f "$(SERVER_BIN)" || echo "没有运行中的 Server"
	@echo "$(COLOR_GREEN)[✓] Server 已停止$(COLOR_RESET)"

# 使用 super_client 停止 server（推荐）
.PHONY: server-stop
server-stop:
	@python3 super_client.py stop

# 停止 eBPF server（需要 root 权限）
.PHONY: stop-server-ebpf
stop-server-ebpf:
	@echo "$(COLOR_YELLOW)[→] 停止 eBPF Server...$(COLOR_RESET)"
	@sudo pkill -9 server_ebpf || echo "$(COLOR_YELLOW)没有运行中的 eBPF Server$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)[✓] eBPF Server 已停止$(COLOR_RESET)"

# 查看 server 状态
.PHONY: server-status
server-status:
	@python3 super_client.py status

# 获取 server 统计信息
.PHONY: server-stats
server-stats:
	@python3 super_client.py stats

# 实时监控 server
.PHONY: server-watch
server-watch:
	@python3 super_client.py watch

# 运行 client（前台运行，同时输出到日志）
.PHONY: run-client
run-client: $(CLIENT_BIN)
	@echo "$(COLOR_BLUE)[→] 运行 Client (日志: $(CLIENT_LOG))$(COLOR_RESET)"
	@$(CLIENT_BIN) 2>&1 | tee $(CLIENT_LOG)

# 完整测试流程
.PHONY: test
test: all
	@echo ""
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)         开始性能基准测试$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo ""
	@$(MAKE) stop-server > /dev/null 2>&1 || true
	@sleep 1
	@$(MAKE) run-server
	@sleep 2
	@$(MAKE) run-client
	@echo ""
	@$(MAKE) stop-server
	@echo ""
	@echo "$(COLOR_GREEN)[✓] 测试完成！查看日志: $(LOG_DIR)/$(COLOR_RESET)"

# eBPF 版本测试流程（需要 root 权限）
.PHONY: test-ebpf
test-ebpf: all-ebpf
	@echo ""
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)       开始 eBPF 版本基准测试$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_YELLOW)[!] 注意: eBPF 测试需要 root 权限$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)[!] 请使用: sudo make test-ebpf$(COLOR_RESET)"
	@echo ""
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "$(COLOR_RED)[✗] 错误: 需要 root 权限运行此目标$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)    请使用: sudo make test-ebpf$(COLOR_RESET)"; \
		exit 1; \
	fi
	@pkill -f "$(SERVER_EBPF_BIN)" 2>/dev/null || true
	@sleep 1
	@echo "$(COLOR_YELLOW)[→] 启动 eBPF Server...$(COLOR_RESET)"
	@$(SERVER_EBPF_BIN) > $(LOG_DIR)/server_ebpf_$(TIMESTAMP).log 2>&1 &
	@sleep 2
	@echo "$(COLOR_YELLOW)[→] 运行 Client...$(COLOR_RESET)"
	@$(CLIENT_BIN) 2>&1 | tee $(LOG_DIR)/client_ebpf_$(TIMESTAMP).log
	@echo ""
	@pkill -f "$(SERVER_EBPF_BIN)" 2>/dev/null || true
	@echo ""
	@echo "$(COLOR_GREEN)[✓] eBPF 测试完成！查看日志: $(LOG_DIR)/$(COLOR_RESET)"

# 性能对比测试（使用 benchmark.sh）
.PHONY: benchmark
benchmark: all-ebpf
	@echo ""
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)         性能对比测试$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo ""
	@./benchmark.sh

# ============================================
# 查看日志
# ============================================
.PHONY: logs
logs:
	@echo "$(COLOR_BLUE)==== 最近的测试日志 =====$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_YELLOW)Server 日志:$(COLOR_RESET)"
	@ls -t $(LOG_DIR)/server_*.log 2>/dev/null | head -n1 | xargs tail -n 30 || echo "无 server 日志"
	@echo ""
	@echo "$(COLOR_YELLOW)Client 日志:$(COLOR_RESET)"
	@ls -t $(LOG_DIR)/client_*.log 2>/dev/null | head -n1 | xargs tail -n 30 || echo "无 client 日志"

# 列出所有测试日志
.PHONY: list-logs
list-logs:
	@echo "$(COLOR_BLUE)==== 所有测试日志 =====$(COLOR_RESET)"
	@ls -lht $(LOG_DIR)/*.log 2>/dev/null || echo "无测试日志"

# 实时查看最新 server 日志
.PHONY: tail-server
tail-server:
	@LATEST=$$(ls -t $(LOG_DIR)/server_*.log 2>/dev/null | head -n1); \
	if [ -n "$$LATEST" ]; then \
		echo "$(COLOR_BLUE)[→] 实时查看: $$LATEST$(COLOR_RESET)"; \
		tail -f $$LATEST; \
	else \
		echo "$(COLOR_YELLOW)[!] 无 server 日志$(COLOR_RESET)"; \
	fi

# 实时查看最新 client 日志
.PHONY: tail-client
tail-client:
	@LATEST=$$(ls -t $(LOG_DIR)/client_*.log 2>/dev/null | head -n1); \
	if [ -n "$$LATEST" ]; then \
		echo "$(COLOR_BLUE)[→] 实时查看: $$LATEST$(COLOR_RESET)"; \
		tail -f $$LATEST; \
	else \
		echo "$(COLOR_YELLOW)[!] 无 client 日志$(COLOR_RESET)"; \
	fi

# ============================================
# 调试和信息
# ============================================

# 显示编译信息
.PHONY: info
info:
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)         编译配置信息$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "编译器:       $(CC)"
	@echo "编译选项:     $(CFLAGS)"
	@echo "链接选项:     $(LDFLAGS)"
	@echo "源码目录:     $(SRC_DIR)"
	@echo "输出目录:     $(OUT_DIR)"
	@echo "日志目录:     $(LOG_DIR)"
	@echo "Server 源码:  $(SERVER_SRC)"
	@echo "Client 源码:  $(CLIENT_SRC)"
	@echo "========================================$(COLOR_RESET)"

# 检查依赖
.PHONY: check
check:
	@echo "$(COLOR_BLUE)[→] 检查编译环境...$(COLOR_RESET)"
	@which $(CC) > /dev/null || (echo "$(COLOR_YELLOW)[!] 未找到 gcc$(COLOR_RESET)" && exit 1)
	@echo "$(COLOR_GREEN)[✓] gcc 版本: $$($(CC) --version | head -n1)$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)[✓] 编译环境检查通过$(COLOR_RESET)"

# ============================================
# 帮助信息
# ============================================
.PHONY: help
help:
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)      TCP Echo Benchmark 构建系统$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_GREEN)编译目标:$(COLOR_RESET)"
	@echo "  make              - 编译所有程序（基础版本）"
	@echo "  make all-ebpf     - 编译所有程序（含 eBPF 版本）"
	@echo "  make clean        - 清理编译产物"
	@echo "  make distclean    - 清理编译产物和日志"
	@echo ""
	@echo "$(COLOR_GREEN)测试目标:$(COLOR_RESET)"
	@echo "  make test         - 测试基础版本（编译→启动→测试→停止）"
	@echo "  make test-ebpf    - 测试 eBPF 版本（需要 root 权限）"
	@echo "  make benchmark    - 性能对比测试（基础 vs eBPF）"
	@echo ""
	@echo "$(COLOR_GREEN)运行目标:$(COLOR_RESET)"
	@echo "  make run-server     - 启动 Server（后台运行，传统方式）"
	@echo "  make run-client     - 运行 Client（前台运行）"
	@echo "  make stop-server    - 停止 Server（传统方式）"
	@echo ""
	@echo "$(COLOR_GREEN)Super Client 控制（推荐）:$(COLOR_RESET)"
	@echo "  make server-start   - 启动 Server（使用 super_client）"
	@echo "  make server-stop    - 停止 Server（使用 super_client）"
	@echo "  make server-status  - 查看 Server 状态"
	@echo "  make server-stats   - 获取 Server 统计信息"
	@echo "  make server-watch   - 实时监控 Server"
	@echo ""
	@echo "$(COLOR_GREEN)eBPF Server 控制:$(COLOR_RESET)"
	@echo "  sudo make stop-server-ebpf  - 停止 eBPF Server（需要 root）"
	@echo ""
	@echo "$(COLOR_GREEN)日志目标:$(COLOR_RESET)"
	@echo "  make logs         - 查看最近的测试日志"
	@echo "  make list-logs    - 列出所有测试日志"
	@echo "  make tail-server  - 实时查看最新 Server 日志"
	@echo "  make tail-client  - 实时查看最新 Client 日志"
	@echo ""
	@echo "$(COLOR_GREEN)工具目标:$(COLOR_RESET)"
	@echo "  make info         - 显示编译配置信息"
	@echo "  make check        - 检查编译环境"
	@echo "  make help         - 显示此帮助信息"
	@echo ""
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"

# ============================================
# 美化输出
# ============================================
.PHONY: banner
banner:
	@echo ""
	@echo "$(COLOR_BLUE)╔════════════════════════════════════════╗$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)║     TCP Echo Benchmark Builder         ║$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)║     高性能 Echo Server + 压测工具       ║$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)╚════════════════════════════════════════╝$(COLOR_RESET)"
	@echo ""

.PHONY: success
success:
	@echo ""
	@echo "$(COLOR_GREEN)╔════════════════════════════════════════╗$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)║          编译成功完成！                 ║$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)╚════════════════════════════════════════╝$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BLUE)下一步:$(COLOR_RESET)"
	@echo "  • 运行完整测试: $(COLOR_YELLOW)make test$(COLOR_RESET)"
	@echo "  • 编译 eBPF 版本: $(COLOR_YELLOW)make all-ebpf$(COLOR_RESET)"
	@echo "  • 单独启动服务: $(COLOR_YELLOW)make run-server$(COLOR_RESET)"
	@echo "  • 查看帮助信息: $(COLOR_YELLOW)make help$(COLOR_RESET)"
	@echo ""

.PHONY: success-ebpf
success-ebpf:
	@echo ""
	@echo "$(COLOR_GREEN)╔════════════════════════════════════════╗$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)║     eBPF 版本编译成功完成！             ║$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)╚════════════════════════════════════════╝$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BLUE)下一步:$(COLOR_RESET)"
	@echo "  • 运行 eBPF 测试: $(COLOR_YELLOW)make test-ebpf$(COLOR_RESET)"
	@echo "  • 性能对比测试: $(COLOR_YELLOW)make benchmark$(COLOR_RESET)"
	@echo "  • 查看帮助信息: $(COLOR_YELLOW)make help$(COLOR_RESET)"
	@echo ""

# ============================================
# 保持 Make 不会删除中间文件
# ============================================
.PRECIOUS: $(SERVER_BIN) $(CLIENT_BIN)

# ============================================
# 禁用内置规则（加快 Make 速度）
# ============================================
.SUFFIXES:
