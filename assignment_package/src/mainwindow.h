#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "cameracontrolshelp.h"
#include "playerinfo.h"

//告诉编译器“在 Ui 这个命名空间下，有一个叫 MainWindow 的类”
namespace Ui {
class MainWindow;
}

// Qt 自带的顶级主窗口类 QMainWindow，
//它天生就带有菜单栏、工具栏、状态栏等主窗口特有的功能。
class MainWindow : public QMainWindow
{
    Q_OBJECT//使用signals+slot就要写

public://构造函数和析构函数
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots://声明了下面这两个函数是“槽函数”
    void on_actionQuit_triggered();

    void on_actionCamera_Controls_triggered();

private:
    Ui::MainWindow *ui;//指针，访问界面控件
    CameraControlsHelp cHelp; //摄像机控制帮助
    PlayerInfo playerInfoWindow;//玩家信息
};


#endif // MAINWINDOW_H
