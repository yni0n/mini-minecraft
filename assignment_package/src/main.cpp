#include <mainwindow.h>

#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>

//打印当前配置在控制台
void debugFormatVersion()
{
    QSurfaceFormat form = QSurfaceFormat::defaultFormat();
    QSurfaceFormat::OpenGLContextProfile prof = form.profile();

    const char *profile =
        prof == QSurfaceFormat::CoreProfile ? "Core" :
        prof == QSurfaceFormat::CompatibilityProfile ? "Compatibility" :
        "None";

    printf("Requested format:\n");
    printf("  Version: %d.%d\n", form.majorVersion(), form.minorVersion());
    printf("  Profile: %s\n", profile);
}

//初始化 Qt 框架
int main(int argc, char *argv[])
{
    //接管系统消息(键鼠输入)
    QApplication a(argc, argv);

    //配置：OpenGL 4.0，使用Core，禁用废弃函数
    QSurfaceFormat format;
    format.setVersion(4, 0);
    format.setOption(QSurfaceFormat::DeprecatedFunctions, false);
    format.setProfile(QSurfaceFormat::CoreProfile);
    //format.setSamples(4);  // Uncomment for nice antialiasing. Not always supported.

    /*** AUTOMATIC TESTING: DO NOT MODIFY ***/
    /*** Check whether automatic testing is enabled */
    //如果系统环境变量里有 CIS277_AUTOTESTING（说明当前是在做自动化测试），
    //那就把抗锯齿关掉（设为0），以此来加快测试速度。
    /***/ if (qgetenv("CIS277_AUTOTESTING") != nullptr) format.setSamples(0);

    QSurfaceFormat::setDefaultFormat(format);
    debugFormatVersion();

    //创建主窗口并显示
    MainWindow w;
    w.show();

    return a.exec();//启动死循环
}
