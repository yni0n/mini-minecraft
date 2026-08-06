#include "player.h"
#include <QString>

Player::Player(glm::vec3 pos, const Terrain &terrain)
    : Entity(pos), m_velocity(0,0,0), m_acceleration(0,0,0),
      m_camera(pos + glm::vec3(0, 1.5f, 0)), mcr_terrain(terrain),
      mcr_camera(m_camera),
      m_flightMode(true)   // ★ 默认飞行模式
{}

Player::~Player()
{}

void Player::tick(float dT, InputBundle &input) {
    processInputs(input);
    computePhysics(dT, mcr_terrain, input);
}

void Player::processInputs(InputBundle &inputs) {
    // Update the Player's velocity and acceleration based on the
    // state of the inputs.
    // ========== F 键：切换飞行/行走模式 ==========
    if(inputs.fPressed) {
        m_flightMode = !m_flightMode;
        m_velocity = glm::vec3(0.f);   // 模式切换时清零速度
        inputs.fPressed = false;       // 消费这个输入（只触发一次）
    }

    // ========== 鼠标：旋转相机 ==========
    const float sens = 0.015f;   // 弧度/像素
    rotateOnUpGlobal(-inputs.mouseX * sens);       // 左右转动（绕世界Y轴）
    // 垂直旋转：限制不超过 ±89°
    float pitchDelta = -inputs.mouseY * sens;   // 本帧俯仰变化量（度）
    float currentPitch = glm::degrees(           // 当前俯仰角（度）
        glm::asin(glm::clamp(m_forward.y, -1.f, 1.f)));
    float maxPitch = 89.f;
    float newPitch = glm::clamp(currentPitch + pitchDelta,
                                -maxPitch, maxPitch);
    float actualDelta = newPitch - currentPitch; // 实际允许旋转的角度
    rotateOnRightLocal(actualDelta);

    // ========== 键盘：根据当前模式设置加速度 ==========
    const float accelMag = 50.0f;
    m_acceleration = glm::vec3(0.f);   // 每帧清零后重新计算

    // -------- 丢弃 Y 分量后重新归一化 --------
    glm::vec3 fwd   = glm::normalize(glm::vec3(m_forward.x, 0, m_forward.z));
    glm::vec3 right = glm::normalize(glm::vec3(m_right.x,   0, m_right.z));
    if(m_flightMode) {
        // -------- 飞行模式：完整 3D 移动 --------
        if(inputs.wPressed) m_acceleration += fwd   * accelMag;
        if(inputs.sPressed) m_acceleration -= fwd   * accelMag;
        if(inputs.dPressed) m_acceleration += right * accelMag;
        if(inputs.aPressed) m_acceleration -= right * accelMag;
        if(inputs.ePressed) m_acceleration += glm::vec3(0.0, 1.0, 0.0)  * accelMag;
        if(inputs.qPressed) m_acceleration -= glm::vec3(0.0, 1.0, 0.0)  * accelMag;
    }
    else {
        // -------- 行走模式： --------
        // 防止向上垂直看时除零
        if(glm::length(glm::vec3(m_forward.x, 0, m_forward.z)) < 0.001f) {
            fwd = m_up;   // 用上方向代替（向下看的情况）
        }
        if(inputs.wPressed) m_acceleration += fwd   * accelMag;
        if(inputs.sPressed) m_acceleration -= fwd   * accelMag;
        if(inputs.dPressed) m_acceleration += right * accelMag;
        if(inputs.aPressed) m_acceleration -= right * accelMag;

    }
}

void Player::computePhysics(float dT, const Terrain &terrain, InputBundle &inputs) {
    // Update the Player's position based on its acceleration
    // and velocity, and also perform collision detection.
    // 防止 dt 爆炸
    dT = glm::min(dT, 0.1f);

    bool onGround = false;
    if(!m_flightMode) {
        glm::vec3 footCheck = m_position + glm::vec3(0.f, -0.05f, 0.f);
        onGround = terrain.checkPlayerCollision(footCheck);
    }

    if(onGround && inputs.spacePressed && m_velocity.y <= 0.001f) {
        float groundY = mcr_terrain.getHeightAt(m_position.x, m_position.z);
        // printf("JUMP! foot=%.3f ground=%.3f vy=%.3f\n",
        //        m_position.y, groundY + 1.0f, m_velocity.y);
        m_acceleration.y += 500.0f;
    }

    // ========== 1. 加速度 → 速度 ==========
    m_velocity += m_acceleration * dT;

    // ========== 2. 重力（仅行走模式） ==========
    if(!m_flightMode) {
        m_velocity.y -= 20.0f * dT;   // 20 m/s² 向下加速度
    }

    // ========== 3. 摩擦/空气阻力 ==========
    // pow(0.95, dt): 每秒速度保留 5%，与帧率无关
    const float damping = 0.01f;
    if(m_flightMode) {
        // 飞行模式：所有轴都减速
        m_velocity *= glm::pow(damping, dT);
    } else {
        // 行走模式：XZ 减速，Y 不减速（重力单独处理）
        m_velocity.x *= glm::pow(damping, dT);
        m_velocity.z *= glm::pow(damping, dT);
        // m_velocity.y 保留，让重力和跳跃自由控制
    }

    // ========== 4. 速度上限 ==========
    float speed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    const float maxSpeed = 10.0f;
    if(speed > maxSpeed) {
        glm::vec2 maxSpeedXZ = glm::normalize(glm::vec2(m_velocity.x, m_velocity.z)) * maxSpeed;
        m_velocity.x = maxSpeedXZ.x;
        m_velocity.z = maxSpeedXZ.y;
    }

    // ========== 5. 计算本帧位移 ==========
    glm::vec3 delta = m_velocity * dT;

    // ========== 6. 地面分轴碰撞 ==========
    // ----- X 轴 -----
    glm::vec3 candidateX = m_position + glm::vec3(delta.x, 0.f, 0.f);
    if(terrain.checkPlayerCollision(candidateX)) {
        m_velocity.x = 0.f;   // 撞墙，清零 X 速度
        delta.x = 0.f;
    }

    // ----- Y 轴 -----
    glm::vec3 candidateY = m_position + glm::vec3(0.f, delta.y, 0.f);
    if(terrain.checkPlayerCollision(candidateY)) {
        if(m_velocity.y<0.0) m_velocity.y = 0.f;   // 落地 / 撞天花板，清零 Y 速度
        delta.y = 0.f;
    }

    // ----- Z 轴 -----
    glm::vec3 candidateZ = m_position + glm::vec3(0.f, 0.f, delta.z);
    if(terrain.checkPlayerCollision(candidateZ)) {
        m_velocity.z = 0.f;   // 撞墙，清零 Z 速度
        delta.z = 0.f;
    }

    if(delta.x != 0.f && delta.z !=0.f){
        glm::vec3 candidateXZ = m_position + glm::vec3(delta.x, 0.f, delta.z);
            if(terrain.checkPlayerCollision(candidateXZ)) {
                if(abs(delta.x)<abs(delta.z)){
                    m_velocity.x = 0.f;   // 撞墙，清零 X 速度
                    delta.x = 0.f;
                }else{
                    m_velocity.z = 0.f;   // 撞墙，清零 Z 速度
                    delta.z = 0.f;
                }
            }
    }
    // ========== 8. 应用位移（Player 重写版：自身 + 相机同步移动） ==========
    moveAlongVector(delta);
}

void Player::setCameraWidthHeight(unsigned int w, unsigned int h) {
    m_camera.setWidthHeight(w, h);
}

void Player::moveAlongVector(glm::vec3 dir) {
    Entity::moveAlongVector(dir);
    m_camera.moveAlongVector(dir);
}
void Player::moveForwardLocal(float amount) {
    Entity::moveForwardLocal(amount);
    m_camera.moveForwardLocal(amount);
}
void Player::moveRightLocal(float amount) {
    Entity::moveRightLocal(amount);
    m_camera.moveRightLocal(amount);
}
void Player::moveUpLocal(float amount) {
    Entity::moveUpLocal(amount);
    m_camera.moveUpLocal(amount);
}
void Player::moveForwardGlobal(float amount) {
    Entity::moveForwardGlobal(amount);
    m_camera.moveForwardGlobal(amount);
}
void Player::moveRightGlobal(float amount) {
    Entity::moveRightGlobal(amount);
    m_camera.moveRightGlobal(amount);
}
void Player::moveUpGlobal(float amount) {
    Entity::moveUpGlobal(amount);
    m_camera.moveUpGlobal(amount);
}
void Player::rotateOnForwardLocal(float degrees) {
    Entity::rotateOnForwardLocal(degrees);
    m_camera.rotateOnForwardLocal(degrees);
}
void Player::rotateOnRightLocal(float degrees) {
    Entity::rotateOnRightLocal(degrees);
    m_camera.rotateOnRightLocal(degrees);
}
void Player::rotateOnUpLocal(float degrees) {
    Entity::rotateOnUpLocal(degrees);
    m_camera.rotateOnUpLocal(degrees);
}
void Player::rotateOnForwardGlobal(float degrees) {
    Entity::rotateOnForwardGlobal(degrees);
    m_camera.rotateOnForwardGlobal(degrees);
}
void Player::rotateOnRightGlobal(float degrees) {
    Entity::rotateOnRightGlobal(degrees);
    m_camera.rotateOnRightGlobal(degrees);
}
void Player::rotateOnUpGlobal(float degrees) {
    Entity::rotateOnUpGlobal(degrees);
    m_camera.rotateOnUpGlobal(degrees);
}

QString Player::posAsQString() const {
    std::string str("( " + std::to_string(m_position.x) + ", " + std::to_string(m_position.y) + ", " + std::to_string(m_position.z) + ")");
    return QString::fromStdString(str);
}
QString Player::velAsQString() const {
    std::string str("( " + std::to_string(m_velocity.x) + ", " + std::to_string(m_velocity.y) + ", " + std::to_string(m_velocity.z) + ")");
    return QString::fromStdString(str);
}
QString Player::accAsQString() const {
    std::string str("( " + std::to_string(m_acceleration.x) + ", " + std::to_string(m_acceleration.y) + ", " + std::to_string(m_acceleration.z) + ")");
    return QString::fromStdString(str);
}
QString Player::lookAsQString() const {
    std::string str("( " + std::to_string(m_forward.x) + ", " + std::to_string(m_forward.y) + ", " + std::to_string(m_forward.z) + ")");
    return QString::fromStdString(str);
}
