#pragma once

#include <QOpenGLWidget>
#include <QTimer>
#include <QOpenGLExtraFunctions>

//QOpenGLWidget让普通的 Qt 窗口变成一个“能画 OpenGL 图形的画板”。
//QOpenGLExtraFunctions给这个类提供“OpenGL 的所有画笔和工具”。
class OpenGLContext
    : public QOpenGLWidget,
      public QOpenGLExtraFunctions
{

public:
    OpenGLContext(QWidget *parent);
    ~OpenGLContext();

    //用于调试：打印当前电脑显卡支持的 OpenGL 版本信息。
    //OpenGL 绘图出错时，打印出具体的错误代码
    //打印着色器（Shader）的编译和链接日志
    void debugContextVersion();
    void printGLErrorLog();
    void printLinkInfoLog(int prog);
    void printShaderInfoLog(int shader);
};
