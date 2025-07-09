#ifndef EMOTION_CONFIG_H
#define EMOTION_CONFIG_H

#include <vector>
#include <string>

// 表情配置结构体
struct EmotionConfig {
    const char* name;           // 表情名称
    const char* emoji;          // emoji表情符号（用于非GIF模式）
    const char* description;    // 表情描述
};

// 动态生成的表情配置 - 基于实际存在的GIF文件
// 当前支持 3 个表情
static const std::vector<EmotionConfig> EMOTION_CONFIGS = {
    {"neutral", "😶", "中性"},
    {"relaxed", "😌", "放松"},
    {"sad", "😔", "悲伤"},
};

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
