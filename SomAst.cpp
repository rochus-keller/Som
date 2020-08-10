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

#include "SomAst.h"
using namespace Som;
using namespace Som::Ast;

Ast::Method*Ast::Class::findMethod(const QByteArray& name) const
{
    for( int i = 0; i < d_methods.size(); i++ )
    {
        if( d_methods[i]->d_name.constData() == name.constData() )
            return d_methods[i].data();
    }
    return 0;
}

Variable*Class::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

Class*Class::getSuper() const
{
    if( d_owner && d_owner->getTag() == Thing::T_Class )
        return static_cast<Class*>(d_owner);
    else
        return 0;
}

void Class::addMethod(Method* m)
{
//    Method* old = findMethod(m->d_name);
//    if( old && old->d_classLevel == m->d_classLevel )
//        qWarning() << "duplicate method name" << m->d_name << "in class" << d_name;
    d_methods.append(m);
    m->d_owner = this;
    d_methodNames[m->d_name.constData()].append(m);
}

void Class::addVar(Variable* v)
{
//    if( findVar(v->d_name ) )
//        qWarning() << "duplicate variable name" << v->d_name << "in class" << d_name;
    d_vars.append(v);
    v->d_owner = this;
    d_varNames[v->d_name.constData()].append(v);
}

QByteArray Method::prettyName(const QByteArrayList& pattern, quint8 kind , bool withSpace)
{
    if( !pattern.isEmpty() )
    {
        switch( kind )
        {
        case UnaryPattern:
        case BinaryPattern:
            return pattern.first();
        case KeywordPattern:
            return pattern.join(withSpace ? ": " : ":" ) + ':';
        }
    }
    return QByteArray();
}

QByteArray Method::prettyName(bool withSpace) const
{
    return prettyName(d_pattern,d_patternType,withSpace);
}

Variable*Method::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

Class*Scope::getClass() const
{
    if( getTag() == Thing::T_Class )
        return static_cast<Class*>( const_cast<Scope*>(this) );
    else if( d_owner )
        return d_owner->getClass();
    else
        return 0;
}

Expression* Method::findByPos(quint32 pos) const
{
    struct Locator : public AstVisitor
    {
        quint32 d_pos;
        bool inline isHit( Thing* t )
        {
            return isHit( t->d_loc.d_pos, t->getLen() );
        }
        bool inline isHit( quint32 pos, quint32 len )
        {
            return pos <= d_pos && d_pos <= pos + len;
        }
        void visit( Method* m)
        {
            for( int i = 0; i < m->d_vars.size(); i++ )
                m->d_vars[i]->accept(this);
            for( int i = 0; i < m->d_body.size(); i++ )
                m->d_body[i]->accept(this);
            for( int i = 0; i < m->d_helper.size(); i++ )
                m->d_helper[i]->accept(this);
        }
        void visit( Block* b)
        {
            for( int i = 0; i < b->d_func->d_vars.size(); i++ )
                b->d_func->d_vars[i]->accept(this);
            for( int i = 0; i < b->d_func->d_body.size(); i++ )
                b->d_func->d_body[i]->accept(this);
        }
        void visit( Cascade* c )
        {
            for( int i = 0; i < c->d_calls.size(); i++ )
                c->d_calls[i]->accept(this);
        }
        void visit( Assig* a )
        {
            for( int i = 0; i < a->d_lhs.size(); i++ )
                a->d_lhs[i]->accept(this);
            a->d_rhs->accept(this);
        }
        void visit( Symbol* s )
        {
            if( isHit(s) )
                throw (Expression*)s;
        }
        void visit( Ident* i )
        {
            if( isHit(i) )
                throw (Expression*)i;
        }
        void visit( Selector* s)
        {
            if( isHit(s) )
                throw (Expression*)s;
        }
        void visit( MsgSend* s)
        {
            for( int i = 0; i < s->d_pattern.size(); i++ )
                if( isHit( s->d_pattern[i].second.d_pos, s->d_pattern[i].first.size() ) )
                    throw (Expression*)s;
            for( int i = 0; i < s->d_args.size(); i++ )
                s->d_args[i]->accept(this);
            s->d_receiver->accept(this);
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

    Locator l;
    l.d_pos = pos;
    try
    {
        const_cast<Method*>(this)->accept(&l);
    }catch( Expression* e )
    {
        return e;
    }
    return 0;
}


Variable*Function::findVar(const QByteArray& name) const
{
    for( int i = 0; i < d_vars.size(); i++ )
    {
        if( d_vars[i]->d_name.constData() == name.constData() )
            return d_vars[i].data();
    }
    return 0;
}

void Function::addVar(Variable* v)
{
//    if( findVar(v->d_name.constData() ) )
//        qWarning() << "duplicate variable name" << v->d_name << "in function" << d_name;
    d_vars.append(v);
    v->d_owner = this;
    d_varNames[v->d_name.constData()].append(v);
}

Method* Scope::getMethod() const
{
    if( getTag() == Thing::T_Method )
        return static_cast<Method*>( const_cast<Scope*>(this) );
    else if( d_owner )
        return d_owner->getMethod();
    else
        return 0;
}

Block::Block()
{
    d_func = new Function();
}

QByteArray MsgSend::prettyName(bool withSpace) const
{
    QByteArrayList tmp;
    for( int i = 0; i < d_pattern.size(); i++ )
        tmp << d_pattern[i].first;
    return Method::prettyName(tmp,d_patternType,withSpace);
}


QList<Named*> Scope::findVars(const QByteArray& name, bool recursive) const
{
    QList<Named*> res = d_varNames.value(name.constData());
    if( res.isEmpty() && recursive && d_owner )
        res = d_owner->findVars(name, recursive);
    return res;
}

QList<Named*> Scope::findMeths(const QByteArray& name, bool recursive) const
{
    QList<Named*> res = d_methodNames.value(name.constData());
    if( res.isEmpty() && recursive && d_owner )
        res = d_owner->findMeths(name, recursive);
    return res;
}
