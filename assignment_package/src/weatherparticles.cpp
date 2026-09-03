#include "weatherparticles.h"
#include "shaderprogram.h"
#include "scene/terrain.h"
#include <cstdlib>
#include <cmath>

WeatherParticles::WeatherParticles(OpenGLContext* context)
    : mp_context(context) {}

WeatherParticles::~WeatherParticles() {
    if(m_quadBuf)  mp_context->glDeleteBuffers(1, &m_quadBuf);
    if(m_instBuf)  mp_context->glDeleteBuffers(1, &m_instBuf);
}

void WeatherParticles::create(int maxParticles) {
    m_maxCount = maxParticles;
    m_particles.resize(maxParticles);
    for(auto& p : m_particles) p.seed = static_cast<float>(rand()) / RAND_MAX;

    // ---- 静态 quad：6 个角点，两个三角形 ----
    const float corners[6][2] = {
        {-0.5f,-0.5f}, {0.5f,-0.5f}, {0.5f, 0.5f},
        {-0.5f,-0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}
    };
    mp_context->glGenBuffers(1, &m_quadBuf);
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_quadBuf);
    mp_context->glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);

    // ---- 动态实例缓冲：先只分配，不填数据 ----
    mp_context->glGenBuffers(1, &m_instBuf);
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_instBuf);
    mp_context->glBufferData(GL_ARRAY_BUFFER,
                             maxParticles * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void WeatherParticles::respawn(glm::vec3& pos, float& seed, float& groundY, const glm::vec3& camPos) {
    seed = static_cast<float>(rand()) / RAND_MAX;
    pos.x = camPos.x + (seed * 2.f - 1.f) * RADIUS;
    pos.z = camPos.z + ((rand() / static_cast<float>(RAND_MAX)) * 2.f - 1.f) * RADIUS;
    pos.y = camPos.y + TOP_OFFSET * (0.5f + 0.5f * (rand() / static_cast<float>(RAND_MAX)));
    groundY = Terrain::getHeightAt(pos.x, pos.z);   // ★ 出生时算一次并缓存
}

void WeatherParticles::tick(float dT, const glm::vec3& camPos,
                            float intensity, bool snow) {
    m_time += dT;
    m_activeCount = static_cast<int>(m_maxCount * intensity);   // 强度控制粒子数

    for(int i = 0; i < m_activeCount; ++i) {
        glm::vec3& pos = m_particles[i].pos;
        float& seed = m_particles[i].seed;

        if(snow) {
            pos.y -= 2.2f * dT;                                  // 雪慢
            // 水平正弦漂移，x/z 相位错开
            pos.x += glm::sin(m_time * 1.2f + seed * 6.28f) * 0.7f * dT;
            pos.z += glm::cos(m_time * 0.9f + seed * 9.42f) * 0.7f * dT;
        } else {
            pos.y -= 18.f * dT;                                  // 雨快
        }

        /// ---- 粒子盒跟随相机：超出水平范围就绕到另一侧 ----
        bool wrapped = false;
        if(pos.x - camPos.x >  RADIUS) { pos.x -= 2.f * RADIUS; wrapped = true; }
        if(pos.x - camPos.x < -RADIUS) { pos.x += 2.f * RADIUS; wrapped = true; }
        if(pos.z - camPos.z >  RADIUS) { pos.z -= 2.f * RADIUS; wrapped = true; }
        if(pos.z - camPos.z < -RADIUS) { pos.z += 2.f * RADIUS; wrapped = true; }
        if(wrapped) m_particles[i].groundY = Terrain::getHeightAt(pos.x, pos.z);

        // ---- 触地回收：用缓存的地面高度，纯比较零开销 ----
        // 第二个条件是安全网：雪漂移导致缓存过期时，兜底回收防止粒子永远下坠
        if(pos.y < m_particles[i].groundY || pos.y < 60.f) {
            respawn(pos, seed, m_particles[i].groundY, camPos);
        }
    }
}

void WeatherParticles::draw(ShaderProgram& prog) {
    if(m_activeCount <= 0) return;
    prog.useMe();

    // ---- 先整体上传实例数据（覆盖式，DYNAMIC_DRAW）----
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_instBuf);
    mp_context->glBufferSubData(GL_ARRAY_BUFFER, 0,
                                m_activeCount * sizeof(Particle),
                                m_particles.data());

    // ---- vs_Corner：逐顶点 (divisor=0) ----
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_quadBuf);
    if(prog.m_attribs.count("vs_Corner")) {
        int h = prog.m_attribs["vs_Corner"];
        mp_context->glEnableVertexAttribArray(h);
        mp_context->glVertexAttribPointer(h, 2, GL_FLOAT, false, 0, nullptr);
        mp_context->glVertexAttribDivisor(h, 0);
    }

    // ---- vs_InstancePos：逐实例 (divisor=1) ----
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_instBuf);
    if(prog.m_attribs.count("vs_InstancePos")) {
        int h = prog.m_attribs["vs_InstancePos"];
        mp_context->glEnableVertexAttribArray(h);
        mp_context->glVertexAttribPointer(h, 4, GL_FLOAT, false, 0, nullptr);
        mp_context->glVertexAttribDivisor(h, 1);
    }

    mp_context->glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_activeCount);

    // ---- 清理：必须把 divisor 复位，否则残留状态会污染后续非实例化绘制 ----
    if(prog.m_attribs.count("vs_Corner")) {
        int h = prog.m_attribs["vs_Corner"];
        mp_context->glVertexAttribDivisor(h, 0);
        mp_context->glDisableVertexAttribArray(h);
    }
    if(prog.m_attribs.count("vs_InstancePos")) {
        int h = prog.m_attribs["vs_InstancePos"];
        mp_context->glVertexAttribDivisor(h, 0);
        mp_context->glDisableVertexAttribArray(h);
    }
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, 0);

}
