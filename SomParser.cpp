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

#include "SomParser.h"
#include "SomLexer.h"
#include <QIODevice>
#include <QtDebug>
#include <QFile>
using namespace Som;
using namespace Som::Ast;

// Adapted from Smalltalk StParser.cpp/h

Parser::Parser(Lexer* l):d_lex(l),d_blockLevel(0)
{
    primitive = Lexer::getSymbol("primitive");
    Object = Lexer::getSymbol("Object");
}

bool Parser::readFile()
{
    d_errs.clear();
    while( d_lex->peek().d_type != Lexer::EoF )
    {
        if( !readClass() )
            return false;
    }
    return d_errs.isEmpty();
}

bool Parser::readClass()
{
    if( !readClassExpr() )
        return false;

    Lexer::Token t = d_lex->next();
    if( t.isValid() )
        return error("only one class per file", t );
    return true;
}

static QByteArray readLine( QIODevice* in )
{
    QByteArray res;
    char ch;
    in->getChar(&ch);
    while( !in->atEnd() && ch != '\r' && ch != '\n' )
    {
        if( ch == 0 )
            res += "§0";
        else if( ch == 12 )
            res += "§12\n"; // 12 == form feed
        else
            res += ch;
        in->getChar(&ch);
    }
    return res;
}

void Parser::convertFile(QIODevice* in, const QString& to)
{
    QFile out(to);
    out.open(QIODevice::WriteOnly);
    while( !in->atEnd() )
    {
        const QByteArray line = readLine(in);
        out.write( line );
        out.write( "\n" );
    }
}

bool Parser::error(const QByteArray& msg, const Lexer::Token& pos)
{
    d_errs.append( Error(msg,pos.d_loc) );
    return false;
}

static void printChunk( const QList<Lexer::Token>& toks, quint32 line )
{
    QByteArray str;
    for( int i = 0; i < toks.size(); i++ )
        str += QByteArray(Lexer::s_typeName[toks[i].d_type]) + " ";
    qDebug() << "Chunk:" << line << str.constData();
}

static bool isBinaryChar( const Lexer::Token& t )
{
    return Lexer::isBinaryTokType(t.d_type);
}

bool Parser::readClassExpr()
{
    Lexer::Token t = d_lex->next();
    if( t.d_type != Lexer::Ident )
        return error("expecting class name", t );

    d_curClass = new Class();
    d_curClass->d_loc = t.d_loc;
    d_curClass->d_name = t.d_val.trimmed();

    t = d_lex->next();
    if( t.d_type != Lexer::Eq )
        return error("expecting '='", t );

    t = d_lex->next();
    if( t.d_type == Lexer::Ident )
    {
        d_curClass->d_superName = t.d_val.trimmed();
        t = d_lex->next();
    }else
        d_curClass->d_superName = Object; // implicit Object super class
    if( t.d_type != Lexer::Lpar )
        return error("expecting '('", t );

    t = d_lex->peek();
    if( t.d_type == Lexer::Bar )
        parseFields(false);

    t = d_lex->peek();
    while( t.d_type == Lexer::Ident || t.d_type == Lexer::Keyword || isBinaryChar(t) || t.d_type == Lexer::BinSelector )
    {
        Ast::Ref<Ast::Method> m = readMethod( d_curClass.data(),false);
        if( m.isNull() )
            return false;
        t = d_lex->peek();
    }

    if( t.d_type == Lexer::Separator )
    {
        d_lex->next(); // eat separator

        t = d_lex->peek();
        if( t.d_type == Lexer::Bar )
            parseFields(true);

        t = d_lex->peek();
        while( t.d_type == Lexer::Ident || t.d_type == Lexer::Keyword || isBinaryChar(t) || t.d_type == Lexer::BinSelector )
        {
            Ast::Ref<Ast::Method> m = readMethod( d_curClass.data(),true);
            if( m.isNull() )
                return false;
            t = d_lex->peek();
        }
    }

    if( t.d_type != Lexer::Rpar )
        return error("expecting ')' at class end", t );
    t = d_lex->next(); // eat ')'
    d_curClass->d_end = t.d_loc;
    return true;
}

Ast::Ref<Method> Parser::readMethod(Class* c, bool classLevel )
{
    Lexer::Token t = d_lex->next();
    const Lexer::Token t0 = t;
    Q_ASSERT( t.d_type == Lexer::Ident || t.d_type == Lexer::Keyword || isBinaryChar(t) || t.d_type == Lexer::BinSelector );

    Ref<Method> m = new Method();
    m->d_loc = t.d_loc;

    QList<Lexer::Token> toks;
    toks << t;
    t = d_lex->next();
    while( t.isValid() && t.d_type != Lexer::Eq )
    {
        toks << t;
        t = d_lex->next();
    }

    if( t.d_type != Lexer::Eq )
    {
        error("expecting '='",t);
        return 0;
    }
    Q_ASSERT( !toks.isEmpty() );

    int curTokIdx = 0;

    if( isBinaryChar(toks.first()) || toks.first().d_type == Lexer::BinSelector )
    {
        // binarySelector
        m->d_pattern << toks[curTokIdx++].d_val;
        if( curTokIdx >= toks.size() || !toks[curTokIdx].d_type == Lexer::Ident )
            error("invalid message header",toks.first());
        else
        {
            Ref<Variable> l = new Variable();
            l->d_kind = Variable::Argument;
            l->d_loc = toks[curTokIdx].d_loc;
            l->d_name = toks[curTokIdx].d_val;
            m->addVar( l.data() );
            curTokIdx++;
        }
        m->d_patternType = Ast::BinaryPattern;
    }else if( toks.first().d_type == Lexer::Keyword )
    {
        // Keyword selector
        m->d_patternType = Ast::KeywordPattern;
        while( ( toks.size() - curTokIdx >= 2 ) && ( toks[curTokIdx].d_type == Lexer::Keyword ) &&
               ( toks[curTokIdx+1].d_type == Lexer::Ident ) )
        {
            m->d_pattern += toks[curTokIdx].d_val;
            Ref<Variable> l = new Variable();
            l->d_kind = Variable::Argument;
            l->d_loc = toks[curTokIdx+1].d_loc;
            l->d_name = toks[curTokIdx+1].d_val;
            m->addVar( l.data() );
            curTokIdx += 2;
        }
    }else if( toks.first().d_type == Lexer::Ident )
    {
        // unary selector
        curTokIdx++;
        m->d_pattern << toks.first().d_val;
        m->d_patternType = Ast::UnaryPattern;
    }else
    {
        Q_ASSERT( false );
    }

    // helper variable so Blocks can refer to it
    m->d_self = new Variable();
    m->d_self->d_kind = Variable::Argument;
    m->d_self->d_loc = m->d_loc;
    m->d_self->d_name = Lexer::getSymbol("self");
    m->d_self->d_owner = m.data();
    m->d_varNames[m->d_self->d_name.constData()].append(m->d_self.data());

    Q_ASSERT( !m->d_pattern.isEmpty() );
    if( m->d_name.isEmpty() )
        m->d_name = Lexer::getSymbol( m->prettyName(false) );

    m->d_classLevel = classLevel;

    Method* other = c->findMethod(m->d_name);
    if( other && other->d_classLevel == m->d_classLevel )
        // apparently both parts of a class (north & south of the separator) can have methods with the same name
        // happens e.g. in Benchmarks/Random.som
        error( "duplicate method name", t0 );
    else
        c->addMethod(m.data());

    TokStream ts;

    t = d_lex->next();
    if( t.d_type == Lexer::Ident )
    {
        if( t.d_val != primitive )
        {
            error("expecting 'primitive'",t );
            return 0;
        }
        m->d_primitive = true;
        m->d_end = t.d_loc;
    }else if( t.d_type == Lexer::Lpar )
    {
        int level = 0;
        t = d_lex->next();
        while( t.isValid() )
        {
            if( t.d_type == Lexer::Rpar && level <= 0 )
                break;
            if( t.d_type == Lexer::Rpar )
                level--;
            else if( t.d_type == Lexer::Lpar )
                level++;
            ts.d_toks << t;
            t = d_lex->next();
        }
        if( t.d_type != Lexer::Rpar )
        {
            error("expecting ')'", t );
            return 0;
        }
        m->d_end = t.d_loc;
    }else
    {
        error( "expecting 'primitive' or '('", t );
        return 0;
    }

    d_curMeth = m;
    parseMethodBody( ts );

    return m;
}

bool Parser::parseMethodBody(TokStream& ts)
{
    Lexer::Token t = ts.peek();
    if( t.d_type == Lexer::Bar )
    {
        // declare locals
        parseLocals( d_curMeth.data(), ts );
        t = ts.peek();
    }

    while( !ts.atEnd() && t.isValid() )
    {
        switch( t.d_type )
        {
        case Lexer::Ident:
        case Lexer::Hash:
        case Lexer::Symbol:
        case Lexer::Lpar:
        case Lexer::Lbrack:
        case Lexer::Real:
        case Lexer::Integer:
        case Lexer::String:
        case Lexer::Char:
        case Lexer::Minus:
            {
                Ref<Expression> e = parseExpression(d_curMeth.data(),ts);
                if( !e.isNull() )
                    d_curMeth->d_body.append(e);
            }
            break;
        case Lexer::Hat:
            {
                Ref<Expression> e = parseReturn(d_curMeth.data(),ts);
                if( !e.isNull() )
                    d_curMeth->d_body.append(e);
            }
            break;
        case Lexer::Dot:
            // NOP
            ts.next();
            break;
        default:
            return error("expecting statement", ts.peek() );
        }
        t = ts.peek();
    }
    return true;
}

bool Parser::parseLocals(Function* m, Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Bar );

    t = ts.peek();
    while( t.isValid() && t.d_type == Lexer::Ident )
    {
        ts.next();
        if( m->findVar(t.d_val) )
            return error("duplicate local name", t );
        Ref<Variable> l = new Variable();
        l->d_name = t.d_val;
        l->d_kind = Variable::Temporary;
        l->d_loc = t.d_loc;
        m->addVar(l.data());
        t = ts.peek();
    }
    if( t.d_type != Lexer::Bar )
        return error("expecting '|' after temps declaration", t );
    // else
    ts.next();
    return true;
}

Ref<Expression> Parser::parseExpression(Ast::Function* scope, Parser::TokStream& ts, quint8 inPattern )
{
    Lexer::Token t = ts.peek();

    Ref<Expression> lhs = simpleExpression(scope,ts);

    if( lhs.isNull() )
        return 0;

    t = ts.peek();


    if( t.d_type == Lexer::Assig )
    {
        // parseAssig inlined here
        ts.next(); // eat assig symbol
        if( lhs->getTag() != Thing::T_Ident || lhs->keyword() )
        {
            error("cannot assign to expression or keywords",t);
            return 0;
        }
        Ref<Assig> a = new Assig();
        a->d_loc = t.d_loc;
        a->d_lhs = static_cast<Ident*>( lhs.data() );
        a->d_rhs = parseExpression(scope, ts);
        if( a->d_rhs.isNull() )
            return 0;
        else
            return a.data();
    }

    Ref<MsgSend> send;
    while( isBinaryChar(t) || t.d_type == Lexer::BinSelector ||
           t.d_type == Lexer::Keyword ||
           t.d_type == Lexer::Ident )
    {
        Ref<MsgSend> c;
        if( t.d_type == Lexer::Ident )
        {
            // unary selector
            ts.next(); // eat current token
            c = new MsgSend();
            c->d_loc = t.d_loc;
            c->d_inMethod = scope->getMethod();
            c->d_pattern << qMakePair(t.d_val,t.d_loc);
            c->d_patternType = Ast::UnaryPattern;
        }else if( isBinaryChar(t) || t.d_type == Lexer::BinSelector )
        {
            if( inPattern == BinaryPattern )
                goto end_;

            // binarySelector
            ts.next(); // eat current token
            c = new MsgSend();
            c->d_loc = t.d_loc;
            c->d_inMethod = scope->getMethod();
            c->d_pattern << qMakePair(t.d_val,t.d_loc);
            Ref<Expression> e = parseExpression(scope,ts,BinaryPattern);
            if( e.isNull() )
                return c.data();
            c->d_args << e;
            c->d_patternType = Ast::BinaryPattern;
        }else if( t.d_type == Lexer::Keyword )
        {
            // Keyword selector

            if( inPattern == KeywordPattern || inPattern == BinaryPattern  )
                goto end_;

            c = new MsgSend();
            c->d_loc = t.d_loc;
            c->d_inMethod = scope->getMethod();
            c->d_patternType = Ast::KeywordPattern;
            while( t.d_type == Lexer::Keyword )
            {
                ts.next(); // eat current token
                c->d_pattern << qMakePair(t.d_val,t.d_loc);
                Ref<Expression> e = parseExpression(scope,ts,KeywordPattern);
                if( e.isNull() )
                    return c.data();
                c->d_args << e;
                t = ts.peek();
            }
        }else
            Q_ASSERT( false );

        if( send.isNull() )
            c->d_receiver = lhs;
        else
            c->d_receiver = send.data();
        send = c;

        t = ts.peek();
    }

end_:
    if( send.isNull() )
        return lhs.data();
    else
        return send.data();
}

Ast::Ref<Expression> Parser::simpleExpression(Function* scope, Parser::TokStream& ts)
{
    Lexer::Token t = ts.peek();
    Ref<Expression> lhs;
    switch( t.d_type )
    {
    case Lexer::Ident:
        lhs = new Ident( t.d_val, t.d_loc, scope->getMethod() );
        ts.next();
        break;
    case Lexer::Minus:
        ts.next(); // eat '-'
        t = ts.peek();
        if( t.d_type == Lexer::Real || t.d_type == Lexer::Integer )
        {
            t.d_val = "-" + t.d_val;
            lhs = new Number( t.d_val, t.d_type == Lexer::Real, t.d_loc );
            ts.next();
        }else
        {
            error("expecting number after '-'",t);
            return lhs.data();
        }
        break;
    case Lexer::Real:
    case Lexer::Integer:
        lhs = new Number( t.d_val, t.d_type == Lexer::Real, t.d_loc );
        ts.next();
        break;
    case Lexer::String:
        lhs = new String( t.d_val, t.d_loc );
        ts.next();
        break;
    case Lexer::Char:
        Q_ASSERT( t.d_val.size() == 1 );
        lhs = new Char( t.d_val[0], t.d_loc );
        ts.next();
        break;
    case Lexer::Hash:
        if( ts.peek(2).d_type == Lexer::Lpar )
        {
            ts.next(); // eat hash
            lhs = parseArray(scope,ts);
        }else
        {
            // lhs = parseSymbol(ts,inKeywordSequence);
            error("expecting '('", t );
            return lhs.data();
        }
        break;
    case Lexer::Symbol:
        lhs = new Symbol( t.d_val, t.d_loc );
        ts.next();
        break;
    case Lexer::Lpar:
        ts.next(); // eat lpar
        lhs = parseExpression(scope,ts);
        t = ts.next();
        if( t.d_type != Lexer::Rpar )
        {
            error("expecting ')'", t );
            return lhs.data();
        }
        break;
    case Lexer::Lbrack:
        lhs = parseBlock(scope,ts);
        break;
    default:
        error("invalid expression",t);
        return 0;
    }
    return lhs;
}

Ast::Ref<Expression> Parser::parseBlock(Ast::Function* outer,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Lbrack );
    d_blockLevel++;
    Ref<Block> b = new Block();
    b->d_loc = t.d_loc;
    b->d_func->d_owner = outer;
    b->d_syntaxLevel = d_blockLevel;
    parseBlockBody( b->d_func.data(), ts );
    d_blockLevel--;
    return b.data();
}

Ast::Ref<Expression> Parser::parseArray(Ast::Function* scope,Parser::TokStream& ts)
{
    static const char* msg = "invalid array element";
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Lpar );
    Ref<ArrayLiteral> a = new ArrayLiteral();
    a->d_loc = t.d_loc;
    t = ts.peek();
    while( t.isValid() && t.d_type != Lexer::Rpar )
    {
        switch( t.d_type )
        {
        case Lexer::Real:
        case Lexer::Integer:
        case Lexer::Minus:
        case Lexer::String:
        case Lexer::Char:
            {
                Ref<Expression> e = parseExpression(scope, ts,true);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e.data();
            }
            break;
        case Lexer::Keyword:
            {
                // CHECK: these idents seem to be symbols, not real idents
                QByteArray str;
                while( t.isValid() && t.d_type == Lexer::Ident && ts.peek(2).d_type == Lexer::Colon )
                {
                    ts.next(); // eat cur
                    str += t.d_val;
                    t = ts.peek();
                }
                a->d_elements << new Symbol( Lexer::getSymbol(str), t.d_loc );
            }
            break;
        case Lexer::Ident:
            // CHECK: these idents seem to be symbols, not real idents
            a->d_elements << new Symbol( t.d_val, t.d_loc );
            ts.next();
            break;
        case Lexer::Hash:
            if( ts.peek(2).d_type == Lexer::Ident )
            {
                Ref<Expression> e = parseExpression(scope, ts);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e;
            }else
            {
                ts.next();
                error( msg, t );
                return a.data();
            }
            break;
        case Lexer::Lpar:
            {
                Ref<Expression> e = parseArray(scope,ts);
                if( e.isNull() )
                    return a.data();
                a->d_elements << e;
            }
            break;
        default:
            error( msg, t );
            ts.next();
            return a.data();
        }
        t = ts.peek();
    }
    if( t.d_type != Lexer::Rpar )
        error( "non-terminated array literal", t );
    ts.next(); // eat rpar
    return a.data();
}

Ast::Ref<Expression> Parser::parseAssig(Ast::Function* scope,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Ident );
    Ref<Assig> a = new Assig();
    a->d_loc = t.d_loc;
    a->d_lhs = new Ident(t.d_val,t.d_loc, scope->getMethod() );
    t = ts.next(); // eat assig
    a->d_rhs = parseExpression(scope, ts);
    return a.data();
}

Ast::Ref<Expression> Parser::parseReturn(Ast::Function* scope,Parser::TokStream& ts)
{
    Lexer::Token t = ts.next();
    Q_ASSERT( t.d_type == Lexer::Hat );
    Ref<Return> r = new Return();
    if( d_blockLevel > 0 )
        r->d_nonLocal = true;
    r->d_loc = t.d_loc;
    r->d_what = parseExpression(scope, ts);
    return r.data();
}

bool Parser::parseBlockBody(Function* block, Parser::TokStream& ts)
{
    Lexer::Token t = ts.peek();
    bool hasParams = false;
    while( t.isValid() && t.d_type == Lexer::Colon )
    {
        ts.next();
        t = ts.next();
        if( t.d_type != Lexer::Ident )
            return error("expecting ident in block argument declaration", t );
        if( block->findVar(t.d_val) )
            return error("block argument names must be unique", t );
        Ref<Variable> l = new Variable();
        hasParams = true;
        l->d_kind = Variable::Argument;
        l->d_loc = t.d_loc;
        l->d_name = t.d_val;
        block->addVar(l.data());
        t = ts.peek();
    }
    if( hasParams && t.d_type == Lexer::Bar )
    {
        ts.next();
        t = ts.peek();
    }
    bool localsAllowed = true;
    while( !ts.atEnd() && t.isValid() )
    {
        switch( t.d_type )
        {
        case Lexer::Ident:
        case Lexer::Hash:
        case Lexer::Symbol:
        case Lexer::Lpar:
        case Lexer::Lbrack:
        case Lexer::Real:
        case Lexer::Integer:
        case Lexer::String:
        case Lexer::Char:
        case Lexer::Minus:
            {
                Ref<Expression> e = parseExpression(block, ts);
                if( !e.isNull() )
                    block->d_body.append(e);
            }
            break;
        case Lexer::Bar:
            if( localsAllowed )
            {
                localsAllowed = false;
                parseLocals( block, ts );
            }else
                return error("temp declaration not allowed here", ts.peek() );
            break;
        case Lexer::Hat:
            {
                Ref<Expression> e = parseReturn(block,ts);
                if( !e.isNull() )
                {
                    block->d_body.append(e);
                    if( d_blockLevel > 0 )
                        d_curMeth->d_hasNonLocalReturn = true;
                }
            }
            break;
        case Lexer::Dot:
            // NOP
            ts.next();
            break;
        case Lexer::Rbrack:
            ts.next();
            block->d_end = t.d_loc;
            if( block->d_body.isEmpty() )
                return error("empty block bodies not supported",t);
            else
                return true; // end of block
        default:
            return error("expecting statement", ts.peek() );
        }
        t = ts.peek();
    }
    return true;
}

bool Parser::parseFields(bool classLevel)
{
    d_lex->next();

    Lexer::Token t = d_lex->next();
    while( t.d_type == Lexer::Ident )
    {
        const QByteArray n = t.d_val.trimmed();
        if( d_curClass->findVar(n) )
            return error("duplicate field name", t );

        Ref<Variable> f = new Variable();
        f->d_name = n;
        f->d_kind = classLevel ? Variable::ClassLevel : Variable::InstanceLevel;
        f->d_loc = t.d_loc;
        d_curClass->addVar(f.data());
        t = d_lex->next();
    }
    if( t.d_type != Lexer::Bar )
        return error( "expecting '|'", t );
}

Lexer::Token Parser::TokStream::next()
{
    if( d_pos < d_toks.size() )
        return d_toks[d_pos++];
    else
        return Lexer::Token();
}

Lexer::Token Parser::TokStream::peek(int la) const
{
    Q_ASSERT( la > 0 );
    if( ( d_pos + la - 1 ) < d_toks.size() )
        return d_toks[ d_pos + la - 1 ];
    else
        return Lexer::Token();
}
