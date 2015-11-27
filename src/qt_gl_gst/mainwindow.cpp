#include "mainwindow.h"
#include "applogger.h"

#include "glwidget.h"
#include "controlsform.h"

MainWindow::MainWindow(int argc, char *argv[], QWidget *parent) :
    QMainWindow(parent)
{
    GlobalLog.SetModuleLogLevel(LOG_GL, Logger::Info);
    GlobalLog.SetModuleLogLevel(LOG_GLSHADERS, Logger::Info);
    GlobalLog.SetModuleLogLevel(LOG_OBJLOADER, Logger::Info);
    GlobalLog.SetModuleLogLevel(LOG_VIDPIPELINE, Logger::Info);

    GLWidget *glWidget = new GLWidget(argc, argv, this);
    glWidget->initVideo();

    ControlsForm *controlsForm = new ControlsForm(glWidget, this);
    setCentralWidget(controlsForm);
}
