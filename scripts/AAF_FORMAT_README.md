# AAF (Animated Asset Format) 官方格式说明

## 概述

AAF是ESP32 EchoEar板子使用的官方动画资源格式，由`esp_emote_gfx`组件支持。

## 文件格式结构

```
Offset  Size    Description
0       1       Magic number (0x89)
1       3       Format string ("AAF") 
4       4       Total number of frames
8       4       Checksum of table + data
12      4       Length of table + data
16      N       Asset table (N = total_frames * 8)
16+N    M       Frame data (M = sum of all frame sizes)
```

## 详细说明

### 1. 文件头部 (16字节)
- **字节 0**: 魔数 `0x89`
- **字节 1-3**: 格式字符串 `"AAF"`
- **字节 4-7**: 总帧数 (小端序)
- **字节 8-11**: 校验和 (小端序)
- **字节 12-15**: 资源表+数据长度 (小端序)

### 2. 资源表 (每帧8字节)
每个帧条目包含：
- **字节 0-3**: 帧数据大小 (小端序)
- **字节 4-7**: 帧数据偏移量 (小端序)

### 3. 帧数据
每个帧以`0x5A5A`魔数开头，后跟RGB565格式的图像数据。

## 使用方法

### 使用官方脚本生成AAF

```bash
# 基本用法
python3 gif_to_aaf_official.py <输入目录> <输出文件.aaf>

# 示例
python3 gif_to_aaf_official.py ./frames ./emotion.aaf

# 指定图像格式
python3 gif_to_aaf_official.py ./frames ./emotion.aaf --pattern "*.png"
```

### 支持的图像格式
- PNG
- JPG/JPEG
- BMP

### 图像要求
- 建议尺寸：480x480 (EchoEar标准)
- 颜色格式：自动转换为RGB565
- 帧顺序：按文件名排序

## 验证生成的AAF文件

### 1. 检查文件头部
```bash
hexdump -C emotion.aaf | head -2
```
应该看到：
```
00000000  89 41 41 46 01 00 00 00  [checksum] [length]  |.AAF...........|
```

### 2. 检查帧数据
```bash
hexdump -C emotion.aaf | grep "5a 5a"
```
应该看到多个`5a 5a`序列，表示帧数据开始。

## 常见问题

### Q: 为什么我的AAF文件无法播放？
A: 检查以下几点：
1. 文件头部是否正确 (`89 41 41 46`)
2. 每帧是否以`5a 5a`开头
3. 校验和是否正确
4. 图像尺寸是否合适

### Q: 如何优化文件大小？
A: 
1. 减少图像尺寸
2. 使用PNG格式减少颜色
3. 减少帧数
4. 使用压缩工具

### Q: 支持哪些颜色深度？
A: 目前支持RGB565 (16位)，每个像素2字节。

## 技术细节

### RGB565格式
- 红色：5位 (0-31)
- 绿色：6位 (0-63)  
- 蓝色：5位 (0-31)

### 校验和算法
```python
def calculate_checksum(data):
    return sum(data) & 0xFFFFFFFF
```

### 字节序
所有多字节值都使用小端序 (Little Endian)。

## 参考

- `esp_emote_gfx`组件源码
- `gfx_aaf_format.c` - AAF格式解析器
- `gfx_aaf_dec.c` - AAF解码器

## 许可证

本工具基于Apache 2.0许可证，与ESP-IDF保持一致。 