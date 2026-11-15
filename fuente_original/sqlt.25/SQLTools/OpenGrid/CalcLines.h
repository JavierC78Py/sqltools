/*
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2004 Aleksey Kochetov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once
#ifndef __CalcLines_h__
#define __CalcLines_h__

#include <string>

// How many display lines a cell's text occupies, capped at lim. Header-only and inline, with no
// state and no MFC. lim <= 1 short-circuits to 1; a trailing '\n' or '\r' does not open a line.

// 22.09.2007 bug fix, compatibility issue with MSVS 2007
inline int calc_lines (const std::string& str, int lim)
{
    int nlines = 1;
    if (nlines < lim)
    {
        char lastchar = 0;
        std::string::const_iterator it = str.begin();

        for (; it != str.end(); ++it)
        {
            lastchar = *it;
            if (lastchar == '\n' && ++nlines == lim)
                break;
        }

        // ignore <CR> after last line
        if (nlines > 1 && it == str.end()
        && (lastchar == '\n' || lastchar == '\r'))
            nlines--;
    }
    return nlines;
}

#endif // __CalcLines_h__
