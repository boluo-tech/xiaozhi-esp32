#!/bin/bash

# 测试 GIF to AAF Converter 脚本
# 使用方法: ./scripts/test_gif_converter.sh

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "🧪 测试 GIF to AAF Converter"
echo "================================"

# 检查脚本是否存在
if [[ ! -f "scripts/gif_to_aaf_converter.sh" ]]; then
    echo -e "${RED}❌ 错误: 找不到 gif_to_aaf_converter.sh 脚本${NC}"
    exit 1
fi

# 检查输入目录
if [[ ! -d "main/assets/gif" ]]; then
    echo -e "${YELLOW}⚠️  警告: 找不到默认输入目录 main/assets/gif${NC}"
    echo "请确保有 GIF 文件用于测试"
    exit 1
fi

# 检查 GIF 文件
gif_count=$(find "main/assets/gif" -name "*.gif" | wc -l)
if [[ $gif_count -eq 0 ]]; then
    echo -e "${YELLOW}⚠️  警告: 在 main/assets/gif 中没有找到 GIF 文件${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 找到 $gif_count 个 GIF 文件${NC}"

# 检查输出目录
if [[ ! -d "managed_components/espressif2022__esp_emote_gfx/emoji_normal" ]]; then
    echo -e "${YELLOW}⚠️  警告: 找不到默认输出目录${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 输出目录存在${NC}"

# 检查必要脚本
if [[ ! -f "managed_components/espressif2022__image_player/script/gif_to_split_bmp.py" ]]; then
    echo -e "${RED}❌ 错误: 找不到 gif_to_split_bmp.py 脚本${NC}"
    exit 1
fi

if [[ ! -f "managed_components/espressif2022__image_player/script/gif_merge.py" ]]; then
    echo -e "${RED}❌ 错误: 找不到 gif_merge.py 脚本${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 所有必要脚本都存在${NC}"

# 检查 Python 依赖
echo "检查 Python 依赖..."
python3 -c "import PIL, numpy, sklearn" 2>/dev/null && {
    echo -e "${GREEN}✅ Python 依赖已安装${NC}"
} || {
    echo -e "${YELLOW}⚠️  Python 依赖未安装，将在运行时自动安装${NC}"
}

echo ""
echo "🎯 测试准备完成！"
echo "运行以下命令开始转换："
echo ""
echo "  ./scripts/gif_to_aaf_converter.sh"
echo ""
echo "或者指定自定义目录："
echo ""
echo "  ./scripts/gif_to_aaf_converter.sh my_gifs/ my_output/"
echo "" 