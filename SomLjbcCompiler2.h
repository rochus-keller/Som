#ifndef SOMLJBCCOMPILER2_H
#define SOMLJBCCOMPILER2_H

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

#include <Som/SomAst.h>
#include <LjTools/LuaJitComposer.h>

namespace Som
{

    class LjbcCompiler2
    {
    public:
        static bool translate( Lua::JitComposer&, Ast::Method* );
        static bool translate( Lua::JitComposer&, Ast::Method*, Ast::Block* );

    private:
        LjbcCompiler2();
    };
}

#endif // SOMLJBCCOMPILER2_H
