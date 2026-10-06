QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = FirstPersonShooter
TEMPLATE = app

SOURCES += \
    main.cpp \
    gamewidget.cpp

HEADERS += \
    gamewidget.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += sounds.qrc
