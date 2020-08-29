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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <QtDebug>
#include <QElapsedTimer>

#ifdef _WIN32
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif


extern "C"
{

DllExport int Som_isWhiteSpace( const char* str )
{
    const int len = ::strlen(str);
    if( len == 0 )
        return 0;
    for( int i = 0; i < len; i++ )
    {
        if( !::isspace(str[i]) )
            return 0;
    }
    return 1;
}

DllExport int Som_isLetters( const char* str )
{
    const int len = ::strlen(str);
    if( len == 0 )
        return 0;
    for( int i = 0; i < len; i++ )
    {
        if( !::isalpha(str[i]) )
            return 0;
    }
    return 1;
}

DllExport int Som_isDigits( const char* str )
{
    const int len = ::strlen(str);
    if( len == 0 )
        return 0;
    for( int i = 0; i < len; i++ )
    {
        if( !::isdigit(str[i]) )
            return 0;
    }
    return 1;
}

DllExport int Som_usecs()
{
    static QElapsedTimer t;
    if( !t.isValid() )
        t.start();
    return t.nsecsElapsed() / 1000;
}


}
