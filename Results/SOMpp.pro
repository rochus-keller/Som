
QT       -= core
QT       -= gui

TARGET = SOM
TEMPLATE = app

CONFIG += c++11

DEFINES += GC_TYPE=MARK_SWEEP

INCLUDEPATH += src

HEADERS += \
    src/compiler/BytecodeGenerator.h \
    src/compiler/ClassGenerationContext.h \
    src/compiler/Disassembler.h \
    src/compiler/Lexer.h \
    src/compiler/MethodGenerationContext.h \
    src/compiler/Parser.h \
    src/compiler/SourcecodeCompiler.h \
    src/interpreter/bytecodes.h \
    src/interpreter/Interpreter.h \
    src/memory/CopyingCollector.h \
    src/memory/CopyingHeap.h \
    src/memory/GarbageCollector.h \
    src/memory/GenerationalCollector.h \
    src/memory/GenerationalHeap.h \
    src/memory/Heap.h \
    src/memory/MarkSweepCollector.h \
    src/memory/MarkSweepHeap.h \
    src/misc/debug.h \
    src/misc/defs.h \
    src/misc/ExtendedList.h \
    src/misc/gettimeofday.h \
    src/misc/Timer.h \
    src/primitives/Array.h \
    src/primitives/Block.h \
    src/primitives/Class.h \
    src/primitives/Double.h \
    src/primitives/Integer.h \
    src/primitives/Method.h \
    src/primitives/Object.h \
    src/primitives/Primitive.h \
    src/primitives/String.h \
    src/primitives/Symbol.h \
    src/primitives/System.h \
    src/primitivesCore/PrimitiveContainer.h \
    src/primitivesCore/PrimitiveLoader.h \
    src/primitivesCore/Routine.h \
    src/vm/Shell.h \
    src/vm/Universe.h \
    src/vmobjects/AbstractObject.h \
    src/vmobjects/IntegerBox.h \
    src/vmobjects/ObjectFormats.h \
    src/vmobjects/PrimitiveRoutine.h \
    src/vmobjects/Signature.h \
    src/vmobjects/VMArray.h \
    src/vmobjects/VMBlock.h \
    src/vmobjects/VMClass.h \
    src/vmobjects/VMDouble.h \
    src/vmobjects/VMEvaluationPrimitive.h \
    src/vmobjects/VMFrame.h \
    src/vmobjects/VMInteger.h \
    src/vmobjects/VMInvokable.h \
    src/vmobjects/VMMethod.h \
    src/vmobjects/VMObject.h \
    src/vmobjects/VMObjectBase.h \
    src/vmobjects/VMPrimitive.h \
    src/vmobjects/VMString.h \
    src/vmobjects/VMSymbol.h

SOURCES += \
    src/compiler/BytecodeGenerator.cpp \
    src/compiler/ClassGenerationContext.cpp \
    src/compiler/Disassembler.cpp \
    src/compiler/Lexer.cpp \
    src/compiler/MethodGenerationContext.cpp \
    src/compiler/Parser.cpp \
    src/compiler/SourcecodeCompiler.cpp \
    src/interpreter/bytecodes.cpp \
    src/interpreter/Interpreter.cpp \
    src/memory/CopyingCollector.cpp \
    src/memory/CopyingHeap.cpp \
    src/memory/GenerationalCollector.cpp \
    src/memory/GenerationalHeap.cpp \
    src/memory/Heap.cpp \
    src/memory/MarkSweepCollector.cpp \
    src/memory/MarkSweepHeap.cpp \
    src/misc/Timer.cpp \
    src/primitives/Array.cpp \
    src/primitives/Block.cpp \
    src/primitives/Class.cpp \
    src/primitives/Double.cpp \
    src/primitives/Integer.cpp \
    src/primitives/Method.cpp \
    src/primitives/Object.cpp \
    src/primitives/Primitive.cpp \
    src/primitives/String.cpp \
    src/primitives/Symbol.cpp \
    src/primitives/System.cpp \
    src/primitivesCore/PrimitiveContainer.cpp \
    src/primitivesCore/PrimitiveLoader.cpp \
    src/vm/Shell.cpp \
    src/vm/Universe.cpp \
    src/vmobjects/AbstractObject.cpp \
    src/vmobjects/IntegerBox.cpp \
    src/vmobjects/Signature.cpp \
    src/vmobjects/VMArray.cpp \
    src/vmobjects/VMBlock.cpp \
    src/vmobjects/VMClass.cpp \
    src/vmobjects/VMDouble.cpp \
    src/vmobjects/VMEvaluationPrimitive.cpp \
    src/vmobjects/VMFrame.cpp \
    src/vmobjects/VMInteger.cpp \
    src/vmobjects/VMInvokable.cpp \
    src/vmobjects/VMMethod.cpp \
    src/vmobjects/VMObject.cpp \
    src/vmobjects/VMPrimitive.cpp \
    src/vmobjects/VMString.cpp \
    src/vmobjects/VMSymbol.cpp \
    src/Main.cpp




