cmake_minimum_required(VERSION 3.1.0)

project(qt_gl_gst)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)


find_package(Qt5Widgets CONFIG REQUIRED)
find_package(Qt5OpenGL CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(GLUT REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)

set(qt_gl_gst_SRCS
	main.cpp
	glwidget.cpp
	glwidget.h
	model.cpp
	model.h
	gstpipeline.cpp
	gstpipeline.h
	pipeline.cpp
	pipeline.h
	shaderlists.cpp
	shaderlists.h
	mainwindow.cpp
	mainwindow.h
	yuvdebugwindow.cpp
	yuvdebugwindow.h
	controlsform.cpp
	controlsform.h
	controlsform.ui
	applogger.cpp
	applogger.h
)

add_executable(qt_gl_gst WIN32 ${qt_gl_gst_SRCS})

add_compile_definitions(UNIX)
add_compile_definitions(VIDI420_SHADERS_NEEDED)
add_compile_definitions(VIDUYVY_SHADERS_NEEDED)
add_compile_definitions(RECTTEX_EXT_NEEDED)
add_compile_definitions(GLU_NEEDED)

include_directories(${GST_INCLUDE_DIRS})
target_link_libraries(qt_gl_gst 
	${GST_LIBRARIES} 
	${GLUT_LIBRARY} 
	${GLEW_LIBRARY}
	${OPENGL_LIBRARIES}
	assimp 
	Qt5::Widgets 
	Qt5::OpenGL
)
