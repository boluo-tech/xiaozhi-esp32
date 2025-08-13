# GIF to AAF Converter for EchoEar Board

这是一个自动化脚本，用于将 GIF 动画文件转换为 AAF 格式并集成到 EchoEar 板子中。

## 功能特性

- 🎬 自动转换 GIF 到 AAF 格式
- 🔧 自动更新头文件和映射
- 📊 显示文件大小统计
- 🛡️ 自动备份原文件
- 🧹 自动清理临时文件
- 🎨 彩色日志输出

## 使用方法

### 基本用法

```bash
# 使用默认目录（推荐）
./scripts/gif_to_aaf_converter.sh

# 指定输入目录
./scripts/gif_to_aaf_converter.sh my_gifs/

# 指定输入和输出目录
./scripts/gif_to_aaf_converter.sh my_gifs/ my_output/
```

### 查看帮助

```bash
./scripts/gif_to_aaf_converter.sh --help
```

## 目录结构

```
xiaozhi-esp32/
├── main/assets/gif/                    # 默认 GIF 输入目录
│   ├── happy.gif
│   ├── sad.gif
│   └── ...
├── managed_components/espressif2022__esp_emote_gfx/emoji_normal/  # 默认输出目录
│   ├── happy.aaf
│   ├── sad.aaf
│   └── ...
└── main/boards/echoear/
    ├── mmap_generate_emoji_normal.h    # 自动更新的头文件
    └── emote_display.cc                # 自动更新的映射文件
```

## 转换参数

- **分片高度**: 16 像素
- **位深度**: 4 位
- **FPS**: 20
- **重复播放**: 是（除了 neutral 和 idle）

## 工作流程

1. **依赖检查** - 检查 Python 依赖和必要脚本
2. **文件复制** - 复制 GIF 文件到临时目录
3. **GIF 转换** - 转换为分片位图格式
4. **AAF 合并** - 合并为 AAF 文件
5. **文件复制** - 复制 AAF 文件到目标目录
6. **统计显示** - 显示文件大小统计
7. **头文件更新** - 更新 `mmap_generate_emoji_normal.h`
8. **映射更新** - 更新 `emote_display.cc` 中的表情映射
9. **清理** - 清理临时文件

## 输出示例

```
========================================
GIF to AAF Converter for EchoEar Board
========================================

[INFO] 输入目录: main/assets/gif
[INFO] 输出目录: managed_components/espressif2022__esp_emote_gfx/emoji_normal

[INFO] 检查依赖...
[SUCCESS] 依赖检查完成
[INFO] 创建临时目录...
[SUCCESS] 临时目录创建完成
[INFO] 复制 GIF 文件到临时目录...
[SUCCESS] 复制了 9 个 GIF 文件
[INFO] 开始转换 GIF 到分片位图...
[SUCCESS] GIF 转换完成
[INFO] 开始合并为 AAF 文件...
[SUCCESS] AAF 合并完成
[INFO] 复制 AAF 文件到目标目录...
[SUCCESS] 复制了 9 个 AAF 文件
[INFO] 文件大小统计:
----------------------------------------
文件名                   大小
----------------------------------------
angry.aaf                391K
crying.aaf               392K
embarrassed.aaf          847K
funny.aaf                463K
happy.aaf                583K
laughing.aaf             1.5M
loving.aaf               263K
neutral.aaf              316K
sad.aaf                  318K
----------------------------------------
总计: 5.0M
----------------------------------------
[INFO] 更新头文件...
[SUCCESS] 头文件更新完成
[INFO] 更新表情映射...
[SUCCESS] 表情映射更新完成
[INFO] 清理临时文件...
[SUCCESS] 清理完成

[SUCCESS] 🎉 GIF 到 AAF 转换完成！

[INFO] 下一步操作:
  1. 编译项目: idf.py build
  2. 测试表情: display->SetEmotion("happy")
  3. 检查生成的文件: managed_components/espressif2022__esp_emote_gfx/emoji_normal/*.aaf
```

## 注意事项

1. **备份文件**: 脚本会自动备份原文件（添加 `.backup` 后缀）
2. **依赖要求**: 需要 Python 3 和 PIL、numpy、sklearn 库
3. **文件命名**: GIF 文件名将直接用作表情名称
4. **编译**: 转换完成后需要重新编译项目

## 故障排除

### 常见错误

1. **找不到脚本文件**
   ```
   [ERROR] 找不到 gif_to_split_bmp.py 脚本
   ```
   - 确保在项目根目录运行脚本

2. **Python 依赖缺失**
   ```
   ModuleNotFoundError: No module named 'PIL'
   ```
   - 脚本会自动安装依赖，或手动运行：`pip3 install pillow numpy scikit-learn`

3. **输入目录不存在**
   ```
   [ERROR] 输入目录不存在: main/assets/gif
   ```
   - 检查输入目录路径是否正确

### 手动恢复

如果脚本执行失败，可以手动恢复：

```bash
# 恢复头文件
cp main/boards/echoear/mmap_generate_emoji_normal.h.backup main/boards/echoear/mmap_generate_emoji_normal.h

# 恢复映射文件
cp main/boards/echoear/emote_display.cc.backup main/boards/echoear/emote_display.cc
```

## 自定义配置

可以通过修改脚本中的以下变量来自定义转换参数：

```bash
# 在脚本中修改这些参数
SPLIT_HEIGHT=16      # 分片高度
BIT_DEPTH=4          # 位深度
FPS=20               # 帧率
REPEAT=true          # 是否重复播放
```

## 版本历史

- v1.0.0 - 初始版本，支持基本的 GIF 到 AAF 转换
- 支持自动头文件和映射更新
- 支持文件大小统计和彩色日志输出 