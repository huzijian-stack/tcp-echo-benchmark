#!/bin/bash
# ============================================
# TCP Echo Benchmark 环境配置脚本
# ============================================
# 功能：自动安装和配置项目所需的所有依赖
# 适用：Ubuntu 20.04+
# ============================================

set -e

# 颜色定义
COLOR_RESET='\033[0m'
COLOR_GREEN='\033[32m'
COLOR_YELLOW='\033[33m'
COLOR_BLUE='\033[34m'
COLOR_RED='\033[31m'

# 日志函数
log_info() {
    echo -e "${COLOR_BLUE}[INFO]${COLOR_RESET} $1"
}

log_success() {
    echo -e "${COLOR_GREEN}[✓]${COLOR_RESET} $1"
}

log_warn() {
    echo -e "${COLOR_YELLOW}[!]${COLOR_RESET} $1"
}

log_error() {
    echo -e "${COLOR_RED}[✗]${COLOR_RESET} $1"
}

# 检查是否为 root 用户
check_root() {
    if [ "$EUID" -eq 0 ]; then
        log_warn "检测到以 root 用户运行，部分操作可能不需要 sudo"
        SUDO=""
    else
        SUDO="sudo"
    fi
}

# 检测系统信息
detect_system() {
    log_info "检测系统信息..."
    OS_NAME=$(lsb_release -is 2>/dev/null || echo "Unknown")
    OS_VERSION=$(lsb_release -rs 2>/dev/null || echo "Unknown")
    KERNEL_VERSION=$(uname -r)

    log_success "操作系统: $OS_NAME $OS_VERSION"
    log_success "内核版本: $KERNEL_VERSION"

    if [ "$OS_NAME" != "Ubuntu" ] && [ "$OS_NAME" != "Debian" ]; then
        log_warn "此脚本主要针对 Ubuntu/Debian 系统，其他系统可能需要手动调整"
    fi
}

# 更新包管理器
update_package_manager() {
    log_info "更新包管理器..."
    $SUDO apt-get update -qq
    log_success "包管理器更新完成"
}

# 安装基础开发工具
install_basic_tools() {
    log_info "检查并安装基础开发工具..."

    BASIC_PACKAGES=(
        "build-essential"
        "git"
        "python3"
        "python3-pip"
        "pkg-config"
        "net-tools"
        "wget"
        "curl"
    )

    MISSING_PACKAGES=()
    for pkg in "${BASIC_PACKAGES[@]}"; do
        if ! dpkg -l | grep -q "^ii  $pkg "; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
        log_info "安装缺失的基础工具: ${MISSING_PACKAGES[*]}"
        $SUDO apt-get install -y "${MISSING_PACKAGES[@]}"
        log_success "基础工具安装完成"
    else
        log_success "基础工具已安装"
    fi

    # 验证安装
    gcc --version | head -n1
    python3 --version
    git --version
}

# 配置 Clang/LLVM
configure_clang() {
    log_info "配置 Clang/LLVM 环境..."

    # 检查 clang-18 是否安装
    if ! command -v clang-18 &> /dev/null; then
        log_info "安装 clang-18..."

        # 添加 LLVM 官方源
        if [ ! -f /etc/apt/sources.list.d/llvm.list ]; then
            wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | $SUDO apt-key add -
            $SUDO add-apt-repository -y "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-18 main"
            $SUDO apt-get update -qq
        fi

        $SUDO apt-get install -y clang-18 llvm-18 lld-18
        log_success "clang-18 安装完成"
    else
        log_success "clang-18 已安装: $(clang-18 --version | head -n1)"
    fi

    # 配置默认 clang 版本
    log_info "配置 clang-18 为默认版本..."
    $SUDO update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100
    $SUDO update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100
    $SUDO update-alternatives --install /usr/bin/llvm-strip llvm-strip /usr/bin/llvm-strip-18 100

    # 移除旧版本 clang-10 的 alternatives（如果存在）
    if command -v clang-10 &> /dev/null; then
        log_warn "检测到 clang-10，建议移除以避免冲突"
        read -p "是否卸载 clang-10? (y/n): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            $SUDO apt-get remove -y clang-10 llvm-10 || true
            $SUDO apt-get autoremove -y
            log_success "clang-10 已卸载"
        fi
    fi

    log_success "默认 clang 版本: $(clang --version | head -n1)"
}

# 安装内核头文件
install_kernel_headers() {
    log_info "检查内核头文件..."

    KERNEL_HEADERS_PKG="linux-headers-$(uname -r)"

    if [ ! -d "/usr/src/linux-headers-$(uname -r)" ]; then
        log_info "安装内核头文件: $KERNEL_HEADERS_PKG"
        $SUDO apt-get install -y "$KERNEL_HEADERS_PKG"
        log_success "内核头文件安装完成"
    else
        log_success "内核头文件已安装: /usr/src/linux-headers-$(uname -r)"
    fi
}

# 安装 eBPF 依赖
install_ebpf_dependencies() {
    log_info "检查并安装 eBPF 依赖..."

    EBPF_PACKAGES=(
        "libelf-dev"
        "zlib1g-dev"
        "libbpf-dev"
    )

    MISSING_PACKAGES=()
    for pkg in "${EBPF_PACKAGES[@]}"; do
        if ! dpkg -l | grep -q "^ii  $pkg "; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done

    if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
        log_info "安装缺失的 eBPF 依赖: ${MISSING_PACKAGES[*]}"
        $SUDO apt-get install -y "${MISSING_PACKAGES[@]}"
        log_success "eBPF 依赖安装完成"
    else
        log_success "eBPF 依赖已安装"
    fi

    # 检查 libbpf 版本
    if dpkg -l | grep -q "^ii  libbpf-dev "; then
        LIBBPF_VERSION=$(dpkg -l | grep "^ii  libbpf-dev " | awk '{print $3}')
        log_success "libbpf-dev 版本: $LIBBPF_VERSION"
    fi
}

# 编译安装 libbpf（可选，用于需要最新版本的场景）
compile_libbpf() {
    log_info "是否需要从源码编译最新版本的 libbpf?"
    log_warn "当前已安装的 libbpf-dev 通常已足够使用"
    read -p "是否继续编译安装 libbpf? (y/n): " -n 1 -r
    echo

    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "跳过 libbpf 源码编译"
        return
    fi

    log_info "从源码编译 libbpf..."

    LIBBPF_DIR="/tmp/libbpf"

    if [ -d "$LIBBPF_DIR" ]; then
        log_warn "删除旧的 libbpf 源码目录"
        rm -rf "$LIBBPF_DIR"
    fi

    # 克隆 libbpf 仓库
    git clone https://github.com/libbpf/libbpf.git "$LIBBPF_DIR"
    cd "$LIBBPF_DIR/src"

    # 编译并安装
    make
    $SUDO make install

    # 更新链接库缓存
    $SUDO ldconfig

    cd -
    log_success "libbpf 源码编译安装完成"
}

# 验证环境
verify_environment() {
    log_info "验证开发环境..."

    echo ""
    log_info "========================================="
    log_info "         环境验证结果"
    log_info "========================================="

    # 检查 gcc
    if command -v gcc &> /dev/null; then
        GCC_VERSION=$(gcc --version | head -n1)
        log_success "gcc: $GCC_VERSION"
    else
        log_error "gcc 未安装"
    fi

    # 检查 clang
    if command -v clang &> /dev/null; then
        CLANG_VERSION=$(clang --version | head -n1)
        log_success "clang: $CLANG_VERSION"
    else
        log_error "clang 未安装"
    fi

    # 检查 python3
    if command -v python3 &> /dev/null; then
        PYTHON_VERSION=$(python3 --version)
        log_success "python3: $PYTHON_VERSION"
    else
        log_error "python3 未安装"
    fi

    # 检查 make
    if command -v make &> /dev/null; then
        MAKE_VERSION=$(make --version | head -n1)
        log_success "make: $MAKE_VERSION"
    else
        log_error "make 未安装"
    fi

    # 检查内核头文件
    if [ -d "/usr/src/linux-headers-$(uname -r)" ]; then
        log_success "内核头文件: /usr/src/linux-headers-$(uname -r)"
    else
        log_error "内核头文件未安装"
    fi

    # 检查 libelf
    if pkg-config --exists libelf; then
        LIBELF_VERSION=$(pkg-config --modversion libelf)
        log_success "libelf: $LIBELF_VERSION"
    else
        log_error "libelf-dev 未安装"
    fi

    # 检查 libbpf
    if pkg-config --exists libbpf; then
        LIBBPF_VERSION=$(pkg-config --modversion libbpf)
        log_success "libbpf: $LIBBPF_VERSION"
    else
        log_warn "libbpf 未通过 pkg-config 检测（可能已安装但未配置）"
    fi

    echo ""
    log_info "========================================="
}

# 测试编译
test_compile() {
    log_info "测试编译项目..."

    if [ ! -f "Makefile" ]; then
        log_error "未找到 Makefile，请在项目根目录运行此脚本"
        return 1
    fi

    # 清理旧的编译产物
    make clean > /dev/null 2>&1 || true

    # 测试基础版本编译
    log_info "测试基础版本编译..."
    if make > /dev/null 2>&1; then
        log_success "基础版本编译成功"
    else
        log_error "基础版本编译失败，请检查编译环境"
        return 1
    fi

    # 测试 eBPF 版本编译
    log_info "测试 eBPF 版本编译..."
    if make all-ebpf > /dev/null 2>&1; then
        log_success "eBPF 版本编译成功"
    else
        log_error "eBPF 版本编译失败，请检查 eBPF 环境配置"
        return 1
    fi

    log_success "所有编译测试通过！"
}

# 主函数
main() {
    echo ""
    echo -e "${COLOR_BLUE}╔════════════════════════════════════════╗${COLOR_RESET}"
    echo -e "${COLOR_BLUE}║   TCP Echo Benchmark 环境配置工具      ║${COLOR_RESET}"
    echo -e "${COLOR_BLUE}╚════════════════════════════════════════╝${COLOR_RESET}"
    echo ""

    check_root
    detect_system

    echo ""
    log_info "开始配置开发环境..."
    echo ""

    update_package_manager
    install_basic_tools
    configure_clang
    install_kernel_headers
    install_ebpf_dependencies

    echo ""
    compile_libbpf

    echo ""
    verify_environment

    echo ""
    log_info "是否测试编译项目？"
    read -p "测试编译? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        test_compile
    fi

    echo ""
    echo -e "${COLOR_GREEN}╔════════════════════════════════════════╗${COLOR_RESET}"
    echo -e "${COLOR_GREEN}║        环境配置完成！                   ║${COLOR_RESET}"
    echo -e "${COLOR_GREEN}╚════════════════════════════════════════╝${COLOR_RESET}"
    echo ""

    log_info "下一步："
    echo "  1. 编译项目:        make clean && make all-ebpf"
    echo "  2. 运行基准测试:    ./benchmark.sh"
    echo "  3. 查看帮助:        make help"
    echo ""
}

# 运行主函数
main "$@"
