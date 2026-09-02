#ifndef WEATHERPARTICLES_H
#define WEATHERPARTICLES_H

#include <glm_includes.h>
#include <vector>
#include "openglcontext.h"

class ShaderProgram;

class WeatherParticles {
public:
    WeatherParticles(OpenGLContext* context);
    ~WeatherParticles();

    void create(int maxParticles);   // initializeGL 里调用一次
    // 每帧推进：camPos=相机位置, intensity=天气强度, snow=是否下雪
    void tick(float dT, const glm::vec3& camPos, float intensity, bool snow);
    void draw(ShaderProgram& prog);  // renderWeather 里调用

private:
    void respawn(glm::vec3& pos, float& seed, float& groundY, const glm::vec3& camPos);

    struct Particle { glm::vec3 pos; float seed; float groundY; };  // ★ 加 groundY

    std::vector<Particle> m_particles;

    OpenGLContext* mp_context;
    GLuint m_quadBuf = 0;    // 静态：6 个角点 (vec2)
    GLuint m_instBuf = 0;    // 动态：每粒子一个 vec4
    int m_maxCount = 0;
    int m_activeCount = 0;
    float m_time = 0.f;      // 雪花漂移相位用
    bool m_snow = false;

    static constexpr float RADIUS = 48.f;     // 粒子盒水平半径
    static constexpr float TOP_OFFSET = 25.f; // 生成高度在相机上方
};

#endif // WEATHERPARTICLES_H
