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

#include "SomAstModel.h"
#include "SomParser.h"
#include "SomLexer.h"
#include <QFileInfo>
#include <QtDebug>
using namespace Som;
using namespace Som::Ast;


Model::Model(QObject *parent) : QObject(parent)
{

}

static bool sortSubs( const Ref<Class>& lhs, const Ref<Class>& rhs )
{
    return lhs->d_name < rhs->d_name;
}

static bool sortFields( const Ref<Variable>& lhs, const Ref<Variable>& rhs )
{
    return lhs->d_name < rhs->d_name;
}

struct Model::ResolveIdents : public AstVisitor
{
    QList<Scope*> stack;
    Model* mdl;
    Method* meth;
    QHash<const char*,int> unresolved;
    bool inAssig;

    void visit( Class* m )
    {
        inAssig = false;
        meth = 0;
        stack.push_back(m);
        for( int i = 0; i < m->d_vars.size(); i++ )
            m->d_vars[i]->accept(this);
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
           mdl->d_ix[v].append(id.data());
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
        stack.push_back(b->d_func.data());
        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
            b->d_func->d_vars[i]->accept(this);
        for( int i = 0; i < b->d_func->d_body.size(); i++ )
            b->d_func->d_body[i]->accept(this);
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
        for( int i = 0; i < a->d_lhs.size(); i++ )
            a->d_lhs[i]->accept(this);
        inAssig = false;
        a->d_rhs->accept(this);
    }
    void visit( Ident* i )
    {
        Q_ASSERT( !stack.isEmpty() );

        if( inAssig )
            i->d_use = Ident::AssigTarget;
        else
            i->d_use = Ident::Rhs;

        if( mdl->d_keywords.contains(i->d_ident.constData()) )
        {
            i->d_keyword = true;
            return;
        }

        if( inAssig )
        {
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() )
                res = mdl->d_globals.findVars(i->d_ident);
            Named* hit = 0;
            for( int j = 0; j < res.size(); j++ )
            {
                if( !res[j]->classLevel() || hit == 0 )
                    hit = res[j];
            }
            if( hit )
            {
                i->d_resolved = hit;
                mdl->d_ix[hit].append(i);
                return;
            }
        }else
        {
            QList<Named*> res = stack.back()->findVars(i->d_ident);
            if( res.isEmpty() )
                res = stack.back()->findMeths(i->d_ident);
            if( res.isEmpty() )
            {
                Named* n = mdl->d_classes2.value(i->d_ident.constData()).data();
                if( n )
                    res << n;
            }
            if( res.isEmpty() )
                res = mdl->d_globals.findVars(i->d_ident);

            if( !res.isEmpty() )
            {
                //if( res.size() > 1 ) // apparently never happens in St80.sources
                //    qDebug() << "more than one result" << i->d_ident << meth->getClass()->d_name << meth->d_name;
                i->d_resolved = res.first();
                mdl->d_ix[res.first()].append(i);
                return;
            }
        }
#if 0
        QList<Named*> res = stack.back()->find(i->d_ident);
        if( res.size() > 1 )
        {
            qDebug() << "***" << meth->getClass()->d_name << meth->d_name << i->d_ident << (i->d_pos - meth->d_pos);
            for( int j = 0; j < res.size(); j++ )
            {
                qDebug() << res[j]->classLevel() << res[j]->getTag() << inAssig;
            }
        }
#endif
        unresolved[i->d_ident.constData()]++;
        //else
        qWarning() << "cannot resolve ident" << i->d_ident << "in class/method" << meth->getClass()->d_name << meth->d_name;
    }
    void visit( MsgSend* s)
    {
        for( int i = 0; i < s->d_args.size(); i++ )
            s->d_args[i]->accept(this);
        s->d_receiver->accept(this);
        QByteArray name = Lexer::getSymbol( s->prettyName(false) );
        mdl->d_tx[name.constData()].append(s);
        if( s->d_receiver->getTag() == Ast::Thing::T_Ident )
        {
            Ast::Ident* id = static_cast<Ast::Ident*>(s->d_receiver.data());
            id->d_use = Ident::MsgReceiver;
        }
    }
    void visit( Return* r )
    {
        r->d_what->accept(this);
    }
    void visit( ArrayLiteral* a)
    {
        for( int i = 0; i < a->d_elements.size(); i++ )
            a->d_elements[i]->accept(this);
    }
};

bool Model::parse(const Files& files)
{
    clear();

    nil = Lexer::getSymbol("nil"); // instance of Nil
    d_keywords.insert(nil.constData());
    d_keywords.insert( Lexer::getSymbol("true").constData()); // instance of True
    d_keywords.insert( Lexer::getSymbol("false").constData()); // instance of False
    d_keywords.insert( Lexer::getSymbol("self").constData());
    d_keywords.insert( Lexer::getSymbol("super").constData() );
    d_keywords.insert( Lexer::getSymbol("primitive").constData() );

    Ref<Variable> v = new Variable();
    v->d_name = Lexer::getSymbol("system"); // instance of System
    v->d_kind = Variable::Global;
    v->d_owner = &d_globals;
    d_globals.d_vars.append(v);
    d_globals.d_varNames[v->d_name.constData()].append( v.data() );
    d_vx[v->d_name.constData()].append( v.data() );

    foreach( const File& f, files )
    {
        Lexer lex;
        lex.setDevice(f.d_dev,f.d_path);
        lex.setEatComments(true);
        Parser p(&lex);
        if( !p.readFile() )
        {
            foreach( const Parser::Error& e, p.getErrs() )
                error( e.d_msg, e.d_loc );
        }
        Ast::Ref<Ast::Class> cls = p.getClass();
        if( cls.isNull() )
            continue;
#if 0
        if( d_classes2.contains( cls->d_name.constData() ) )
            error( tr("class '%1' already defined").arg(cls->d_name.constData()), cls->d_loc );
        else
#else
        Classes2::const_iterator i = d_classes2.find(cls->d_name.constData());
        if( i != d_classes2.end() )
            qWarning() << "class" << cls->d_name << "already exists, overwriting" << i.value()->d_loc.d_source
                       << "with" << cls->d_loc.d_source;
#endif
            d_classes2.insert( cls->d_name.constData(), cls );
    }

    // check super classes
    Classes2::const_iterator i;
    for( i = d_classes2.begin(); i != d_classes2.end(); ++i )
    {
        if( i.value()->d_superName.isEmpty() )
            error( tr("class '%1' without super class").arg(i.value()->d_name.constData()), i.value()->d_loc );
        else if( i.value()->d_superName.constData() == nil.constData() )
        {
            if( i.value()->d_name != "Object" )
                error( tr("only 'Object' is a subclass of nil"), i.value()->d_loc );
            i.value()->d_superName.clear();
        }else
        {
            Classes2::const_iterator j = d_classes2.find( i.value()->d_superName.constData() );
            if( j == d_classes2.end() )
                error( tr("unknown super class '%1'").arg(i.value()->d_superName.constData()), i.value()->d_loc );
            else
            {
                j.value()->d_subs.append(i.value());
                i.value()->d_owner = j.value().data();
            }
        }
        d_classes.insert( i.value()->d_name, i.value() );
        if( !i.value()->d_category.isEmpty() )
            d_cats[ i.value()->d_category ].append( i.value().data() );
        // don't sort vars: std::sort( i.value()->d_vars.begin(), i.value()->d_vars.end(), sortFields );
        for( int j = 0; j < i.value()->d_methods.size(); j++ )
        {
            d_mx[ i.value()->d_methods[j]->d_name.constData() ].append( i.value()->d_methods[j].data() );
            if( i.value()->d_methods[j]->d_primitive )
                d_px[ i.value()->d_methods[j]->d_name.constData() ].append( i.value()->d_methods[j].data() );
        }
        for( int j = 0; j < i.value()->d_vars.size(); j++ )
        {
            d_vx[ i.value()->d_vars[j]->d_name.constData() ].append( i.value()->d_vars[j].data() );
        }
    }

    if( !d_errs.isEmpty() )
        return false;

    ResolveIdents ri;
    ri.mdl = this;
    for( i = d_classes2.begin(); i != d_classes2.end(); ++i )
    {
        std::sort( i.value()->d_subs.begin(), i.value()->d_subs.end(), sortSubs );
        i.value()->accept(&ri);
    }
    return d_errs.isEmpty();
}

void Model::clear()
{
    d_mx.clear();
    d_px.clear();
    d_errs.clear();
    d_classes.clear();
    d_classes2.clear();
    d_cats.clear();
    d_keywords.clear();
    d_ix.clear();
    d_tx.clear();
    d_vx.clear();
    d_globals.d_varNames.clear();
    d_globals.d_vars.clear();
}

void Model::error(const QString& msg, const Loc& loc)
{
    d_errs += QString("%1:%2:%3: %4").arg( QFileInfo(loc.d_source).baseName() ).arg(loc.d_line).arg(loc.d_col).arg( msg );
}



