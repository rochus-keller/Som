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

#include "LjSOM.h"
#include "SomLjObjectManager.h"
#include <LjTools/Engine2.h>
#include <LjTools/LuaJitComposer.h>
#include <LuaJIT/src/lua.hpp>
#include <QCoreApplication>
#include <QtDebug>
#include <lua.hpp>
#include <iostream>
#include <QFile>
using namespace Som;
using namespace Lua;

static void loadLuaLib( Lua::Engine2* lua, const QByteArray& name )
{
    QFile lib( QString(":/%1.lua").arg(name.constData()) );
    lib.open(QIODevice::ReadOnly);
    if( !lua->addSourceLib( lib.readAll(), name ) )
        qCritical() << "compiling" << name << ":" << lua->getLastError();
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

static QtMessageHandler s_oldHandler = 0;
static void messageHander(QtMsgType type, const QMessageLogContext& ctx, const QString& message)
{
    if( s_oldHandler )
        s_oldHandler(type, ctx, message );
}

LjSOM::LjSOM(QObject* parent) : QObject(parent)
{
    s_oldHandler = qInstallMessageHandler(messageHander);

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
        qCritical() << "error initializing JIT:" << d_lua->getLastError();
#endif

    d_om = new LjObjectManager( d_lua, this );
}

bool LjSOM::load(const QString& file, const QString& paths)
{
    QStringList classPaths = paths.isEmpty() ? QStringList() : paths.split(':');
    classPaths.prepend(":/Smalltalk");
    loadLuaLib( d_lua, "SomPrimitives");
    return d_om->load(file,classPaths);
}

#if 0
static int dump_trace(lua_State* L)
{
    // args: what, tr, func, pc, otr, oex
    qDebug() << "trace"
             << lua_tostring( L, 1 ) // what
             << lua_tostring( L, 2 ) // trace number
             << lua_topointer( L, 3 ) // the function
             << lua_tostring( L, 4 ); // the pc
    return 0;
}
#endif

static void printJitInfo(lua_State* L, QTextStream& out)
{
    int n;
    const char *s;
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, "jit");  /* Get jit.* module table. */
    lua_remove(L, -2);
    lua_getfield(L, -1, "status");
    lua_remove(L, -2);
    n = lua_gettop(L);
    lua_call(L, 0, LUA_MULTRET);
    out << (lua_toboolean(L, n) ? "JIT: ON" : "JIT: OFF");
    for (n++; (s = lua_tostring(L, n)); n++) {
        out << ' ' << s;
    }
    out << endl;
}

bool LjSOM::run(bool useJit, bool trace, const QStringList& extraArgs)
{
    if( !useJit )
        luaJIT_setmode( d_lua->getCtx(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF );
    else if( trace )
    {
#if 0
        lua_getglobal( d_lua->getCtx(), "jit" );
        lua_getfield( d_lua->getCtx(), -1, "attach" );
        lua_pushcfunction( d_lua->getCtx(), dump_trace );
        lua_pushliteral( d_lua->getCtx(), "trace" );
        const int err = lua_pcall( d_lua->getCtx(), 2, 0, 0 );
        Q_ASSERT( err == 0 );
        lua_pop(d_lua->getCtx(),1); // jit
#else
        loadLuaLib( d_lua, "LjTraceDump");
#endif
    }

    QTextStream out(stdout);
    d_om->setArgs(extraArgs);

    out << "based on " << LUAJIT_VERSION << " " << LUAJIT_COPYRIGHT << " " << LUAJIT_URL << endl;
    printJitInfo(d_lua->getCtx(),out);
    out << endl;

    return d_om->run();
}

QStringList LjSOM::getLuaFiles() const
{
    QStringList res;
    for( int i = 0; i < d_om->getGenerated().size(); i++ )
        res.append( d_om->getGenerated()[i].second );
    res.prepend( ":/SomPrimitives.lua" );
    return res;
}

QByteArrayList LjSOM::getClassNames() const
{
    return d_om->getClassNames();
}

void LjSOM::setGenLua(bool on)
{
    d_om->setGenLua(on);
}

void LjSOM::onNotify(int messageType, QByteArray val1, int val2)
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
            qCritical() << "ERR" << msg.d_source.constData() <<
                           JitComposer::unpackRow2(msg.d_line) <<
                        JitComposer::unpackCol2(msg.d_line) << msg.d_message.constData();
        }
        break;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("http://github.com/rochus-keller/Som");
    a.setApplicationName("LjSOM");
    a.setApplicationVersion("0.7.3");

    QTextStream out(stdout);

    out << a.applicationName() << " version: " << a.applicationVersion() <<
                 " license: GPL; see " <<  a.organizationDomain().toUtf8()  << endl;

    QString somFile;
    QString somPaths;
    bool lua = false;
    bool useJit = true;
    bool trace = false;
    QStringList extraArgs;
    const QStringList args = QCoreApplication::arguments();
    for( int i = 1; i < args.size(); i++ ) // arg 0 enthaelt Anwendungspfad
    {
        if( args[i] == "-h" || args.size() == 1 )
        {
            out << "usage: [options] som_file [extra_args]" << endl;
            out << "options:" << endl;
            out << "  -cp       paths to som files, separated by ':'" << endl;
            out << "            Note that path of som_file is automatically added and" << endl;
            out << "            the Smalltalk files are integrated in the executable" << endl;
            out << "  -lua      generate Lua source code instead of bytecode" << endl;
            out << "  -nojit    switch off JIT" << endl;
            out << "  -trace    output tracer results" << endl;
            out << "  -h        display this information" << endl;
            return 0;
        }else if( args[i] == "-lua" )
                    lua = true;
        else if( args[i] == "-nojit" )
                    useJit = false;
        else if( args[i] == "-trace" )
                    trace = true;
        else if( args[i] == "-cp" )
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

    LjSOM vm;

    if( somFile.isEmpty() )
    {
        qCritical() << "error: expecting a SOM file with a run method; use -h for help.";
        return -1;
    }
    vm.setGenLua(lua);
    if( !vm.load(somFile, somPaths) )
        return -1;

    if( vm.run(useJit,trace,extraArgs) )
        return 0;
    else
        return -1;
}
