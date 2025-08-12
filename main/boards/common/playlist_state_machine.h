#ifndef PLAYLIST_STATE_MACHINE_H
#define PLAYLIST_STATE_MACHINE_H

#include <functional>
#include <map>
#include <vector>
#include <atomic>
#include <string>

// 播放状态枚举
enum class PlaylistState {
    IDLE,           // 空闲状态
    LOADING,        // 加载歌单
    PLAYING,        // 播放中
    PAUSED,         // 暂停
    STOPPING,       // 停止中
    ERROR           // 错误状态
};

// 播放事件
enum class PlaylistEvent {
    PLAY_REQUESTED,     // 请求播放
    PAUSE_REQUESTED,    // 请求暂停
    STOP_REQUESTED,     // 请求停止
    NEXT_REQUESTED,     // 请求下一首
    PREV_REQUESTED,     // 请求上一首
    SEEK_REQUESTED,     // 请求跳转
    EMERGENCY_STOP,     // 紧急停止
    LOAD_COMPLETED,     // 加载完成
    PLAY_COMPLETED,     // 播放完成
    ERROR_OCCURRED,     // 发生错误
    RESET_REQUESTED     // 请求重置
};

// 状态转换规则
struct StateTransition {
    PlaylistState from_state;
    PlaylistEvent event;
    PlaylistState to_state;
    std::function<bool()> condition;  // 转换条件
    std::function<void()> action;     // 转换动作
    
    StateTransition(PlaylistState from, PlaylistEvent evt, PlaylistState to,
                   std::function<bool()> cond = nullptr,
                   std::function<void()> act = nullptr)
        : from_state(from), event(evt), to_state(to), 
          condition(cond), action(act) {}
};

// 状态机回调
struct StateMachineCallbacks {
    std::function<void(PlaylistState, PlaylistState)> on_state_change;
    std::function<void(PlaylistEvent, const std::string&)> on_event;
    std::function<void(const std::string&)> on_error;
};

class PlaylistStateMachine {
private:
    // 当前状态
    std::atomic<PlaylistState> current_state_;
    
    // 状态转换规则
    std::vector<StateTransition> transitions_;
    
    // 回调函数
    StateMachineCallbacks callbacks_;
    
    // 状态历史
    std::vector<std::pair<PlaylistState, uint32_t>> state_history_;
    static constexpr size_t MAX_HISTORY_SIZE = 10;
    
    // 错误信息
    std::string last_error_;
    
    // 私有方法
    bool IsValidTransition(PlaylistState from, PlaylistEvent event) const;
    void ExecuteTransition(const StateTransition& transition);
    void AddToHistory(PlaylistState state);
    void LogStateChange(PlaylistState old_state, PlaylistState new_state);
    
public:
    PlaylistStateMachine();
    ~PlaylistStateMachine();
    
    // 初始化和配置
    void Initialize();
    void SetCallbacks(const StateMachineCallbacks& callbacks);
    
    // 状态转换
    bool ProcessEvent(PlaylistEvent event, const std::string& context = "");
    bool SetState(PlaylistState new_state, const std::string& reason = "");
    
    // 状态查询
    PlaylistState GetCurrentState() const;
    bool IsInState(PlaylistState state) const;
    bool CanProcessEvent(PlaylistEvent event) const;
    
    // 状态历史
    const std::vector<std::pair<PlaylistState, uint32_t>>& GetStateHistory() const;
    void ClearHistory();
    
    // 错误处理
    const std::string& GetLastError() const;
    void ClearError();
    
    // 调试和日志
    void PrintCurrentState() const;
    void PrintStateHistory() const;
    void PrintValidTransitions() const;
    
    // 状态机验证
    bool ValidateStateMachine() const;
    bool HasDeadlock() const;
    bool HasUnreachableStates() const;
};

// 状态机工厂
class StateMachineFactory {
public:
    static PlaylistStateMachine* CreateDefaultStateMachine();
    static PlaylistStateMachine* CreateMinimalStateMachine();
    static PlaylistStateMachine* CreateAdvancedStateMachine();
};

#endif // PLAYLIST_STATE_MACHINE_H 