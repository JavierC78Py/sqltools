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

#include "stdafx.h"
#include "OpenEditor/OESmartIndent.h"
#include "OpenEditor/OEStorage.h"
#include "OpenEditor/OEPlsSqlParser.h"

#include <vector>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{
    using namespace std;
    using namespace Common::PlsSql;

    namespace
    {
        // Both caps are generous because the backward scan stops early at a lone '/',
        // the compilation-unit boundary PlSqlParserEx reports as etEOS.
        const int kMaxScanLines = 200;
        const int kMaxScanChars = 64 * 1024;

        // Three levels per frame: a frame's own keyword lines and its body lines are not
        // at the same indent, and bodyLevel moves as the frame's sections are entered.
        enum EFrameKind
        {
            fkBlock,
            fkDecl,       // DECLARE .. BEGIN
            fkHeader,     // IS/AS ending a line; closed by the BEGIN or END that follows
            fkInlineDef,  // IS/AS ending a CURSOR/TYPE/SUBTYPE line; closed by ';'
            fkIf,
            fkLoop,
            fkCaseStmt,   // CASE <selector> WHEN .. END CASE
            fkCaseExpr,   // CASE used as a value
            fkHandlers,   // EXCEPTION .. WHEN .. THEN .. END
            fkParen
        };

        struct Frame
        {
            EFrameKind kind;
            int  parentLevel;
            int  headerLevel;
            int  bodyLevel;
            bool pendingThen;   // IF/ELSIF seen, its THEN not yet

            Frame (EFrameKind k, int parent, int header, int body)
                : kind(k), parentLevel(parent), headerLevel(header)
                , bodyLevel(body), pendingThen(false) {}
        };

        typedef std::vector<Frame> FrameStack;

        // The SQL*Plus "run what you have compiled" terminator, not a division
        // operator. Needed here because the scan must decide its extent before tokenizing.
        bool isLoneSlash (const wchar_t* str, int len)
        {
            int i = 0;
            while (i < len && iswspace(str[i])) i++;
            if (i >= len || str[i] != '/') return false;
            i++;
            while (i < len && iswspace(str[i])) i++;
            return i == len;
        }

        // Tokens that carry no indent meaning of their own.
        bool isFiller (EToken type)
        {
            return type == etWHITESPACE || type == etCOMMENT
                || type == etQUOTED_STRING || type == etDOUBLE_QUOTED_STRING
                || type == etNUMBER || type == etUNKNOWN || type == etEOL
                || type == etEOF;
        }

        // The lexer files '+' under etUNKNOWN, the bucket identifiers land in too, so only
        // its text tells an operator from a name; callers fold it into etMINUS.
        bool isPlus (const Token& tk)
        {
            return tk.token == etUNKNOWN
                && tk.value.length() == 1 && tk.value[0] == L'+';
        }

        // Tokens an expression is left unfinished by, or a continuation starts with: the formatter
        // breaks BEFORE them (sec.8/9.1). AND/OR: sec.9.2 and plan §6; BETWEEN: plan §10 lim. 8.
        bool isContinuationToken (EToken type)
        {
            switch (type)
            {
            case etCOMMA:
            case etCONCAT_OP:                                        // ||
            case etMINUS: case etSTAR: case etSLASH: case etEXPONENT:
            case etEQUAL: case etNOT_EQUAL:
            case etLESS:  case etGREATER:
            case etLESS_EQUAL: case etGREATER_EQUAL:
            case etAND:   case etOR:
                return true;
            default:
                return false;
            }
        }

        // Tokens that can follow CASE and still leave it a STATEMENT. Combined with
        // "CASE is the first significant token of its line" this separates the two forms.
        bool opensCaseStatement (EToken prev)
        {
            switch (prev)
            {
            case etNONE: case etSEMICOLON: case etEOS: case etBEGIN:
            case etTHEN: case etELSE: case etELSIF: case etLOOP:
            case etEXCEPTION: case etDECLARE:
                return true;
            default:
                return false;
            }
        }
        // Reduces each token onto the opener stack as it arrives: GetTokens() returns its vector by
        // value, a deep copy on a keystroke path. AtStatementStart() stays false - SQL*Plus only.
        class SmartIndentAnalyser : public SyntaxAnalyser
        {
        public:
            SmartIndentAnalyser () { Clear(); }

            virtual void Clear ()
            {
                m_frames.clear();
                m_prevSignificant = etNONE;
                m_stmtStart       = etNONE;
                m_pendingHeader   = false;
                m_endJustSeen     = false;
                m_lastPoppedKind  = fkBlock;
                m_lineLast        = etNONE;
                m_prevLineLast    = etNONE;
                resetLine(0);
            }

            // Called by scanWindow before each PutLine, with the level in force.
            void resetLine (int levelBefore)
            {
                m_levelBeforeLine = levelBefore;
                m_lineLevel       = levelBefore;
                m_lineLevelSet    = false;
                m_lineWasDedent   = false;
                m_lineHadToken    = false;
                // A continuation hangs off the line ABOVE's last token, which must outlive
                // that line; m_prevSignificant skips fillers, so it cannot report it.
                m_prevLineLast    = m_lineLast;
                m_lineLast        = etNONE;
            }

            int  currentLevel  () const { return m_frames.empty() ? 0 : m_frames.back().bodyLevel; }
            int  lineLevel     () const { return m_lineLevelSet ? m_lineLevel : m_levelBeforeLine; }
            bool lineWasDedent () const { return m_lineWasDedent; }

            int parenDepth () const
            {
                int depth = 0;
                for (size_t i = 0; i < m_frames.size(); i++)
                    if (m_frames[i].kind == fkParen) depth++;
                return depth;
            }

            // True when the line above ended mid-expression, so this one sits one level
            // deeper. fkParen already carries that indent, hence the parenDepth() guard.
            bool continuesExpression () const
            {
                return parenDepth() == 0 && isContinuationToken(m_prevLineLast);
            }

            virtual void PutToken (const Token& tk)
            {
                const EToken type = tk.token;

                if (type == etEOL)
                {
                    // An IS/AS opens a frame only when it ENDS its line.
                    commitPendingHeader();
                    // m_stmtStart names the statement an IS/AS belongs to, so it must not
                    // outlive its line or 'declare' would still own the next line's cursor.
                    m_stmtStart     = etNONE;
                    m_endJustSeen   = false;
                    return;
                }

                if (type == etWHITESPACE || type == etEOF)
                    return;

                // '+' is an etUNKNOWN the analyser has to read as an operator; see isPlus.
                const EToken head = isPlus(tk) ? etMINUS : type;

                // Decided by the first significant token BEFORE it is reduced: 'end'
                // belongs to the level of the line that opened it, not to the body level.
                if (!m_lineLevelSet)
                {
                    m_lineLevel     = levelForLineStart(head);
                    m_lineLevelSet  = true;
                    m_lineWasDedent = m_lineLevel < m_levelBeforeLine;
                }

                const bool firstOnLine = !m_lineHadToken;
                m_lineHadToken = true;

                if (type != etCOMMENT)
                    m_lineLast = head;

                if (type == etEOS)
                {
                    // A compilation-unit boundary: nothing above it can matter.
                    m_frames.clear();
                }
                else if (type != etCOMMENT)
                {
                    reduce(type, firstOnLine);
                }

                if (!isFiller(type))
                {
                    if (type == etSEMICOLON || type == etEOS)
                        m_stmtStart = etNONE;
                    else if (m_stmtStart == etNONE)
                        m_stmtStart = type;

                    m_prevSignificant = type;
                }
            }

        private:
            int levelForLineStart (EToken type) const
            {
                const int raw = currentLevel();

                if (type == etRIGHT_ROUND_BRACKET)
                {
                    // ')' belongs to the level of the line its '(' was on.
                    for (int i = (int)m_frames.size() - 1; i >= 0; i--)
                        if (m_frames[i].kind == fkParen)
                            return m_frames[i].parentLevel;
                    return raw;
                }

                // plsql_formatter_rules.md sec.8 and sec.9.1 break BEFORE the operator,
                // so a wrapped expression's continuation is the line that STARTS with it.
                if (isContinuationToken(type) && parenDepth() == 0)
                    return raw + 1;

                if (m_frames.empty())
                    return continuesExpression() ? raw + 1 : raw;

                const Frame& top = m_frames.back();

                switch (type)
                {
                case etEND:
                    // 'end' always sits at the level of the line that opened its frame
                    return top.parentLevel;
                case etEXCEPTION:
                    return (top.kind == fkBlock) ? top.parentLevel : raw;
                case etELSE:
                case etELSIF:
                    return (top.kind == fkIf || top.kind == fkCaseStmt) ? top.headerLevel : raw;
                case etWHEN:
                    // EXIT WHEN is an ordinary loop statement, not a branch header
                    if (m_prevSignificant == etEXIT) return raw;
                    return (top.kind == fkCaseStmt || top.kind == fkHandlers) ? top.headerLevel : raw;
                case etBEGIN:
                    return (top.kind == fkDecl || top.kind == fkHeader) ? top.parentLevel : raw;
                default:
                    // The other spelling of a wrap: the operator TRAILS the line above,
                    // which is what typing 'x := a +' and pressing Enter leaves behind.
                    return continuesExpression() ? raw + 1 : raw;
                }
            }

            void reduce (EToken type, bool firstOnLine)
            {
                // Any token after IS/AS on the same line makes the definition inline.
                m_pendingHeader = false;

                if (m_endJustSeen)
                {
                    m_endJustSeen = false;

                    // 'end if' / 'end loop' / 'end case' would otherwise read as openers.
                    if (type == etIF || type == etLOOP || type == etCASE)
                    {
                        reconcileEnd(type == etIF   ? fkIf
                                   : type == etLOOP ? fkLoop : fkCaseStmt);
                        return;
                    }
                }

                switch (type)
                {
                case etDECLARE:
                    pushFrame(fkDecl, 0);
                    break;

                case etBEGIN:
                    // BEGIN closes the DECLARE/IS section and opens the body at its level.
                    if (!m_frames.empty()
                        && (m_frames.back().kind == fkDecl || m_frames.back().kind == fkHeader))
                        popFrame();
                    pushFrame(fkBlock, 0);
                    break;

                case etEXCEPTION:
                    // EXCEPTION replaces the block's body section: handlers sit one level
                    // below the block keyword, their bodies one below that.
                    if (!m_frames.empty() && m_frames.back().kind == fkBlock)
                    {
                        const int p = m_frames.back().parentLevel;
                        popFrame();
                        m_frames.push_back(Frame(fkHandlers, p, p + 1, p + 1));
                    }
                    break;

                case etEND:
                    popFrame();
                    m_endJustSeen = true;
                    break;

                case etIF:
                    {
                        const int p = currentLevel();
                        Frame frame(fkIf, p, p, p);   // the body only opens at THEN
                        frame.pendingThen = true;
                        m_frames.push_back(frame);
                    }
                    break;

                case etELSIF:
                    if (!m_frames.empty() && m_frames.back().kind == fkIf)
                    {
                        m_frames.back().bodyLevel   = m_frames.back().headerLevel;
                        m_frames.back().pendingThen = true;
                    }
                    break;

                case etELSE:
                    if (!m_frames.empty() && m_frames.back().kind == fkIf)
                    {
                        m_frames.back().bodyLevel   = m_frames.back().headerLevel + 1;
                        m_frames.back().pendingThen = false;
                    }
                    else if (!m_frames.empty() && m_frames.back().kind == fkCaseStmt)
                        m_frames.back().bodyLevel = m_frames.back().headerLevel + 1;
                    break;

                case etTHEN:
                    if (!m_frames.empty())
                    {
                        Frame& top = m_frames.back();

                        if (top.kind == fkIf)
                        {
                            if (top.pendingThen)
                            {
                                top.pendingThen = false;
                                top.bodyLevel   = top.headerLevel + 1;
                            }
                        }
                        else if (top.kind == fkCaseStmt || top.kind == fkHandlers)
                            top.bodyLevel = top.headerLevel + 1;
                    }
                    break;

                case etWHEN:
                    if (m_prevSignificant == etEXIT) break;   // EXIT WHEN
                    if (!m_frames.empty())
                    {
                        Frame& top = m_frames.back();
                        // step back out of the previous branch's body
                        if (top.kind == fkCaseStmt || top.kind == fkHandlers)
                            top.bodyLevel = top.headerLevel;
                    }
                    break;

                case etLOOP:
                    pushFrame(fkLoop, 0);
                    break;

                case etCASE:
                    {
                        const bool stmt = firstOnLine && opensCaseStatement(m_prevSignificant);
                        // A statement indents its WHEN branches one level below CASE and
                        // their bodies one below that; an expression indents neither.
                        pushFrame(stmt ? fkCaseStmt : fkCaseExpr, stmt ? 1 : 0);
                    }
                    break;

                case etLEFT_ROUND_BRACKET:
                    pushFrame(fkParen, 0);
                    break;

                case etRIGHT_ROUND_BRACKET:
                    popToParen();
                    break;

                case etSEMICOLON:
                    // Must NOT close an fkHeader: a package spec is 'as' followed by many
                    // ';'-terminated members.
                    if (!m_frames.empty() && m_frames.back().kind == fkInlineDef)
                        popFrame();
                    break;

                case etIS:
                case etAS:
                    m_pendingHeader = true;
                    break;

                default:
                    break;
                }
            }

            void pushFrame (EFrameKind kind, int headerOffset)
            {
                const int p = currentLevel();
                m_frames.push_back(Frame(kind, p, p + headerOffset, p + 1));
            }

            void popFrame ()
            {
                if (m_frames.empty())
                {
                    m_lastPoppedKind = fkBlock;
                    return;
                }
                m_lastPoppedKind = m_frames.back().kind;
                m_frames.pop_back();
            }

            // 'end' has already popped one frame. A qualifier naming a different kind
            // means a missing inner END, so pop on to it rather than stay off by one.
            void reconcileEnd (EFrameKind want)
            {
                if (m_lastPoppedKind == want) return;

                for (int i = (int)m_frames.size() - 1; i >= 0; i--)
                {
                    if (m_frames[i].kind == want)
                    {
                        m_frames.erase(m_frames.begin() + i, m_frames.end());
                        return;
                    }
                }
            }

            void popToParen ()
            {
                for (int i = (int)m_frames.size() - 1; i >= 0; i--)
                {
                    if (m_frames[i].kind == fkParen)
                    {
                        m_frames.erase(m_frames.begin() + i, m_frames.end());
                        return;
                    }
                }
                // unmatched ')': leave the stack alone
            }

            void commitPendingHeader ()
            {
                if (!m_pendingHeader) return;
                m_pendingHeader = false;

                // These are closed by their own ';'; every other header waits for the
                // BEGIN or END that follows it.
                const bool inlineDef = m_stmtStart == etCURSOR  || m_stmtStart == etTYPE
                                    || m_stmtStart == etSUBTYPE || m_stmtStart == etRECORD;

                pushFrame(inlineDef ? fkInlineDef : fkHeader, 0);
            }

            FrameStack m_frames;
            EToken     m_prevSignificant; // last non-filler token, across line breaks
            EToken     m_stmtStart;       // first keyword of the current statement
            bool       m_pendingHeader;   // IS/AS seen, waiting for the end of its line
            bool       m_endJustSeen;     // the next token may qualify an END
            EFrameKind m_lastPoppedKind;
            EToken     m_lineLast;        // this line's last token, a trailing comment aside
            EToken     m_prevLineLast;    // ... and the previous line's: what a continuation hangs off

            int  m_levelBeforeLine;
            int  m_lineLevel;
            bool m_lineLevelSet;
            bool m_lineWasDedent;
            bool m_lineHadToken;
        };

        // Feeds [firstLine..caretLine] to a LOCAL parser, caretLine cut at caretCol (or
        // read whole when caretCol is kWholeLine). dedentLevel: caret level, else -1.
        bool scanWindow (const Storage& storage, int caretLine, int caretCol,
                         SmartIndentInfo& info, int* dedentLevel)
        {
            const int lineCount = storage.GetLineCount();
            if (caretLine < 0 || caretLine >= lineCount || caretCol < 0)
                return false;

            OEStringW text;

            int firstLine = caretLine;
            int chars     = 0;

            for (int ln = caretLine - 1; ln >= 0; ln--)
            {
                text.erase(0);
                storage.GetLineW(ln, text);

                if (isLoneSlash(text.data(), (int)text.length()))
                {
                    firstLine = ln + 1;   // the terminator itself is not part of the window
                    break;
                }

                firstLine = ln;
                chars += (int)text.length() + 1;

                // Not an error: the answer is a delta measured on one window, so a
                // truncated window degrades to "no change" rather than to a wrong jump.
                if (caretLine - ln >= kMaxScanLines || chars >= kMaxScanChars)
                    break;
            }

            SmartIndentAnalyser     analyser;
            OpenEditor::PlSqlParserEx parser(analyser);

            bool baseFound  = false;
            int  baseLevel  = 0;
            bool caretFound = false;
            int  caretLevel = 0;

            for (int ln = firstLine; ln <= caretLine; ln++)
            {
                text.erase(0);
                storage.GetLineW(ln, text);

                int len = (int)text.length();
                if (ln == caretLine)
                {
                    len = min(len, caretCol);

                    // A lone '/' terminates the unit and belongs to no statement, so it is
                    // not a tail to indent; the backward scan excludes it likewise.
                    if (isLoneSlash(text.data(), (int)text.length()))
                        len = 0;
                }

                analyser.resetLine(analyser.currentLevel());

                if (len <= 0)
                    continue;   // an empty line changes nothing and is never a base

                parser.PutLine(ln, text.data(), len);

                // The base line is the last non-empty line above the caret, the very line
                // eiAuto copies the indentation of. That makes the answer a delta.
                if (ln < caretLine)
                {
                    baseLevel = analyser.lineLevel();
                    baseFound = true;
                }
                else
                {
                    // Stage 2: what is left to the RIGHT of the caret is the tail of the
                    // same statement, so its own first token sets its level.
                    caretLevel = analyser.lineLevel();
                    caretFound = true;
                }
            }

            info.confident  = true;
            info.parenDepth = analyser.parenDepth();

            // With col == 0 the caret line is empty and cannot be reduced, so the level in
            // force after the window is the answer; otherwise the tail spoke for itself.
            const int after = caretFound ? caretLevel : analyser.currentLevel();

            info.levels = after - (baseFound ? baseLevel : 0);

            // An empty caret line never reached levelForLineStart, so a continuation
            // hanging off the base line has to be applied here instead.
            if (!caretFound && analyser.continuesExpression())
                info.levels += 1;

            // Inside a continued literal the new line is literal content and keeps the
            // base line's indentation verbatim.
            switch (parser.GetOpenLiteral())
            {
            case Common::PlsSql::PlSqlParserEx::olComment:
                info.inComment = true;
                info.levels    = 0;
                break;
            case Common::PlsSql::PlSqlParserEx::olString:
                info.inString = true;
                info.levels   = 0;
                break;
            default:
                break;
            }

            if (dedentLevel)
                *dedentLevel = analyser.lineWasDedent() ? analyser.lineLevel() : -1;

            return true;
        }

    };//anonymous namespace

    SmartIndentInfo PlSqlSmartIndent::Compute (const Storage& storage, int line, int col) const
    {
        SmartIndentInfo info;
        scanWindow(storage, line, col, info, NULL);
        return info;
    }

    int PlSqlSmartIndent::DedentLevel (const Storage& storage, int line, int col) const
    {
        SmartIndentInfo info;
        int level = -1;

        if (!scanWindow(storage, line, col, info, &level))
            return -1;

        if (!info.confident || info.inComment || info.inString)
            return -1;

        return level;
    }

};//namespace OpenEditor
