QT += core
QT -= gui

CONFIG += c++11

TARGET = stage2-firmware-loader
CONFIG += console
CONFIG -= app_bundle

LIBS += -lgcrypt -lgpg-error

TEMPLATE = app

INCLUDEPATH += $$PWD/../../
SOURCES += ../../signetdev/host/signetdev.c
SOURCES += ../../signetdev/host/signetdev_emulate.c

unix {
HEADERS += ../../signetdev/host/signetdev_unix.h
SOURCES += ../../signetdev/host/signetdev_unix.c
}

win32 {
SOURCES += ../../signetdev/host/rawhid/hid_WINDOWS.c \
        ../../signetdev/host/signetdev_win32.c
}

macx {
SOURCES += ../../signetdev/host/signetdev_osx.c
}

unix:!macx {
SOURCES += ../../signetdev/host/signetdev_linux.c
}

SOURCES += main.cpp

