#include "weather.h"

void WeatherSystem::tick(float dT) {
    const float RATE = 1.f / TRANSITION;   // 强度每秒变化量

    if(m_current == m_target) {
        if(m_current == WeatherState::CLEAR) {
            m_intensity = glm::max(m_intensity - RATE * dT, 0.f);   // 晴天渐出
        } else {
            m_intensity = glm::min(m_intensity + RATE * dT, 1.f);   // 降水渐入
        }
    } else {
        // 过渡期：先把当前天气降到 0，降完才真正切换
        m_intensity -= RATE * dT;
        if(m_intensity <= 0.f) {
            m_intensity = 0.f;
            m_current = m_target;
        }
    }
}

void WeatherSystem::cycleState() {
    switch(m_target) {
    case WeatherState::CLEAR: m_target = WeatherState::RAIN;  break;
    case WeatherState::RAIN:  m_target = WeatherState::SNOW;  break;
    case WeatherState::SNOW:  m_target = WeatherState::CLEAR; break;
    }
}
