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

#include "SomLjObjectManager.h"
#include "SomParser.h"
#include "SomLexer.h"
#include "SomLuaTranspiler.h"
#include <QDir>
#include <QFileInfo>
#include <QtDebug>
#include <LjTools/Engine2.h>
#include <QDateTime>
#include <lua.hpp>
using namespace Som;
using namespace Som::Ast;

/* Object Model Mapping

    Each SOM class is two lua tables, one for the class and one for the metaclass
    SOM doesn't support idents starting with "_", so we use those for internal names
    The metaclass Lua table is accessible as global by classname; the class table is accessible by "_class" field
    The classname is also in the field "_name" of the class table.
    Note that we differ "metaclass" from "Metaclass" here; the former is a role, the latter a SOM class.
    A metaclass is *not* the metatable of a class! The class has field "_meta" which points to its metaclass.
    The superclass is accessible by the "_super" field; as defined in the Smalltalk Bluebook the class hierarchy is
        mirrored by the metaclass hierarchy; "_super" in a class table points to the super class whereas
        "_super" in a metaclass table points to the corresponding super metaclass
    Metaclass is the metatable of each metaclass, including Metaclass Meta.
    Class is the superclass or Object Meta and Metaclass; Object is the superclass of Class.
    Methods of super classes are replicated to subclasses.
    Each instance refers to its class by the Lua metatable concept (set/getmetatable); equally a class refers to
        its metaclass by the Lua metatable concept
    Fields (on instance or class level) are referenced by their declaration order number (0..)
        whereas subclasses continue after the last field number of their super class
    The field names are available in the "_fields" array in both the class and metaclass.
    Methods are referenced by their name (keywords with ':' and no whitespace); instance methods by plain name
    Keyword selectors are converted to Lua compatible names by replacing the ':' by '_'
    Binary selectors are converted to Lua compatible names by replacing the chars by their single letter alpha code
        prefixed with "_0";
        '~':t '&':a '|':b '*':s '/':h '\':B '+':p '=':q '>':g '<':l ',':c '@':A '%':r '-':m
    true, false and nil are represented by Lua values with metatable pointing to the appropriate SOM class
    SOM Integer is represented by Lua number with metaclass pointing to Integer class

    Internal names: _primitives, _name, _super, _class, _meta, _fields
  */
/*
    TODO:
    support unknownGlobal: name methods
*/
struct LjObjectManager::ResolveIdents : public Visitor
{
    QList<Scope*> stack;
    LjObjectManager* mdl;
    Method* meth;
    QList<Ident*> unresolved;
    QSet<const char*> d_toInline;
    QList<Block*> blocks;
    bool inAssig;


    ResolveIdents()
    {
        d_toInline.insert( Lexer::getSymbol("ifTrue:"));
        d_toInline.insert( Lexer::getSymbol("ifFalse:"));
        d_toInline.insert( Lexer::getSymbol("or:"));
        d_toInline.insert( Lexer::getSymbol("and:"));
        d_toInline.insert( Lexer::getSymbol("ifTrue:ifFalse:"));
        d_toInline.insert( Lexer::getSymbol("whileFalse:"));
        d_toInline.insert( Lexer::getSymbol("whileTrue:"));
    }

    static QPair<int,int> countVars(Class* cls)
    {
        QPair<int,int> res;
        if( cls == 0 )
            return res;
        if( cls->d_owner )
            res = countVars(static_cast<Class*>(cls->d_owner));
        res.first += cls->d_instVars.size();
        res.second += cls->d_classVars.size();
        return res;
    }

    void visit( Class* m )
    {
        inAssig = false;
        meth = 0;
        stack.push_back(m);
        QPair<int,int> count = countVars(static_cast<Class*>(m->d_owner));
        for( int i = 0; i < m->d_instVars.size(); i++ )
        {
            m->d_instVars[i]->accept(this);
            m->d_instVars[i]->d_slot = count.first + i;
        }
        for( int i = 0; i < m->d_classVars.size(); i++ )
        {
            m->d_classVars[i]->accept(this);
            m->d_classVars[i]->d_slot = count.second + i;
        }
        for( int i = 0; i < m->d_methods.size(); i++ )
            m->d_methods[i]->accept(this);
        stack.pop_back();
    }
    void visit( Variable* v )
    {
       if( meth )
       {
           Ref<Ident> id = new Ident(v->d_name, v->d_loc, meth);
           id->d_resolved = v;
           id->d_use = Ident::Declaration;
           meth->d_helper << id.data();
       }
    }
    void visit( Method* m)
    {
        meth = m;
        stack.push_back(m);
        for( int i = 0; i < m->d_vars.size(); i++ )
            m->d_vars[i]->accept(this);
        for( int i = 0; i < m->d_body.size(); i++ )
            m->d_body[i]->accept(this);
        stack.pop_back();
        meth = 0;
    }
    void visit( Block* b)
    {
        if( blocks.isEmpty() )
        {
            if( b->d_inline )
                b->d_inlinedLevel = 0;
            else
                b->d_inlinedLevel = 1;
        }else
        {
            if( b->d_inline )
                b->d_inlinedLevel = blocks.back()->d_inlinedLevel;
            else
                b->d_inlinedLevel = blocks.back()->d_inlinedLevel + 1;
        }
        stack.push_back(b->d_func.data());
        blocks.push_back(b);
        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
            b->d_func->d_vars[i]->accept(this);
        for( int i = 0; i < b->d_func->d_body.size(); i++ )
            b->d_func->d_body[i]->accept(this);
        blocks.pop_back();
        stack.pop_back();
    }
    void visit( Cascade* c )
    {
        for( int i = 0; i < c->d_calls.size(); i++ )
            c->d_calls[i]->accept(this);
    }
    void visit( Assig* a )
    {
        inAssig = true;
        a->d_lhs->accept(this);
        inAssig = false;
        a->d_rhs->accept(this);
        if( a->d_lhs->keyword() )
            mdl->error( a->d_lhs->d_loc, "cannot assign to keyword" );
        switch( a->d_rhs->keyword() )
        {
        case Expression::_super:
            mdl->error( a->d_lhs->d_loc, "cannot assign 'super' to variable" );
            // NOTE: SOM would allow this, but it is only used in SuperTest.som, even if without necessity
            break;
        case Expression::_primitive:
            mdl->error( a->d_lhs->d_loc, "cannot assign 'primitive' to variable" );
            break;
        default:
            break;
        }
    }
    void visit( Ident* i )
    {
        Q_ASSERT( !stack.isEmpty() );

        if( inAssig )
            i->d_use = Ident::AssigTarget;
        else
            i->d_use = Ident::Rhs;

        i->d_keyword = mdl->d_keywords.value(i->d_ident.constData());
        if( i->d_keyword )
        {
            // Ident::MsgReceiver will be set elsewhere
            return;
        }

        if( inAssig ) // lhs of Assig
        {
            // can be variable (local, arg, global)
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() && mdl->d_system->d_name.constData() == i->d_ident.constData() )
                res << mdl->d_system.data();
            Named* hit = 0;
            for( int j = 0; j < res.size(); j++ )
            {
                // prefer instance level if both (class and instance) are present
                if( !res[j]->classLevel() || hit == 0 )
                    hit = res[j];
            }
            if( hit )
            {
                i->d_resolved = hit;
                return;
            }
        }else // not inAssig or rhs
        {
            // can be a variable, method, class
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() )
                res = stack.back()->findMeths(i->d_ident);
            if( res.isEmpty() )
            {
                Named* n = mdl->d_classes.value(i->d_ident.constData()).data();
                if( n )
                    res << n;
            }
            if( res.isEmpty() && mdl->d_system->d_name.constData() == i->d_ident.constData() )
                res << mdl->d_system.data();

            if( !res.isEmpty() )
            {
                if( res.size() > 1 ) // TODO
                   qDebug() << "more than one result" << i->d_ident << meth->getClass()->d_name << meth->d_name;
                i->d_resolved = res.first();
                return;
            }
        }
        unresolved << i; // This must therefore be a not yet loaded class
    }
    void visit( MsgSend* s)
    {
        if( s->d_patternType == KeywordPattern )
        {
            const QByteArray name = Lexer::getSymbol( s->prettyName(false) );
            if( d_toInline.contains( name.constData() ) )
            {
                for( int i = 0; i < s->d_args.size(); i++ )
                {
                    if( s->d_args[i]->getTag() == Thing::T_Block )
                        static_cast<Block*>( s->d_args[i].data() )->d_inline = true;
                }
            }
        }
        for( int i = 0; i < s->d_args.size(); i++ )
            s->d_args[i]->accept(this);
        s->d_receiver->accept(this);
        if( s->d_receiver->getTag() == Ast::Thing::T_Ident )
        {
            Ast::Ident* id = static_cast<Ast::Ident*>(s->d_receiver.data());
            id->d_use = Ident::MsgReceiver;
        }
    }
    void visit( Return* r )
    {
        if( !blocks.isEmpty() && blocks.back()->d_inlinedLevel > 0 )
        {
            meth->d_hasNonLocalReturnIfInlined = true;
            r->d_nonLocalIfInlined = true;
        }
        r->d_what->accept(this);
    }
    void visit( ArrayLiteral* a)
    {
        for( int i = 0; i < a->d_elements.size(); i++ )
            a->d_elements[i]->accept(this);
    }
};

LjObjectManager::LjObjectManager(Lua::Engine2* lua, QObject *parent) : QObject(parent),d_lua(lua)
{
    Q_ASSERT( d_lua );
    _nil = Lexer::getSymbol("nil"); // instance of Nil
    _Class = Lexer::getSymbol("Class");
    _Object = Lexer::getSymbol("Object");
    d_keywords.insert(_nil.constData(), Ast::Ident::_nil);
    d_keywords.insert( Lexer::getSymbol("true").constData(), Ast::Ident::_true); // instance of True
    d_keywords.insert( Lexer::getSymbol("false").constData(), Ast::Ident::_false); // instance of False
    d_keywords.insert( Lexer::getSymbol("self").constData(), Ast::Ident::_self);
    d_keywords.insert( Lexer::getSymbol("super").constData(), Ast::Ident::_super );
    d_keywords.insert( Lexer::getSymbol("primitive").constData(), Ast::Ident::_primitive );
    d_system = new Variable();
    d_system->d_name = Lexer::getSymbol("system"); // instance of System
    d_system->d_kind = Variable::Global;
}

bool LjObjectManager::load(const QString& file, const QStringList& paths)
{
    d_errors.clear();
    d_mainClass.reset();
    d_classes.clear();
    d_loadingOrder.clear();
    d_instantiated = 0;
    d_generated.clear();

    d_classPaths = paths;
    d_mainPath = file;
    QFileInfo info(file);
    if( file.isEmpty() || !info.isFile() || !info.isReadable() )
        return error( tr("invalid main SOM file '%1'").arg(file) );
    QDir home = info.absoluteDir();
    for( int i = 0; i < d_classPaths.size(); i++ )
    {
        if( QFileInfo(d_classPaths[i]).isRelative() )
            d_classPaths[i] = home.absoluteFilePath(d_classPaths[i]);
    }
    d_classPaths.append(home.absolutePath());

    getOrLoadClass("Metaclass"); // instantiates Object and some others
    getOrLoadClass("Class");
    getOrLoadClass("System");
    getOrLoadClass("Boolean");
    getOrLoadClass("True");
    getOrLoadClass("False");
    getOrLoadClass("Nil");
    getOrLoadClass("Block");
    getOrLoadClass("String");
    getOrLoadClass("Symbol");
    getOrLoadClass("Integer");
    getOrLoadClass("Double");
    getOrLoadClass("Array");
    getOrLoadClass("Method");
    getOrLoadClass("Primitive");

    // We have to load an parse all classes provided in the path; otherwise we would have to detect
    // a missing class at runtime and then compile it
    if( !parseMain(file) )
        return false;

#if 1
    foreach( Ast::Class* c, d_loadingOrder )
        qDebug() << "Loaded" << c->d_name << "sub of" << c->d_superName << ( c->d_owner ? "connected" : "" );
    if( d_loadingOrder.size() != d_classes.size() )
    {
        QSet<const char*> all = d_classes.keys().toSet();
        foreach( Ast::Class* c, d_loadingOrder )
            all.remove( c->d_name.constData() );
        qCritical() << "missing in loadingOrder:" << all;
    }
#endif

    instantiateClasses();

    QByteArray run = "run";
    if( d_mainClass->findMethod( Lexer::getSymbol("run") ) == 0 )
        run += "_";
    if( !d_lua->executeCmd( "function runSom() " + d_mainClass->d_name + "._class:" +
                            run + "(_primitives._inst(Array):at_put_(1,_primitives._newString(\"" +
                            d_mainClass->d_loc.d_source.toUtf8()+ "\"))) end") )
        d_errors += d_lua->getLastError();

    return d_errors.isEmpty();
}

bool LjObjectManager::run()
{
    if( !d_lua->executeCmd( "runSom()") )
        d_errors += d_lua->getLastError();
    return d_errors.isEmpty();
}

QByteArrayList LjObjectManager::getClassNames() const
{
    QByteArrayList res;
    for(int i = 0; i < d_loadingOrder.size(); i++ )
        res += d_loadingOrder[i]->d_name;
    return res;
}

bool LjObjectManager::parseMain(const QString& mainFile)
{
    d_mainClass = parseFile(mainFile);
    if( d_mainClass.isNull() )
        return false;

    d_classes.insert( d_mainClass->d_name.constData(), d_mainClass );

    // First we identify and load all super classes; these can statically be identified an initially loaded
    // In general it is not possible to identify all receiver classes up-front because the names of
    // receivers can be the result of runtime computations; a dynamic class loader is thus unavoidable
    if( !loadAndSetSuper( d_mainClass.data() ) )
        return false;
    return handleUnresolved();
}

Ast::Ref<Ast::Class> LjObjectManager::parseFile(const QString& file)
{
    QFile in(file);
    if( !in.open(QIODevice::ReadOnly) )
    {
        error( tr("cannot open file for reading '%1'").arg(file) );
        return 0;
    }
    const quint32 errCountBefore = d_errors.size();
    Lexer lex;
    lex.setDevice(&in,file);
    lex.setEatComments(true);
    Parser p(&lex);
    if( !p.readFile() )
    {
        foreach( const Parser::Error& e, p.getErrs() )
            error(e.d_loc,e.d_msg);
    }

    if( d_errors.size() != errCountBefore )
        return 0;

    return p.getClass();
}

bool LjObjectManager::error(const Ast::Loc& loc, const QString& msg)
{
    return error( QString("%1:%2:%3: %4").arg( QFileInfo(loc.d_source).baseName() )
            .arg(loc.d_line).arg(loc.d_col).arg(msg) );
}

bool LjObjectManager::error(const QString& msg)
{
    d_errors += msg;
    qCritical() << msg.toUtf8().constData();
    return false;
}

QString LjObjectManager::findClassFile(const char* className)
{
    for( int i = d_classPaths.size() - 1; i >= 0; i-- )
    {
        const QString fileName = QString("%1.som").arg(className);
        const QString path = QDir( d_classPaths[i] ).absoluteFilePath(fileName);
        if( QFileInfo(path).exists() )
            return path;
    }
    return QString();
}

bool LjObjectManager::loadAndSetSuper(Ast::Class* cls)
{
    Q_ASSERT( cls );
    Ast::Ref<Ast::Class> sub = cls;
    QByteArray superName = cls->d_superName;
    Q_ASSERT( !superName.isEmpty() ); // was set by parser to Object if not explicitly declared

    QList<Ast::Class*> order;
    order << cls;
    while( !superName.isEmpty() && superName.constData() != _nil.constData() )
    {
        const char* className = superName.constData(); // this is a Lexer symbol, so we can savely operate on the pointer
        superName = QByteArray();
        bool loaded = false;
        Ast::Ref<Ast::Class> super = getOrLoadClassImp(className, &loaded );
        if( loaded )
        {
            order << super.data();
            superName = super->d_superName;
            Q_ASSERT( !superName.isEmpty() );
        }else
            superName = QByteArray();
        if( !super.isNull() )
        {
            super->d_subs.append(sub);
            sub->d_owner = super.data();
            sub = super;
        }
    }
    for( int i = order.size() - 1; i >= 0; i-- )
    {
        d_loadingOrder.append( order[i] );
        resolveIdents(order[i]);
    }

    return d_errors.isEmpty();
}

bool LjObjectManager::resolveIdents(Class* cls)
{
    ResolveIdents visitor;
    visitor.mdl = this;
    cls->accept( &visitor );
    d_unresolved += visitor.unresolved;
    return d_errors.isEmpty();
}

Ast::Ref<Class> LjObjectManager::getOrLoadClass(const QByteArray& name)
{
    const QByteArray symbol = Lexer::getSymbol(name);
    bool loaded;
    Ast::Ref<Ast::Class> cls = getOrLoadClassImp( symbol.constData(), &loaded);
    if( loaded )
    {
        if( !loadAndSetSuper( cls.data() ) )
            return false;
        handleUnresolved();
    }
    return cls;
}

Ast::Ref<Ast::Class> LjObjectManager::getOrLoadClassImp(const char* className, bool* loaded)
{
    if( loaded )
        *loaded = false;
    Ast::Ref<Ast::Class> cls = d_classes.value(className);
    if( cls.isNull() )
    {
        // class is not yet loaded; parse it from file
        const QString path = findClassFile( className );
        if( path.isEmpty() )
            error( tr("cannot find class file of '%1'").arg(className) );
        else
            cls = parseFile(path);
        if( !cls.isNull() )
        {
            if( loaded )
                *loaded = true;
            d_classes.insert( cls->d_name.constData(), cls );
        }
    }
    return cls;
}

bool LjObjectManager::handleUnresolved()
{
    int i = 0;
    while( i < d_unresolved.size() )
    {
        bool loaded;
        Ast::Ref<Ast::Class> cls = getOrLoadClassImp( d_unresolved[i]->d_ident.constData(), &loaded );
        if( !cls.isNull() )
        {
            if( loaded )
                loadAndSetSuper( cls.data() );
            d_unresolved[i]->d_resolved = cls.data();
        }else
        {
            cls = getOrLoadClassImp( d_unresolved[i]->d_ident.constData() );
            error( d_unresolved[i]->d_loc, tr("cannot resolve identifier '%1").arg(
                       d_unresolved[i]->d_ident.constData()) );
        }
        i++;
    }
    d_unresolved.clear();
    return d_errors.isEmpty();
}

bool LjObjectManager::instantiateClasses()
{
    const int top = lua_gettop(d_lua->getCtx());
    const bool firstRun = d_instantiated == 0;

    const int oldInstantiated = d_instantiated;
    while( d_instantiated < d_loadingOrder.size() )
        instantiateClass( d_loadingOrder[ d_instantiated++ ] );

    if( firstRun )
    {
        lua_State* L = d_lua->getCtx();

        lua_getglobal( L, _Object.constData() );
        Q_ASSERT( !lua_isnil(L,-1) );
        lua_getglobal( L, _Class.constData() );
        Q_ASSERT( !lua_isnil(L,-1) );
        lua_getfield( L, -1, "_class" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_setfield( L, -3, "_super" ); // Object -> nil, Object Meta -> Class
        lua_pop(L,2);

        lua_pushnil(L);
        lua_getglobal( L, "Nil" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_getfield( L, -1, "_class" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_setmetatable(L,-3);
        lua_pop(L,2);

        lua_pushnumber(L,0);
        lua_getglobal( L, "Integer" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_getfield( L, -1, "_class" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_setmetatable(L,-3);
        lua_pop(L,2);

        lua_pushboolean(L,true);
        lua_getglobal( L, "Boolean" ); // NOTE: originally True and False, but we use modified Boolean instead
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_getfield( L, -1, "_class" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_setmetatable(L,-3);
        lua_pop(L,2);

        lua_createtable(L,0,0);
        lua_pushvalue(L,-1);
        lua_setglobal(L,"system");
        lua_getglobal(L,"System");
        lua_getfield( L, -1, "_class" );
        Q_ASSERT( !lua_isnil( L, -1 ) );
        lua_setmetatable(L,-3);
        lua_pop(L,2);
    }

    for( int i = oldInstantiated; i < d_loadingOrder.size(); i++ )
        compileMethods( d_loadingOrder[i] );

    Q_ASSERT( top == lua_gettop(d_lua->getCtx()) );
    return true;
}

static QSet<QByteArray> methodNamesOf(Ast::Class* cls, bool classLevel )
{
    QSet<QByteArray> res;
    if( cls->d_owner )
    {
        Q_ASSERT( cls->d_owner->getTag() == Ast::Thing::T_Class );
        Ast::Class* super = static_cast<Ast::Class*>(cls->d_owner);
        res = methodNamesOf( super, classLevel );
    }
    for( int i = 0; i < cls->d_methods.size(); i++ )
    {
        Ast::Method* m = cls->d_methods[i].data();
        if( ( classLevel && m->d_classLevel ) || ( !classLevel && !m->d_classLevel ) )
            res.insert( m->d_name );
    }
    return res;
}

static QByteArrayList fieldsOf(Ast::Class* cls, bool classLevel )
{
    QByteArrayList res;
    if( cls->d_owner )
    {
        Q_ASSERT( cls->d_owner->getTag() == Ast::Thing::T_Class );
        Ast::Class* super = static_cast<Ast::Class*>(cls->d_owner);
        res = fieldsOf( super, classLevel );
    }
    if( classLevel )
        for( int i = 0; i < cls->d_classVars.size(); i++ )
            res << cls->d_classVars[i]->d_name;
    else
        for( int i = 0; i < cls->d_instVars.size(); i++ )
            res << cls->d_instVars[i]->d_name;
    return res;
}

bool LjObjectManager::instantiateClass(Ast::Class* cls)
{
    lua_State* L = d_lua->getCtx();

    lua_getglobal( L, cls->d_name.constData() );
    if( !lua_isnil( L, -1 ) )
    {
        error( tr("class '%1' already instantiated").arg(cls->d_name.constData()) );
        lua_pop(L,1);
        return false;
    }
    lua_pop(L,1);

    lua_createtable( L, 0, 0 );
    const int classT = lua_gettop(L);
    lua_createtable( L, 0, 0 );
    const int metaT = lua_gettop(L);

    // metaT cannot be metatable of classT
    lua_pushvalue( L, metaT );
    lua_setfield( L, classT, "_meta");

    lua_pushvalue( L, metaT );
    lua_setglobal( L, cls->d_name.constData() );

    lua_pushstring( L, cls->d_name.constData() );
    lua_setfield( L, classT, "_name" );

    lua_pushvalue( L, classT );
    lua_setfield( L, metaT, "_class" );

    lua_pushvalue( L, classT );
    lua_setfield( L, classT, "__index" ); // instances of class can access the methods in classT

    if( cls->d_superName.constData() != _nil.constData() )
    {
        // this is anything below Object

        lua_getglobal( L, cls->d_superName.constData() );
        const int superMetaT = lua_gettop(L);
        Q_ASSERT( !lua_isnil(L,superMetaT) );
        lua_getfield( L, superMetaT, "_class" );
        const int superT = lua_gettop(L);

        lua_pushvalue( L, superMetaT );
        lua_setfield( L, metaT, "_super" );
        lua_pushvalue( L, superT );
        lua_setfield( L, classT, "_super" );

        lua_pop( L, 1 ); // superT
        lua_pop( L, 1 ); // superMetaT

        QByteArrayList fields = fieldsOf( cls, false );
        lua_createtable( L, 0, 0 );
        int ft = lua_gettop(L);
        for( int i = 0; i < fields.size(); i++ )
        {
            lua_pushstring( L, fields[i].constData() );
            lua_rawseti( L, ft, i+1 );
        }
        lua_setfield( L, classT, "_fields" );

        fields = fieldsOf( cls, true );
        lua_createtable( L, 0, 0 );
        ft = lua_gettop(L);
        for( int i = 0; i < fields.size(); i++ )
        {
            lua_pushstring( L, fields[i].constData() );
            lua_rawseti( L, ft, i+1 );
        }
        lua_setfield( L, metaT, "_fields" );
    }

    lua_pop(L,1); // metaT
    lua_pop(L,1); // classT

#if 1
    QDir homeDir( QFileInfo( d_mainPath ).absoluteDir() );
    homeDir.mkdir("Ast");
    homeDir.cd("Ast");
    QFile out( homeDir.absoluteFilePath( cls->d_name + ".txt" ) );
    if( !out.open(QIODevice::WriteOnly) )
        qDebug() << "cannot open for writing:" << out.fileName();
    QTextStream ts( &out );
    cls->dump(ts);
#endif

    return true;
}

bool LjObjectManager::compileMethods(Ast::Class* cls)
{
    lua_State* L = d_lua->getCtx();

    lua_getglobal( L, cls->d_name.constData() );
    const int metaT = lua_gettop(L);
    Q_ASSERT( !lua_isnil(L,metaT) );
    lua_getfield( L, metaT, "_class" );
    const int classT = lua_gettop(L);
    Q_ASSERT( !lua_isnil(L,classT) );

    lua_getglobal( L, "Metaclass" );
    Q_ASSERT( !lua_isnil(L,-1) );
    lua_getfield( L, -1, "_class" );
    Q_ASSERT( !lua_isnil( L, -1 ) );
    lua_setmetatable( L, metaT ); // Metaclass is metatable of metaT, even if classT=Metaclass
    lua_pop(L,1);

    if( cls->d_superName.constData() != _nil.constData() )
    {
        // this is anything below Object

        lua_getglobal( L, cls->d_superName.constData() );
        const int superMetaT = lua_gettop(L);
        Q_ASSERT( !lua_isnil(L,superMetaT) );
        lua_getfield( L, superMetaT, "_class" );
        const int superT = lua_gettop(L);
        Q_ASSERT( !lua_isnil(L,superT) );

        // copy methods of superclass to this class
        // class
        Q_ASSERT( cls->d_owner && cls->d_owner->getTag() == Ast::Thing::T_Class );
        QSet<QByteArray> superMethodNames = methodNamesOf(static_cast<Ast::Class*>(cls->d_owner), false);
        QSet<QByteArray>::const_iterator i;
        for( i = superMethodNames.begin(); i != superMethodNames.end(); ++i )
        {
            // all methods of all superclasses are copied to the class
            const QByteArray name = LuaTranspiler::map(*i);
            lua_getfield( L, superT, name.constData() );
            lua_setfield( L, classT, name.constData() );
        }
        lua_pop( L, 1 ); // superT

        // metaclass
        superMethodNames = methodNamesOf(static_cast<Ast::Class*>(cls->d_owner), true);
        for( i = superMethodNames.begin(); i != superMethodNames.end(); ++i )
        {
            const QByteArray name = LuaTranspiler::map(*i);
            lua_getfield( L, superMetaT, name.constData() );
            lua_setfield( L, metaT, name.constData() );
        }
        lua_pop( L, 1 ); // superMetaT
    }else
    {
        // Object Meta -> Class is set in instantiateClasses after Class is instantiated
    }

    lua_getglobal( L, "_primitives" );
    if( !lua_isnil(L,-1) )
    {
        lua_getfield( L, -1, cls->d_name.constData() );
        if( lua_isnil(L,-1) )
            lua_pop(L,1);
        else
            lua_remove(L,-2);
    }
    const int primitivesT = lua_gettop(L);

    QDir homeDir( QFileInfo(d_mainClass->d_loc.d_source).absoluteDir() );
    homeDir.mkdir("Lua");
    homeDir.cd("Lua");
    QFile out( homeDir.absoluteFilePath( cls->d_name + ".lua" ) );
    d_generated << out.fileName();
    out.open(QIODevice::WriteOnly);
    QTextStream ts( &out );
    ts << tr("-- generated by SomLjVirtualMachine on ") << QDateTime::currentDateTime().toString() << endl << endl;

    ts << "local metaclass = " << cls->d_name << endl;
    ts << "local class = " << cls->d_name << "._class" << endl;
    ts << "local function _block(f) local t = { _f = f }; setmetatable(t,Block._class); return t end" << endl;
    // ts << "local function _nil(p) TRAP( p == nil ); return p end" << endl;
    ts << "local _str = _primitives._newString" << endl;
    ts << "local _sym = _primitives._newSymbol" << endl;
    ts << "local _dbl = _primitives._newDouble" << endl;
    ts << "local _lit = _primitives._newLit" << endl;
    ts << "local _cl = _primitives._checkLoad" << endl << endl;

    ts << "class.__unm = _primitives.__unm" << endl << endl; // each instance becomes convertible to a number

    for( int i = 0; i < cls->d_methods.size(); i++ )
    {
        Ast::Method* m = cls->d_methods[i].data();
        if( m->d_primitive )
        {
            const QByteArray toName = LuaTranspiler::map(m->d_name,m->d_patternType);
            QByteArray fromName = m->d_name;
            if( m->d_classLevel )
                fromName = "^" + fromName; // class level primitives are prefixed
            // copy the method from the Primitives implementation (even nil)
            lua_getfield(L,primitivesT, fromName.constData() );
            if( lua_isnil(L,-1) )
                qWarning() << "primitive" << cls->d_name << m->d_name << "not implemented";

            if( m->d_classLevel )
                lua_setfield(L,metaT,toName.constData() );
            else
                lua_setfield(L,classT,toName.constData() );
        }else
            // compile the method and attach it to the class
            LuaTranspiler::transpile(ts, m);
    }

    ts.flush();
    out.close();

    lua_pop(L,1); // primitivesT
    lua_pop(L,1); // metaT
    lua_pop(L,1); // classT

    if( !d_lua->executeFile( out.fileName().toUtf8() ) )
        error(d_lua->getLastError());

    return true;
}
