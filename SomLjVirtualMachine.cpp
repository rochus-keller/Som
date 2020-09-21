/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the SOM Smalltalk parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

#include "SomLjVirtualMachine.h"
#include "SomLjObjectManager.h"
#include <LjTools/Engine2.h>
#include <LjTools/LuaIde.h>
#include <LjTools/LuaProject.h>
#include <LjTools/Terminal2.h>
#include <LjTools/LuaJitComposer.h>
#include <LjTools/LjBcDebugger.h>
#include <LuaJIT/src/lua.hpp>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QtDebug>
#include <lua.hpp>
#include <QPainter>
#include <iostream>
using namespace Som;
using namespace Lua;

static void loadLuaLib( Lua::Engine2* lua, const QByteArray& name )
{
    QFile lib( QString(":/%1.lua").arg(name.constData()) );
    lib.open(QIODevice::ReadOnly);
    if( !lua->addSourceLib( lib.readAll(), name ) )
        qCritical() << "compiling" << name << ":" << lua->getLastError().constData();
}

#define lj_rol(x, n)	(((x)<<(n)) | ((x)>>(-(int)(n)&(8*sizeof(x)-1))))

static int hashOf(lua_State * L)
{
    int h = 0;
    switch( lua_type(L,1) )
    {
    case LUA_TNUMBER:
        h = lua_tointeger(L,1);
        break;
    case LUA_TNIL:
        h = 0;
        break;
    case LUA_TBOOLEAN:
        h = lua_toboolean(L,1);
        break;
    case LUA_TSTRING:
        {
            const char* str = lua_tostring(L,1);
            const int len = ::strlen(str);
            h = len;
            int a = *(const quint8 *)str;
            h ^= *(const quint8 *)(str+len-1);
            int b = *(const quint8 *)(str+(len>>1));
            h ^= b; h -= lj_rol(b, 14);
            return h;
        }
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TUSERDATA:
    case LUA_TTHREAD:
    case LUA_TLIGHTUSERDATA:
        h = (lua_Integer)lua_topointer( L, 1 );
        break;
    }

    lua_pushinteger( L, h );
    return 1;
}

#if LUAJIT_VERSION_NUM >= 20100
#define ST_USE_MONITOR
#endif

#ifdef ST_USE_MONITOR
#ifdef ST_USE_MONITOR_GUI
class JitMonitor : public QWidget
{
public:
    class Gauge : public QWidget
    {
    public:
        Gauge( QWidget* p):QWidget(p),d_val(0){ setMinimumWidth(100);}
        quint32 d_val;
        void paintEvent(QPaintEvent *)
        {
            QPainter p(this);
            p.fillRect(0,0, width() / 100 * d_val, height(),Qt::red);
            p.drawText(rect(), Qt::AlignCenter, QString("%1%").arg(d_val));
        }
    };

    enum Bar { Compiled, Interpreted, C, GC, Compiler, MAX };
    quint32 d_state[MAX];
    quint32 d_count;
    Gauge* d_bars[MAX];
    JitMonitor():d_count(1)
    {
        QFormLayout* vb = new QFormLayout(this);
        static const char* names[] = {
            "Compiled:", "Interpreted:", "C Code:", "Garbage Collector:", "JIT Compiler:"
        };
        for( int i = 0; i < MAX; i++ )
        {
            d_bars[i] = new Gauge(this);
            d_state[i] = 0;
            vb->addRow(names[i],d_bars[i]);
        }
    }
};
static JitMonitor* s_jitMonitor = 0;

static void profile_callback(void *data, lua_State *L, int samples, int vmstate)
{
    if( s_jitMonitor == 0 )
        s_jitMonitor = new JitMonitor();
    switch( vmstate )
    {
    case 'N':
        vmstate = JitMonitor::Compiled;
        break;
    case 'I':
        vmstate = JitMonitor::Interpreted;
        break;
    case 'C':
        vmstate = JitMonitor::C;
        break;
    case 'G':
        vmstate = JitMonitor::GC;
        break;
    case 'J':
        vmstate = JitMonitor::Compiler;
        break;
    default:
        qCritical() << "profile_callback: unknown vmstate" << vmstate;
        return;
    }
    JitMonitor* m = s_jitMonitor;
    m->d_count += samples;
    m->d_state[vmstate] += samples;
    m->d_bars[vmstate]->d_val = m->d_state[vmstate] / double(m->d_count) * 100.0 + 0.5;
    m->d_bars[vmstate]->update();
    m->show();
    // qDebug() << "profile_callback" << Display::inst()->getTicks() << samples << vmstate << (char)vmstate;
}
#else

static inline int percent( double a, double b )
{
    return a / b * 100 + 0.5;
}

static void profile_callback(void *data, lua_State *L, int samples, int vmstate)
{
    static quint32 compiled = 0, interpreted = 0, ccode = 0, gc = 0, compiler = 0, count = 1, lag = 0;
    count += samples;
    switch( vmstate )
    {
    case 'N':
        compiled += samples;
        break;
    case 'I':
        interpreted += samples;
        break;
    case 'C':
        ccode += samples;
        break;
    case 'G':
        gc += samples;
        break;
    case 'J':
        compiler += samples; // never seen so far
        break;
    default:
        qCritical() << "profile_callback: unknown vmstate" << vmstate;
        return;
    }
    lag++;
    if( lag >= 0 ) // adjust to luaJIT_profile_start i parameter
    {
        lag = 0;

        qDebug() << "profile_callback" << percent(compiled,count) << percent(interpreted,count) <<
                    percent(ccode,count) << percent(gc,count) <<
                    percent(compiler,count) << "time:" << St::Display::inst()->getTicks();
    }
}
#endif
#endif

LjVirtualMachine::LjVirtualMachine(QObject* parent) : QObject(parent)
{
    d_lua = new Engine2(this);
    Engine2::setInst(d_lua);
    connect( d_lua,SIGNAL(onNotify(int,QByteArray,int)), this, SLOT(onNotify(int,QByteArray,int)) );
    d_lua->addStdLibs();
    d_lua->addLibrary(Engine2::PACKAGE);
    d_lua->addLibrary(Engine2::IO);
    d_lua->addLibrary(Engine2::BIT);
    d_lua->addLibrary(Engine2::JIT);
    d_lua->addLibrary(Engine2::FFI);
    d_lua->addLibrary(Engine2::OS);

    lua_pushcfunction( d_lua->getCtx(), hashOf );
    lua_setglobal( d_lua->getCtx(), "hashOf" );
    lua_pushcfunction( d_lua->getCtx(), Engine2::TRAP );
    lua_setglobal( d_lua->getCtx(), "TRAP" );
    lua_pushcfunction( d_lua->getCtx(), Engine2::TRACE );
    lua_setglobal( d_lua->getCtx(), "TRACE" );
    lua_pushcfunction( d_lua->getCtx(), Engine2::ABORT );
    lua_setglobal( d_lua->getCtx(), "ABORT" );


#ifdef ST_SET_JIT_PARAMS_BY_LUA
    // works in principle, but the JIT runs about 5% slower than when directly set via lj_jit.h
    QByteArray cmd = "jit.opt.start(";
    cmd += "\"maxtrace=100000\",";
    cmd += "\"maxrecord=40000\",";
    cmd += "\"maxside=1000\",";
    cmd += "\"sizemcode=64\",";
    cmd += "\"maxmcode=4096\")";
    if( !d_lua->executeCmd(cmd) )
        qCritical() << "error initializing JIT:" << d_lua->getLastError().constData();
#endif

    d_om = new LjObjectManager( d_lua, this );
}

bool LjVirtualMachine::load(const QString& file, const QString& paths)
{
    QStringList classPaths = paths.isEmpty() ? QStringList() : paths.split(':');
    classPaths.prepend(":/Smalltalk");
    loadLuaLib( d_lua, "SomPrimitives");
    if( !d_om->load(file,classPaths) )
    {
        foreach( const QString& str, d_om->getErrors() )
            qCritical() << str.toUtf8().constData();
        return false;
    }else
        return true;
}

void LjVirtualMachine::run(bool useJit, bool useProfiler)
{
#ifdef ST_USE_MONITOR
    if( useProfiler )
        luaJIT_profile_start( d_lua->getCtx(), "i1000", profile_callback, 0);
#endif
    if( !useJit )
        luaJIT_setmode( d_lua->getCtx(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF );

    if( !d_om->run() )
    {
        foreach( const QString& str, d_om->getErrors() )
            qCritical() << str.toUtf8().constData();
    }
}

QStringList LjVirtualMachine::getLuaFiles() const
{
    QStringList res;
    for( int i = 0; i < d_om->getGenerated().size(); i++ )
        res.append( d_om->getGenerated()[i].second );
    res.prepend( ":/SomPrimitives.lua" );
    return res;
}

QByteArrayList LjVirtualMachine::getClassNames() const
{
    return d_om->getClassNames();
}

void LjVirtualMachine::setGenLua(bool on)
{
    d_om->setGenLua(on);
}

void LjVirtualMachine::onNotify(int messageType, QByteArray val1, int val2)
{
    switch(messageType)
    {
    case Lua::Engine2::Print:
        qDebug() << val1.trimmed().constData();
        break;
    case Lua::Engine2::Cout:
        std::cout << val1.constData();
        std::cout << std::flush;
        break;
    case Lua::Engine2::Cerr:
        std::cerr << val1.constData();
        std::cerr << std::flush;
        break;
    case Lua::Engine2::Error:
        {
            Engine2::ErrorMsg msg = Engine2::decodeRuntimeMessage(val1);
            qCritical() << msg.d_source.constData() <<
                           JitComposer::unpackRow2(msg.d_line) <<
                        JitComposer::unpackCol2(msg.d_line) << msg.d_message.constData();
        }
        break;
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Som");
    a.setApplicationName("SOM on LuaJIT");
    a.setApplicationVersion("0.7.2");
    a.setStyle("Fusion");

    QString somFile;
    QString somPaths;
    QString proFile;
    bool ide = false;
    bool lua = false;
    bool useProfiler = false;
    bool interactive = false;
    bool debugger = false;
    bool useJit = true;
    QStringList extraArgs;
    const QStringList args = QCoreApplication::arguments();
    for( int i = 1; i < args.size(); i++ ) // arg 0 enthaelt Anwendungspfad
    {
        if( args[i] == "-h" || args.size() == 1 )
        {
            QTextStream out(stdout);
            out << a.applicationName() << " version: " << a.applicationVersion() <<
                         " author: me@rochus-keller.ch  license: GPL" << endl;
            out << "usage: [options] som_file" << endl;
            out << "options:" << endl;
            out << "  -cp       paths to som files, separated by ':'" << endl;
            out << "            Note that path of som_file is automatically added and" << endl;
            out << "            the Smalltalk files are integrated in the executable" << endl;
            out << "  -ide      start Lua IDE (overrides -lua)" << endl;
            out << "  -lua      generate Lua source code instead of bytecode" << endl;
            out << "  -i        run script and start an interactive session" << endl;
            out << "  -dbg      run script in the debugger (overrides -i)" << endl;
            out << "  -pro file open given project in LuaIDE" << endl;
            out << "  -nojit    switch off JIT" << endl;
            out << "  -stats    use LuaJIT profiler (if present)" << endl;
            out << "  -h        display this information" << endl;
            return 0;
        }else if( args[i] == "-ide" )
        {
            ide = true;
            lua = true;
        }else if( args[i] == "-lua" )
                    lua = true;
        else if( args[i] == "-i" )
                    interactive = true;
        else if( args[i] == "-dbg" )
                    debugger = true;
        else if( args[i] == "-nojit" )
                    useJit = false;
        else if( args[i] == "-stats" )
                    useProfiler = true;
        else if( args[i] == "-pro" )
        {
            ide = true;
            lua = true;
            if( i+1 >= args.size() )
            {
                qCritical() << "error: invalid -pro option";
                return -1;
            }else
            {
                proFile = args[i+1];
                i++;
            }
        }else if( args[i] == "-cp" )
        {
            if( i+1 >= args.size() )
            {
                qCritical() << "error: invalid -cp option";
                return -1;
            }else
            {
                somPaths = args[i+1];
                i++;
            }
        }else if( !args[ i ].startsWith( '-' ) )
        {
            if( somFile.isEmpty() )
                somFile = args[ i ];
            else
                extraArgs += args[ i ];
        }else
        {
            qCritical() << "error: invalid command line option " << args[i];
            return -1;
        }
    }

    LjVirtualMachine vm;

    if( somFile.isEmpty() )
    {
        somFile = QFileDialog::getOpenFileName(0,LjVirtualMachine::tr("Open SOM File"), QString(), "Source *.som" );
        if( somFile.isEmpty() )
            return 0;
    }
    vm.setGenLua(lua);
    if( !vm.load(somFile, somPaths) )
        return -1;

    vm.getOm()->setArgs(extraArgs);

    if( ide )
    {
        LuaIde win( vm.getLua() );
        //win.setSpecialInterpreter(false);
        win.getProject()->addBuiltIn("toaddress");
        win.getProject()->addBuiltIn("_primitives");
        win.getProject()->addBuiltIn("hashOf");
        win.getProject()->addBuiltIn("loadClassByName");
        foreach( const QByteArray& n, vm.getClassNames() )
            win.getProject()->addBuiltIn(n);
        if( !proFile.isEmpty() )
            win.loadFile(proFile);
        else
        {
            win.getProject()->setWorkingDir(QFileInfo(somFile).absolutePath()+"/Lua");
            win.getProject()->initializeFromFiles( vm.getLuaFiles(), "runSom", false );
            win.compile();
        }
        // vm.run(useJit,useProfiler);
        return a.exec();
    }else
    {
        if( debugger )
        {
            BcDebugger win( vm.getLua() );
            vm.getOm()->generateSomPrimitives();
            LjObjectManager::GeneratedFiles gf = vm.getOm()->getGenerated();
            win.initializeFromFiles( gf, QFileInfo(somFile).absolutePath()+"/Lua", "runSom()" );
            //vm.run(useJit,useProfiler);
            return a.exec();
        }else if( interactive )
        {
            Lua::Terminal2 t(0,vm.getLua());
            t.show();
            vm.run(useJit,useProfiler);
            return a.exec();
        }else
        {
            vm.run(useJit,useProfiler);
            return 0;
        }
    }
}
