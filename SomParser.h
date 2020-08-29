#ifndef SOM_PARSER_H
#define SOM_PARSER_H

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
#include <Som/SomLexer.h>

class QIODevice;

namespace Som
{
class Parser
{
public:
    struct Error
    {
        QByteArray d_msg;
        Ast::Loc d_loc;
        Error( const QByteArray& msg, const Ast::Loc& loc ):d_msg(msg),d_loc(loc){}
    };

    explicit Parser(Lexer*);

    bool readFile();
    bool readClass();
    // bool readExpression();

    Ast::Class* getClass() const { return d_curClass.data(); }
    const QList<Error>& getErrs() const { return d_errs; }

    static void convertFile( QIODevice*, const QString& to );

protected:
    bool error( const QByteArray& msg, const Lexer::Token& pos );
    bool readClassExpr();
    Ast::Ref<Ast::Method> readMethod(Ast::Class* c, bool classLevel);

    struct TokStream
    {
        typedef QList<Lexer::Token> Toks;
        Toks d_toks;
        quint32 d_pos;
        TokStream(const Toks& t = Toks(), quint32 pos = 0 ):d_toks(t),d_pos(pos){}
        Lexer::Token next();
        Lexer::Token peek(int la = 1) const;
        bool atEnd() const { return d_pos >= d_toks.size(); }
    };

    bool parseMethodBody(TokStream& );
    bool parseLocals(Ast::Function*, TokStream& );
    Ast::Ref<Ast::Expression> parseExpression(Ast::Function*, TokStream&, quint8 inPattern = 0);
    Ast::Ref<Ast::Expression> simpleExpression(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseBlock(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseArray(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseAssig(Ast::Function*, TokStream&);
    Ast::Ref<Ast::Expression> parseReturn(Ast::Function*, TokStream&);
    bool parseBlockBody(Ast::Function* block, TokStream& );
    bool parseFields( bool classLevel );
private:
    Lexer* d_lex;
    QByteArray primitive, Object;
    QList<Error> d_errs;
    Ast::Ref<Ast::Class> d_curClass;
    Ast::Ref<Ast::Method> d_curMeth;
    int d_blockLevel;
};
}

#endif // SOM_PARSER_H
