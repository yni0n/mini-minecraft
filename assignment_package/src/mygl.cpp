#include "mygl.h"
#include <glm_includes.h>
#include "scene/chunk.h"
#include <iostream>
#include <QApplication>
#include <QKeyEvent>
#include <QDateTime>

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

    // ★ 新增：创建描边方块 VBO
    m_blockWireframe.createVBOdata();

    // ★ 新增：加载纹理
    loadTexture();
    // ★ 新增：后处理着色器和全屏四边形
    m_progPostProcess.create(":/glsl/postprocess.vert.glsl",
                             ":/glsl/postprocess.frag.glsl");
    // ★ 新增:天空着色器(复用 postprocess.vert.glsl 做顶点着色器)
    m_progSky.create(":/glsl/postprocess.vert.glsl", ":/glsl/sky.frag.glsl");
    m_screenQuad.createVBOdata();
    createFBO(this->width(), this->height());
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

    renderSky(viewproj);   // ★ 新增:先画天空(不写深度),地形后画自动遮挡
    renderTerrain();//绘制地形

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
    // ★ 新增：激活纹理单元 0 并绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    m_progLambert.setUnifInt("u_Texture", 0);
    m_progLambert.setUnifFloat("u_Time", m_elapsedTime);

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
    for(int dx = -3; dx <= 3; ++dx) {
        for(int dz = -3; dz <= 3; ++dz) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            // ★ 视锥体剔除：Zone 的 AABB = (zoneX, 0, zoneZ) ~ (zoneX+64, 256, zoneZ+64)
            if(!zoneInFrustum(frustum, zoneX, 0.f, zoneZ,
                           zoneX + 64.f, 256.f, zoneZ + 64.f)) {
                continue;
            }
            visibleCount++;
            m_terrain.draw(zoneX, zoneX + 64, zoneZ, zoneZ + 64,
                           &m_progLambert);
        }
    }

    // ========== 2. 绘制透明方块（Alpha 混合） ==========
    //glDisable(GL_CULL_FACE);   // ← 新增：液体面从内部也能看见
    glDepthMask(GL_FALSE);   // 透明物体不写深度

    for(int dx = -3; dx <= 3; ++dx) {
        for(int dz = -3; dz <= 3; ++dz) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            if(!zoneInFrustum(frustum, zoneX, 0.f, zoneZ,
                           zoneX + 64.f, 256.f, zoneZ + 64.f)) {
                continue;
            }

            m_terrain.drawTransparent(zoneX, zoneX + 64, zoneZ, zoneZ + 64,
                                      &m_progLambert);
        }
    }

    glDepthMask(GL_TRUE);
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
    const float dayLength = 120.0f;                    // 一个昼夜的秒数,测试期用短值便于观察
    const float TWO_PI = 6.2831853f;
    float phase = glm::mod(m_elapsedTime, dayLength) / dayLength * TWO_PI;

    // 若希望轨迹偏向南方(+Z),第三个分量加一个非零常量,如 0.35f
    glm::vec3 sunDir = glm::normalize(glm::vec3(
        glm::cos(phase),     // 东(+X) → 西(-X)
        glm::sin(phase),     // 地平线(-1) → 天顶(1) → 地平线(-1)
        0.f));

    m_progSky.setUnifVec3("u_SunDir", sunDir);

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
    // ★ 过滤掉 moveMouseToCenter() 触发的合成事件
    if(!e->spontaneous()) return;

    QPoint center(width() / 2, height() / 2);
    QPoint delta = e->pos() - center;

    m_inputs.mouseX += delta.x();   // 累积本帧所有鼠标移动
    m_inputs.mouseY += delta.y();

    moveMouseToCenter();  // 鼠标锁回屏幕中央
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
                                   placePos.z, LAVA);
        m_terrain.getChunkAt(cx, cz)->createVBOdata();
    }
}
