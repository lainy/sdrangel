#--------------------------------------------------------
#
# Pro file for Android and Windows builds with Qt Creator
#
#--------------------------------------------------------

TEMPLATE = lib
CONFIG += plugin

QT += core gui widgets multimedia opengl

TARGET = inputrtlsdr

DEFINES += USE_SSE2=1
QMAKE_CXXFLAGS += -msse2
DEFINES += USE_SSE4_1=1
QMAKE_CXXFLAGS += -msse4.1
QMAKE_CXXFLAGS += -std=c++11

CONFIG(MINGW32):LIBRTLSDRSRC = "C:\softs\librtlsdr"
CONFIG(MINGW64):LIBRTLSDRSRC = "C:\softs\librtlsdr"
INCLUDEPATH += $$PWD
INCLUDEPATH += ../../../exports
INCLUDEPATH += ../../../sdrbase
INCLUDEPATH += ../../../sdrgui
INCLUDEPATH += ../../../swagger/sdrangel/code/qt5/client
!macx:INCLUDEPATH += $$LIBRTLSDRSRC/include
macx:INCLUDEPATH += /opt/local/include

CONFIG(Release):build_subdir = release
CONFIG(Debug):build_subdir = debug

SOURCES += rtlsdrgui.cpp\
  rtlsdrinput.cpp\
  rtlsdrplugin.cpp\
  rtlsdrsettings.cpp\
  rtlsdrthread.cpp

HEADERS += rtlsdrgui.h\
  rtlsdrinput.h\
  rtlsdrplugin.h\
  rtlsdrsettings.h\
  rtlsdrthread.h

FORMS += rtlsdrgui.ui

LIBS += -L../../../sdrbase/$${build_subdir} -lsdrbase
LIBS += -L../../../sdrgui/$${build_subdir} -lsdrgui
LIBS += -L../../../swagger/$${build_subdir} -lswagger
!macx:LIBS += -L../../../librtlsdr/$${build_subdir} -llibrtlsdr
macx:LIBS += -L/opt/local/lib -lrtlsdr

RESOURCES = ../../../sdrgui/resources/res.qrc
