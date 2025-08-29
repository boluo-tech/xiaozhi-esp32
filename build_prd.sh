#!/bin/bash

# ===================================
# EchoEar 喵伴 构建和烧录脚本
# 功能：
# 1. 设置编译目标为 esp32p4
# 2. 复制配置文件
# 3. 动态配置OTA地址
# 4. 编译
# 5. 合并固件
# 6. 烧录固件
# ===================================

# 默认配置
ota_url="https://api.tenclass.net/xiaozhi/ota/"  # 默认生产环境OTA
ota_url="http://121.40.150.97:8002/xiaozhi/ota/"
do_set_target=1    # 1表示设置编译目标
do_copy_config=0   # 1表示复制配置文件
do_config_ota=1    # 1表示配置OTA地址
do_clean=1         # 1表示清除编译
do_build=1         # 1表示编译
do_merge=1         # 1表示合并固件
do_flash=0         # 1表示烧录固件

# 解析命令行参数
while getopts "t:c:o:l:b:m:f:" opt; do
  case $opt in
    t)
      if [ "$OPTARG" = "test" ]; then
        ota_url="http://121.40.150.97:8002/xiaozhi/ota/"
      elif [ "$OPTARG" = "prod" ]; then
        ota_url="https://api.tenclass.net/xiaozhi/ota/"
      else
        ota_url="$OPTARG"
      fi
      ;;
    c)
      do_copy_config=$OPTARG
      ;;
    o)
      do_config_ota=$OPTARG
      ;;
    l)
      do_clean=$OPTARG
      ;;
    b)
      do_build=$OPTARG
      ;;
    m)
      do_merge=$OPTARG
      ;;
    f)
      do_flash=$OPTARG
      ;;
    \?)
      echo "使用方法: $0 [选项]"
      echo "选项:"
      echo "  -t <test|prod|url>    设置OTA地址 (test=测试环境, prod=生产环境, 或自定义URL)"
      echo "  -c <0|1>              是否复制配置文件 (默认: 1)"
      echo "  -o <0|1>              是否配置OTA地址 (默认: 1)"
      echo "  -l <0|1>              是否清除编译 (默认: 1)"
      echo "  -b <0|1>              是否编译 (默认: 1)"
      echo "  -m <0|1>              是否合并固件 (默认: 1)"
      echo "  -f <0|1>              是否烧录固件 (默认: 0)"
      echo ""
      echo "示例:"
      echo "  $0 -t test -f 1        # 使用测试环境OTA并烧录"
      echo "  $0 -t prod -m 1        # 使用生产环境OTA并合并固件"
      echo "  $0 -b 1 -m 1           # 只编译和合并固件"
      exit 1
      ;;
  esac
done

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

 
# 激活ESP-IDF环境
# ===================================
if [ -z "$IDF_PATH" ]; then
    print_error "IDF_PATH 环境变量未设置，请先运行 source \$IDF_PATH/export.sh"
    exit 1
fi

source $IDF_PATH/export.sh 

print_info "OTA URL: $ota_url"

# ===================================
# 步骤1: 设置编译目标为 esp32p4
# ===================================
if [ $do_set_target -eq 1 ]; then
    print_info "Step 1: Setting target to esp32p4..."
    idf.py set-target esp32s3
    if [ $? -ne 0 ]; then
        print_error "Set target failed!"
        exit 1
    fi
else
    print_info "Step 1: Skipping set-target..."
fi

# ===================================
# 步骤2: 复制配置文件
# ===================================
if [ $do_copy_config -eq 1 ]; then
    print_info "Step 2: Copying config file..."
    cp main/boards/echoear/sdkconfig.echoear sdkconfig
    if [ $? -ne 0 ]; then
        print_error "Copy config failed!"
        exit 1
    fi
else
    print_info "Step 2: Skipping copy config..."
fi

# ===================================
# 步骤3: 动态配置OTA地址
# ===================================
if [ $do_config_ota -eq 1 ]; then
    print_info "Step 3: Configuring OTA URL..."
    # 使用sed命令替换sdkconfig中的OTA_URL
    sed -i.bak "s|CONFIG_OTA_URL=.*|CONFIG_OTA_URL=\"$ota_url\"|" sdkconfig
    if [ $? -ne 0 ]; then
        print_error "OTA URL configuration failed!"
        exit 1
    fi
    print_info "OTA URL configured to: $ota_url"
else
    print_info "Step 3: Skipping OTA configuration..."
fi

# ===================================
# 步骤4: 清除编译
# ===================================
if [ $do_clean -eq 1 ]; then
    print_info "Step 4: Cleaning build..."
    idf.py clean
    if [ $? -ne 0 ]; then
        print_error "Clean failed!"
        exit 1
    fi
else
    print_info "Step 4: Skipping clean..."
fi

# ===================================
# 步骤5: 开始编译
# ===================================
if [ $do_build -eq 1 ]; then
    print_info "Step 5: Starting build..."
    idf.py build
    if [ $? -ne 0 ]; then
        print_error "Build failed!"
        exit 1
    fi
else
    print_info "Step 5: Skipping build..."
fi

# ===================================
# 步骤6: 合并固件为单个bin文件
# ===================================
if [ $do_merge -eq 1 ]; then
    print_info "Step 6: Merging firmware into single bin file..."
    esptool.py --chip esp32p4 merge_bin --output xiaozhi_merged.bin --flash_mode dio --flash_size 32MB 0x2000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10d000 build/ota_data_initial.bin 0x110000 build/srmodels/srmodels.bin 0x200000 build/xiaozhi.bin
    if [ $? -ne 0 ]; then
        print_error "Merge firmware failed!"
        exit 1
    fi
    print_info "Merged firmware saved as: xiaozhi_merged.bin"
else
    print_info "Step 6: Skipping merge firmware..."
fi

# ===================================
# 步骤7: 烧录固件
# ===================================
if [ $do_flash -eq 1 ]; then
    print_info "Step 7: Flashing firmware..."
    idf.py flash
    if [ $? -ne 0 ]; then
        print_error "Flash failed!"
        exit 1
    fi
else
    print_info "Step 7: Skipping flash..."
fi

print_info "All steps completed successfully!" 
 