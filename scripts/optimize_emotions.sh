#!/bin/bash

# =============================================================================
# 表情文件优化脚本 - 将GIF转换为优化的AAF格式
# 适用于ESP32 EchoEar设备
# =============================================================================

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    print_info "检查系统依赖..."
    
    # 检查Python3
    if ! command -v python3 &> /dev/null; then
        print_error "Python3 未安装，请先安装Python3"
        exit 1
    fi
    
    # 检查PIL库
    if ! python3 -c "from PIL import Image" &> /dev/null; then
        print_error "PIL (Pillow) 库未安装，请运行: pip3 install Pillow"
        exit 1
    fi
    
    print_success "所有依赖检查通过"
}

# 显示帮助信息
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help          显示此帮助信息"
    echo "  -i, --input DIR     输入GIF文件目录 (默认: xiaohei_gif)"
    echo "  -o, --output DIR    输出AAF文件目录 (默认: xiaohei_aaf_optimized)"
    echo "  -w, --width NUM     目标宽度 (默认: 32)"
    echo "  -h, --height NUM    目标高度 (默认: 32)"
    echo "  -q, --quality LEVEL 质量级别: low/medium/high (默认: high)"
    echo "  -c, --clean         清理临时文件"
    echo ""
    echo "示例:"
    echo "  $0                           # 使用默认设置"
    echo "  $0 -i my_gifs -o my_aafs    # 自定义输入输出目录"
    echo "  $0 -w 48 -h 48              # 自定义分辨率"
    echo "  $0 -q medium                 # 使用中等质量"
}

# 默认参数
INPUT_DIR="xiaohei_gif"
OUTPUT_DIR="xiaohei_aaf_optimized"
TARGET_WIDTH=32
TARGET_HEIGHT=32
QUALITY="high"
CLEAN_TEMP=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -i|--input)
            INPUT_DIR="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -w|--width)
            TARGET_WIDTH="$2"
            shift 2
            ;;
        --height)
            TARGET_HEIGHT="$2"
            shift 2
            ;;
        -q|--quality)
            QUALITY="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_TEMP=true
            shift
            ;;
        *)
            print_error "未知参数: $1"
            show_help
            exit 1
            ;;
    esac
done

# 主函数
main() {
    print_info "开始表情文件优化流程..."
    echo "============================================================"
    print_info "输入目录: $INPUT_DIR"
    print_info "输出目录: $OUTPUT_DIR"
    print_info "目标分辨率: ${TARGET_WIDTH}x${TARGET_HEIGHT}"
    print_info "质量级别: $QUALITY"
    echo "============================================================"
    
    # 检查依赖
    check_dependencies
    
    # 检查输入目录
    if [[ ! -d "$INPUT_DIR" ]]; then
        print_error "输入目录不存在: $INPUT_DIR"
        exit 1
    fi
    
    # 统计GIF文件
    GIF_COUNT=$(find "$INPUT_DIR" -name "*.gif" | wc -l)
    if [[ $GIF_COUNT -eq 0 ]]; then
        print_error "在 $INPUT_DIR 中未找到GIF文件"
        exit 1
    fi
    
    print_info "找到 $GIF_COUNT 个GIF文件"
    
    # 清理并创建输出目录
    if [[ -d "$OUTPUT_DIR" ]]; then
        print_info "清理之前的输出目录: $OUTPUT_DIR"
        rm -rf "$OUTPUT_DIR"
    fi
    mkdir -p "$OUTPUT_DIR"
    print_info "创建输出目录: $OUTPUT_DIR"
    
    # 运行Python优化脚本
    print_info "开始转换GIF文件..."
    
    # 调用Python脚本进行转换
    if python3 convert_optimized.py; then
        print_success "所有文件转换完成！"
        
        # 显示结果统计
        echo ""
        echo "============================================================"
        echo "转换结果统计:"
        echo "============================================================"
        
        # 统计文件大小 (macOS兼容)
        TOTAL_ORIGINAL=$(du -sk "$INPUT_DIR" | cut -f1)
        TOTAL_OPTIMIZED=$(du -sk "$OUTPUT_DIR" | cut -f1)
        
        echo "原始GIF文件总大小: ${TOTAL_ORIGINAL}KB"
        echo "优化AAF文件总大小: ${TOTAL_OPTIMIZED}KB"
        
        # 计算压缩比例
        if [[ $TOTAL_ORIGINAL -gt 0 ]]; then
            COMPRESSION_RATIO=$(echo "scale=1; (1 - $TOTAL_OPTIMIZED / $TOTAL_ORIGINAL) * 100" | bc -l 2>/dev/null || echo "计算失败")
            echo "总体压缩比例: ${COMPRESSION_RATIO}%"
        fi
        
        echo ""
        print_info "输出目录: $OUTPUT_DIR"
        print_info "所有文件已准备就绪，可以复制到EchoEar设备使用"
        
    else
        print_error "转换过程中出现错误"
        exit 1
    fi
}

# 清理临时文件
cleanup() {
    if [[ "$CLEAN_TEMP" == "true" ]]; then
        print_info "清理临时文件..."
        rm -rf "$OUTPUT_DIR"
        print_success "临时文件已清理"
    fi
}

# 错误处理
trap 'print_error "脚本执行被中断"; cleanup; exit 1' INT TERM

# 执行主函数
main "$@"

# 成功退出
print_success "表情优化流程完成！"
exit 0 