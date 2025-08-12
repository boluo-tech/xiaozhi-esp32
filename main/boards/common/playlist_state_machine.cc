#include "playlist_state_machine.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "PlaylistStateMachine";

// 状态字符串映射
static const char* STATE_STRINGS[] = {
    "IDLE",
    "LOADING", 
    "PLAYING",
    "PAUSED",
    "STOPPING",
    "ERROR"
};

// 事件字符串映射
static const char* EVENT_STRINGS[] = {
    "PLAY_REQUESTED",
    "PAUSE_REQUESTED", 
    "STOP_REQUESTED",
    "NEXT_REQUESTED",
    "PREV_REQUESTED",
    "SEEK_REQUESTED",
    "EMERGENCY_STOP",
    "LOAD_COMPLETED",
    "PLAY_COMPLETED",
    "ERROR_OCCURRED",
    "RESET_REQUESTED"
};

PlaylistStateMachine::PlaylistStateMachine() 
    : current_state_(PlaylistState::IDLE) {
    ESP_LOGI(TAG, "PlaylistStateMachine created");
}

PlaylistStateMachine::~PlaylistStateMachine() {
    ESP_LOGI(TAG, "PlaylistStateMachine destroyed");
}

void PlaylistStateMachine::Initialize() {
    ESP_LOGI(TAG, "Initializing PlaylistStateMachine");
    
    // 定义状态转换规则
    transitions_.clear();
    
    // IDLE状态的转换
    transitions_.emplace_back(PlaylistState::IDLE, PlaylistEvent::PLAY_REQUESTED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::IDLE, PlaylistEvent::EMERGENCY_STOP, PlaylistState::IDLE);
    
    // LOADING状态的转换
    transitions_.emplace_back(PlaylistState::LOADING, PlaylistEvent::LOAD_COMPLETED, PlaylistState::PLAYING);
    transitions_.emplace_back(PlaylistState::LOADING, PlaylistEvent::ERROR_OCCURRED, PlaylistState::ERROR);
    transitions_.emplace_back(PlaylistState::LOADING, PlaylistEvent::EMERGENCY_STOP, PlaylistState::STOPPING);
    transitions_.emplace_back(PlaylistState::LOADING, PlaylistEvent::STOP_REQUESTED, PlaylistState::STOPPING);
    
    // PLAYING状态的转换
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::PAUSE_REQUESTED, PlaylistState::PAUSED);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::STOP_REQUESTED, PlaylistState::STOPPING);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::NEXT_REQUESTED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::PREV_REQUESTED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::SEEK_REQUESTED, PlaylistState::PLAYING);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::PLAY_COMPLETED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::ERROR_OCCURRED, PlaylistState::ERROR);
    transitions_.emplace_back(PlaylistState::PLAYING, PlaylistEvent::EMERGENCY_STOP, PlaylistState::STOPPING);
    
    // PAUSED状态的转换
    transitions_.emplace_back(PlaylistState::PAUSED, PlaylistEvent::PLAY_REQUESTED, PlaylistState::PLAYING);
    transitions_.emplace_back(PlaylistState::PAUSED, PlaylistEvent::STOP_REQUESTED, PlaylistState::STOPPING);
    transitions_.emplace_back(PlaylistState::PAUSED, PlaylistEvent::NEXT_REQUESTED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::PAUSED, PlaylistEvent::PREV_REQUESTED, PlaylistState::LOADING);
    transitions_.emplace_back(PlaylistState::PAUSED, PlaylistEvent::EMERGENCY_STOP, PlaylistState::STOPPING);
    
    // STOPPING状态的转换
    transitions_.emplace_back(PlaylistState::STOPPING, PlaylistEvent::RESET_REQUESTED, PlaylistState::IDLE);
    transitions_.emplace_back(PlaylistState::STOPPING, PlaylistEvent::EMERGENCY_STOP, PlaylistState::IDLE);
    
    // ERROR状态的转换
    transitions_.emplace_back(PlaylistState::ERROR, PlaylistEvent::RESET_REQUESTED, PlaylistState::IDLE);
    transitions_.emplace_back(PlaylistState::ERROR, PlaylistEvent::EMERGENCY_STOP, PlaylistState::IDLE);
    
    // 初始化状态历史
    state_history_.clear();
    AddToHistory(PlaylistState::IDLE);
    
    ESP_LOGI(TAG, "PlaylistStateMachine initialized with %d transitions", (int)transitions_.size());
}

void PlaylistStateMachine::SetCallbacks(const StateMachineCallbacks& callbacks) {
    callbacks_ = callbacks;
    ESP_LOGI(TAG, "State machine callbacks set");
}

bool PlaylistStateMachine::ProcessEvent(PlaylistEvent event, const std::string& context) {
    PlaylistState old_state = current_state_.load();
    
    ESP_LOGI(TAG, "Processing event %s in state %s, context: %s", 
             EVENT_STRINGS[static_cast<int>(event)], 
             STATE_STRINGS[static_cast<int>(old_state)], 
             context.c_str());
    
    // 查找有效的状态转换
    for (const auto& transition : transitions_) {
        if (transition.from_state == old_state && transition.event == event) {
            // 检查转换条件
            if (transition.condition && !transition.condition()) {
                ESP_LOGW(TAG, "Transition condition failed for event %s", EVENT_STRINGS[static_cast<int>(event)]);
                continue;
            }
            
            // 执行转换
            ExecuteTransition(transition);
            return true;
        }
    }
    
    // 没有找到有效的转换
    ESP_LOGW(TAG, "No valid transition found for event %s in state %s", 
             EVENT_STRINGS[static_cast<int>(event)], 
             STATE_STRINGS[static_cast<int>(old_state)]);
    
    // 调用错误回调
    if (callbacks_.on_error) {
        std::string error_msg = "Invalid event " + std::string(EVENT_STRINGS[static_cast<int>(event)]) + 
                               " in state " + std::string(STATE_STRINGS[static_cast<int>(old_state)]);
        callbacks_.on_error(error_msg);
    }
    
    return false;
}

bool PlaylistStateMachine::SetState(PlaylistState new_state, const std::string& reason) {
    PlaylistState old_state = current_state_.load();
    
    if (old_state == new_state) {
        ESP_LOGD(TAG, "State already in %s", STATE_STRINGS[static_cast<int>(new_state)]);
        return true;
    }
    
    ESP_LOGI(TAG, "Setting state from %s to %s, reason: %s", 
             STATE_STRINGS[static_cast<int>(old_state)], 
             STATE_STRINGS[static_cast<int>(new_state)], 
             reason.c_str());
    
    current_state_.store(new_state);
    AddToHistory(new_state);
    LogStateChange(old_state, new_state);
    
    // 调用状态变化回调
    if (callbacks_.on_state_change) {
        callbacks_.on_state_change(old_state, new_state);
    }
    
    return true;
}

void PlaylistStateMachine::ExecuteTransition(const StateTransition& transition) {
    ESP_LOGI(TAG, "Executing transition: %s -> %s", 
             STATE_STRINGS[static_cast<int>(transition.from_state)], 
             STATE_STRINGS[static_cast<int>(transition.to_state)]);
    
    // 执行转换动作
    if (transition.action) {
        transition.action();
    }
    
    // 设置新状态
    SetState(transition.to_state, "Transition executed");
}

void PlaylistStateMachine::AddToHistory(PlaylistState state) {
    uint32_t timestamp = esp_timer_get_time() / 1000; // 转换为毫秒
    
    state_history_.emplace_back(state, timestamp);
    
    // 保持历史记录在限制范围内
    if (state_history_.size() > MAX_HISTORY_SIZE) {
        state_history_.erase(state_history_.begin());
    }
}

void PlaylistStateMachine::LogStateChange(PlaylistState old_state, PlaylistState new_state) {
    ESP_LOGI(TAG, "State changed: %s -> %s", 
             STATE_STRINGS[static_cast<int>(old_state)], 
             STATE_STRINGS[static_cast<int>(new_state)]);
}

PlaylistState PlaylistStateMachine::GetCurrentState() const {
    return current_state_.load();
}

bool PlaylistStateMachine::IsInState(PlaylistState state) const {
    return current_state_.load() == state;
}

bool PlaylistStateMachine::CanProcessEvent(PlaylistEvent event) const {
    PlaylistState current = current_state_.load();
    
    for (const auto& transition : transitions_) {
        if (transition.from_state == current && transition.event == event) {
            return true;
        }
    }
    return false;
}

const std::vector<std::pair<PlaylistState, uint32_t>>& PlaylistStateMachine::GetStateHistory() const {
    return state_history_;
}

void PlaylistStateMachine::ClearHistory() {
    state_history_.clear();
    ESP_LOGI(TAG, "State history cleared");
}

const std::string& PlaylistStateMachine::GetLastError() const {
    return last_error_;
}

void PlaylistStateMachine::ClearError() {
    last_error_.clear();
}

void PlaylistStateMachine::PrintCurrentState() const {
    ESP_LOGI(TAG, "Current state: %s", STATE_STRINGS[static_cast<int>(current_state_.load())]);
}

void PlaylistStateMachine::PrintStateHistory() const {
    ESP_LOGI(TAG, "State history (%zu entries):", state_history_.size());
    for (size_t i = 0; i < state_history_.size(); ++i) {
        const auto& entry = state_history_[i];
        ESP_LOGI(TAG, "  [%lu] %s at %lu ms", (unsigned long)i, 
                 STATE_STRINGS[static_cast<int>(entry.first)], entry.second);
    }
}

void PlaylistStateMachine::PrintValidTransitions() const {
    PlaylistState current = current_state_.load();
    ESP_LOGI(TAG, "Valid transitions from state %s:", STATE_STRINGS[static_cast<int>(current)]);
    
    for (const auto& transition : transitions_) {
        if (transition.from_state == current) {
            ESP_LOGI(TAG, "  %s -> %s", 
                     EVENT_STRINGS[static_cast<int>(transition.event)], 
                     STATE_STRINGS[static_cast<int>(transition.to_state)]);
        }
    }
}

bool PlaylistStateMachine::ValidateStateMachine() const {
    ESP_LOGI(TAG, "Validating state machine...");
    
    if (HasDeadlock()) {
        ESP_LOGE(TAG, "State machine has deadlock!");
        return false;
    }
    
    if (HasUnreachableStates()) {
        ESP_LOGE(TAG, "State machine has unreachable states!");
        return false;
    }
    
    ESP_LOGI(TAG, "State machine validation passed");
    return true;
}

bool PlaylistStateMachine::HasDeadlock() const {
    // 简单的死锁检测：检查是否有状态没有出边
    for (PlaylistState state = PlaylistState::IDLE; 
         state <= PlaylistState::ERROR; 
         state = static_cast<PlaylistState>(static_cast<int>(state) + 1)) {
        
        bool has_outgoing = false;
        for (const auto& transition : transitions_) {
            if (transition.from_state == state) {
                has_outgoing = true;
                break;
            }
        }
        
        if (!has_outgoing && state != PlaylistState::IDLE) {
            ESP_LOGW(TAG, "State %s has no outgoing transitions", STATE_STRINGS[static_cast<int>(state)]);
            return true;
        }
    }
    
    return false;
}

bool PlaylistStateMachine::HasUnreachableStates() const {
    // 简单的不可达状态检测：从IDLE状态开始可达性分析
    std::vector<bool> reachable(6, false); // 6个状态
    reachable[static_cast<int>(PlaylistState::IDLE)] = true;
    
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& transition : transitions_) {
            if (reachable[static_cast<int>(transition.from_state)] && 
                !reachable[static_cast<int>(transition.to_state)]) {
                reachable[static_cast<int>(transition.to_state)] = true;
                changed = true;
            }
        }
    }
    
    // 检查是否有不可达状态
    for (int i = 0; i < 6; ++i) {
        if (!reachable[i]) {
            ESP_LOGW(TAG, "State %s is unreachable", STATE_STRINGS[i]);
            return true;
        }
    }
    
    return false;
}

// 状态机工厂实现
PlaylistStateMachine* StateMachineFactory::CreateDefaultStateMachine() {
    auto* sm = new PlaylistStateMachine();
    sm->Initialize();
    return sm;
}

PlaylistStateMachine* StateMachineFactory::CreateMinimalStateMachine() {
    auto* sm = new PlaylistStateMachine();
    // 只定义基本的转换规则
    sm->Initialize();
    return sm;
}

PlaylistStateMachine* StateMachineFactory::CreateAdvancedStateMachine() {
    auto* sm = new PlaylistStateMachine();
    sm->Initialize();
    // 可以在这里添加更复杂的转换规则
    return sm;
} 