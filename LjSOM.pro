#/*
#* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
#*
#* This file is part of the SOM Smalltalk VM application.
#*
#* The following is the license that applies to this copy of the
#* application. For a license to use the application under conditions
#* other than those described here, please email to me@rochus-keller.ch.
#*
#* GNU General Public License Usage
#* This file may be used under the terms of the GNU General Public
#* License (GPL) versions 2.0 or 3.0 as published by the Free Software
#* Foundation and appearing in the file LICENSE.GPL included in
#* the packaging of this file. Please review the following information
#* to ensure GNU General Public Licensing requirements will be met:
#* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
#* http://www.gnu.org/copyleft/gpl.html.
#*/

QT       += core
QT       -= gui

TARGET = LjSOM
CONFIG   += console
CONFIG   -= app_bundle
TEMPLATE = app

INCLUDEPATH += .. ../LuaJIT/src

SOURCES += \
    SomLjObjectManager.cpp \
    SomAst.cpp \
    SomLexer.cpp \
    SomParser.cpp \
    SomLuaTranspiler.cpp \
    SomLjLibFfi.cpp \
    SomLjbcCompiler.cpp \
    ../LjTools/LuaJitBytecode.cpp \
    ../LjTools/Engine2.cpp \
    ../LjTools/LuaJitComposer.cpp \
    LjSOM.cpp \
    SomLjbcCompiler2.cpp


HEADERS  += \ 
    SomLjObjectManager.h \
    SomAst.h \
    SomLexer.h \
    SomParser.h \
    SomLuaTranspiler.h \
    SomLjbcCompiler.h \
    ../LjTools/LuaJitBytecode.h \
    ../LjTools/Engine2.h \
    ../LjTools/LuaJitComposer.h \
    LjSOM.h \
    SomLjbcCompiler2.h


win32 {
    LIBS += -L../LuaJIT/src -llua51
}
linux {
    include( ../LuaJIT/src/LuaJit.pri ){
        LIBS += -ldl
    } else {
        LIBS += -lluajit
    }
    QMAKE_LFLAGS += -rdynamic -ldl
    #rdynamic is required so that the LjLibFfi functions are visible to LuaJIT FFI
}
macx {
    include( ../LuaJIT/src/LuaJit.pri )
    QMAKE_LFLAGS += -rdynamic -ldl -pagezero_size 10000 -image_base 100000000
}

CONFIG(debug, debug|release) {
        DEFINES += _DEBUG
}

!win32 {
    QMAKE_CXXFLAGS += -Wno-reorder -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
    QMAKE_CXXFLAGS += -fno-stack-protector # see https://stackoverflow.com/questions/1345670/stack-smashing-detected
}

RESOURCES += \
    SomLjVirtualMachine.qrc
