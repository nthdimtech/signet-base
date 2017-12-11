QT += core
QT -= gui

CONFIG += c++11

TARGET = button-press-test
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

CONFIG(release, debug|release):LIBS += -L$$PWD/../../build-signetdev-$$QT_ARCH-release
CONFIG(debug, debug|release):LIBS += -L$$PWD/../../build-signetdev-$$QT_ARCH-debug
LIBS += -lsignetdev
INCLUDEPATH += $$PWD/../../

SOURCES += main.cpp

