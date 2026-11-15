/* 
    Copyright (C) 2026 Aleksey Kochetov

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

/*
    Language-keyed smart indent strategies. md/editor_smart_indent_plan.md holds the
    design and the staging; md/plsql_formatter_rules.md is the oracle for the levels.
*/

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OESmartIndent_h__
#define __OESmartIndent_h__

namespace OpenEditor
{
    class Storage;

    // Levels are INDENT LEVELS, not columns: the caller multiplies by
    // EditContext::GetIndentSpacing() so TabExpand and the clamped width are honoured.
    struct SmartIndentInfo
    {
        int  levels;      // net indent-level delta for the line the caret is on; may be < 0
        int  parenDepth;  // >0 while the caret sits inside an unclosed '('
        bool inComment;   // caret is inside a continued /* ... */ comment
        bool inString;    // caret is inside a continued '...' or q'[...]' literal
        bool confident;   // false => the caller must fall back to plain eiAuto

        SmartIndentInfo ()
            : levels(0), parenDepth(0), inComment(false), inString(false), confident(false) {}
    };

    // Read-only over the Storage and safe to share: one instance serves every document.
    class SmartIndent
    {
    public:
        // Pass as `col` to read the WHOLE line instead of a prefix of it. Enter parks the
        // caret at column 0 of a split's tail, where the empty prefix says nothing.
        enum { kWholeLine = 0x7FFFFFFF };

        virtual ~SmartIndent () {}

        // Text to the RIGHT of col is never read, so the answer describes the caret
        // position only - unless col is kWholeLine. !confident means fall back to eiAuto.
        virtual SmartIndentInfo Compute (const Storage&, int line, int col) const = 0;

        // Absolute level a line whose first significant token is a dedent token (END,
        // ELSE, ELSIF, EXCEPTION, WHEN, ')') belongs at; -1 when no rule applies.
        virtual int DedentLevel (const Storage&, int line, int col) const = 0;
    };

    // A bounded backward window is tokenized with a local PlSqlParserEx and reduced onto
    // an opener stack, so literals and comments go through the formatter's own lexer.
    class PlSqlSmartIndent : public SmartIndent
    {
    public:
        virtual SmartIndentInfo Compute (const Storage&, int line, int col) const;
        virtual int DedentLevel (const Storage&, int line, int col) const;
    };

};//namespace OpenEditor

#endif//__OESmartIndent_h__
