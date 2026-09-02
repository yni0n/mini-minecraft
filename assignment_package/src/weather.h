#ifndef WEATHER_H
#define WEATHER_H

#include <glm_includes.h>

enum class WeatherState : unsigned char {
    CLEAR, RAIN, SNOW
};

class WeatherSystem {
public:
    void tick(float dT);       // 每帧推进状态机
    void cycleState();         // R 键：切到下一种天气

    float intensity() const { return m_intensity; }          // 降水强度 0~1
    WeatherState state() const { return m_current; }
    bool isSnow() const { return m_current == WeatherState::SNOW; }

private:
    WeatherState m_current = WeatherState::CLEAR;   // 当前正在呈现的天气
    WeatherState m_target  = WeatherState::CLEAR;   // 目标天气，定义这两个状态以实现渐变，MC原效果
    float m_intensity = 0.f;                        // 渐变中的实际强度
    static constexpr float TRANSITION = 4.f;       // 渐变秒数
};

#endif // WEATHER_H
