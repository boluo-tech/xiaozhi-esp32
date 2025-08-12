# 唤醒词打断音乐播放修复方案

## 问题描述

在播放歌单音乐时，当用户使用唤醒词"你好小智"打断时：
- 有时候会报错直接强制重启设备
- 有时候会继续播放歌曲
- 希望打断时能停止歌单播放，清空音乐播放相关任务

## 问题根源

### 1. 竞态条件
- 歌单播放任务在检查设备状态时，可能正好在状态切换的瞬间
- 多个线程同时访问和修改播放状态

### 2. 资源清理不彻底
- `StopStreaming()` 只停止当前歌曲，不停止歌单任务
- 歌单任务可能继续运行，导致资源冲突

### 3. 状态检查时机
- 歌单任务的状态检查有延迟，可能错过状态变化

## 解决方案

### 1. 新增紧急停止方法

```cpp
void Esp32Music::EmergencyStop() {
    ESP_LOGI(TAG, "Emergency stop triggered - clearing all music tasks");
    
    // 1. 立即设置停止标志
    is_playing_ = false;
    is_downloading_ = false;
    
    // 2. 通知所有等待的线程
    buffer_cv_.notify_all();
    
    // 3. 强制停止所有任务
    if (playlist_task_handle_ != nullptr) {
        vTaskDelete(playlist_task_handle_);
        playlist_task_handle_ = nullptr;
    }
    
    if (lyric_task_handle_ != nullptr) {
        is_lyric_running_ = false;
        vTaskDelete(lyric_task_handle_);
        lyric_task_handle_ = nullptr;
    }
    
    // 4. 清理所有资源
    ClearAudioBuffer();
    CleanupMp3Decoder();
    lyrics_.clear();
    
    // 5. 重置播放状态
    current_playlist_index_ = 0;
    playlist_.clear();
    current_song_name_.clear();
    song_name_displayed_ = false;
}
```

### 2. 修改状态切换逻辑

```cpp
// 在Application::SetDeviceState中
if (previous_state == kDeviceStateIdle && state != kDeviceStateIdle) {
    auto music = board.GetMusic();
    if (music) {
        // 使用紧急停止来彻底清理所有音乐相关任务
        music->EmergencyStop();
    }
}
```

### 3. 改进状态检查逻辑

```cpp
// 在歌单播放任务中
while (music->is_playing_ && wait_count < max_wait_count) {
    DeviceState current_state = app.GetDeviceState();
    
    // 如果用户唤醒，立即停止播放
    if (current_state == kDeviceStateListening) {
        music->EmergencyStop();
        break;
    }
    
    // 如果设备状态不是idle或speaking，立即停止播放
    if (current_state != kDeviceStateIdle && current_state != kDeviceStateSpeaking) {
        music->EmergencyStop();
        break;
    }
}
```

### 4. 改进音频播放状态检查

```cpp
// 在PlayAudioStream中
while (is_playing_) {
    DeviceState current_state = app.GetDeviceState();
    
    // 如果用户唤醒，立即停止播放
    if (current_state == kDeviceStateListening) {
        break;  // 直接退出播放循环
    }
    
    // 如果设备状态不是idle或speaking，立即停止播放
    if (current_state != kDeviceStateIdle && current_state != kDeviceStateSpeaking) {
        break;  // 直接退出播放循环
    }
}
```

## 修复效果

### 修复前
- ❌ 唤醒时可能继续播放音乐
- ❌ 可能出现系统重启
- ❌ 资源清理不彻底

### 修复后
- ✅ 唤醒时立即停止所有音乐播放
- ✅ 彻底清理所有音乐相关任务
- ✅ 避免资源冲突和系统重启
- ✅ 响应更加及时和可靠

## 测试方法

1. 播放歌单音乐
2. 在播放过程中说"你好小智"
3. 验证：
   - 音乐立即停止
   - 没有系统重启
   - 设备正常进入聆听状态
   - 可以正常进行对话

## 注意事项

1. EmergencyStop是强制停止，会丢失当前播放进度
2. 如果需要恢复播放，需要重新调用PlayPlaylist
3. 建议在UI上给用户明确的音乐停止提示 