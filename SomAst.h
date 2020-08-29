#ifndef SOM_AST_H
#define SOM_AST_H

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

#include <QMetaType>
#include <QSharedData>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>

class QTextStream;

namespace Som
{
namespace Ast
{
    template <class T>
    struct Ref : public QExplicitlySharedDataPointer<T>
    {
        Ref(T* t = 0):QExplicitlySharedDataPointer<T>(t) {}
        bool isNull() const { return QExplicitlySharedDataPointer<T>::constData() == 0; }
    };

    struct MsgSend; struct Return; struct ArrayLiteral; struct Variable; struct Class; struct Method;
    struct Block; struct Variable; struct Cascade; struct Assig; struct Char; struct String;
    struct Number; struct Symbol; struct Ident; struct Selector; struct Scope; struct Named; struct Function;

    struct Visitor
    {
        virtual void visit( MsgSend* ) {}
        virtual void visit( Return* ) {}
        virtual void visit( ArrayLiteral* ) {}
        virtual void visit( Variable* ) {}
        virtual void visit( Class* ) {}
        virtual void visit( Method* ) {}
        virtual void visit( Block* ) {}
        virtual void visit( Cascade* ) {}
        virtual void visit( Assig* ) {}
        virtual void visit( Char* ) {}
        virtual void visit( String* ) {}
        virtual void visit( Number* ) {}
        virtual void visit( Symbol* ) {}
        virtual void visit( Ident* ) {}
        virtual void visit( Selector* ) {}
    };

    struct Loc
    {
        quint32 d_pos;
        quint32 d_line;
        quint16 d_col;
        quint16 d_len;
        QString d_source;
        Loc():d_pos(0),d_line(0),d_col(0),d_len(0){}
    };

    struct Thing : public QSharedData
    {
        enum Tag { T_Thing, T_Class, T_Variable, T_Method, T_Block, T_Return, T_Cascade, T_Func,
                   T_MsgSend, T_Assig, T_Array, T_Char, T_String, T_Number, T_Symbol, T_Ident, T_Selector,
                 T_MAX };

        Loc d_loc;
        Thing() {}
        void dump( QTextStream& );
        virtual int getTag() const { return T_Thing; }
        virtual void accept(Visitor* v){}
        virtual quint32 getLen() const { return 0; }
    };

    struct Expression : public Thing
    {
        enum Keyword { None = 0, _nil, _true, _false, _self, _super, _primitive };
        virtual quint8 keyword() const { return 0; }
    };
    typedef QList<Ref<Expression> > ExpList;

    struct Ident : public Expression
    {
        enum Use { Undefined, AssigTarget, MsgReceiver, Rhs, Declaration };

        QByteArray d_ident;
        Named* d_resolved;
        Method* d_inMethod;
        quint8 d_use;
        quint8 d_keyword;

        Ident( const QByteArray& ident, const Loc& pos, Method* f = 0 ):d_ident(ident),d_resolved(0),
            d_use(Undefined),d_inMethod(f),d_keyword(None) { d_loc = pos; }
        int getTag() const { return T_Ident; }
        quint32 getLen() const { return d_ident.size(); }
        void accept(Visitor* v) { v->visit(this); }
        quint8 keyword() const { return d_keyword; }
    };

    struct Symbol : public Expression
    {
        QByteArray d_sym;

        Symbol( const QByteArray& sym, const Loc& pos ):d_sym(sym) { d_loc = pos; }
        int getTag() const { return T_Symbol; }
        quint32 getLen() const { return d_sym.size(); }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Selector : public Expression
    {
        QByteArray d_pattern;
        int getTag() const { return T_Selector; }
        quint32 getLen() const { return d_pattern.size(); }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Number : public Expression
    {
        QByteArray d_num;
        bool d_real;
        Number( const QByteArray& num, bool real, const Loc& pos ):d_num(num),d_real(real) { d_loc = pos; }
        int getTag() const { return T_Number; }
        quint32 getLen() const { return d_num.size(); }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct String : public Expression
    {
        QByteArray d_str;
        String( const QByteArray& str, const Loc& pos ):d_str(str) { d_loc = pos; }
        int getTag() const { return T_String; }
        quint32 getLen() const { return d_str.size() + 2; } // +2 because of ''
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Char : public Expression
    {
        char d_ch;
        Char( char ch, const Loc& pos ):d_ch(ch) { d_loc = pos; }
        int getTag() const { return T_Char; }
        quint32 getLen() const { return 2; } // 2 because of $ prefix
        void accept(Visitor* v) { v->visit(this); }
    };

    struct ArrayLiteral : public Expression
    {
        ExpList d_elements;
        int getTag() const { return T_Array; }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Assig : public Expression
    {
        Ref<Ident> d_lhs; // Ident designates a Variable
        Ref<Expression> d_rhs;
        int getTag() const { return T_Assig; }
        void accept(Visitor* v) { v->visit(this); }
    };

    enum PatternType { NoPattern, UnaryPattern, BinaryPattern, KeywordPattern };

    struct MsgSend : public Expression
    {
        quint8 d_patternType;
        QList< QPair<QByteArray,Loc> > d_pattern; // name, pos
        ExpList d_args;
        Ref<Expression> d_receiver;
        Method* d_inMethod;
        MsgSend():d_patternType(NoPattern),d_inMethod(0){}
        int getTag() const { return T_MsgSend; }
        void accept(Visitor* v) { v->visit(this); }
        QByteArray prettyName(bool withSpace = true) const;
    };

    struct Cascade : public Expression
    {
        // NOTE: all d_calls must point to the same d_receiver Expression!
        QList< Ref<MsgSend> > d_calls;
        int getTag() const { return T_Cascade; }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Return : public Expression
    {
        Ref<Expression> d_what;
        bool d_nonLocal;
        bool d_nonLocalIfInlined;
        Return():d_nonLocal(false),d_nonLocalIfInlined(false){}
        int getTag() const { return T_Return; }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Named : public Thing
    {
        QByteArray d_name;
        Scope* d_owner;
        Named():d_owner(0){}
        virtual bool classLevel() const { return false; }
    };

    struct Scope : public Named
    {
        QHash<const char*,QList<Named*> > d_varNames, d_methodNames;

        QList<Named*> findVars(const QByteArray& name, bool recursive = true ) const;
        QList<Named*> findMeths(const QByteArray& name, bool recursive = true ) const;
        Method* getMethod() const;
        Class* getClass() const;
    };

    struct GlobalScope : public Scope
    {
        QList< Ref<Variable> > d_vars;
    };

    struct Function : public Scope
    {
        QList< Ref<Variable> > d_vars;
        ExpList d_body;
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Func; }
        void addVar(Variable*);
    };

    struct Block : public Expression
    {
        Ref<Function> d_func; // not named
        quint8 d_syntaxLevel, d_inlinedLevel; // 0 is method
        bool d_inline;
        Block();
        int getTag() const { return T_Block; }
        void accept(Visitor* v) { v->visit(this); }
    };

    struct Method : public Function
    {
        quint8 d_patternType;
        quint8 d_classLevel : 1;
        quint8 d_primitive : 1; // specified primitive id or zero
        quint8 d_hasNonLocalReturn : 1;
        quint8 d_hasNonLocalReturnIfInlined : 1;
        QByteArrayList d_pattern; // compact form in d_name
        quint32 d_endPos;
        QByteArray d_category;
        ExpList d_helper;

        Method():d_patternType(NoPattern),d_classLevel(false),d_endPos(0),d_primitive(false),
            d_hasNonLocalReturn(false),d_hasNonLocalReturnIfInlined(false){}
        static QByteArray prettyName(const QByteArrayList& pattern, quint8 kind, bool withSpace = true );
        QByteArray prettyName(bool withSpace = true) const;
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Method; }
        Expression* findByPos( quint32 ) const;
        void accept(Visitor* v) { v->visit(this); }
        bool classLevel() const { return d_classLevel; }
    };
    typedef Ref<Method> MethodRef;

    struct Variable : public Named
    {
        enum { InstanceLevel, ClassLevel, Argument, Temporary, Global };
        quint8 d_kind;
        quint16 d_slot; // to be set by compiler
        Variable():d_kind(InstanceLevel),d_slot(0){}
        int getTag() const { return T_Variable; }
        void accept(Visitor* v) { v->visit(this); }
        bool classLevel() const { return d_kind == ClassLevel; }
    };
    typedef Ref<Variable> VarRef;

    struct Class : public Scope
    {
        QByteArray d_superName;
        QByteArray d_category;
        QByteArray d_comment, d_classComment;

        QList< Ref<Variable> > d_instVars, d_classVars;
        QList< Ref<Method> > d_methods;
        QList< Ref<Class> > d_subs;

        Method* findMethod(const QByteArray& ) const;
        Variable* findVar( const QByteArray& ) const;
        int getTag() const { return T_Class; }
        Class* getSuper() const;
        void addMethod(Method*);
        void addVar(Variable*);
        void accept(Visitor* v) { v->visit(this); }
    };
    typedef Ref<Class> ClassRef;
}
}

Q_DECLARE_METATYPE( Som::Ast::ClassRef )
Q_DECLARE_METATYPE( Som::Ast::MethodRef )
Q_DECLARE_METATYPE( Som::Ast::VarRef )

#endif // SOM_AST_H
