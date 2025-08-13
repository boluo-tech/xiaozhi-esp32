#!/bin/bash

# GIF to AAF Converter for EchoEar Board
# 自动化 GIF 转 AAF 格式并集成到 echoear 板子的脚本
# 使用方法: ./scripts/gif_to_aaf_converter.sh [input_dir] [output_dir]

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认参数
INPUT_DIR="${1:-main/assets/gif}"
OUTPUT_DIR="${2:-managed_components/espressif2022__esp_emote_gfx/emoji_normal}"
TEMP_DIR="temp_gif_conversion"
BOARD_DIR="main/boards/echoear"

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."
    
    # 检查 Python 依赖
    python3 -c "import PIL, numpy, sklearn" 2>/dev/null || {
        log_warning "缺少 Python 依赖，正在安装..."
        pip3 install pillow numpy scikit-learn
    }
    
    # 检查必要文件
    if [[ ! -f "managed_components/espressif2022__image_player/script/gif_to_split_bmp.py" ]]; then
        log_error "找不到 gif_to_split_bmp.py 脚本"
        exit 1
    fi
    
    if [[ ! -f "managed_components/espressif2022__image_player/script/gif_merge.py" ]]; then
        log_error "找不到 gif_merge.py 脚本"
        exit 1
    fi
    
    log_success "依赖检查完成"
}

# 创建临时目录
create_temp_dirs() {
    log_info "创建临时目录..."
    rm -rf "$TEMP_DIR"
    mkdir -p "$TEMP_DIR/gifs" "$TEMP_DIR/output"
    log_success "临时目录创建完成"
}

# 复制 GIF 文件
copy_gif_files() {
    log_info "复制 GIF 文件到临时目录..."
    
    if [[ ! -d "$INPUT_DIR" ]]; then
        log_error "输入目录不存在: $INPUT_DIR"
        exit 1
    fi
    
    local gif_count=$(find "$INPUT_DIR" -name "*.gif" | wc -l)
    if [[ $gif_count -eq 0 ]]; then
        log_error "在 $INPUT_DIR 中没有找到 GIF 文件"
        exit 1
    fi
    
    cp "$INPUT_DIR"/*.gif "$TEMP_DIR/gifs/"
    log_success "复制了 $gif_count 个 GIF 文件"
}

# 转换 GIF 到分片位图
convert_gif_to_bmp() {
    log_info "开始转换 GIF 到分片位图..."
    
    cd "$TEMP_DIR"
    python3 "../managed_components/espressif2022__image_player/script/gif_to_split_bmp.py" \
        "./gifs" "./output" --split 16 --depth 4
    
    if [[ $? -ne 0 ]]; then
        log_error "GIF 转换失败"
        exit 1
    fi
    
    cd ..
    log_success "GIF 转换完成"
}

# 合并为 AAF 文件
merge_to_aaf() {
    log_info "开始合并为 AAF 文件..."
    
    cd "$TEMP_DIR"
    python3 "../managed_components/espressif2022__image_player/script/gif_merge.py" "./output"
    
    if [[ $? -ne 0 ]]; then
        log_error "AAF 合并失败"
        exit 1
    fi
    
    cd ..
    log_success "AAF 合并完成"
}

# 复制 AAF 文件到目标目录
copy_aaf_files() {
    log_info "复制 AAF 文件到目标目录..."
    
    if [[ ! -d "$OUTPUT_DIR" ]]; then
        log_error "输出目录不存在: $OUTPUT_DIR"
        exit 1
    fi
    
    cp "$TEMP_DIR/output"/*.aaf "$OUTPUT_DIR/"
    
    local aaf_count=$(find "$TEMP_DIR/output" -name "*.aaf" | wc -l)
    log_success "复制了 $aaf_count 个 AAF 文件"
}

# 显示文件大小统计
show_file_stats() {
    log_info "文件大小统计:"
    echo "----------------------------------------"
    echo "文件名                   大小"
    echo "----------------------------------------"
    
    for aaf_file in "$TEMP_DIR/output"/*.aaf; do
        if [[ -f "$aaf_file" ]]; then
            filename=$(basename "$aaf_file")
            size=$(du -h "$aaf_file" | cut -f1)
            echo "$filename$(printf '%*s' $((25 - ${#filename})) '')$size"
        fi
    done
    
    total_size=$(du -sh "$TEMP_DIR/output"/*.aaf | tail -1 | cut -f1)
    echo "----------------------------------------"
    echo "总计: $total_size"
    echo "----------------------------------------"
}

# 更新头文件
update_header_file() {
    log_info "更新头文件..."
    
    local header_file="$BOARD_DIR/mmap_generate_emoji_normal.h"
    local backup_file="$header_file.backup"
    
    # 备份原文件
    cp "$header_file" "$backup_file"
    
    # 获取当前文件数量
    local current_files=$(grep "MMAP_EMOJI_NORMAL_FILES" "$header_file" | grep -o '[0-9]*')
    local new_files=$((current_files + $(find "$TEMP_DIR/output" -name "*.aaf" | wc -l)))
    
    # 生成新的校验和（简单递增）
    local current_checksum=$(grep "MMAP_EMOJI_NORMAL_CHECKSUM" "$header_file" | grep -o '0x[0-9A-F]*')
    local new_checksum=$(printf "0x%04X" $((0x${current_checksum#0x} + 0x100)))
    
    # 更新文件数量和校验和
    sed -i.bak "s/MMAP_EMOJI_NORMAL_FILES[[:space:]]*[0-9]*/MMAP_EMOJI_NORMAL_FILES           $new_files/" "$header_file"
    sed -i.bak "s/MMAP_EMOJI_NORMAL_CHECKSUM[[:space:]]*0x[0-9A-F]*/MMAP_EMOJI_NORMAL_CHECKSUM        $new_checksum/" "$header_file"
    
    # 添加新的枚举项
    local enum_start=$(grep -n "enum MMAP_EMOJI_NORMAL_LISTS" "$header_file" | cut -d: -f1)
    local enum_end=$(grep -n "};" "$header_file" | tail -1 | cut -d: -f1)
    
    # 在枚举结束前添加新的 AAF 文件
    local insert_line=$((enum_end - 1))
    local enum_index=$current_files
    
    for aaf_file in "$TEMP_DIR/output"/*.aaf; do
        if [[ -f "$aaf_file" ]]; then
            filename=$(basename "$aaf_file")
            name_upper=$(echo "${filename%.*}" | tr '[:lower:]' '[:upper:]')
            enum_name="MMAP_EMOJI_NORMAL_${name_upper}_AAF"
            
            # 插入新的枚举项
            sed -i.bak "${insert_line}a\\    $enum_name = $enum_index,        /*!< $filename */" "$header_file"
            enum_index=$((enum_index + 1))
        fi
    done
    
    log_success "头文件更新完成"
}

# 更新 emote_display.cc 映射
update_emotion_mapping() {
    log_info "更新表情映射..."
    
    local display_file="$BOARD_DIR/emote_display.cc"
    local backup_file="$display_file.backup"
    
    # 备份原文件
    cp "$display_file" "$backup_file"
    
    # 创建新的映射内容
    local new_mapping="    using EmotionParam = std::tuple<int, bool, int>;\n    static const std::unordered_map<std::string, EmotionParam> emotion_map = {\n"
    
    # 添加新的映射项
    for aaf_file in "$TEMP_DIR/output"/*.aaf; do
        if [[ -f "$aaf_file" ]]; then
            filename=$(basename "$aaf_file")
            emotion_name="${filename%.*}"
            name_upper=$(echo "$emotion_name" | tr '[:lower:]' '[:upper:]')
            enum_name="MMAP_EMOJI_NORMAL_${name_upper}_AAF"
            
            new_mapping+="        {\"$emotion_name\",      {$enum_name,        true,  20}},\n"
        fi
    done
    
    # 添加原有的其他映射
    new_mapping+="        {\"confident\",   {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"delicious\",   {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"sleepy\",      {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"silly\",       {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"surprised\",   {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"shocked\",     {MMAP_EMOJI_NORMAL_SHOCKED_ONE_AAF,   true,  20}},\n"
    new_mapping+="        {\"thinking\",    {MMAP_EMOJI_NORMAL_THINKING_ONE_AAF,  true,  20}},\n"
    new_mapping+="        {\"winking\",     {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"relaxed\",     {MMAP_EMOJI_NORMAL_HAPPY_AAF,         true,  20}},\n"
    new_mapping+="        {\"confused\",    {MMAP_EMOJI_NORMAL_DIZZY_ONE_AAF,     true,  20}},\n"
    new_mapping+="        {\"idle\",        {MMAP_EMOJI_NORMAL_IDLE_ONE_AAF,      false, 20}},\n"
    new_mapping+="    };"
    
    # 替换原有的映射
    local start_line=$(grep -n "using EmotionParam" "$display_file" | cut -d: -f1)
    local end_line=$(grep -n "};" "$display_file" | grep -A 20 "emotion_map" | tail -1 | cut -d: -f1)
    
    if [[ -n "$start_line" && -n "$end_line" ]]; then
        sed -i.bak "${start_line},${end_line}c\\$new_mapping" "$display_file"
        log_success "表情映射更新完成"
    else
        log_warning "无法找到表情映射位置，请手动更新"
    fi
}

# 清理临时文件
cleanup() {
    log_info "清理临时文件..."
    rm -rf "$TEMP_DIR"
    log_success "清理完成"
}

# 显示使用说明
show_usage() {
    echo "GIF to AAF Converter for EchoEar Board"
    echo ""
    echo "使用方法:"
    echo "  $0 [input_dir] [output_dir]"
    echo ""
    echo "参数:"
    echo "  input_dir    GIF 文件输入目录 (默认: main/assets/gif)"
    echo "  output_dir   AAF 文件输出目录 (默认: managed_components/espressif2022__esp_emote_gfx/emoji_normal)"
    echo ""
    echo "示例:"
    echo "  $0                                    # 使用默认目录"
    echo "  $0 my_gifs/                          # 指定输入目录"
    echo "  $0 my_gifs/ my_output/               # 指定输入和输出目录"
}

# 主函数
main() {
    echo "========================================"
    echo "GIF to AAF Converter for EchoEar Board"
    echo "========================================"
    echo ""
    
    # 显示参数
    log_info "输入目录: $INPUT_DIR"
    log_info "输出目录: $OUTPUT_DIR"
    echo ""
    
    # 执行转换流程
    check_dependencies
    create_temp_dirs
    copy_gif_files
    convert_gif_to_bmp
    merge_to_aaf
    copy_aaf_files
    show_file_stats
    update_header_file
    update_emotion_mapping
    cleanup
    
    echo ""
    log_success "🎉 GIF 到 AAF 转换完成！"
    echo ""
    log_info "下一步操作:"
    echo "  1. 编译项目: idf.py build"
    echo "  2. 测试表情: display->SetEmotion(\"happy\")"
    echo "  3. 检查生成的文件: $OUTPUT_DIR/*.aaf"
    echo ""
}

# 处理命令行参数
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    show_usage
    exit 0
fi

# 执行主函数
main "$@" 