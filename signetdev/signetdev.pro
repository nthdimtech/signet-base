#-------------------------------------------------
#
# Project created by QtCreator 2017-11-19T14:38:37
#
#-------------------------------------------------

QT       -= core gui
CONFIG += static
TARGET = signetdev
TEMPLATE = lib

DEFINES += SIGNETDEV_LIBRARY

SOURCES += host/signetdev.c \
    host/signetdev_unix.c

HEADERS +=\
        signetdev_global.h \
        host/signetdev.h \
        host/hid_keyboard.h \
        host/signetdev_priv.h

unix {
HEADERS +=
    host/signetdev_unix.h \
}

win32 {
SOURCES += host/rawhid/hid_WINDOWS.c \
        host/signetdev_win32.c
}

macx {
SOURCES += host/signetdev_osx.c
HEADERS += host/signetdev_osx.h
}

unix:!macx {
SOURCES += host/signetdev_linux.c
}

unix {
    target.path = /usr/lib
    INSTALLS += target
}
