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

#include "SomLjbcCompiler2.h"
#include "SomLuaTranspiler.h"
#include "SomLexer.h"
#include <QFileInfo>
#include <QtDebug>
using namespace Som;
using namespace Som::Ast;

/*
 NOTE: param arrays are used instead of param and local slots. A param array has values at the indices corresponding
 to the param and local slots, i.e. 0 ... Variable::d_vars.size()-1. The param array of the calling function is
 at index Variable::d_vars.size(); the param array of the calling function of the calling function is at
 index Variable::d_vars.size()+1, and so on.
*/
struct LjBcGen2: public Visitor
{
    Lua::JitComposer& bc;

    LjBcGen2(Lua::JitComposer& _bc, Method* m, Block* b):bc(_bc),meth(m), block(b), ctx(m,b) {}

    struct NoMoreFreeSlots {};

    struct Ctx
    {
        Function* fun;
        Lua::JitComposer::SlotPool pool;
        typedef QHash<Function*,quint16> Upvals;
        Upvals upvals;
        Ctx(Method* m, Block* b)
        {
            if( b )
                fun = b->d_func.data();
            else
                fun = m;
        }

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
        quint16 getUpvalNr(Function* f)
        {
            Q_ASSERT( f->d_slotValid );
            Upvals::const_iterator i = upvals.find(f);
            if( i != upvals.end() )
                return i.value();
            const int nr = upvals.size();
            upvals[ f ] = nr;
            return nr;
        }
    };
    Ctx ctx;
    QList<quint8> slotStack;
    Method* meth;
    Block* block;

    bool inline error( const Loc& l, const QString& msg )
    {
        qCritical() << l.d_source << ":" << l.d_line << ":" << l.d_col << ":" << msg;
        // err->error(Errors::Semantics, mod->d_file, l.d_row, l.d_col, msg );
        return false;
    }

    Method* owningMethod() const
    {
        return meth;
    }

    static inline int paramTableSize( Function* f )
    {
        return f->d_vars.size() + 1 + f->d_inlineds.size();
    }

    Lua::JitComposer::UpvalList getUpvals()
    {
        Lua::JitComposer::UpvalList uvl(ctx.upvals.size());
        Ctx::Upvals::const_iterator i;
        for( i = ctx.upvals.begin(); i != ctx.upvals.end(); ++i )
        {
            Lua::JitComposer::Upval u;
            u.d_uv = i.key()->d_slot;
            u.d_isLocal = true;
            u.d_isRo = true; // only read function
            // u.d_name = QByteArray::number(i.key()->d_slot); // TODO
            uvl[i.value()] = u;
        }
        return uvl;
    }

    void createParamArray( Function* f )
    {
        ctx.buySlots( 1 ); // self

        const int params = ctx.buySlots( f->getParamCount() );
        const int paramArray = ctx.buySlots(1);
        bc.TNEW( paramArray, paramTableSize(f), 0, f->d_loc.packed() );
        bc.TSETi( 0, paramArray, 0, f->d_loc.packed()); // copy self
        for( int i = 0; i < f->getParamCount(); i++ )
            bc.TSETi(i+1,paramArray,i+1,f->d_loc.packed()); // copy params to paramArray

        bc.MOV(0,paramArray,f->d_loc.packed()); // param array is now in slot 0

        ctx.sellSlots(paramArray);
        ctx.sellSlots(params); // params are now free again to be used as temporaries
    }

    virtual void visit( Method* m )
    {
        Q_ASSERT( false );
    }
    void emitMethod()
    {
        Q_ASSERT( meth != 0 && block == 0 );
        Method* m = meth;

        const int id = bc.openFunction(m->getParamCount() + 1, // +1 because of self which is always slot(0)
                                       m->d_name,
                        m->d_loc.packed(), m->d_end.packed() );
        Q_ASSERT( id >= 0 );

        for( int i = 0; i < m->d_vars.size(); i++ )
            Q_ASSERT( m->d_vars[i]->d_slot == i+1 ); // this was preset in ObjectManager, don't touch it here
        for( int i = 0; i < m->d_inlineds.size(); i++ )
            m->d_inlineds[i]->d_slot = i+m->d_vars.size()+1;

        // TODO: only do this when method is upvalue source; use slots otherwise
        createParamArray( m );
        // slot 0 is now the param array and the other param slots are freed

        for( int i = 0; i < m->d_body.size(); i++ )
        {
            m->d_body[i]->accept( this );
            ctx.sellSlots(slotStack.back());
            slotStack.pop_back(); // we don't use the results
        }

        if( m->d_body.isEmpty() || m->d_body.last()->getTag() != Thing::T_Return )
        {
            const int tmp = ctx.buySlots(1);
            bc.TGETi( tmp, 0, 0, m->d_end.packed());
            bc.RET( tmp, 1, m->d_end.packed() ); // return self
            ctx.sellSlots(tmp);
        }

        bc.setUpvals( getUpvals() );
        bc.closeFunction(ctx.pool.d_frameSize);

        bc.FNEW( m->d_slot, id, m->d_loc.packed() );
    }

    void inlineBlock( Block* b )
    {
        Q_ASSERT( b->d_func->d_inline );

        for( int i = 0; i < b->d_func->d_body.size(); i++ )
        {
            b->d_func->d_body[i]->accept( this );
            if( i < b->d_func->d_body.size() - 1 )
            {
                // the last expression in the block body is the one returned to the caller
                ctx.sellSlots(slotStack.back());
                slotStack.pop_back();
            }
        }

        if( b->d_func->d_body.isEmpty() )
        {
            const int res = ctx.buySlots(1);
            slotStack.push_back(res);
            bc.KNIL(res,1,b->d_loc.packed()); // empty blocks return nil (TODO: check if true)
        }
    }

    void emitReceiver( MsgSend* s, bool doInline = true )
    {
        if( s->d_receiver->keyword() == Expression::_super )
        {
            const int slot = ctx.buySlots(1);
            bc.GGET(slot, s->d_inMethod->d_owner->d_name, s->d_loc.packed() );
            if( !s->d_inMethod->d_classLevel )
                bc.TGET(slot,slot,"_class",s->d_loc.packed());
            bc.TGET(slot,slot,"_super",s->d_loc.packed());
            slotStack.push_back(slot);
        }else if( doInline && s->d_receiver->getTag() == Thing::T_Block )
        {
            inlineBlock( static_cast<Block*>(s->d_receiver.data()));
        }else
            s->d_receiver->accept(this);
    }

    void inlineIfBlock( Expression* e )
    {
        if( e->getTag() == Thing::T_Block )
            inlineBlock( static_cast<Block*>(e));
        else
            e->accept(this);
    }

    void inlineIf( MsgSend* s )
    {
        emitReceiver(s);
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
        ctx.sellSlots(slotStack.back());
        slotStack.pop_back();
        bc.JMP(ctx.pool.d_frameSize,0,s->d_loc.packed());
        const int label = bc.getCurPc();

        Q_ASSERT( !s->d_args.isEmpty() );
        inlineIfBlock(s->d_args.first().data());
        // the result is in slotStack.back()

        if( s->d_flowControl == IfElse )
        {
            // before emitting the else part take care that the if part jumps over it
            bc.JMP(ctx.pool.d_frameSize,0,s->d_loc.packed());
            const int label2 = bc.getCurPc();

            bc.patch(label);
            const int res = slotStack.back();
            Q_ASSERT( s->d_args.size() == 2 );
            inlineIfBlock(s->d_args.last().data());
            // the result is in slotStack.back()
            bc.MOV(res,slotStack.back(),s->d_loc.packed());
            ctx.sellSlots(slotStack.back());
            slotStack.pop_back();

            bc.patch(label2);
        }else
            bc.patch(label);

    }

    void inlineWhile( MsgSend* s )
    {
        bc.LOOP( ctx.pool.d_frameSize, 0, s->d_loc.packed() ); // while true do
        const quint32 startLoop = bc.getCurPc();
        const int res = ctx.buySlots(1);

        emitReceiver(s);
        // the result is in slotStack.back()
        switch( s->d_flowControl )
        {
        case WhileTrue:
            bc.ISF(slotStack.back(),s->d_loc.packed());
            break;
        case WhileFalse:
            bc.IST(slotStack.back(),s->d_loc.packed());
            break;
        default:
            Q_ASSERT(false);
            break;
        }
        ctx.sellSlots(slotStack.back());
        slotStack.pop_back();
        bc.JMP(ctx.pool.d_frameSize,0,s->d_loc.packed());
        const int label = bc.getCurPc();

        Q_ASSERT( !s->d_args.isEmpty() );
        inlineIfBlock(s->d_args.first().data());
        // the result is in slotStack.back()
        bc.MOV( res, slotStack.back(), s->d_loc.packed());
        ctx.sellSlots(slotStack.back());
        slotStack.pop_back();

        bc.jumpToLoop( startLoop, ctx.pool.d_frameSize, s->d_loc.packed() ); // loop to start

        bc.patch( label );

        slotStack.push_back(res);
    }

    static inline QString printLoc(const Loc& loc )
    {
        return QString("%1:%2:%3").arg(QFileInfo(loc.d_source).baseName()).arg(loc.d_line).arg(loc.d_col);
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
        case WhileTrue:
        case WhileFalse:
            inlineWhile(s);
            return;
        }

        emitReceiver(s,false);
        // the result is in slotStack.back()
        const int args = ctx.buySlots( s->d_args.size() + 2, true );
        bc.TGET( args, slotStack.back(),
                 LuaTranspiler::map(s->prettyName(false),s->d_patternType), s->d_loc.packed() );
        if( s->d_receiver->keyword() == Expression::_super )
        {
            int _self = selfToSlot( s->d_loc );
            bc.MOV( args+1, _self, s->d_loc.packed() ); // use self for calls to super
            ctx.sellSlots(_self);
        }else
            bc.MOV( args+1, slotStack.back(), s->d_loc.packed() );
        ctx.sellSlots(slotStack.back());
        slotStack.pop_back();
        for( int i = 0; i < s->d_args.size(); i++ )
        {
            s->d_args[i]->accept(this);
            // the result is in slotStack.back()
            bc.MOV(args+2+i, slotStack.back(), s->d_loc.packed() );
            ctx.sellSlots(slotStack.back());
            slotStack.pop_back();
        }
        bc.CALL(args,2,s->d_args.size() + 1, s->d_loc.packed() );

        if( block || !s->d_inMethod->d_hasNonLocalReturnIfInlined )
        {
            // if there is a second return value which is not nil we directly return from blocks
            // and methods where no local return block was defined and just pass through the second value
            bc.ISF( args+1, s->d_loc.packed() );
            bc.JMP(ctx.pool.d_frameSize,1, s->d_loc.packed() );
            const int label = bc.getCurPc();
            bc.RET(args, 2,s->d_loc.packed());
            bc.patch( label );
        }else // if not in block and s->d_inMethod->d_hasNonLocalReturn
        {
            // here we are on method level; check whether it is the method in which the no local return was defined.
            bc.ISF( args+1, s->d_loc.packed() );
            bc.JMP(ctx.pool.d_frameSize,0, s->d_loc.packed() );
            const int label = bc.getCurPc();
            const int tmp = ctx.buySlots(1);
            bc.KSET( tmp, (ptrdiff_t)owningMethod(), s->d_loc.packed() );
            bc.ISEQ( args+1, tmp, s->d_loc.packed() );
            bc.JMP(ctx.pool.d_frameSize,1, s->d_loc.packed() );
            const int label2 = bc.getCurPc();
            bc.RET(args, 2,s->d_loc.packed()); // return with second argument, because this is not the method where
                                               // the non-local return was defined in
            bc.patch( label2 );
            bc.RET(args, 1,s->d_loc.packed()); // return with no second argument
            bc.patch(label);
        }

        // otherwise just use the return value as the expression result
        const int res = ctx.buySlots(1);
        slotStack.push_back(res);
        bc.MOV(res,args,s->d_loc.packed());
        ctx.sellSlots(args, s->d_args.size() + 2);
    }

    virtual void visit( Return* r )
    {
        r->d_what->accept(this);
        // the result is in slotStack.back()
        if( block && r->d_nonLocalIfInlined )
        {
            // Block Level
            // an explicit return in a block is always a non-local return
            const int slot = ctx.buySlots(2);
            bc.MOV(slot,slotStack.back(),r->d_loc.packed());
            // use the AST address of the Method as id (RISK: 64 bit architectures)
            bc.KSET(slot+1, (ptrdiff_t)owningMethod(), r->d_loc.packed() );
            bc.RET( slot, 2, r->d_loc.packed() );
            ctx.sellSlots(slot,2);
        }else
        {
            // Method Level
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
                int _self = selfToSlot(  a->d_loc );

                const int index = lhs->d_slot + 1; // object field indices are one-based
                if( index <= 255 )
                    bc.TSETi( slotStack.back(), _self, index, a->d_loc.packed() ); // self == slot(0)
                else
                {
                    int tmp = ctx.buySlots(1);
                    bc.KSET( tmp, quint32(index), a->d_loc.packed() );
                    bc.TSET( slotStack.back(), _self, tmp, a->d_loc.packed() ); // self == slot(0)
                    ctx.sellSlots(tmp);
                }

                ctx.sellSlots(_self);
            }
            break;
        case Variable::Argument:
        case Variable::Temporary:
            // either local or outer value
            if( !( block == 0 ) && lhs->d_inlinedOwner != block->d_func.data() )
            {
                const int tmp = ctx.buySlots(1);
                getOuterParamTable( tmp, lhs, a->d_loc );
                Q_ASSERT( lhs->d_slot <= 255 );
                bc.TSETi( slotStack.back(), tmp, lhs->d_slot, a->d_loc.packed() );
                ctx.sellSlots(tmp);
            }else
            {
                bc.TSETi( slotStack.back(), 0, lhs->d_slot, a->d_loc.packed() );
            }
            break;
        case Variable::Global:
            error( a->d_loc, "cannot assign to global variables" );
            break;
        }
        // the rhs result is still in slotStack.back() and stays there as the result of the assignment expression
    }

    void emitBlock()
    {
        Q_ASSERT( meth != 0 && block != 0 );
        Block* b = block;

        const int id = bc.openFunction( b->d_func->getParamCount() + 1, // +1 because of self which is always slot(0)
                        b->d_loc.d_source.toUtf8(), b->d_func->d_loc.packed(), b->d_func->d_end.packed() );
        Q_ASSERT( id >= 0 );

        for( int i = 0; i < b->d_func->d_vars.size(); i++ )
            Q_ASSERT( b->d_func->d_vars[i]->d_slot == i+1 ); // this was preset in ObjectManager, don't touch it here
        for( int i = 0; i < b->d_func->d_inlineds.size(); i++ )
            b->d_func->d_inlineds[i]->d_slot = i+b->d_func->d_vars.size()+1;

        // TODO: only do this when block is upvalue source; use slots otherwise
        createParamArray( b->d_func.data() );
        // slot 0 is now the param array and the other param slots are freed

        for( int i = 0; i < b->d_func->d_body.size(); i++ )
        {
            b->d_func->d_body[i]->accept( this );
            if( i == b->d_func->d_body.size() - 1 && b->d_func->d_body.last()->getTag() != Thing::T_Return )
            {
                // if last return is missing, add it and return last expression result
                bc.RET( slotStack.back(),1,b->d_func->d_end.packed());
            }
            ctx.sellSlots(slotStack.back());
            slotStack.pop_back();
        }

        if( b->d_func->d_body.isEmpty() )
        {
            const int tmp = ctx.buySlots(1);
            bc.KNIL(tmp,1,b->d_loc.packed());
            bc.RET(tmp,1,b->d_func->d_end.packed()); // return nil (TODO: check)
            ctx.sellSlots(tmp);
        }

        bc.setUpvals( getUpvals() ); // only for Block functions
        bc.closeFunction(ctx.pool.d_frameSize);

        bc.FNEW( b->d_func->d_slot, id, b->d_loc.packed() );
    }

    virtual void visit( Block* blockNode )
    {
        // assumes that the block function was already allocated on module level

        const int blockInst = ctx.buySlots(1);
        slotStack.push_back(blockInst);
        // return a table which carries the function

        const int blockFunc = ctx.buySlots(1);
        const int id = ctx.getUpvalNr(blockNode->d_func.data());
        bc.UGET( blockFunc, id, blockNode->d_loc.packed() );

        // here we create the Block instance and associate it with the pre-existing block function.
        bc.TNEW( blockInst, blockNode->d_func->d_inlinedLevel, 0, blockNode->d_loc.packed() );
        bc.TSET( blockFunc, blockInst,"_f", blockNode->d_loc.packed());

        // Block instance is also used to carry environment param tables
        // layout: index0: outer(inlinelevel0), index1: outer(inlinelevel1), ...

        // slot 0 is the param array of this function (method or block) where this Block literal is spotted.
        if( ( block == 0 ) )
        {
            Q_ASSERT( blockNode->d_func->d_inlinedLevel == 1 );
            // we're on method level
            // param array of this method is in slot 0
            bc.TSETi( 0, blockInst, 0, blockNode->d_loc.packed() );
        }else
        {
            Q_ASSERT( blockNode->d_func->d_inlinedLevel > 1 );
            // we're on block level
            // block instance is at index 0 or the param array in slot 0
            const int outerBlockInst = ctx.buySlots(1);
            bc.TGETi( outerBlockInst, 0, 0, blockNode->d_loc.packed() );
            const int tmp = ctx.buySlots(1);
            for( int i = 0; i < blockNode->d_func->d_inlinedLevel - 1; i++ )
            {
                // copy the remaining outer param tables
                bc.TGETi(tmp, outerBlockInst, i, blockNode->d_loc.packed() );
                bc.TSETi( tmp, blockInst, i, blockNode->d_loc.packed() );
            }
            // param array of this block is in slot 0
            bc.TSETi( 0, blockInst, blockNode->d_func->d_inlinedLevel - 1, blockNode->d_loc.packed() );

            ctx.sellSlots(tmp);
            ctx.sellSlots(outerBlockInst);
        }

        ctx.sellSlots(blockFunc);

        emitSetmetatable( blockInst, "Block", blockNode->d_loc );
    }

    virtual void visit( ArrayLiteral* a )
    {
        const int res = ctx.buySlots(1);
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
                int tmp = ctx.buySlots(1);
                bc.KSET( tmp, quint32(i+1), a->d_loc.packed() );
                bc.TSET( slotStack.back(), 0, tmp, a->d_loc.packed() );
                ctx.sellSlots(tmp);
            }
            ctx.sellSlots(slotStack.back());
            slotStack.pop_back();
        }
    }

    void emitSetmetatable( int t, const QByteArray& cls, const Loc& loc )
    {
        const int args = ctx.buySlots(3,true);
        bc.GGET(args,"setmetatable",loc.packed() );
        bc.MOV(args+1,t,loc.packed());
        bc.GGET(args+2,cls,loc.packed());
        bc.TGET(args+2,args+2,"_class",loc.packed());
        bc.CALL(args,0,2,loc.packed());
        ctx.sellSlots(args,3);
    }

    void emitString( const QByteArray& string, const QByteArray& cls, const Loc& loc )
    {
        const int str = ctx.buySlots(1);
        bc.KSET(str, LuaTranspiler::escape(string), loc.packed() );
        const int res = ctx.buySlots(1);
        slotStack.push_back(res);
        bc.TNEW(res,0,0,loc.packed());
        bc.TSET(str,res,"_str",loc.packed());
        ctx.sellSlots(str);
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
        const int res = ctx.buySlots(1);
        slotStack.push_back(res);
        if( n->d_real )
        {
            const int dbl = ctx.buySlots(1);
            bool ok;
            bc.KSET(dbl, n->toNumber(&ok), n->d_loc.packed() );
            if( !ok )
                error(n->d_loc, QString("invalid real %1").arg(n->d_num.constData()) );
            bc.TNEW(res,0,0,n->d_loc.packed());
            bc.TSET(dbl,res,"_dbl",n->d_loc.packed());
            ctx.sellSlots(dbl);
            emitSetmetatable( res, "Double", n->d_loc );
        }else
        {
            bool ok;
            bc.KSET( res, n->toNumber(&ok), n->d_loc.packed() );
            if( !ok )
                error(n->d_loc, QString("invalid integer %1").arg(n->d_num.constData()) );
        }
    }

    int selfToSlot( const Loc& loc )
    {
        const int self = ctx.buySlots(1);
        // slot 0 is parameter array for both methods and block functions
        bc.TGETi( self, 0, 0, loc.packed() );
        if( ( block == 0 ) )
        {
            // we are on method level; self is already propperly fetched
        }else
        {
            // we are on block level; self currently points to the current block instance
            // self is also used as a temporary here
            // the params table of the method level is at index 0 of the block instance
            bc.TGETi( self, self, 0, loc.packed() );
            // the method level self is at index 0 of the method params table
            bc.TGETi( self, self, 0, loc.packed() );
        }
        return self;
    }

    void getOuterParamTable( quint8 to, Variable* v, const Loc& loc )
    {
        Q_ASSERT( !( block == 0 ) && v->d_inlinedOwner != block->d_func.data() );
        // we're on block level

        bc.TGETi( to, 0, 0, loc.packed() );
        // "to" now contains the Block instance

        bc.TGETi( to, to, v->d_inlinedOwner->d_inlinedLevel, loc.packed() );
        // to now contains the param table
    }

    virtual void visit( Ident* id )
    {
        const int res = ctx.buySlots(1);
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
                            const int _self = selfToSlot( id->d_loc );

                            const int index = v->d_slot + 1; // object field indices are one-based
                            if( index <= 255 )
                                bc.TGETi( res, _self, index, id->d_loc.packed() );
                            else
                            {
                                int tmp = ctx.buySlots(1);
                                bc.KSET( tmp, quint16(index), id->d_loc.packed() );
                                bc.TGET( res, _self, tmp, id->d_loc.packed() );
                                ctx.sellSlots(tmp);
                            }

                            ctx.sellSlots(_self);
                        }
                        break;
                    case Variable::Argument:
                    case Variable::Temporary:
                        // either local or outer value
                        if( !( block == 0 ) && v->d_inlinedOwner != block->d_func.data() )
                        {
                            getOuterParamTable( res, v, id->d_loc );
                            Q_ASSERT( v->d_slot <= 255 );
                            bc.TGETi( res, res, v->d_slot, id->d_loc.packed() );
                        }else
                        {
                            bc.TGETi( res, 0, v->d_slot, id->d_loc.packed() );
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
                Q_ASSERT( block == 0 );
                bc.TGETi( res, 0, 0, id->d_loc.packed() );
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

bool LjbcCompiler2::translate(Lua::JitComposer& bc, Ast::Method* m)
{
    Q_ASSERT( m && m->d_owner && m->d_owner->getTag() == Ast::Thing::T_Class );
    Ast::Class* c = static_cast<Ast::Class*>( m->d_owner );
    LjBcGen2 gen(bc,m, 0);
    gen.emitMethod();
    return false;
}

bool LjbcCompiler2::translate(Lua::JitComposer& bc, Method* m, Block* b)
{
    Q_ASSERT( m && m->d_owner && m->d_owner->getTag() == Ast::Thing::T_Class );
    LjBcGen2 gen(bc, m, b);
    gen.emitBlock();
    return true;
}
