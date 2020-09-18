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

#include "SomLjbcCompiler.h"
#include "SomLuaTranspiler.h"
#include "SomLexer.h"
#include <QtDebug>
using namespace Som;
using namespace Som::Ast;

struct LjBcGen: public Visitor
{
    Lua::JitComposer& bc;

    LjBcGen(Lua::JitComposer& _bc):bc(_bc){}

    struct NoMoreFreeSlots {};

    struct Ctx
    {
        Function* fun;
        Block* block;
        Lua::JitComposer::SlotPool pool;
        typedef QHash<Variable*,quint16> Upvals;
        Upvals upvals;
        Ctx(Function* f = 0, Block* b = 0):fun(f),block(b) { }

        int buySlots(int len = 1, bool call = false )
        {
            const int tmp = Lua::JitComposer::nextFreeSlot(pool,len, call );
            if( tmp < 0 )
                throw NoMoreFreeSlots();
            return tmp;
        }
        void sellSlots(quint8 base, int len = 1 )
        {
            // qDebug() << "sell" << base << len;
            Lua::JitComposer::releaseSlot(pool,base, len );
        }
        quint16 getUpvalNr(Variable* n)
        {
            Upvals::const_iterator i = upvals.find(n);
            if( i != upvals.end() )
                return i.value();
            const int nr = upvals.size();
            upvals[ n ] = nr;
            return nr;
        }
    };
    QList<Ctx> ctx;
    QList<quint8> slotStack;

    bool inline error( const Loc& l, const QString& msg )
    {
        qCritical() << msg << l.d_line << l.d_col;
        // err->error(Errors::Semantics, mod->d_file, l.d_row, l.d_col, msg );
        return false;
    }

    Method* owningMethod() const
    {
        int i = ctx.size() - 1;
        while( i >= 0 )
        {
            if( ctx[i].block == 0 )
            {
                Q_ASSERT( ctx[i].fun->getTag() == Thing::T_Method );
                return static_cast<Method*>( ctx[i].fun );
            }
            i--;
        }
        Q_ASSERT( false );
        return 0;
    }
    quint16 resolveUpval( Variable* n, const Loc& loc )
    {
        Q_ASSERT( n->d_inlinedOwner != ctx.back().fun );
        // get the upval id from the present context
        const quint16 res = ctx.back().getUpvalNr(n);
        bool foundHome = false;
        // now register the upval in all upper contexts too up to but not including the
        // one where the symbol was defined
        for( int i = ctx.size() - 2; i >= 0; i-- )
        {
            // NOTE that inlined Blocks are not in the ctx stack!
            if( n->d_inlinedOwner == ctx[i].fun )
            {
                foundHome = true;
                break;
            }else
                ctx[i].getUpvalNr(n);
        }
        if( !foundHome )
            error( loc, QString("cannot find module level symbol for upvalue '%1'").arg(n->d_name.constData()) );
        return res;
    }

    Lua::JitComposer::VarNameList getSlotNames( Function* m, bool inBlock )
    {
        Lua::JitComposer::VarNameList vnl(m->d_vars.size()+1);
        for( int i = 0; i < m->d_vars.size(); i++ )
        {
            Variable* n = m->d_vars[i].data();
            Lua::JitComposer::VarName& vn = vnl[n->d_slot];
            Q_ASSERT( vn.d_name.isEmpty() );
            vn.d_name = n->d_name;
            // we currently don't reuse slots, so they're valid over the whole body
            vn.d_from = 0; // n->d_liveFrom;
            vn.d_to = bc.getCurPc(); // n->d_liveTo;
        }
        if( !inBlock )
        {
            Lua::JitComposer::VarName& vn = vnl[0];
            Q_ASSERT( vn.d_name.isEmpty() );
            vn.d_name = Lexer::getSymbol("self");
            vn.d_from = 0; // n->d_liveFrom;
            vn.d_to = bc.getCurPc(); // n->d_liveTo;
        }
        return vnl;
    }

    Lua::JitComposer::UpvalList getUpvals()
    {
        Function* fowner = ctx.back().fun->inlinedOwner();
        Lua::JitComposer::UpvalList uvl(ctx.back().upvals.size());
        Ctx::Upvals::const_iterator i;
        for( i = ctx.back().upvals.begin(); i != ctx.back().upvals.end(); ++i )
        {
            Lua::JitComposer::Upval u;
            u.d_name = i.key()->d_name;
            // if( i.key()->d_uvRo )
            //    u.d_isRo = true; // TODO
            if( i.key()->d_inlinedOwner == fowner )
            {
                u.d_uv = i.key()->d_slot;
                u.d_isLocal = true;
            }else if( ctx.size() > 1 )
            {
                u.d_uv = ctx[ ctx.size() - 2 ].getUpvalNr(i.key());
            }else
                error( i.key()->d_loc, QString("cannot compose upvalue list because of '%1'").arg(u.d_name.constData() ) );
            uvl[i.value()] = u;
        }
        return uvl;
    }

    void closeUpvals()
    {
        if( ctx.back().fun->d_upvalSource )
            bc.UCLO(0,0, ctx.back().fun->d_end.packed() );
    }

    virtual void visit( Method* m )
    {
        ctx.push_back( Ctx(m) );
        const int id = bc.openFunction(m->getParamCount() + 1, // +1 because of self which is always slot(0)
                                       m->d_name,
                        m->d_loc.packed(), m->d_end.packed() );
        Q_ASSERT( id >= 0 );

        // we do this for sake of completeness; a self Variable is an invisible argument (i.e. not part of d_vars)
        // and used so Idents in Blocks refering to self can resolve Upvals with the regular mechanism
        QList<Named*> selfs = m->findVars(Lexer::getSymbol("self"));
        Q_ASSERT( selfs.size() == 1 );
        Variable* self = static_cast<Variable*>(selfs.first());
        self->d_slot = ctx.back().buySlots( 1 ); // because of self

        for( int i = 0; i < m->d_vars.size(); i++ )
        {
            const int var = ctx.back().buySlots(1);
            m->d_vars[i]->d_slot = var;
        }

        for( int i = 0; i < m->d_body.size(); i++ )
        {
            m->d_body[i]->accept( this );
            ctx.back().sellSlots(slotStack.back());
            slotStack.pop_back(); // we don't use the results
        }

        if( m->d_body.isEmpty() || m->d_body.last()->getTag() != Thing::T_Return )
        {
            closeUpvals();
            bc.RET(0,1,m->d_end.packed()); // return self
        }

        bc.setVarNames( getSlotNames(m,false) );
        bc.setUpvals( getUpvals() );
        bc.closeFunction(ctx.back().pool.d_frameSize);

        // add the function to the metaclass or class table
        const int f = ctx.back().buySlots(1);
        bc.FNEW( f, id, m->d_loc.packed() );
        const int c = ctx.back().buySlots(1);
        bc.GGET( c, m->d_owner->d_name, m->d_loc.packed() );
        if( !m->d_classLevel )
            bc.TGET( c, c, "_class", m->d_loc.packed() );
        bc.TSET( f, c,  LuaTranspiler::map(m->d_name,m->d_patternType), m->d_loc.packed() );
        ctx.back().sellSlots(c);
        ctx.back().sellSlots(f);

        ctx.pop_back();
    }

    void inlineBlock( Block* b )
    {
        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
        {
            const int var = ctx.back().buySlots(1);
            Q_ASSERT( b->d_func->d_vars[i]->d_kind == Variable::Temporary );
            b->d_func->d_vars[i]->d_slot = var;
        }

        for( int i = 0; i < b->d_func->d_body.size(); i++ )
        {
            b->d_func->d_body[i]->accept( this );
            if( i < b->d_func->d_body.size() - 1 )
            {
                // the last expression in the block body is the one returned to the caller
                ctx.back().sellSlots(slotStack.back());
                slotStack.pop_back();
            }
        }

        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
            ctx.back().sellSlots(b->d_func->d_vars[i]->d_slot );
    }

    void inlineIf( MsgSend* s )
    {
        if( s->d_receiver->keyword() == Expression::_super )
        {
            const int slot = ctx.back().buySlots(1);
            bool toSell = false;
            int _self = selfToSlot( &toSell, s->d_loc );
            bc.TGET(slot,_self,"_super",s->d_loc.packed());
            if( toSell )
                ctx.back().sellSlots(_self);
            slotStack.push_back(slot);
        }else if( s->d_receiver->getTag() == Thing::T_Block )
        {
            inlineBlock( static_cast<Block*>(s->d_receiver.data()));
        }else
            s->d_receiver->accept(this);
        // the result is in slotStack.back()
        switch( s->d_flowControl )
        {
        case IfTrue:
        case IfElse:
            bc.ISF(slotStack.back(),s->d_loc.packed());
            break;
        case IfFalse:
            bc.IST(slotStack.back(),s->d_loc.packed());
            break;
        default:
            Q_ASSERT(false);
            break;
        }
        ctx.back().sellSlots(slotStack.back());
        slotStack.pop_back();
        bc.JMP(ctx.back().pool.d_frameSize,0,s->d_loc.packed());
        const int label = bc.getCurPc();

        Q_ASSERT( !s->d_args.isEmpty() && s->d_args.first()->getTag() == Thing::T_Block );
        inlineBlock( static_cast<Block*>(s->d_args.first().data()));
        // the result is in slotStack.back()

        if( s->d_flowControl == IfElse )
        {
            // before emitting the else part take care that the if part jumps over it
            bc.JMP(ctx.back().pool.d_frameSize,0,s->d_loc.packed());
            const int label2 = bc.getCurPc();

            bc.patch(label);
            const int res = slotStack.back();
            Q_ASSERT( s->d_args.size() == 2 && s->d_args.last()->getTag() == Thing::T_Block );
            inlineBlock( static_cast<Block*>(s->d_args.last().data()));
            // the result is in slotStack.back()
            bc.MOV(res,slotStack.back(),s->d_loc.packed());
            ctx.back().sellSlots(slotStack.back());
            slotStack.pop_back();

            bc.patch(label2);
        }else
            bc.patch(label);

    }

    virtual void visit( MsgSend* s )
    {
        switch( s->d_flowControl )
        {
        case IfTrue:
        case IfFalse:
        case IfElse:
            inlineIf(s);
            return;
        }

        const bool toSuper = s->d_receiver->keyword() == Expression::_super;
        if( toSuper )
        {
            const int slot = ctx.back().buySlots(1);
            bool toSell = false;
            int _self = selfToSlot( &toSell, s->d_loc );
            bc.TGET(slot,_self,"_super",s->d_loc.packed());
            if( toSell )
                ctx.back().sellSlots(_self);
            slotStack.push_back(slot);
        }else
            s->d_receiver->accept(this);
        // the result is in slotStack.back()
        const int args = ctx.back().buySlots( s->d_args.size() + 2, true );
        bc.TGET( args, slotStack.back(),
                 LuaTranspiler::map(s->prettyName(false),s->d_patternType), s->d_loc.packed() );
        if( toSuper )
        {
            bool toSell = false;
            int _self = selfToSlot( &toSell, s->d_loc );
            bc.MOV( args+1, _self, s->d_loc.packed() ); // use self for calls to super
            if( toSell )
                ctx.back().sellSlots(_self);
        }else
            bc.MOV( args+1, slotStack.back(), s->d_loc.packed() );
        ctx.back().sellSlots(slotStack.back());
        slotStack.pop_back();
        for( int i = 0; i < s->d_args.size(); i++ )
        {
            s->d_args[i]->accept(this);
            // the result is in slotStack.back()
            bc.MOV(args+2+i, slotStack.back(), s->d_loc.packed() );
            ctx.back().sellSlots(slotStack.back());
            slotStack.pop_back();
        }
        bc.CALL(args,2,s->d_args.size() + 1, s->d_loc.packed() );

        if( ctx.back().block || !s->d_inMethod->d_hasNonLocalReturnIfInlined )
        {
            // if there is a second return value which is not nil we directly return from blocks
            // and methods where no local return block was defined and just pass through the second value
            bc.ISF( args+1, s->d_loc.packed() );
            bc.JMP(ctx.back().pool.d_frameSize,1, s->d_loc.packed() );
            const int label = bc.getCurPc();
            closeUpvals();
            bc.RET(args, 2,s->d_loc.packed());
            bc.patch( label );
        }else // if not in block and s->d_inMethod->d_hasNonLocalReturn
        {
            // here we are on method level; check whether it is the method in which the no local return was defined.
            bc.ISF( args+1, s->d_loc.packed() );
            bc.JMP(ctx.back().pool.d_frameSize,0, s->d_loc.packed() );
            const int label = bc.getCurPc();
            const int tmp = ctx.back().buySlots(1);
            bc.KSET( tmp, (ptrdiff_t)owningMethod(), s->d_loc.packed() );
            bc.ISEQ( args+1, tmp, s->d_loc.packed() );
            bc.JMP(ctx.back().pool.d_frameSize,1, s->d_loc.packed() );
            const int label2 = bc.getCurPc();
            closeUpvals();
            bc.RET(args, 2,s->d_loc.packed()); // return with second argument, because this is not the method where
                                               // the non-local return was defined in
            bc.patch( label2 );
            closeUpvals();
            bc.RET(args, 1,s->d_loc.packed()); // return with no second argument
            bc.patch(label);
        }

        // otherwise just use the return value as the expression result
        const int res = ctx.back().buySlots(1);
        slotStack.push_back(res);
        bc.MOV(res,args,s->d_loc.packed());
        ctx.back().sellSlots(args, s->d_args.size() + 2);
    }

    virtual void visit( Return* r )
    {
        r->d_what->accept(this);
        // the result is in slotStack.back()
        if( ctx.back().block && r->d_nonLocalIfInlined )
        {
            // Block Level
            // an explicit return in a block is always a non-local return
            closeUpvals();
            const int slot = ctx.back().buySlots(2);
            bc.MOV(slot,slotStack.back(),r->d_loc.packed());
            // use the AST address of the Method as id (RISK: 64 bit architectures)
            bc.KSET(slot+1, (ptrdiff_t)owningMethod(), r->d_loc.packed() );
            bc.RET( slot, 2, r->d_loc.packed() );
            ctx.back().sellSlots(slot,2);
        }else
        {
            // Method Level
            closeUpvals();
            bc.RET( slotStack.back(), 1, r->d_loc.packed() );
        }
        // we leave slotStack.back() so the caller of the statement can handle it uniformely
    }

    virtual void visit( Assig* a )
    {
        a->d_rhs->accept(this);
        // the result is in slotStack.back()

        // we intentionally don't accept on the lhs ident
        Q_ASSERT( a->d_lhs->d_resolved && a->d_lhs->d_resolved->getTag() == Thing::T_Variable );
        Q_ASSERT( !a->d_lhs->d_keyword );
        Variable* lhs = static_cast<Variable*>(a->d_lhs->d_resolved);
        switch( lhs->d_kind )
        {
        case Variable::InstanceLevel:
        case Variable::ClassLevel:
            {
                bool toSell = false;
                int _self = selfToSlot( &toSell, a->d_loc );

                const int index = lhs->d_slot + 1; // object field indices are one-based
                if( index <= 255 )
                    bc.TSETi( slotStack.back(), _self, index, a->d_loc.packed() ); // self == slot(0)
                else
                {
                    int tmp = ctx.back().buySlots(1);
                    bc.KSET( tmp, quint32(index), a->d_loc.packed() );
                    bc.TSET( slotStack.back(), _self, tmp, a->d_loc.packed() ); // self == slot(0)
                    ctx.back().sellSlots(tmp);
                }

                if( toSell )
                    ctx.back().sellSlots(_self);
            }
            break;
        case Variable::Argument:
        case Variable::Temporary:
            // either local or upval
            if( lhs->d_inlinedOwner != ctx.back().fun )
            {
                const int uv = resolveUpval(lhs,lhs->d_loc);
                bc.USET( uv, slotStack.back(), a->d_loc.packed() );
            }else
            {
                Q_ASSERT( ctx.back().block || lhs->d_slot != 0 );
                bc.MOV( lhs->d_slot, slotStack.back(), a->d_loc.packed() );
            }
            break;
        case Variable::Global:
            error( a->d_loc, "cannot assign to global variables" );
            break;
        }
        // the rhs result is still in slotStack.back() and stays there as the result of the assignment expression
    }

    virtual void visit( Block* b )
    {
        ctx.push_back( Ctx(b->d_func.data(),b) );
        const int id = bc.openFunction(b->d_func->getParamCount(), // no self here, we use the self of the surrounding method
                        "Block", b->d_loc.packed(), b->d_func->d_end.packed() );
        Q_ASSERT( id >= 0 );

        // ctx.back().buySlots( b->d_func->d_vars.size() );
        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
        {
            const int var = ctx.back().buySlots(1);
            b->d_func->d_vars[i]->d_slot = var;
        }

        for( int i = 0; i < b->d_func->d_body.size(); i++ )
        {
            b->d_func->d_body[i]->accept( this );
            if( i == b->d_func->d_body.size() - 1 && b->d_func->d_body.last()->getTag() != Thing::T_Return )
            {
                // if last return is missing, add it and return last expression result
                closeUpvals();
                bc.RET( slotStack.back(),1,b->d_func->d_end.packed());
            }
            ctx.back().sellSlots(slotStack.back());
            slotStack.pop_back();
        }

        Q_ASSERT( !b->d_func->d_body.isEmpty() );
        //if( b->d_func->d_body.isEmpty() )
        //    bc.RET(0,1,b->d_func->d_end.packed()); // return self

        bc.setVarNames( getSlotNames(b->d_func.data(),true) );
        bc.setUpvals( getUpvals() );
        bc.closeFunction(ctx.back().pool.d_frameSize);
        ctx.pop_back();

        const int res = ctx.back().buySlots(1);
        slotStack.push_back(res);

        // return a table which carries the function
        const int f = ctx.back().buySlots(1);
        bc.FNEW( f, id, b->d_loc.packed() );
        bc.TNEW(res,0,0,b->d_loc.packed());
        bc.TSET(f,res,"_f",b->d_loc.packed());
        ctx.back().sellSlots(f);
        emitSetmetatable( res, "Block", b->d_loc );
    }

    virtual void visit( ArrayLiteral* a )
    {
        const int res = ctx.back().buySlots(1);
        slotStack.push_back(res);
        bc.TNEW(res,0,0,a->d_loc.packed());
        emitSetmetatable( res, "Array", a->d_loc );

        for( int i = 0; i < a->d_elements.size(); i++ )
        {
            a->d_elements[i]->accept(this);
            if( (i+1) <= 255 )
                bc.TSETi( slotStack.back(), res, i+1, a->d_elements[i]->d_loc.packed() );
            else
            {
                int tmp = ctx.back().buySlots(1);
                bc.KSET( tmp, quint32(i+1), a->d_loc.packed() );
                bc.TSET( slotStack.back(), 0, tmp, a->d_loc.packed() );
                ctx.back().sellSlots(tmp);
            }
            ctx.back().sellSlots(slotStack.back());
            slotStack.pop_back();
        }
    }

    void emitSetmetatable( int t, const QByteArray& cls, const Loc& loc )
    {
        const int args = ctx.back().buySlots(3,true);
        bc.GGET(args,"setmetatable",loc.packed() );
        bc.MOV(args+1,t,loc.packed());
        bc.GGET(args+2,cls,loc.packed());
        bc.TGET(args+2,args+2,"_class",loc.packed());
        bc.CALL(args,0,2,loc.packed());
        ctx.back().sellSlots(args,3);
    }

    void emitString( const QByteArray& string, const QByteArray& cls, const Loc& loc )
    {
        const int str = ctx.back().buySlots(1);
        bc.KSET(str, LuaTranspiler::escape(string), loc.packed() );
        const int res = ctx.back().buySlots(1);
        slotStack.push_back(res);
        bc.TNEW(res,0,0,loc.packed());
        bc.TSET(str,res,"_str",loc.packed());
        ctx.back().sellSlots(str);
        emitSetmetatable( res, cls, loc );
    }

    virtual void visit( String* s )
    {
        emitString( s->d_str, "String", s->d_loc );
    }
    virtual void visit( Char* c )
    {
        emitString( QByteArray(1,c->d_ch), "String", c->d_loc );
    }
    virtual void visit( Symbol* s)
    {
        if( s->d_sym.startsWith('"') )
            emitString( s->d_sym.mid(1,s->d_sym.size() - 2), "Symbol", s->d_loc );
        else
            emitString( LuaTranspiler::map(s->d_sym), "Symbol", s->d_loc );
    }

    virtual void visit( Number* n )
    {
        const int res = ctx.back().buySlots(1);
        slotStack.push_back(res);
        if( n->d_real )
        {
            const int dbl = ctx.back().buySlots(1);
            bool ok;
            bc.KSET(dbl, n->toNumber(&ok), n->d_loc.packed() );
            if( !ok )
                error(n->d_loc, QString("invalid real %1").arg(n->d_num.constData()) );
            bc.TNEW(res,0,0,n->d_loc.packed());
            bc.TSET(dbl,res,"_dbl",n->d_loc.packed());
            ctx.back().sellSlots(dbl);
            emitSetmetatable( res, "Double", n->d_loc );
        }else
        {
            bool ok;
            bc.KSET( res, n->toNumber(&ok), n->d_loc.packed() );
            if( !ok )
                error(n->d_loc, QString("invalid integer %1").arg(n->d_num.constData()) );
        }
    }

    int selfToSlot( bool* toSell, const Loc& loc )
    {
        int _self = 0;
        if( toSell )
            *toSell = false;
        if( ctx.back().block )
        {
            QList<Named*> selfs = ctx.back().fun->findVars(Lexer::getSymbol("self"));
            Q_ASSERT( selfs.size() == 1 );
            Variable* self = static_cast<Variable*>(selfs.first());

            _self = ctx.back().buySlots(1);
            if( toSell )
                *toSell = true;
            const int uv = resolveUpval(self,loc);
            bc.UGET( _self, uv, loc.packed() );
        }
        return _self;
    }

    virtual void visit( Ident* id )
    {
        const int res = ctx.back().buySlots(1);
        slotStack.push_back( res );
        if( id->d_resolved )
        {
            switch( id->d_resolved->getTag() )
            {
            case Thing::T_Class:
                bc.GGET(res, id->d_ident, id->d_loc.packed() );
                break;
            case Thing::T_Variable:
                {
                    Variable* v = static_cast<Variable*>(id->d_resolved);
                    switch( v->d_kind )
                    {
                    case Variable::InstanceLevel:
                    case Variable::ClassLevel:
                        {
                            bool toSell = false;
                            int _self = selfToSlot( &toSell, id->d_loc );

                            const int index = v->d_slot + 1; // object field indices are one-based
                            if( index <= 255 )
                                bc.TGETi( res, _self, index, id->d_loc.packed() ); // self == slot(0)
                            else
                            {
                                int tmp = ctx.back().buySlots(1);
                                bc.KSET( tmp, quint16(index), id->d_loc.packed() );
                                bc.TGET( res, _self, tmp, id->d_loc.packed() ); // self == slot(0)
                                ctx.back().sellSlots(tmp);
                            }

                            if( toSell )
                                ctx.back().sellSlots(_self);
                        }
                        break;
                    case Variable::Argument:
                    case Variable::Temporary:
                        // either local or upval
                        if( v->d_inlinedOwner != ctx.back().fun )
                        {
                            const int uv = resolveUpval(v,id->d_loc);
                            bc.UGET( res, uv, id->d_loc.packed() );
                        }else
                        {
                            bc.MOV( res, v->d_slot, id->d_loc.packed() );
                        }
                        break;
                    case Variable::Global:
                        bc.GGET(res, v->d_name, id->d_loc.packed() );
                        break;
                    }
                }
                break;
            default:
                Q_ASSERT(false);
                break;
            }
        }else if( id->d_keyword )
        {
            switch( id->d_keyword )
            {
            case Expression::_nil:
                bc.KSET(res,QVariant(),id->d_loc.packed());
                break;
            case Expression::_true:
                bc.KSET(res,true,id->d_loc.packed());
                break;
            case Expression::_false:
                bc.KSET(res,false,id->d_loc.packed());
                break;
            case Expression::_self:
                Q_ASSERT( ctx.back().block == 0 );
                bc.MOV(res,0,id->d_loc.packed());
                break;
            default:
                Q_ASSERT( false );
                break;
            }
        }else
            Q_ASSERT( false );
    }

    virtual void visit( Variable* ) { Q_ASSERT(false); }
    virtual void visit( Class* ) { Q_ASSERT(false); }

};

bool LjbcCompiler::translate(Lua::JitComposer& bc, Ast::Method* m)
{
    Q_ASSERT( m && m->d_owner && m->d_owner->getTag() == Ast::Thing::T_Class );
    Ast::Class* c = static_cast<Ast::Class*>( m->d_owner );
    LjBcGen v(bc);
    m->accept(&v);
    return false;
}
