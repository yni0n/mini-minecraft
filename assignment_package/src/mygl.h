#ifndef MYGL_H
#define MYGL_H
#include <QImage>     // 顶部加
#include "openglcontext.h"
#include "shaderprogram.h"
#include "scene/worldaxes.h"
#include "scene/camera.h"
#include "scene/terrain.h"
#include "scene/player.h"
#include "scene/blockwireframe.h"          // ★ 新增

#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <smartpointerhelp.h>


class MyGL : public OpenGLContext
{
    Q_OBJECT
private:
    WorldAxes m_worldAxes; // 世界坐标轴。这是一个线框模型，用来在屏幕上画出 X、Y、Z 轴，方便你开发时调试方向。硬编码中心在(32, 128, 32).
    ShaderProgram m_progLambert;// 使用Lambert反射的着色器(漫反射)

    ShaderProgram m_progFlat;// Flat 反射，也就是纯色渲染，不考虑任何光影。
    ShaderProgram m_progInstanced;//专门为实例化渲染（Instanced Rendering）设计的。

    GLuint vao; // vertex array object句柄. This will store the VBOs created in our geometry classes.
                // Don't worry too much about this. Just know it is necessary in order to render geometry.

    Terrain m_terrain; // 地形对象。它包含了组成整个世界的所有区块（Chunks）。
    Player m_player; // 玩家对象。里面包含了玩家的位置、速度，以及摄像机。
    InputBundle m_inputs; // 输入数据包。专门用来收集玩家操作：keyPressEvent, mouseMoveEvent, mousePressEvent, etc.

    QTimer m_timer; // Timer linked to tick(). 60帧/s。

    qint64 m_prevFrameTime;   // ★ 新增：上一帧的时间戳（毫秒）
    float m_elapsedTime;          // ★ 游戏运行总时间 (秒)

    // ★ 新增：方块描边
    BlockWireframe m_blockWireframe;
    glm::ivec3 m_targetBlock;    // 当前瞄准的方块坐标
    bool m_hasTarget;            // 当前是否有瞄准目标

    GLuint m_texture;             // ★ 新增：纹理对象句柄
    void loadTexture();           // ★ 新增：纹理加载函数

    void moveMouseToCenter(); // 强制把鼠标移动到屏幕正中心。 You should call this
                              // from within a mouse move event after reading the mouse movement so that
                              // your mouse stays within the screen bounds and is always read.

    void sendPlayerDataToGUI() const;//把玩家数据发送给界面。signal


public:
    explicit MyGL(QWidget *parent = nullptr);
    ~MyGL();

    // Called once when MyGL is initialized.
    // Once this is called, all OpenGL function
    // invocations are valid (before this, they
    // will cause segfaults)
    void initializeGL() override;
    // Called whenever MyGL is resized.玩家拖拽改变了窗口的大小，就会触发。
    void resizeGL(int w, int h) override;
    // Called whenever MyGL::update() is called.主渲染循环
    // In the base code, update() is called from tick().
    void paintGL() override;

    // Called from paintGL().绘制地形
    // Calls Terrain::draw().
    void renderTerrain();

protected:
    // Automatically invoked when the user
    // presses a key on the keyboard
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    // Automatically invoked when the user
    // moves the mouse
    void mouseMoveEvent(QMouseEvent *e) override;
    // Automatically invoked when the user
    // presses a mouse button
    void mousePressEvent(QMouseEvent *e) override;

    void handleBlockInteraction(QMouseEvent *e);   // ★ 新增

private slots:
    void tick(); // 定时器绑定的槽函数。called ~60 times per second by m_timer firing.

signals:
    void sig_sendPlayerPos(QString) const;
    void sig_sendPlayerVel(QString) const;
    void sig_sendPlayerAcc(QString) const;
    void sig_sendPlayerLook(QString) const;
    void sig_sendPlayerChunk(QString) const;
    void sig_sendPlayerTerrainZone(QString) const;
};


#endif // MYGL_H
