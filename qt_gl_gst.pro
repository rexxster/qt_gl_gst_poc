#-------------------------------------------------
#
# Project created by QtCreator 2012-03-03T06:40:34
#
#-------------------------------------------------

QT       += core gui opengl
CONFIG   += console


TARGET = qt_gl_gst
TEMPLATE = app

DEFINES += UNIX

SOURCES += main.cpp \
    glwidget.cpp \
    pipeline.cpp \
    gstthread.cpp

HEADERS  += \
    glwidget.h \
    pipeline.h \
    gstthread.h \
    AsyncQueue.h

LIBS += -lglut \
    -lgstreamer-0.10 \
    -lgstinterfaces-0.10 \
    -lglib-2.0 \
    -lgmodule-2.0 \
    -lgobject-2.0 \
    -lgthread-2.0 \
    -lGLU \
    -lGL \
    -lGLEW

INCLUDEPATH += /usr/include/gstreamer-0.10 \
    /usr/local/include/gstreamer-0.10 \
    /usr/include/glib-2.0 \
    /usr/lib/i386-linux-gnu/glib-2.0/include \
    /usr/include/libxml2
