#ifndef SOMLJOBJECTMANAGER_H
#define SOMLJOBJECTMANAGER_H

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

#include <QObject>
#include <QStringList>
#include <Som/SomAst.h>

namespace Lua
{
    class Engine2;
}

namespace Som
{
    class LjObjectManager : public QObject
    {
    public:
        typedef QList<QPair<QString,QString> > GeneratedFiles; // source path -> generated path
        explicit LjObjectManager(Lua::Engine2*, QObject *parent = 0);
        bool load( const QString& som, const QStringList& paths = QStringList() );
        bool run();
        const QStringList& getErrors() const { return d_errors; }
        const GeneratedFiles& getGenerated() const { return d_generated; }
        QByteArrayList getClassNames() const;
        void setGenLua( bool on ) { d_genLua = on; }
        QString pathInDir( const QString& dir, const QString& name );
    protected:
        bool parseMain(const QString& mainFile);
        Ast::Ref<Ast::Class> parseFile( const QString& file );
        bool error( const Ast::Loc&, const QString& msg );
        bool error( const QString& msg );
        QString findClassFile(const char* className);
        bool loadAndSetSuper( Ast::Class* );
        bool resolveIdents( Ast::Class* );
        Ast::Ref<Ast::Class> getOrLoadClass( const QByteArray& );
        Ast::Ref<Ast::Class> getOrLoadClassImp( const char* className, bool* loaded = 0 );
        bool handleUnresolved();
        bool instantiateClasses();
        bool instantiateClass( Ast::Class* );
        bool compileMethods( Ast::Class* );
        void writeLua( QIODevice* out, Ast::Class* cls);
        void writeBc( QIODevice* out, Ast::Class* cls);
    private:
        class ResolveIdents;
        Lua::Engine2* d_lua;
        QStringList d_classPaths;
        QStringList d_errors;
        QString d_mainPath;
        Ast::Ref<Ast::Class> d_mainClass;
        typedef QHash<const char*,Ast::Ref<Ast::Class> > Classes;
        Classes d_classes;
        QList<Ast::Class*> d_loadingOrder;
        quint32 d_instantiated;
        QByteArray _nil, _Class, _Object;
        QHash<const char*,quint8> d_keywords;
        Ast::Ref<Ast::Variable> d_system;
        QList<Ast::Ident*> d_unresolved;
        GeneratedFiles d_generated;
        bool d_genLua;
    };
}

#endif // SOMLJOBJECTMANAGER_H
