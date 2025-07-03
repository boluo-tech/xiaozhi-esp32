#!/bin/bash

# ===================================
# 构建和烧录脚本
# 功能：
# 1. 重置git到远程分支
# 2. 转换图片
# 3. 复制文件
# 4. 编译
# 5. 合并bin文件
# 6. 烧录固件
# ===================================


# 激活环境
source $IDF_PATH/export.sh
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
# 步骤3: 清除编译
# ===================================
print_info "Step 3: Cleaning build..."
idf.py clean
if [ $? -ne 0 ]; then
    print_error "Clean failed!"
    exit 1
fi

# ===================================
# 步骤4: 开始编译
# ===================================
print_info "Step 4: Starting build..."
idf.py build
if [ $? -ne 0 ]; then
    print_error "Build failed!"
    exit 1
fi

# ===================================
# 步骤5: 复制bin文件到新文件夹并打包
# ===================================
print_info "Step 5: Copying bin files and creating archive..."

# 创建以当前日期时间命名的文件夹
#RELEASE_DIR="tmp_releases/$(date +%m-%d-%H-%M)"
#mokdir -p "$RELEASE_DIR"

# 复制bin文件
#cp build/bootloader/bootloader.bin "$RELEASE_DIR/"
#cp build/partition_table/partition-table.bin "$RELEASE_DIR/"
#cp build/ota_data_initial.bin "$RELEASE_DIR/"
#cp build/srmodels/srmodels.bin "$RELEASE_DIR/"
#cp build/xiaozhi.bin "$RELEASE_DIR/"

# 创建压缩包
#cd "$RELEASE_DIR"
#zip -r "../$(basename $RELEASE_DIR).zip" .
#cd - > /dev/null

# 删除原文件夹
#rm -rf "$RELEASE_DIR"

print_info "Bin files archived to releases/$(basename $RELEASE_DIR).zip"

# ===================================
# 步骤6: 烧录固件
# ===================================
idf.py flash 
#python3 $IDF_PATH/components/esptool_py/esptool/esptool.py  \
#  --chip esp32s3 -p /dev/tty.usbmodem-141101 -b 921600 \
#  --before=default_reset --after=hard_reset write_flash \
#  --flash_mode dio --flash_freq 80m --flash_size 16MB \
#  0x0 releases/merged_firmware.bin

print_info "All steps completed successfully!"

 


