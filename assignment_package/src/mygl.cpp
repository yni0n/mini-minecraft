#include "mygl.h"
#include <glm_includes.h>

#include <iostream>
#include <QApplication>
#include <QKeyEvent>
#include <QDateTime>

MyGL::MyGL(QWidget *parent)
    : OpenGLContext(parent), //初始化列表
      m_worldAxes(this),
      m_progLambert(this), m_progFlat(this), m_progInstanced(this),
      m_terrain(this), m_player(glm::vec3(48.f, 129.f, 48.f), m_terrain),
      m_blockWireframe(this),           // ★ 新增
      m_hasTarget(false)                // ★ 新增
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
}

//改变窗口大小就触发
void MyGL::resizeGL(int w, int h) {
    //This code sets the concatenated view and perspective projection matrices used for
    //our scene's camera view.
    m_player.setCameraWidthHeight(static_cast<unsigned int>(w), static_cast<unsigned int>(h));
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

    m_terrain.expandTerrain(m_player.mcr_position);   // ★ 新增：每帧检查

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
    // Clear the screen so that we only see newly drawn images
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //计算VP，并给3个shader发送
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);
    m_progInstanced.setUnifMat4("u_ViewProj", viewproj);

    renderTerrain();//绘制地形

    // ★ 新增：渲染方块描边
    if(m_hasTarget) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f),
                                         glm::vec3(m_targetBlock));
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
}

// TODO: Change this so it renders the nine zones of generated
// terrain that surround the player (refer to Terrain::m_generatedTerrain
// for more info)
//指定区域并绘制，使用实例绘制
void MyGL::renderTerrain() {
    //m_terrain.draw(0, 64, 0, 64, &m_progInstanced);
    // 玩家所在的 64×64 区域左下角，需要渲染玩家周围的9个Zone
    int playerZoneX = static_cast<int>(glm::floor(
                          m_player.mcr_position.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(
                          m_player.mcr_position.z / 64.f)) * 64;

    // Chunk 顶点是世界坐标，不需要模型变换
    m_progLambert.setUnifMat4("u_Model", glm::mat4(1.0f));
    m_progLambert.setUnifMat4("u_ModelInvTr", glm::mat4(1.0f));
    // 绘制玩家周围的 3×3 个 64×64 区域
    for(int dx = -1; dx <= 1; ++dx) {
        for(int dz = -1; dz <= 1; ++dz) {
            int zoneX = playerZoneX + dx * 64;
            int zoneZ = playerZoneZ + dz * 64;
            m_terrain.draw(zoneX, zoneX + 64, zoneZ, zoneZ + 64,
                           &m_progLambert);
        }
    }
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
                                   placePos.z, STONE);
        m_terrain.getChunkAt(cx, cz)->createVBOdata();
    }
}
