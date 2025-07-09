#!/bin/bash
source $IDF_PATH/export.sh
# ===================================
# 构建和烧录脚本
# 功能：
# 1. 重置git到远程分支
# 2. 转换图片
# 3. 复制文件
# 4. 生成动态表情配置
# 5. 编译
# 6. 合并bin文件
# 7. 烧录固件
# ===================================

release_name='doro'
output_dir='tmp_releases'  # 默认输出目录
skip_conversion=1  # 0表示不跳过图片转换步骤，1表示跳过
image_input_dir="" # 图片输入目录
do_build=1 # 1表示编译
do_package=1 # 1表示打包
do_flash=0 # 1表示烧录

# 解析命令行参数
while getopts "s:r:i:b:p:f:o:" opt; do
  case $opt in
    s)
      skip_conversion=$OPTARG
      ;;
    r)
      release_name=$OPTARG
      ;;
    i)
      image_input_dir=$OPTARG
      ;;
    b)
      do_build=$OPTARG
      ;;
    p)
      do_package=$OPTARG
      ;;
    f)
      do_flash=$OPTARG
      ;;
    o)
      output_dir=$OPTARG
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
  esac
done

# 激活环境
# source /Users/xufeili/esp/v5.4.1/esp-idf/export.sh # 由调用方(gui.py)负责

# 设置颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 打印带颜色的信息
print_info() {
    echo -e "${GREEN}[INFO] $1${NC}"
}

print_error() {
    echo -e "${RED}[ERROR] $1${NC}"
}
 
# ===================================
# 步骤1: 执行图片转换
# ===================================
if [ $skip_conversion -eq 0 ]; then
    print_info "Step 1: Converting LVGL8 images to LVGL9 format..."
    if [ -z "$image_input_dir" ]; then
        print_error "Image input directory not provided for conversion."
        exit 1
    fi
    cd scripts/lvgl8_2_lvgl9
    python3 convert_lvgl8_to_lvgl9.py "$image_input_dir"
    if [ $? -ne 0 ]; then
        print_error "Image conversion failed!"
        exit 1
    fi

    # ===================================
    # 步骤2: 清空并复制文件到 main/assets/gif
    # ===================================
    print_info "Step 2: Copying converted files to main/assets/gif..."
    cd ../..
    rm -f main/assets/gif/*.c
    cp scripts/lvgl8_2_lvgl9/output/*.c main/assets/gif/
    if [ $? -ne 0 ]; then
        print_error "Copying files failed!"
        exit 1
    fi

    # ===================================
    # 步骤2.5: 生成动态表情配置
    # ===================================
    print_info "Step 2.5: Generating dynamic emotion configuration..."
    python3 scripts/generate_emotion_config.py
    if [ $? -ne 0 ]; then
        print_error "Emotion configuration generation failed!"
        exit 1
    fi
else
    print_info "Skipping steps 1, 2, and 2.5 (image conversion, copying, and emotion config generation)..."
fi

# ===================================
# 步骤3: 清除编译
# ===================================
if [ $do_build -eq 1 ]; then
    print_info "Step 3: Cleaning build..."
    idf.py clean
    if [ $? -ne 0 ]; then
        print_error "Clean failed!"
        exit 1
    fi
else
    print_info "Skipping step 3 (Clean)..."
fi

# ===================================
# 步骤4: 开始编译
# ===================================
if [ $do_build -eq 1 ]; then
    print_info "Step 4: Starting build..."
    idf.py build
    if [ $? -ne 0 ]; then
        print_error "Build failed!"
        exit 1
    fi
else
    print_info "Skipping step 4 (Build)..."
fi

# ===================================
# 步骤5: 复制bin文件到新文件夹并打包
# ===================================
if [ $do_package -eq 1 ]; then
    print_info "Step 5: Copying bin files and creating archive..."

    # 创建以当前日期时间命名的文件夹，使用 output_dir 变量
    RELEASE_DIR="${output_dir}/${release_name}_$(date +%m%d%H-%M)"
    mkdir -p "$RELEASE_DIR"

    # 复制bin文件
    cp build/bootloader/bootloader.bin "$RELEASE_DIR/"
    cp build/partition_table/partition-table.bin "$RELEASE_DIR/"
    cp build/ota_data_initial.bin "$RELEASE_DIR/"
    cp build/srmodels/srmodels.bin "$RELEASE_DIR/"
    cp build/xiaozhi.bin "$RELEASE_DIR/"

    # 创建压缩包
    cd "$RELEASE_DIR"
    zip -r "../$(basename $RELEASE_DIR).zip" .
    cd - > /dev/null

    # 删除原文件夹
    rm -rf "$RELEASE_DIR"

    #python3 scripts/release.py ${release_name}
    #print_info "Bin files archived to releases/$(basename $RELEASE_DIR).zip"
else
    print_info "Skipping step 5 (Packaging)..."
fi

# ===================================
# 步骤6: 烧录固件
# ===================================
if [ $do_flash -eq 1 ]; then
    print_info "Step 6: Flashing firmware..."
    idf.py flash
    if [ $? -ne 0 ]; then
        print_error "Flash failed!"
        exit 1
    fi
else
    print_info "Skipping step 6 (Flashing)..."
fi

print_info "All steps completed successfully!" 