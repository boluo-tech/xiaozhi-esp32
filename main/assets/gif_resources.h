#ifndef GIF_RESOURCES_H
#define GIF_RESOURCES_H

#include <lvgl.h>

// GIF资源声明宏
#define DECLARE_GIF_EMOTION(name) LV_IMG_DECLARE(name)

// 动态声明所有GIF表情资源
// 当前支持 9 个表情
DECLARE_GIF_EMOTION(angry);
DECLARE_GIF_EMOTION(crying);
DECLARE_GIF_EMOTION(embarrassed);
DECLARE_GIF_EMOTION(funny);
DECLARE_GIF_EMOTION(happy);
DECLARE_GIF_EMOTION(laughing);
DECLARE_GIF_EMOTION(loving);
DECLARE_GIF_EMOTION(neutral);
DECLARE_GIF_EMOTION(sad);

// GIF资源映射结构体
struct GifEmotionResource {
    const char* name;
    const lv_img_dsc_t* gif;
};

// 获取GIF资源映射表
inline std::vector<GifEmotionResource> GetGifEmotionResources() {
    std::vector<GifEmotionResource> resources;
    resources.reserve(9);
    
    resources.push_back(GifEmotionResource{"angry", &angry});
    resources.push_back(GifEmotionResource{"crying", &crying});
    resources.push_back(GifEmotionResource{"embarrassed", &embarrassed});
    resources.push_back(GifEmotionResource{"funny", &funny});
    resources.push_back(GifEmotionResource{"happy", &happy});
    resources.push_back(GifEmotionResource{"laughing", &laughing});
    resources.push_back(GifEmotionResource{"loving", &loving});
    resources.push_back(GifEmotionResource{"neutral", &neutral});
    resources.push_back(GifEmotionResource{"sad", &sad});
    
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
