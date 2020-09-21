#ifndef SOMLjSOM_H
#define SOMLjSOM_H

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

namespace Lua
{
    class Engine2;
}

namespace Som
{
    class LjObjectManager;

    class LjSOM : public QObject
    {
        Q_OBJECT
    public:
        explicit LjSOM(QObject *parent = 0);
        bool load(const QString& file, const QString& paths = QString() );
        bool run(bool useJit = true, const QStringList& extraArgs = QStringList());
        Lua::Engine2* getLua() const { return d_lua; }
        QStringList getLuaFiles() const;
        QByteArrayList getClassNames() const;
        void setGenLua( bool );
        LjObjectManager* getOm() const { return d_om;}
    protected slots:
        void onNotify( int messageType, QByteArray val1, int val2 );
    private:
        Lua::Engine2* d_lua;
        LjObjectManager* d_om;
    };
}

#endif // SOMLjSOM_H
