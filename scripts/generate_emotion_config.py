#!/usr/bin/env python3
"""
动态生成表情配置文件
根据实际存在的GIF文件自动生成表情配置
"""

import os
import re
import sys

# 表情名称到emoji的映射
EMOTION_EMOJI_MAP = {
    "neutral": "😶",
    "happy": "🙂", 
    "laughing": "😆",
    "funny": "😂",
    "sad": "😔",
    "angry": "😠",
    "crying": "😭",
    "loving": "🥰",
    "embarrassed": "😳",
    "surprised": "😯",
    "shocked": "😱",
    "thinking": "🤔",
    "winking": "😉",
    "cool": "😎",
    "relaxed": "😌",
    "delicious": "😋",
    "kissy": "😘",
    "confident": "😏",
    "sleepy": "😴",
    "silly": "🤪",
    "confused": "🙄"
}

# 表情名称到中文描述的映射
EMOTION_DESC_MAP = {
    "neutral": "中性",
    "happy": "开心",
    "laughing": "大笑",
    "funny": "搞笑",
    "sad": "悲伤",
    "angry": "愤怒",
    "crying": "哭泣",
    "loving": "爱心",
    "embarrassed": "尴尬",
    "surprised": "惊讶",
    "shocked": "震惊",
    "thinking": "思考",
    "winking": "眨眼",
    "cool": "酷炫",
    "relaxed": "放松",
    "delicious": "美味",
    "kissy": "亲吻",
    "confident": "自信",
    "sleepy": "困倦",
    "silly": "傻气",
    "confused": "困惑"
}

def get_gif_files():
    """获取所有GIF文件名（不含扩展名）"""
    gif_dir = "main/assets/gif"
    gif_files = []
    
    if not os.path.exists(gif_dir):
        print(f"错误: GIF目录不存在 {gif_dir}")
        return []
    
    for filename in os.listdir(gif_dir):
        if filename.endswith('.c') and filename != '.DS_Store':
            # 提取文件名（不含.c扩展名）
            emotion_name = filename[:-2]
            gif_files.append(emotion_name)
    
    return sorted(gif_files)

def generate_emotion_config_h(gif_files):
    """生成emotion_config.h文件"""
    content = f'''#ifndef EMOTION_CONFIG_H
#define EMOTION_CONFIG_H

#include <vector>
#include <string>

// 表情配置结构体
struct EmotionConfig {{
    const char* name;           // 表情名称
    const char* emoji;          // emoji表情符号（用于非GIF模式）
    const char* description;    // 表情描述
}};

// 动态生成的表情配置 - 基于实际存在的GIF文件
// 当前支持 {len(gif_files)} 个表情
static const std::vector<EmotionConfig> EMOTION_CONFIGS = {{
'''
    
    for emotion in gif_files:
        emoji = EMOTION_EMOJI_MAP.get(emotion, "😶")
        desc = EMOTION_DESC_MAP.get(emotion, emotion)
        content += f'    {{"{emotion}", "{emoji}", "{desc}"}},\n'
    
    content += '''};

// 获取表情名称列表（用于随机选择）
inline std::vector<const char*> GetEmotionNames() {
    std::vector<const char*> names;
    names.reserve(EMOTION_CONFIGS.size());
    for (const auto& config : EMOTION_CONFIGS) {
        names.push_back(config.name);
    }
    return names;
}

// 根据名称查找表情配置
inline const EmotionConfig* FindEmotionConfig(const char* name) {
    for (const auto& config : EMOTION_CONFIGS) {
        if (std::string(config.name) == std::string(name)) {
            return &config;
        }
    }
    return nullptr;
}

// 获取默认表情（neutral）
inline const EmotionConfig* GetDefaultEmotion() {
    return &EMOTION_CONFIGS[0]; // neutral
}

// 获取表情数量
inline size_t GetEmotionCount() {
    return EMOTION_CONFIGS.size();
}

// 获取所有表情配置
inline const std::vector<EmotionConfig>& GetAllEmotionConfigs() {
    return EMOTION_CONFIGS;
}

#endif // EMOTION_CONFIG_H
'''
    
    return content

def generate_gif_resources_h(gif_files):
    """生成gif_resources.h文件"""
    content = f'''#ifndef GIF_RESOURCES_H
#define GIF_RESOURCES_H

#include <lvgl.h>

// GIF资源声明宏
#define DECLARE_GIF_EMOTION(name) LV_IMG_DECLARE(name)

// 动态声明所有GIF表情资源
// 当前支持 {len(gif_files)} 个表情
'''
    
    for emotion in gif_files:
        content += f'DECLARE_GIF_EMOTION({emotion});\n'
    
    content += '''
// GIF资源映射结构体
struct GifEmotionResource {
    const char* name;
    const lv_img_dsc_t* gif;
};

// 获取GIF资源映射表
inline std::vector<GifEmotionResource> GetGifEmotionResources() {
    std::vector<GifEmotionResource> resources;
    resources.reserve(''' + str(len(gif_files)) + ''');
    
'''
    
    for emotion in gif_files:
        content += f'    resources.push_back(GifEmotionResource{{"{emotion}", &{emotion}}});\n'
    
    content += '''    
    return resources;
}

// 根据名称查找GIF资源
inline const lv_img_dsc_t* FindGifEmotionResource(const char* name) {
    auto resources = GetGifEmotionResources();
    for (const auto& resource : resources) {
        if (std::string(resource.name) == std::string(name)) {
            return resource.gif;
        }
    }
    return &neutral; // 默认返回neutral
}

#endif // GIF_RESOURCES_H
'''
    
    return content

def main():
    """主函数"""
    print("🔍 扫描GIF文件...")
    gif_files = get_gif_files()
    
    if not gif_files:
        print("❌ 没有找到GIF文件")
        sys.exit(1)
    
    print(f"✅ 找到 {len(gif_files)} 个GIF文件:")
    for emotion in gif_files:
        print(f"  - {emotion}")
    
    # 生成emotion_config.h
    print("\n📝 生成 emotion_config.h...")
    config_content = generate_emotion_config_h(gif_files)
    
    config_file = "main/assets/emotion_config.h"
    with open(config_file, 'w', encoding='utf-8') as f:
        f.write(config_content)
    print(f"✅ 已生成 {config_file}")
    
    # 生成gif_resources.h
    print("📝 生成 gif_resources.h...")
    resources_content = generate_gif_resources_h(gif_files)
    
    resources_file = "main/assets/gif_resources.h"
    with open(resources_file, 'w', encoding='utf-8') as f:
        f.write(resources_content)
    print(f"✅ 已生成 {resources_file}")
    
    print(f"\n🎉 完成! 动态生成了支持 {len(gif_files)} 个表情的配置文件")
    print("💡 提示: 添加新的GIF文件后，重新运行此脚本即可更新配置")

if __name__ == "__main__":
    main() 