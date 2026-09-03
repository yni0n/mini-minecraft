#ifndef MYGL_H
#define MYGL_H
#include <QImage>
#include <QWheelEvent>
#include <QDateTime>
#include <vector>
#include "openglcontext.h"
#include "shaderprogram.h"
#include "screenquad.h"
#include "scene/worldaxes.h"
#include "scene/camera.h"
#include "scene/terrain.h"
#include "scene/player.h"
#include "scene/blockwireframe.h"
#include "weather.h"
#include "weatherparticles.h"

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

    QPoint m_lastMouseGlobal;   // 上一次鼠标的全局位置
    bool m_haveLastMouse = false;

    qint64 m_prevFrameTime;   // 上一帧的时间戳（毫秒）
    float m_elapsedTime;          // 游戏运行总时间 (秒)
    // 方块描边
    BlockWireframe m_blockWireframe;
    glm::ivec3 m_targetBlock;    // 当前瞄准的方块坐标
    bool m_hasTarget;            // 当前是否有瞄准目标

    GLuint m_texture;             // 纹理对象句柄
    GLuint m_normalTexture;       //法线纹理
    void loadTexture();           // 纹理加载函数

    void moveMouseToCenter(); // 强制把鼠标移动到屏幕正中心。 You should call this
                              // from within a mouse move event after reading the mouse movement so that
                              // your mouse stays within the screen bounds and is always read.

    void sendPlayerDataToGUI() const;//把玩家数据发送给界面。signal

    // 后处理管线
    GLuint m_frameBuffer;          // FBO 句柄
    GLuint m_renderTexture;        // FBO 颜色纹理
    GLuint m_depthRenderBuffer;    // FBO 深度缓冲
    ShaderProgram m_progPostProcess;  // 后处理着色器
    ScreenQuad m_screenQuad;          // 全屏四边形
    ShaderProgram m_progSky;      // 天空纹理映射着色器

    ShaderProgram m_progShadow;      // 深度 pass 着色器
    bool m_shadowsEnabled = true;
    static constexpr int SHADOW_FRAMES = 4;          // 时间累积帧数
    GLuint m_shadowFBO[SHADOW_FRAMES] = {0,0,0,0};
    GLuint m_shadowDepthTex[SHADOW_FRAMES] = {0,0,0,0};
    glm::mat4 m_shadowVP[SHADOW_FRAMES];             // [m_shadowIdx]=最新,往前越旧
    int m_shadowIdx = 0;
    bool m_shadowFirstFrame = true;

    glm::vec3 m_lightDir;            // 阴影用光源方向(与 renderTerrain 的 lightDir 一致)

    void createShadowFBO();          // 创建一次(固定 2048,不随窗口重建)
    void renderShadowPass();         // 每帧的深度 pass


    void createFBO(int width, int height);   // 创建/重建 FBO
    int getFluidType() const;                // 检测相机所在流体类型

    //blinn-Phong
    glm::vec3 computeSunDir() const;   // 太阳方向（指向太阳）
    glm::vec3 m_sunDir;                // 每帧更新
    bool m_normalMapEnabled = false;   // 法线贴图是否启用，默认关

    void computeFogColors(glm::vec3& fogSun, glm::vec3& fogDusk) const;  // 两个方向的雾色   // 与天空地平线一致的雾色
    float m_fogDensity = 0.012f;         // 雾密度，越大越雾
    WeatherSystem m_weather;   // ★ 天气系统
    ShaderProgram m_progWeather;            // ★ 天气粒子着色器
    WeatherParticles m_weatherParticles;    // ★ 粒子池
    void renderWeather();                   // ★ 粒子绘制



    // ★ 热键栏：滚轮在这 8 种方块之间循环
    std::vector<BlockType> m_hotbar = { GRASS, DIRT, STONE, SAND,
                                       WATER, SNOW, LAVA, BEDROCK,
                                       LOG, LEAVES, CACTUS, TALLGRASS, FLOWER };
    int m_hotbarIndex = 0;

    BlockType currentBlock() const { return m_hotbar[m_hotbarIndex]; }
    void selectHotbar(int index);   // 唯一的切换入口
    qint64 m_lastWheelTime = 0;


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
    void renderSky(const glm::mat4 &viewproj);

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

    void handleBlockInteraction(QMouseEvent *e);   //鼠标输入
    void wheelEvent(QWheelEvent *e) override;   // 滚轮


private slots:
    void tick(); // 定时器绑定的槽函数。called ~60 times per second by m_timer firing.

signals:
    void sig_sendPlayerPos(QString) const;
    void sig_sendPlayerVel(QString) const;
    void sig_sendPlayerAcc(QString) const;
    void sig_sendPlayerLook(QString) const;
    void sig_sendPlayerChunk(QString) const;
    void sig_sendPlayerTerrainZone(QString) const;
    void sig_sendCurrentBlock(QString) const;
};


#endif // MYGL_H
