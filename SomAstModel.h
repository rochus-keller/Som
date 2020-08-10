#ifndef SOM_AST_MODEL_H
#define SOM_AST_MODEL_H

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

class QIODevice;

namespace Som
{
class Model : public QObject
{
    Q_OBJECT
public:
    typedef QMap<QByteArray, Ast::Ref<Ast::Class> > Classes;
    typedef QHash< const char*, Ast::Ref<Ast::Class> > Classes2;
    typedef QMap<QByteArray, QList<Ast::Class*> > ClassCats;
    typedef QHash<const char*, QList<Ast::Method*> > MethodXref;
    typedef QHash<const char*, QList<Ast::Variable*> > VariableXref;
    typedef QHash<const char*, QList<Ast::Method*> > PrimitiveXref;
    typedef QHash<Ast::Named*, QList<Ast::Ident*> > IdentXref;
    typedef QHash<const char*, QList<Ast::MsgSend*> > PatternXref;

    struct File
    {
        QIODevice* d_dev;
        QString d_path;
        File( QIODevice* d = 0, const QString& n = QString() ):d_dev(d),d_path(n){}
    };

    explicit Model(QObject *parent = 0);

    typedef QList<File> Files;
    bool parse( const Files& );
    void clear();

    const Classes& getClasses() const { return d_classes; }
    const ClassCats& getCats() const { return d_cats; }
    const MethodXref& getMxref() const { return d_mx; }
    const PrimitiveXref& getPxref() const { return d_px; }
    const IdentXref& getIxref() const { return d_ix; }
    const VariableXref& getVxref() const { return d_vx; }
    const PatternXref& getTxref() const { return d_tx; }
    const QStringList& getErrs() const { return d_errs; }
protected:
    void error(const QString& msg, const Ast::Loc& loc );
private:
    class ResolveIdents;
    QStringList d_errs;
    Classes d_classes;
    Classes2 d_classes2;
    ClassCats d_cats;
    MethodXref d_mx;
    VariableXref d_vx; // only global, instance and class vars
    PrimitiveXref d_px;
    IdentXref d_ix;
    PatternXref d_tx;
    QByteArray nil;
    Ast::GlobalScope d_globals;
    QSet<const char*> d_keywords;
};
}

#endif // SOM_AST_MODEL_H
