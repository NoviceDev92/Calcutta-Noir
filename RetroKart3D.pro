QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    game_canvas.cpp \
    sprite_atlas.cpp \
    network_manager.cpp \
    garage_system.cpp \
    hazard_system.cpp \
    kolkata_map.cpp

HEADERS += \
    mainwindow.h \
    game_canvas.h \
    sprite_atlas.h \
    network_manager.h \
    kolkata_types.h \
    garage_system.h \
    hazard_system.h \
    kolkata_map.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
