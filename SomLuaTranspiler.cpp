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

#include "SomLuaTranspiler.h"
#include "SomLexer.h"
#include <QTextStream>
#include <QtDebug>
using namespace Som;
using namespace Som::Ast;

#define _USE_PCALL_

static const char* s_luaKeywords[] = {
    "and",       "break",     "do",        "else",      "elseif",
         "end",       "false",     "for",       "function",  "if",
         "in",        "local",     "nil",       "not",       "or",
         "repeat",    "return",    "then",      "true",      "until",     "while",
    0
};
static QSet<const char*> s_check;

static QByteArray prefix( const QByteArray& name )
{
    if( s_check.contains(name.constData() ) )
        return "_" + name;
    else
        return name;
}

/* Lua:
 '\a' (bell), '\b' (backspace), '\f' (form feed), '\n' (newline), '\r' (carriage return),
'\t' (horizontal tab), '\v' (vertical tab), '\\' (backslash), '\"' (quotation mark [double quote]),
and '\'' (apostrophe [single quote]). Moreover, a backslash followed by a real newline results in a
newline in the string. A character in a string can also be specified by its numerical value using the
escape sequence \ddd, where ddd is a sequence of up to three decimal digits. (Note that if a numerical
escape is to be followed by a digit, it must be expressed using exactly three digits.) Strings in Lua
can contain any 8-bit value, including embedded zeros, which can be specified as '\0'.

-> rather similar to SOM except embedded "
 */

static QByteArray _escape( QByteArray str )
{
    str.replace('"', "\\\"" );
    str.replace('\n', "\\n");
    str.replace('\r', "\\r");
    str.replace('\b', "\\b");
    str.replace('\f', "\\f");
    str.replace('\t', "\\t");
    str.replace('\v', "\\v");
    str.replace('\r', "\\r");
    return str;
}

struct LuaTranspilerVisitor : public Ast::Visitor
{
    LuaTranspilerVisitor( QTextStream& ts, Ast::Class* c ):out(ts),cls(c),level(0){}
    QTextStream& out;
    Ast::Class* cls;
    Ast::Method* method;
    int level;

    QByteArray ws()
    {
        return QByteArray(level,'\t');
    }

    virtual void visit( Return* r)
    {
        if( r->d_nonLocal )
        {
#if 0
            out << "_nonLocal = true; error( ";
            assigToExpr( r->d_what.data() );
            out << " )";
#else
            out << "_nlRes = ";
            assigToExpr( r->d_what.data() );
            out << "; _nonLocal = true; error(_nlRes)";
            // NOTE: error(x) doesn't deliver x to pcall result, but instead a string
#endif
        }else
        {
            out << "return ";
            assigToExpr( r->d_what.data() );
        }
    }

    virtual void visit( ArrayLiteral* a )
    {
        out << "_lit({ ";
        for( int i = 0; i < a->d_elements.size(); i++ )
        {
            if( i != 0 )
                out << ", ";
            a->d_elements[i]->accept(this);
        }
        out << " })";
    }

    virtual void visit( Variable* v )
    {
        Q_ASSERT( false );
        switch( v->d_kind )
        {
        case Variable::InstanceLevel:
        case Variable::ClassLevel:
        case Variable::Global:
            Q_ASSERT( false );
            break;
        case Variable::Argument:
        case Variable::Temporary:
            out << "local " << prefix(v->d_name);
            break;
        }
    }

    virtual void visit( Method* m )
    {
        // under the global var className we find the metaclass
        method = m;
        out << "function ";
        if( m->d_classLevel )
            out << "metaclass";
        else
            out << "class";
        out << "." << LuaTranspiler::map(m->d_name,m->d_patternType) << "(";
        int i = 0;
        out << "self";
        while( i < m->d_vars.size() )
        {
            if( m->d_vars[i]->d_kind != Ast::Variable::Argument )
                break;
            out << ",";
            out << prefix(m->d_vars[i]->d_name);
            i++;
        }
        out << ")" << endl;
        level++;
#ifdef _USE_PCALL_
        if( m->d_hasNonLocalReturn )
        {
            out << ws() << "local _nonLocal, _nlRes" << endl;
            out << ws() << "local _status, _pcallRes = pcall( function()" << endl;
            level++;
        }
#endif
        while( i < m->d_vars.size() )
        {
            out << ws() << "local " << prefix(m->d_vars[i]->d_name) << endl;
            i++;
        }

        for( i = 0; i < m->d_body.size(); i++ )
        {
            out << ws();
            m->d_body[i]->accept( this );
            out << ";" << endl; // to avoid Lua "ambiguous syntax (function call x new statement)" error
        }
        if( m->d_body.isEmpty() || m->d_body.last()->getTag() != Thing::T_Return )
            out << ws() << "return self" << endl;

        level--;
#ifdef _USE_PCALL_
        if( m->d_hasNonLocalReturn )
        {
            out << ws() << "end )" << endl;
            out << ws() << "if _status then return _pcallRes "
                << "elseif _nonLocal then return _nlRes "
                << "else error(_pcallRes) end" << endl;
            level--;
        }
#endif

        out << ws() << "end" << endl << endl;

    }

    virtual void visit( Block* b )
    {
        // TODO: inline Blocks

        // under the global var className we find the metaclass
        out << "_block( function(";
        int i = 0;
        while( i < b->d_func->d_vars.size() )
        {
            if( b->d_func->d_vars[i]->d_kind != Ast::Variable::Argument )
                break;
            if( i != 0 )
                out << ",";
            out << prefix(b->d_func->d_vars[i]->d_name);
            i++;
        }
        out << ")" << endl;
        level++;
        while( i < b->d_func->d_vars.size() )
        {
            out << ws() << "local " << prefix(b->d_func->d_vars[i]->d_name) << endl;
            i++;
        }
        for( i = 0; i < b->d_func->d_body.size(); i++ )
        {
            out << ws();
            if( i == b->d_func->d_body.size() - 1 )
            {
                const int tag = b->d_func->d_body[i]->getTag();
                if( tag == Thing::T_Assig )
                {
                    Assig* a = static_cast<Assig*>( b->d_func->d_body[i].data() );
                    b->d_func->d_body[i]->accept( this );
                    out << endl;
                    out << ws() << "return ";
                    a->d_lhs->accept(this);
                }else if( tag == Thing::T_Return )
                {
                    b->d_func->d_body[i]->accept( this );
                }else
                {
                    out << ws() << "return ";
                    b->d_func->d_body[i]->accept( this );
                }
            }else
            {
                b->d_func->d_body[i]->accept( this );
                out << ";";
            }
            out << endl;
        }
        level--;

        out << ws() << "end )";
    }

    void assigToExpr( Expression* e )
    {
        if( e->getTag() == Thing::T_Assig )
        {
            out << "( function()";
            Assig* a = static_cast<Assig*>( e );
            e->accept( this );
            out << "; ";
            out << "return ";
            a->d_lhs->accept(this);
            out << " end )()";
        }else
            e->accept(this);
    }

    virtual void visit( MsgSend* ms )
    {
        const bool toSuper = ms->d_receiver->keyword() == Expression::_super;
        if( toSuper )
            out << "self._super.";
        else
        {
            out << "(";
            assigToExpr( ms->d_receiver.data() );
            out << "):";
        }
        out << LuaTranspiler::map(ms->prettyName(false),ms->d_patternType) << "(";
        if( toSuper )
            out << "self";
        for( int i = 0; i < ms->d_args.size(); i++ )
        {
            if( toSuper || i != 0 )
                out << ",";
            ms->d_args[i]->accept(this);
        }
        out << ")";
    }

    virtual void visit( Cascade* c )
    {
        Q_ASSERT( false ); // not supported by SOM
    }

    virtual void visit( Assig* a )
    {
        Q_ASSERT( a->d_lhs->d_resolved );
        a->d_lhs->accept(this);
        out << " = ";
        assigToExpr( a->d_rhs.data() );
    }

    virtual void visit( Char* c )
    {
        out << "_str(\"" << _escape(QByteArray(1,c->d_ch)) << "\")";
    }

    virtual void visit( String* s )
    {
        out << "_str(\"" << _escape(s->d_str) << "\")"; // [[]] gives wrong string length if s is only a \n
    }

    virtual void visit( Number* n )
    {
        out << ( n->d_real ? "_dbl": "" ) << "(" << n->d_num << ")";
    }

    virtual void visit( Symbol* s)
    {
        if( s->d_sym.startsWith('"') )
            out << "_sym(" << s->d_sym << ")";
        else
            out << "_sym(\"" << LuaTranspiler::map(s->d_sym) << "\")";
    }

    virtual void visit( Ident* i)
    {
        if( i->d_keyword )
        {
            if( i->d_keyword == Ast::Ident::_super )
                out << "self._super";
            else
                out << i->d_ident;
        }else
        {
            Q_ASSERT( i->d_resolved );
            switch( i->d_resolved->getTag() )
            {
            case Thing::T_Variable:
                {
                    Variable* v = static_cast<Variable*>( i->d_resolved );
                    switch( v->d_kind )
                    {
                    case Variable::InstanceLevel:
                    case Variable::ClassLevel:
                        out << "self[" << ( v->d_slot + 1 ) << "]"; // one based
                        break;
                    case Variable::Argument:
                    case Variable::Temporary:
                        out << prefix(v->d_name);
                        break;
                    case Variable::Global:
                        out << "_cl(\"" << prefix(v->d_name) << "\")";
                        break;
                    }
                }
                break;
            case Thing::T_Method:
                {
                    Method* m = static_cast<Method*>( i->d_resolved );
                    out << "self." << LuaTranspiler::map( m->d_name, m->d_patternType );
                }
                break;
            case Thing::T_Class:
                out << prefix(i->d_ident);
                break;
            default:
                Q_ASSERT( false );
            }
        }
    }

    virtual void visit( Selector* )
    {
        Q_ASSERT( false );
    }
};

bool LuaTranspiler::transpile(QTextStream& out, Ast::Method* m)
{
    s_check.clear();
    const char** p = s_luaKeywords;
    while( *p != 0 )
        s_check.insert( Lexer::getSymbol(*p++).constData() );

    Q_ASSERT( m && m->d_owner && m->d_owner->getTag() == Ast::Thing::T_Class );
    Ast::Class* c = static_cast<Ast::Class*>( m->d_owner );
    LuaTranspilerVisitor v(out,c);
    m->accept(&v);
    return true;
}

QByteArray LuaTranspiler::map(QByteArray name)
{
    if( name.contains(':') )
        return map( name, Ast::KeywordPattern );
    else if( !name.isEmpty() && Lexer::isBinaryChar(name[0]) )
        return map( name, Ast::BinaryPattern );
    else
        return map( name, Ast::UnaryPattern );
}

QByteArray LuaTranspiler::map(const QByteArray& in, quint8 patternType)
{
    QByteArray name = in;
    switch( patternType )
    {
    case Ast::NoPattern:
    case Ast::UnaryPattern:
        return prefix(name);
    case Ast::KeywordPattern:
        name.replace(':','_');
        return name;
    case Ast::BinaryPattern:
        break;
    }
    // BinaryPattern
    char* p = name.data();
    for( int i = 0; i < name.size(); i++ )
    {
        char& ch = p[i];
        switch( ch )
        {
        case '~':
            ch = 't';
            break;
        case '&':
            ch = 'a';
            break;
        case '|':
            ch = 'b';
            break;
        case '*':
            ch = 's';
            break;
        case '/':
            ch = 'h';
            break;
        case '\\':
            ch = 'B';
            break;
        case '+':
            ch = 'p';
            break;
        case '=':
            ch = 'q';
            break;
        case '>':
            ch = 'g';
            break;
        case '<':
            ch = 'l';
            break;
        case ',':
            ch = 'c';
            break;
        case '@':
            ch = 'A';
            break;
        case '%':
            ch = 'r';
            break;
        case '-':
            ch = 'm';
            break;
        default:
            Q_ASSERT(false);
            break;
        }
    }
    return "_0" + name;
}

QByteArray LuaTranspiler::escape(const QByteArray& string)
{
    return _escape(string);
}
