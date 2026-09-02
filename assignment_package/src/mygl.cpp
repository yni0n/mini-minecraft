#include "mygl.h"
#include <glm_includes.h>
#include "scene/chunk.h"
#include <iostream>
#include <QApplication>
#include <QKeyEvent>
#include <QDateTime>

// GL 错误探针:消费并打印错误发生位置
static void glProbe(const char* where) {
    GLenum e = glGetError();
    if(e != GL_NO_ERROR) {
        fprintf(stderr, "[GL探针] 错误 0x%X 发生于: %s\n", e, where);
    }
}

// ============================================================
// 视锥体裁剪工具（放在 mygl.cpp 顶部，所有函数之前）
// ============================================================

struct Frustum {
    glm::vec4 planes[6];  // left, right, bottom, top, near, far
};

// 从 View-Projection 矩阵提取 6 个裁剪平面
static Frustum extractFrustum(const glm::mat4& vp) {
    Frustum f;
    // Left   = row3 + row0
    f.planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                            vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    // Right  = row3 - row0
    f.planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                            vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    // Bottom = row3 + row1
    f.planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                            vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    // Top    = row3 - row1
    f.planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                            vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    // Near   = row3 + row2
    f.planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                            vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    // Far    = row3 - row2
    f.planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                            vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    // 归一化
    for(int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(f.planes[i]));
        f.planes[i] /= len;
    }
    return f;
}
//返回方块名称
static QString blockTypeName(BlockType b) {
    switch(b) {
    case EMPTY:   return "EMPTY";
    case GRASS:   return "GRASS";
    case DIRT:    return "DIRT";
    case STONE:   return "STONE";
    case SAND:    return "SAND";
    case WATER:   return "WATER";
    case SNOW:    return "SNOW";
    case LAVA:    return "LAVA";
    case BEDROCK: return "BEDROCK";
    default:      return "UNKNOWN";
    }
}

// 测试 AABB 是否在视锥体内（true = 可见，false = 完全在外面）
static bool zoneInFrustum(const Frustum& f,
                      float mx, float my, float mz,
                      float Mx, float My, float Mz) {
    for(int i = 0; i < 6; i++) {
        // 取 AABB 在平面法线方向上最远的那个顶点（p-vertex）
        float px = (f.planes[i].x > 0) ? Mx : mx;
        float py = (f.planes[i].y > 0) ? My : my;
        float pz = (f.planes[i].z > 0) ? Mz : mz;
        if(f.planes[i].x * px + f.planes[i].y * py +
                f.planes[i].z * pz + f.planes[i].w < 0.f) {
            return false;  // 完全在平面外侧 → 不可见
        }
    }
    return true;
}

MyGL::MyGL(QWidget *parent)
    : OpenGLContext(parent), //初始化列表
      m_worldAxes(this),
      m_progLambert(this), m_progFlat(this), m_progInstanced(this),
      m_terrain(this), m_player(glm::vec3(48.f, 129.f, 48.f), m_terrain),
      m_blockWireframe(this),           // ★ 新增
      m_progPostProcess(this),      // ★ 新增
      m_progSky(this),              // ★ 新增
      m_screenQuad(this),            // ★ 新增
      m_progShadow(this),
      m_progWeather(this),
      m_weatherParticles(this),
      m_hasTarget(false),                // ★ 新增
      m_texture(0),   // ★ 初始化为 0
      m_frameBuffer(0),              // ★ 新增
      m_renderTexture(0),            // ★ 新增
      m_depthRenderBuffer(0)         // ★ 新增
{
    // Connect the timer to a function so that when the timer ticks the function is executed
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    // Tell the timer to redraw 60 times per second
    m_timer.start(16);
    setFocusPolicy(Qt::ClickFocus);

    setMouseTracking(true);  // MyGL 会追踪鼠标活动即使没有按下
    setCursor(Qt::BlankCursor); // 光标不可见
    m_prevFrameTime = QDateTime::currentMSecsSinceEpoch();   // ★ 新增
}

MyGL::~MyGL() {
    makeCurrent(); //唤醒 OpenGL 上下文
    glDeleteVertexArrays(1, &vao);
}


void MyGL::moveMouseToCenter() {
    QCursor::setPos(this->mapToGlobal(QPoint(width() / 2, height() / 2)));
}

void MyGL::initializeGL()
{
    fprintf(stderr, "=== initializeGL ===\n");//debug
    fflush(stderr);

    // 激活 Qt 提供的 OpenGL 函数库， using Qt's QOpenGLFunctions_3_2_Core class
    // If you were programming in a non-Qt context you might use GLEW (GL Extension Wrangler)instead
    initializeOpenGLFunctions();
    // 打印当前电脑显卡支持的 OpenGL 版本信息
    debugContextVersion();

    // Set a few settings/modes in OpenGL rendering
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set the color with which the screen is filled at the start of each render call.
    glClearColor(0.37f, 0.74f, 1.0f, 1);

    printGLErrorLog();

    // Create a Vertex Attribute Object
    glGenVertexArrays(1, &vao);

    //Create the 实例 of the 坐标轴
    m_worldAxes.createVBOdata();

    // Create and set up the diffuse shader
    m_progLambert.create(":/glsl/lambert.vert.glsl", ":/glsl/lambert.frag.glsl");

    // Create and set up the flat lighting shader
    m_progFlat.create(":/glsl/flat.vert.glsl", ":/glsl/flat.frag.glsl");
    m_progInstanced.create(":/glsl/instanced.vert.glsl", ":/glsl/lambert.frag.glsl");

    // We have to have a VAO bound in OpenGL 3.2 Core. But if we're not
    // using multiple VAOs, we can just bind one once.
    glBindVertexArray(vao);

    m_terrain.CreateTestScene(m_player.mcr_position);//初始化地形

    // ★ 把玩家抬到地表以上 2 格
    float groundY = m_terrain.getHeightAt(
        m_player.mcr_position.x,
        m_player.mcr_position.z);
    float deltaY = (groundY + 3.f) - m_player.mcr_position.y;
    m_player.moveUpGlobal(deltaY);       // Player 重写了此方法，相机也会同步移动

    // 创建描边方块 VBO
    m_blockWireframe.createVBOdata();

    // 加载纹理
    loadTexture();
    // 后处理着色器和全屏四边形
    m_progPostProcess.create(":/glsl/postprocess.vert.glsl",
                             ":/glsl/postprocess.frag.glsl");
    // 天空着色器(复用 postprocess.vert.glsl 做顶点着色器)
    m_progSky.create(":/glsl/postprocess.vert.glsl", ":/glsl/sky.frag.glsl");
    m_screenQuad.createVBOdata();
    createFBO(this->width(), this->height());

    //影子
    m_progShadow.create(":/glsl/shadow.vert.glsl", ":/glsl/shadow.frag.glsl");
    createShadowFBO();
    // 天气粒子着色器 + 粒子池(6000 个)
    m_progWeather.create(":/glsl/weather.vert.glsl", ":/glsl/weather.frag.glsl");
    m_weatherParticles.create(8000);

    //发送当前选择方块
    emit sig_sendCurrentBlock(QString("1/%1  %2")
                                  .arg(m_hotbar.size())
                                  .arg(blockTypeName(currentBlock())));

}

//改变窗口大小就触发
void MyGL::resizeGL(int w, int h) {
    //This code sets the concatenated view and perspective projection matrices used for
    //our scene's camera view.
    m_player.setCameraWidthHeight(static_cast<unsigned int>(w), static_cast<unsigned int>(h));
    createFBO(w, h);                          // ★ 新增：重建 FBO
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();

    // Upload the view-projection matrix to our shaders (i.e. onto the graphics card)

    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);
    m_progInstanced.setUnifMat4("u_ViewProj", viewproj);

    printGLErrorLog();
}


// MyGL's constructor links tick() to a timer that fires 60 times per second.
// We're treating MyGL as our game engine class, so we're going to perform
// all per-frame actions here, such as performing physics updates on all
// entities in the scene.
void MyGL::tick() {
    // ---- 计算帧间隔 ----
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    float dT = (now - m_prevFrameTime) / 1000.f;  // 毫秒 → 秒
    m_prevFrameTime = now;
    dT = glm::min(dT, 0.1f);   // 防止断点调试时 dt 爆炸

    //m_terrain.expandTerrain(m_player.mcr_position);   // ★ 新增：每帧检查
    m_terrain.tick(m_player.mcr_position);

    m_player.tick(dT, m_inputs);
    m_weather.tick(dT);   // ★ 天气状态机推进
    // ★ 粒子推进：相机位置 + 天气强度 + 雨雪类型
    m_weatherParticles.tick(dT, m_player.mcr_camera.mcr_position,
                            m_weather.intensity(),
                            m_weather.isSnow());


    m_inputs.mouseX = 0.f;
    m_inputs.mouseY = 0.f;

    // ★ 新增：每帧射线检测，确定瞄准方块
    glm::vec3 origin = m_player.mcr_camera.mcr_position;
    glm::vec3 dir    = m_player.mcr_camera.mcr_forward;
    RaycastResult result = m_terrain.raycast(origin, dir, 3.0f);
    m_hasTarget = result.hit;
    if(m_hasTarget) {
        m_targetBlock = result.blockPos;
    }

    m_elapsedTime += dT;//时间累计防止精度问题

    update(); // Calls paintGL() as part of a larger QOpenGLWidget pipeline
    sendPlayerDataToGUI(); // Updates the info in the secondary window displaying player data
}

void MyGL::sendPlayerDataToGUI() const {
    //emit 是 Qt 的关键字，意思是“发射信号”
    emit sig_sendPlayerPos(m_player.posAsQString());//转成字符串之后发送
    emit sig_sendPlayerVel(m_player.velAsQString());
    emit sig_sendPlayerAcc(m_player.accAsQString());
    emit sig_sendPlayerLook(m_player.lookAsQString());
    glm::vec2 pPos(m_player.mcr_position.x, m_player.mcr_position.z);
    glm::ivec2 chunk(16 * glm::ivec2(glm::floor(pPos / 16.f)));
    glm::ivec2 zone(64 * glm::ivec2(glm::floor(pPos / 64.f)));
    emit sig_sendPlayerChunk(QString::fromStdString("( " + std::to_string(chunk.x) + ", " + std::to_string(chunk.y) + " )"));
    emit sig_sendPlayerTerrainZone(QString::fromStdString("( " + std::to_string(zone.x) + ", " + std::to_string(zone.y) + " )"));
}

// This function is called whenever update() is called.
// MyGL's constructor links update() to a timer that fires 60 times per second,
// so paintGL() called at a rate of 60 frames per second.
void MyGL::paintGL() {
    //计算太阳方向
    m_sunDir = computeSunDir();

    renderShadowPass();   glProbe("renderShadowPass 之后");// ★ Pass 0:渲染太阳视角深度
    // ============================================================
    // Pass 1：渲染 3D 场景到 FBO 纹理
    // ============================================================
    // Clear the screen so that we only see newly drawn images
    glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //计算VP，并给3个shader发送
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);
    m_progInstanced.setUnifMat4("u_ViewProj", viewproj);
    m_progWeather.setUnifMat4("u_ViewProj", viewproj);


    renderSky(viewproj);   glProbe("renderSky 之后");// ★ 新增:先画天空(不写深度),地形后画自动遮挡
    renderTerrain();    glProbe("renderTerrain 之后");//绘制地形
    renderWeather();    glProbe("renderWeather 之后");// ★ 天气粒子(画在透明地形之后)

    // ★ 新增：渲染方块描边
    if(m_hasTarget) {
        // 新：先平移再微缩放，原点在方块中心
        glm::vec3 center = glm::vec3(m_targetBlock) + glm::vec3(0.5f);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center)
                          * glm::scale(glm::mat4(1.0f), glm::vec3(1.004f))
                          * glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));
        m_progFlat.setUnifMat4("u_Model", model);

        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        m_progFlat.draw(m_blockWireframe);
        glPolygonOffset(0.0f, 0.0f);
        glDisable(GL_POLYGON_OFFSET_LINE);
    }

    //绘制坐标轴
    glDisable(GL_DEPTH_TEST);
    m_progFlat.setUnifMat4("u_Model", glm::mat4());
    m_progFlat.draw(m_worldAxes);
    glEnable(GL_DEPTH_TEST);

    // ============================================================
    // Pass 2：后处理 — 全屏四边形采样 FBO 纹理 + 流体滤镜
    // ============================================================
    glBindFramebuffer(GL_FRAMEBUFFER, 0);   // 切回默认帧缓冲
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 绑定 FBO 纹理到纹理单元 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_renderTexture);

    // 设置后处理 uniform
    m_progPostProcess.setUnifInt("u_ScreenTexture", 0);
    m_progPostProcess.setUnifInt("u_FluidType", getFluidType());
    m_progPostProcess.setUnifFloat("u_Time", m_elapsedTime);

    // 关闭深度测试和混合，全屏四边形直接覆盖
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    m_progPostProcess.drawScreenQuad(m_screenQuad);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    glProbe("帧末尾");//debug
}

void MyGL::createFBO(int width, int height) {
    // 释放旧的 FBO 资源
    if(m_frameBuffer) {
        glDeleteFramebuffers(1, &m_frameBuffer);
        glDeleteTextures(1, &m_renderTexture);
        glDeleteRenderbuffers(1, &m_depthRenderBuffer);
    }

    // 创建帧缓冲
    glGenFramebuffers(1, &m_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer);

    // 颜色附件：RGBA8 纹理
    glGenTextures(1, &m_renderTexture);
    glBindTexture(GL_TEXTURE_2D, m_renderTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_renderTexture, 0);

    // 深度附件：Renderbuffer（不可读，但更高效）
    glGenRenderbuffers(1, &m_depthRenderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthRenderBuffer);

    // 检查完整性
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
        qDebug() << "FBO incomplete! status =" << status;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MyGL::createShadowFBO() {
    const int SHADOW_RES = 4096;

    for(int i = 0; i < SHADOW_FRAMES; ++i) {
        glGenFramebuffers(1, &m_shadowFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO[i]);

        glGenTextures(1, &m_shadowDepthTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_shadowDepthTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                     SHADOW_RES, SHADOW_RES, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        // ★ 关键:开启硬件深度比较,LINEAR = 免费 2x2 PCF 软化
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                        GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, m_shadowDepthTex[i], 0);

        if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            qDebug() << "Shadow FBO" << i << "incomplete!";
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void MyGL::renderShadowPass() {
    if(!m_shadowsEnabled) {
        m_shadowFirstFrame = true; //下次绘画是第一帧
        return;   // 按键关闭
    }

    // ---------- 0. 保存当前视口,结束后原样恢复 ----------
    GLint origViewport[4];
    glGetIntegerv(GL_VIEWPORT, origViewport);

    // ---------- 1. 光源方向:与 renderTerrain 的逻辑一致(夜里翻转为月亮) ----------
    m_lightDir = (m_sunDir.y >= -0.05f) ? m_sunDir : -m_sunDir;
    // 防止太阳贴地平线时 lookAt 退化(up 与视线平行)
    glm::vec3 s = m_lightDir;
    if(s.y < 0.08f) {
        s.y = 0.08f;
        s = glm::normalize(s);
    }
    // ★ 把太阳方位角/俯仰角量化到 0.5° 步长
    // float yaw   = std::atan2(s.x, s.z);
    // float pitch = std::asin(glm::clamp(s.y, -1.f, 1.f));
    // const float step = glm::radians(0.25f);  // 将太阳步长改为0.25
    // yaw   = std::round(yaw / step) * step;
    // pitch = std::round(pitch / step) * step;
    // s = glm::vec3(std::cos(pitch) * std::sin(yaw),
    //               std::sin(pitch),
    //               std::cos(pitch) * std::cos(yaw));

    // ---------- 2. 太阳正交矩阵:以玩家为中心 ±100 的盒子 ----------
    glm::vec3 playerPos = m_player.mcr_position;
    // 光空间三轴在世界空间中的方向(太阳已量化,三轴恒定)
    glm::vec3 fwd   = -s;                                             // 视线:太阳 → 目标
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 0, 1)));
    glm::vec3 up    = glm::cross(right, fwd);
    // ★ 纹素对齐(修正版):把盒中心在【光空间】的 x/y 取整到纹素网格
    //   参考点是世界原点(而非玩家)——玩家在"跟随自己的视图"里坐标恒定,取整无效
    const float texelWorld = 300.f / 4096.f;   // ≈ 0.049m
    float rx = glm::dot(playerPos, right);
    float ry = glm::dot(playerPos, up);
    rx = glm::round(rx / texelWorld) * texelWorld;
    ry = glm::round(ry / texelWorld) * texelWorld;
    float rz = glm::dot(playerPos, fwd);       // 沿视线方向:正交投影下平移不影响贴图
    glm::vec3 center = right * rx + up * ry + fwd * rz;

    glm::vec3 eye = center + s * 300.f;
    glm::mat4 lightView = glm::lookAt(eye, center, glm::vec3(0, 0, 1));
    glm::mat4 lightProj = glm::ortho(-150.f, 150.f, -150.f, 150.f, 0.1f, 600.f);
    glm::mat4 newVP = lightProj * lightView;
    m_shadowIdx = (m_shadowIdx + 1) % SHADOW_FRAMES;    // 环形推进
    m_shadowVP[m_shadowIdx] = newVP;

    // ---------- 3. 渲染深度 ----------
    // 首帧没有历史,两张贴图用同一个 VP 各渲染一次,避免首帧全黑
    int renderCount = m_shadowFirstFrame ? SHADOW_FRAMES : 1;
    if(m_shadowFirstFrame) {
        for(int i = 0; i < SHADOW_FRAMES; ++i) m_shadowVP[i] = newVP;
    }
    for(int k = 0; k < renderCount; ++k) {
        int idx = (m_shadowIdx - k + SHADOW_FRAMES) % SHADOW_FRAMES;
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFBO[idx]);
        glViewport(0, 0, 4096, 4096);
        glClear(GL_DEPTH_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);   // 只写深度

        m_progShadow.setUnifMat4("u_ViewProj", m_shadowVP[idx]);

        int pzX = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
        int pzZ = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;
        for(int dx = -2; dx <= 2; ++dx) {
            for(int dz = -2; dz <= 2; ++dz) {
                m_terrain.draw(pzX + dx * 64, pzX + dx * 64 + 64,
                               pzZ + dz * 64, pzZ + dz * 64 + 64,
                               &m_progShadow, nullptr);
            }
        }
    }
    m_shadowFirstFrame = false;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(origViewport[0], origViewport[1],
               origViewport[2], origViewport[3]);  // ★ 恢复成保存的屏幕分辨率
}


glm::vec3 MyGL::computeSunDir() const {
    const float dayLength = 600.0f;
    const float TWO_PI = 6.2831853f;
    float phase = glm::mod(m_elapsedTime + dayLength * 0.125f, dayLength) / dayLength * TWO_PI;
    return glm::normalize(glm::vec3(glm::cos(phase), glm::sin(phase), 0.f));
    //return glm::normalize(glm::vec3(0.7071, 0.7071, 0.0));
}

void MyGL::computeFogColors(glm::vec3& fogSun, glm::vec3& fogDusk) const {
    float sy = m_sunDir.y;
    // 与 sky.frag.glsl 地平线条目一致
    glm::vec3 sunset0(255.f/255.f, 229.f/255.f, 119.f/255.f);  // sunset[0] 朝太阳
    glm::vec3 dusk0  (144.f/255.f,  96.f/255.f, 144.f/255.f);  // dusk[0]   背太阳
    glm::vec3 daySky(0.45f, 0.68f, 1.0f);
    glm::vec3 nightSky(0.13f, 0.10f, 0.30f);
    float dayBlend   = glm::smoothstep(0.0f, 0.6f, sy);
    float nightBlend = 1.0f - glm::smoothstep(-0.35f, -0.05f, sy);
    fogSun  = glm::mix(sunset0, daySky,   dayBlend   * 0.85f);
    fogSun  = glm::mix(fogSun,  nightSky, nightBlend * 0.9f);
    fogDusk = glm::mix(dusk0,   daySky,   dayBlend   * 0.85f);
    fogDusk = glm::mix(fogDusk, nightSky, nightBlend * 0.9f);
}

int MyGL::getFluidType() const {
    // 检查相机（眼睛）所在位置的方块
    glm::vec3 camPos = m_player.mcr_camera.mcr_position;
    int bx = static_cast<int>(glm::floor(camPos.x));
    int by = static_cast<int>(glm::floor(camPos.y));
    int bz = static_cast<int>(glm::floor(camPos.z));

    if(m_terrain.hasChunkAt(bx, bz) && by >= 0 && by < 256) {
        BlockType b = m_terrain.getGlobalBlockAt(bx, by, bz);
        if(b == WATER) return 1;
        if(b == LAVA)  return 2;
    }
    return 0;  // 不在流体中
}

// TODO: Change this so it renders the nine zones of generated
// terrain that surround the player (refer to Terrain::m_generatedTerrain
// for more info)
//指定区域并绘制，使用实例绘制
void MyGL::renderTerrain() {
    glDisable(GL_BLEND);

    // ★ 新增：激活纹理单元 0 并绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    m_progLambert.setUnifInt("u_Texture", 0);
    m_progLambert.setUnifFloat("u_Time", m_elapsedTime);
    //绑定法线纹理
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_normalTexture);
    m_progLambert.setUnifInt("u_NormalMap", 1);
    glActiveTexture(GL_TEXTURE0);   // 记得切回 0，别破坏后面的绑定
    // ★ 绑定阴影贴图到纹理单元 2(0/1 已被图集和法线占用)
    // 单元 2=最新,3/4/5=逐级旧一帧
    for(int k = 0; k < SHADOW_FRAMES; ++k) {
        int idx = (m_shadowIdx - k + SHADOW_FRAMES) % SHADOW_FRAMES;
        glActiveTexture(GL_TEXTURE2 + k);
        glBindTexture(GL_TEXTURE_2D, m_shadowDepthTex[idx]);
        m_progLambert.setUnifInt(k == 0 ? "u_ShadowMap"
                                        : "u_PrevShadowMap" + std::to_string(k),
                                 2 + k);
        m_progLambert.setUnifMat4(k == 0 ? "u_LightVP"
                                         : "u_PrevLightVP" + std::to_string(k),
                                  m_shadowVP[idx]);
    }
    m_progLambert.setUnifInt("u_ShadowEnabled", m_shadowsEnabled ? 1 : 0);;
    glActiveTexture(GL_TEXTURE0);   // 切回 0


    // 根据太阳/月亮位置决定定向光
    glm::vec3 sunDir = m_sunDir;
    float sy = m_sunDir.y;
    // dayFactor: 0 = 深夜(只有环境光), 1 = 白天(满漫反射)
    float sunFactor = glm::smoothstep(-0.05f, 0.1f, sy);
    float moonFactor = glm::smoothstep(0.1f, 0.20f, -sy);
    // 光照方向始终指向太阳;夜里 lightColor=0,方向无所谓
    glm::vec3 lightDir = m_lightDir;   // 由 renderShadowPass 每帧更新
    // 低角度偏暖橙,正午偏白
    glm::vec3 sunTint = glm::mix(glm::vec3(0.6f, 0.45f, 0.25f),
                                 glm::vec3(0.7f, 0.7, 0.65),
                                 glm::smoothstep(0.0f, 0.25f, sy));
    glm::vec3 moonTint(0.18f, 0.22f, 0.35f);
    glm::vec3 lightColor   = sunTint * sunFactor + moonTint * moonFactor;
    glm::vec3 ambientColor = glm::mix(glm::vec3(0.07f, 0.08f, 0.13f),  // 夜:冷暗
                                      glm::vec3(0.45f),                 // 日:中性
                                      sunFactor);
    // ★ 天气调制：阴天直射光被云削弱，环境光偏灰
    float weatherW = m_weather.intensity();
    lightColor   *= (1.f - 0.65f * weatherW);
    glm::vec3 overcastAmb = m_weather.isSnow() ? glm::vec3(0.40f, 0.42f, 0.46f)  // 雪天苍白
                                               : glm::vec3(0.30f, 0.32f, 0.37f); // 雨天铅灰
    ambientColor  = glm::mix(ambientColor, overcastAmb, weatherW);
    m_progLambert.setUnifVec3("u_LightDir", lightDir);
    m_progLambert.setUnifVec3("u_LightColor", lightColor);
    m_progLambert.setUnifVec3("u_AmbientColor", ambientColor);
    m_progLambert.setUnifVec3("u_Eye", m_player.mcr_camera.mcr_position);
    glm::vec3 fogSun, fogDusk;
    computeFogColors(fogSun, fogDusk);
    // ★ 天气调制：雾色趋同阴天色 + 雾密度增大（雪天更浓）
    glm::vec3 overcastFog = m_weather.isSnow() ? glm::vec3(0.78f, 0.80f, 0.84f)
                                               : glm::vec3(0.45f, 0.47f, 0.52f);
    fogSun  = glm::mix(fogSun,  overcastFog, weatherW);
    fogDusk = glm::mix(fogDusk, overcastFog, weatherW);
    m_progLambert.setUnifVec3("u_FogSunColor", fogSun);
    m_progLambert.setUnifVec3("u_FogDuskColor", fogDusk);
    m_progLambert.setUnifVec3("u_FogSunDir", m_sunDir);   // 方向雾用（注意不是 u_LightDir，夜里不翻转）
    float fogDensity = m_fogDensity * (1.f + weatherW * (m_weather.isSnow() ? 3.f : 2.f));//天气影响雾效果
    m_progLambert.setUnifFloat("u_FogDensity", fogDensity);
    m_progLambert.setUnifInt("u_NormalMapEnabled", m_normalMapEnabled ? 1 : 0);


    //m_terrain.draw(0, 64, 0, 64, &m_progInstanced);
    // 玩家所在的 64×64 区域左下角，需要渲染玩家周围的9个Zone
    int playerZoneX = static_cast<int>(glm::floor(
                          m_player.mcr_position.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(
                          m_player.mcr_position.z / 64.f)) * 64;

    // ★ 构建视锥体（每帧根据当前摄像机重新计算）
    glm::mat4 vp = m_player.mcr_camera.getViewProj();
    Frustum frustum = extractFrustum(vp);

    // Chunk 顶点是世界坐标，不需要模型变换
    m_progLambert.setUnifMat4("u_Model", glm::mat4(1.0f));
    m_progLambert.setUnifMat4("u_ModelInvTr", glm::mat4(1.0f));
    // 绘制玩家周围的 3×3 个 64×64 区域
    int visibleCount = 0;
    for(int dx = -4; dx <= 4; ++dx) {
        for(int dz = -4; dz <= 4; ++dz) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            // ★ 视锥体剔除：Zone 的 AABB = (zoneX, 0, zoneZ) ~ (zoneX+64, 256, zoneZ+64)
            if(!zoneInFrustum(frustum, zoneX, 0.f, zoneZ,
                           zoneX + 64.f, 256.f, zoneZ + 64.f)) {
                continue;
            }
            visibleCount++;
            m_terrain.draw(zoneX, zoneX + 64, zoneZ, zoneZ + 64,
                           &m_progLambert, frustum.planes);
        }
    }

    // ========== 2. 绘制透明方块（Alpha 混合） ==========
    //glDisable(GL_CULL_FACE);   // ← 新增：液体面从内部也能看见
    glDepthMask(GL_FALSE);   // 透明物体不写深度
    glEnable(GL_BLEND);

    for(int dx = -4; dx <= 4; ++dx) {
        for(int dz = -4; dz <= 4; ++dz) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            if(!zoneInFrustum(frustum, zoneX, 0.f, zoneZ,
                           zoneX + 64.f, 256.f, zoneZ + 64.f)) {
                continue;
            }

            m_terrain.drawTransparent(zoneX, zoneX + 64, zoneZ, zoneZ + 64,
                                      &m_progLambert, frustum.planes);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    //glEnable(GL_CULL_FACE);    // ← 新增：恢复面剔除
}

void MyGL::renderSky(const glm::mat4 &viewproj) {
    // 方案要求传逆 ViewProj,把屏幕 NDC 反投影回世界空间
    glm::mat4 invVP = glm::inverse(viewproj);

    m_progSky.setUnifMat4("u_ViewProj", invVP);
    m_progSky.setUnifIVec2("u_Dimensions", glm::ivec2(width(), height()));
    m_progSky.setUnifVec3("u_Eye", m_player.mcr_camera.mcr_position);
    m_progSky.setUnifFloat("u_Time", m_elapsedTime);

    // ★ 太阳东升西落:绕 X 轴转。东=+X,西=-X
    glm::vec3 sunDir = m_sunDir;

    m_progSky.setUnifVec3("u_SunDir", sunDir);
    m_progSky.setUnifFloat("u_WeatherFactor", m_weather.intensity()); //传入天气影响因子
    m_progSky.setUnifInt("u_IsSnow", m_weather.isSnow() ? 1 : 0);    //是否下雪

    // 天空永远在最远处:不参与深度测试、不写深度
    // 这样后画的地形能正常遮挡天空
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    m_progSky.drawScreenQuad(m_screenQuad);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
}

// ★ 天气粒子：透明混合绘制，不写深度，不进阴影pass
void MyGL::renderWeather() {
    if(m_weather.intensity() <= 0.001f) return;

    // 相机 right/up（billboard 展开用）
    glm::vec3 fwd = m_player.mcr_camera.mcr_forward;
    glm::vec3 camRight = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 camUp    = glm::cross(camRight, glm::normalize(fwd));

    m_progWeather.setUnifVec3("u_CameraRight", camRight);
    m_progWeather.setUnifVec3("u_CameraUp", camUp);
    m_progWeather.setUnifVec3("u_Eye", m_player.mcr_camera.mcr_position);
    m_progWeather.setUnifFloat("u_Snow", m_weather.isSnow() ? 1.f : 0.f);
    m_progWeather.setUnifFloat("u_Alpha",
                               m_weather.isSnow() ? 0.9f : 0.55f);   // 雨整体更透

    // 状态组合：深度测试开着(被山遮挡)、不写深度(粒子互相不打架)、alpha混合
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);   // ★ 粒子是双面薄面片，且绕序与地形的 CW 设置相反

    m_weatherParticles.draw(m_progWeather);

    glEnable(GL_CULL_FACE);    // ★ 恢复
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}


// ★ 新增：纹理加载函数实现
void MyGL::loadTexture() {
    QImage img(":/textures/atlas.png");
    if(img.isNull()) {
        qDebug() << "Failed to load texture :/textures/atlas.png";
        return;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
    // 镜像 y 轴：OpenGL 纹理坐标原点在左下角，QImage 原点在左上角

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.bits());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    //法线贴图
    QImage nimg(":/textures/normal_atlas.png");
    if(nimg.isNull()) { qDebug() << "Failed to load normal atlas"; return; }
    nimg = nimg.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);

    glGenTextures(1, &m_normalTexture);
    glBindTexture(GL_TEXTURE_2D, m_normalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 nimg.width(), nimg.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nimg.bits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

//控制移动旋转和加速等
void MyGL::keyPressEvent(QKeyEvent *e) {
    switch(e->key()) {
    case Qt::Key_Escape: QApplication::quit();  break;
    case Qt::Key_W:     m_inputs.wPressed     = true; break;
    case Qt::Key_A:     m_inputs.aPressed     = true; break;
    case Qt::Key_S:     m_inputs.sPressed     = true; break;
    case Qt::Key_D:     m_inputs.dPressed     = true; break;
    case Qt::Key_Q:     m_inputs.qPressed     = true; break;
    case Qt::Key_E:     m_inputs.ePressed     = true; break;
    case Qt::Key_Space: m_inputs.spacePressed = true; break;
    case Qt::Key_F:     m_inputs.fPressed     = true; break;
    case Qt::Key_Y: m_shadowsEnabled = !m_shadowsEnabled; break;
    case Qt::Key_R: m_weather.cycleState(); break;
    case Qt::Key_N: m_normalMapEnabled = !m_normalMapEnabled; break;
    }
}

void MyGL::keyReleaseEvent(QKeyEvent *e) {
    switch(e->key()) {
    case Qt::Key_W:     m_inputs.wPressed     = false; break;
    case Qt::Key_A:     m_inputs.aPressed     = false; break;
    case Qt::Key_S:     m_inputs.sPressed     = false; break;
    case Qt::Key_D:     m_inputs.dPressed     = false; break;
    case Qt::Key_Q:     m_inputs.qPressed     = false; break;
    case Qt::Key_E:     m_inputs.ePressed     = false; break;
    case Qt::Key_Space: m_inputs.spacePressed = false; break;
    case Qt::Key_F:     m_inputs.fPressed     = false; break;
    }
}

void MyGL::mouseMoveEvent(QMouseEvent *e) {
    if(!e->spontaneous()) return;

    QPoint g = e->globalPos();
    if(!m_haveLastMouse) {              // 第一个事件只记基准,不算位移
        m_lastMouseGlobal = g;
        m_haveLastMouse = true;
        return;
    }

    QPoint delta = g - m_lastMouseGlobal;

    // 剔除异常跳变(warp 竞争残留、跨屏、DPI 突变产生的假位移)
    if(qAbs(delta.x()) > 150 || qAbs(delta.y()) > 150) {
        m_lastMouseGlobal = g;
        return;
    }

    m_inputs.mouseX += delta.x();
    m_inputs.mouseY += delta.y();

    // 惰性回中:只有离中心够远才 warp,大幅缩小竞态窗口
    QPoint center = this->mapToGlobal(QPoint(width() / 2, height() / 2));
    if(qAbs(g.x() - center.x()) > 200 || qAbs(g.y() - center.y()) > 200) {
        moveMouseToCenter();
        m_lastMouseGlobal = center;     // warp 后把基准设为中心
    } else {
        m_lastMouseGlobal = g;
    }
}

void MyGL::mousePressEvent(QMouseEvent *e) {
    // ---- 构造射线 ----
    glm::vec3 origin = m_player.mcr_camera.mcr_position;
    glm::vec3 dir    = m_player.mcr_camera.mcr_forward;

    RaycastResult result = m_terrain.raycast(origin, dir, 3.0f);

    // ============ 左键：破坏方块 ============
    if(e->button() == Qt::LeftButton) {
        if(!result.hit) return;

        // ★ 如果命中方块是 BEDROCK，不破坏
        BlockType target = m_terrain.getGlobalBlockAt(result.blockPos.x,
                                                      result.blockPos.y,
                                                      result.blockPos.z);
        if(target == BEDROCK) return;

        m_terrain.setGlobalBlockAt(result.blockPos.x,
                                   result.blockPos.y,
                                   result.blockPos.z, EMPTY);

        // 重建该 Chunk 的 VBO
        int cx = static_cast<int>(glm::floor(
                     result.blockPos.x / 16.f)) * 16;
        int cz = static_cast<int>(glm::floor(
                     result.blockPos.z / 16.f)) * 16;
        if(m_terrain.hasChunkAt(cx, cz)) {
            m_terrain.getChunkAt(cx, cz)->createVBOdata();
        }
    }

    // ============ 右键：放置方块 ============
    else if(e->button() == Qt::RightButton) {
        if(!result.hit) return;

        // 根据命中面法线计算放置位置
        glm::ivec3 placePos = result.blockPos;
        switch(result.faceNormal) {
        case XPOS: placePos.x += 1; break;
        case XNEG: placePos.x -= 1; break;
        case YPOS: placePos.y += 1; break;
        case YNEG: placePos.y -= 1; break;
        case ZPOS: placePos.z += 1; break;
        case ZNEG: placePos.z -= 1; break;
        }

        // Y 轴边界检查
        if(placePos.y < 0 || placePos.y >= 256) return;

        // 目标 Chunk 必须存在
        int cx = static_cast<int>(glm::floor(
                     placePos.x / 16.f)) * 16;
        int cz = static_cast<int>(glm::floor(
                     placePos.z / 16.f)) * 16;
        if(!m_terrain.hasChunkAt(cx, cz)) return;

        m_terrain.setGlobalBlockAt(placePos.x, placePos.y,
                                   placePos.z, currentBlock());   // ← 替换 LAVA
        m_terrain.getChunkAt(cx, cz)->createVBOdata();
    }
}

// 统一的切换入口：处理回绕 + 发信号
void MyGL::selectHotbar(int index) {
    if(m_hotbar.empty()) return;
    int n = static_cast<int>(m_hotbar.size());
    index = ((index % n) + n) % n;          // 循环回绕，-1 → 7
    if(index == m_hotbarIndex) return;

    m_hotbarIndex = index;
    emit sig_sendCurrentBlock(QString("%1/%2  %3")
                                  .arg(m_hotbarIndex + 1)
                                  .arg(n)
                                  .arg(blockTypeName(currentBlock())));
}

// 滚轮切换
void MyGL::wheelEvent(QWheelEvent *e) {
    // 防止高分辨率滚轮一次触发多个事件
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(now - m_lastWheelTime < 100) { e->accept(); return; }
    m_lastWheelTime = now;

    int delta = e->angleDelta().y();
    if(delta > 0)      selectHotbar(m_hotbarIndex - 1);  // 上滚 → 上一个
    else if(delta < 0) selectHotbar(m_hotbarIndex + 1);  // 下滚 → 下一个
    e->accept();
}