QT       += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    imageunderstandgui.cpp

HEADERS += \
    imageunderstandgui.h

# Application name
TARGET = image-understand

# Set output directory
DESTDIR = $$PWD/build

# Set object files directory
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

# Windows specific settings
win32 {

    #set ICON
    ICON += ico.ico

    # Console output for debugging
    CONFIG += console
    
    # Copy Python backend to build directory on Windows
    QMAKE_POST_LINK += $${QMAKE_COPY} $$shell_quote($$shell_path($$PWD/image_understand_backend.py)) $$shell_quote($$shell_path($$OUT_PWD/build/))
}

# macOS specific settings
macx {
    
    # Copy Python backend
    QMAKE_POST_LINK += cp $$PWD/image_understand_backend.py $$OUT_PWD/build/
}

# Linux specific settings
unix:!macx {
    # Copy Python backend
    QMAKE_POST_LINK += cp $$PWD/image_understand_backend.py $$OUT_PWD/build/
}

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# Installation rules
unix {
    target.path = /usr/local/bin
    INSTALLS += target
}

RESOURCES += \
    resources.qrc

DISTFILES += \
    lines.json \
    slider.json \
    ico.png \
    logo.png \
    ico.ico
