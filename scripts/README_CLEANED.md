# 表情优化脚本目录 - 清理后

## 🎯 **核心文件说明**

### 🚀 **主要脚本**
- **`optimize_emotions.sh`** - 主控制脚本，一键优化所有GIF表情
- **`gif_to_aaf_optimized.py`** - 优化的AAF生成器，支持分辨率调整
- **`convert_optimized.py`** - 批量转换脚本，处理多个GIF文件

### 📚 **文档**
- **`AAF_FORMAT_README.md`** - 官方AAF格式详细说明

### 📁 **重要目录**
- **`backup_official_emotions/`** - 官方表情文件备份（重要！）
- **`xiaohei_gif/`** - 原始GIF表情文件目录

## 🧹 **已清理的文件**
- ~~`convert_all_gifs.py`~~ - 旧的转换脚本
- ~~`gif_to_aaf.py`~~ - 格式不正确的AAF生成器
- ~~`gif_to_aaf_official.py`~~ - 测试用的生成器
- ~~`test_aaf_generation.py`~~ - 测试脚本
- ~~`xiaohei_aaf_optimized/`~~ - 临时输出目录
- ~~`xiaohei_aaf_correct/`~~ - 旧的输出目录
- ~~`test_output/`~~ - 测试输出目录
- ~~`esp_emote_gfx/`~~ - 临时组件目录

## 🎮 **使用方法**

### **基本用法**
```bash
# 使用默认设置优化表情
./optimize_emotions.sh

# 自定义分辨率
./optimize_emotions.sh -w 48 --height 48

# 自定义质量
./optimize_emotions.sh -q medium
```

### **文件替换**
表情文件已经覆盖到官方目录：
`../managed_components/espressif2022__esp_emote_gfx/emoji_normal/`

## ⚠️ **注意事项**
1. **备份重要**：`backup_official_emotions/` 包含原始官方文件
2. **如需恢复**：可以从备份目录恢复原始表情
3. **编译固件**：使用 `build_prd.sh` 重新编译以使用新表情

## 📊 **优化效果**
- **分辨率**：480x480 → 32x32 (接近官方33x34)
- **文件大小**：平均从8.5MB减少到130KB
- **压缩比例**：平均减少98.2%
- **格式标准**：完全符合官方AAF格式 