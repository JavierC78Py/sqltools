/* 
    Copyright (C) 2018-2026 Aleksey Kochetov

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
#include "OpenEditor/OEView.h"
#include <COMMON/AppGlobal.h>
#include <COMMON/ErrorDlg.h>
#include <afxtaskdialog.h>
#include "OpenEditor/OEPlsSqlParser.h"
#include "OpenEditor/OEPlsSqlFormatter.h"
#include "OpenEditor/OEPlsSqlFormatterCommon.h"
#include "OpenEditor/OEPlsSqlFormatterBlocks.h"
#include "OpenEditor/OEPlsSqlSyntax.h"
#include "OpenEditor/OEDocument.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

    using namespace std;
    using namespace OpenEditor;
    using namespace Common::PlsSql;

    // FMT_ formatter settings are read from GetSettings() inside OnEditFormatSql().
    // FMT_INDENT_SIZE is sourced from GetIndentSpacing() (ClassSettings::IndentSpacing).

#define RETURN_IF_LOCKED { if (!m_bAttached) return; if (IsLocked()) { Global::SetStatusText("The content is locked and cannot be modified!"); MessageBeep((UINT)-1); return; } } 

// ============================================================================
// verifyFormatterLosslessness
//
// Reparses the formatted buffer, then compares its non-whitespace token stream
// against the original one.  Reports up to 50 mismatches with line numbers.
// ============================================================================
static void verifyFormatterLosslessness (
    const std::vector<OpenEditor::Token>& original,
    const std::wstring&                   formatted)
{
    // --- reparse the formatted buffer ---
    OpenEditor::SimpleSyntaxAnalyser analyser2;
    OpenEditor::PlSqlParserEx        parser2(analyser2);

    // split formatted into lines and feed them one by one
    int lineNo = (original.size() > 0) ? original.begin()->line : 0;
    std::wstring::size_type pos = 0;
    while (pos <= formatted.size())
    {
        auto nl = formatted.find(L'\n', pos);
        std::wstring::size_type end = (nl == std::wstring::npos) ? formatted.size() : nl;
        parser2.PutLine(lineNo, formatted.c_str() + pos, (int)(end - pos));
        ++lineNo;
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }

    const auto& reparsed = analyser2.GetTokens();

    // --- compare ignoring etEOL / etWHITESPACE ---
    auto isSpace = [](const OpenEditor::Token& t)
    {
        return t.token == etEOL || t.token == etWHITESPACE;
    };

    auto skipSpace = [&](const std::vector<OpenEditor::Token>& v,
                         size_t i) -> size_t
    {
        while (i < v.size() && isSpace(v[i])) ++i;
        return i;
    };

    // Normalize a token value for comparison: for multiline comments, strip
    // leading whitespace from every line after the first so that alignment
    // differences introduced by align_comment() are ignored.
    auto normalizeValue = [](const OpenEditor::Token& t) -> std::wstring
    {
        if (t.token != etCOMMENT || t.value.find(L'\n') == std::wstring::npos)
            return t.value;
        std::wstring result;
        result.reserve(t.value.size());
        bool afterNewline = false;
        for (wchar_t ch : t.value)
        {
            if (ch == L'\n') { result += ch; afterNewline = true; continue; }
            if (afterNewline && (ch == L' ' || ch == L'\t')) continue;
            afterNewline = false;
            result += ch;
        }
        return result;
    };

    const int maxErrors = 10;
    int errorCount = 0;
    std::string errorLog;

    size_t oi = 0, ri = 0;
    oi = skipSpace(original,  oi);
    ri = skipSpace(reparsed,  ri);

    while (oi < original.size() && ri < reparsed.size())
    {
        const auto& ot = original[oi];
        const auto& rt = reparsed[ri];

        if (ot.token != rt.token || normalizeValue(ot) != normalizeValue(rt))
        {
            wchar_t msg[512];
            auto ov = ot.value.length() < 20 ? ot.value : (ot.value.substr(0, 17) + L"...");
            auto rv = rt.value.length() < 20 ? rt.value : (rt.value.substr(0, 17) + L"...");
            swprintf_s(msg, _countof(msg),
                L"Original  line %d: token=%d len=%d value=\"%s\"\n"
                L"Formatted line %d: token=%d len=%d value=\"%s\"\n\n",
                ot.line + 1, (int)ot.token, (int)ot.value.length(), ov.c_str(),
                rt.line + 1, (int)rt.token, (int)rt.value.length(), rv.c_str());
            TRACE(L"%s", msg);
            errorLog += CW2A(msg);
            if (++errorCount >= maxErrors)
            {
                errorLog += "(further errors suppressed)\n";
                break;
            }
        }

        oi = skipSpace(original, oi + 1);
        ri = skipSpace(reparsed, ri + 1);
    }

    // skip trailing spaces in both
    oi = skipSpace(original, oi);
    ri = skipSpace(reparsed, ri);

    if (errorCount == 0)
    {
        if (oi < original.size())
        {
            wchar_t msg[256];
            swprintf_s(msg, _countof(msg),
                L"Formatted output is missing tokens starting at original line %d: \"%s\"\n",
                original[oi].line + 1, original[oi].value.c_str());
            TRACE(L"%s", msg);
            errorLog += CW2A(msg);
        }
        else if (ri < reparsed.size())
        {
            wchar_t msg[256];
            swprintf_s(msg, _countof(msg),
                L"Formatted output has extra tokens starting at formatted line %d: \"%s\"\n",
                reparsed[ri].line + 1, reparsed[ri].value.c_str());
            TRACE(L"%s", msg);
            errorLog += CW2A(msg);
        }
    }

    if (!errorLog.empty())
    {
        errorLog = "Formatter losslessness check FAILED!\n\n" + errorLog;
        if (formatterDiagnosticsActive())
        {
            // Automated test in progress (see OEPlsSqlFormatter.h): report
            // through the diagnostics sink. CErrorDlg::DoModal() would block
            // the UI thread and deadlock the COM caller.
            *g_pFormatterDiagnostics += std::wstring(CA2W(errorLog.c_str()));
            *g_pFormatterDiagnostics += L"\n";
        }
        else
        {
            CErrorDlg(errorLog.c_str(), "FormatterLosslessnessCheck").DoModal();
        }
    }
    else
    {
        TRACE(L"Formatter losslessness check passed.\n");
    }
}

#ifdef _DEBUG
// ============================================================================
// verifyOperatorTokenization
//
// Feeds one line of source text per legal PL/SQL operator/delimiter into
// PlSqlParserEx and verifies that each produces exactly the expected token
// type (and, for word-like delimiters, the expected literal value). Returns
// an empty string if every case passed, otherwise a human-readable report of
// all mismatches (does not stop at the first failure).
// ============================================================================
static std::string verifyOperatorTokenization ()
{
    struct Case
    {
        const wchar_t* source;   // line of text containing the operator
        EToken         expected; // expected token type for the operator
        const wchar_t* value;    // expected literal value of the operator token
    };

    // One case per delimiter listed in the Oracle PL/SQL delimiters table.
    // Simple/unambiguous single-character delimiters are included too so
    // that regressions in the fallback single-char path are also caught.
    static const Case cases[] =
    {
        { L"a + b",        etUNKNOWN,             L"+"  }, // Addition operator (currently untyped)
        { L"a := b",       etASSIGN,              L":=" }, // Assignment operator
        { L"a => b",       etASSOC,               L"=>" }, // Association operator
        { L"a%rowtype",    etPERCENT_SIGN,        L"%"  }, // Attribute indicator
        { L"a.b",          etDOT,                 L"."  }, // Component indicator
        { L"a || b",       etCONCAT_OP,           L"||" }, // Concatenation operator
        { L"a / b",        etSLASH,               L"/"  }, // Division operator
        { L"a ** b",       etEXPONENT,            L"**" }, // Exponentiation operator
        { L"(a)",          etLEFT_ROUND_BRACKET,  L"("  }, // Expression/list delimiter (begin)
        { L"(a)",          etRIGHT_ROUND_BRACKET, L")"  }, // Expression/list delimiter (end)
        { L":a",           etCOLON,               L":"  }, // Host variable indicator
        { L"a, b",         etCOMMA,               L","  }, // Item separator
        { L"<<lbl>>",      etLABEL_BEGIN,         L"<<" }, // Label delimiter (begin)
        { L"<<lbl>>",      etLABEL_END,           L">>" }, // Label delimiter (end)
        { L"a * b",        etSTAR,                L"*"  }, // Multiplication operator
        { L"a..b",         etRANGE,               L".." }, // Range operator
        { L"a = b",        etEQUAL,               L"="  }, // Relational operator (equal)
        { L"a <> b",       etNOT_EQUAL,           L"<>" }, // Relational operator (not equal)
        { L"a != b",       etNOT_EQUAL,           L"!=" }, // Relational operator (not equal)
        { L"a ~= b",       etNOT_EQUAL,           L"~=" }, // Relational operator (not equal)
        { L"a ^= b",       etNOT_EQUAL,           L"^=" }, // Relational operator (not equal)
        { L"a < b",        etLESS,                L"<"  }, // Relational operator (less than)
        { L"a > b",        etGREATER,             L">"  }, // Relational operator (greater than)
        { L"a <= b",       etLESS_EQUAL,          L"<=" }, // Relational operator (less than or equal)
        { L"a >= b",       etGREATER_EQUAL,       L">=" }, // Relational operator (greater than or equal)
        { L"a@b",          etAT_SIGN,             L"@"  }, // Remote access indicator
        { L"a;",           etSEMICOLON,           L";"  }, // Statement terminator
        { L"a - b",        etMINUS,               L"-"  }, // Subtraction or negation operator
    };

    std::string errorLog;
    int errorCount = 0;
    const int maxErrors = 50;

    for (const auto& c : cases)
    {
        if (errorCount >= maxErrors)
        {
            errorLog += "(further errors suppressed)\n";
            break;
        }

        OpenEditor::SimpleSyntaxAnalyser analyser;
        OpenEditor::PlSqlParserEx        parser(analyser);

        std::wstring line(c.source);
        parser.PutLine(0, line.c_str(), (int)line.size());
        parser.PutEOF(1);

        const auto& tokens = analyser.GetTokens();

        // find the operator token among the emitted tokens by matching its value
        const OpenEditor::Token* found = nullptr;
        for (const auto& tk : tokens)
        {
            if (tk.token == etEOL || tk.token == etWHITESPACE || tk.token == etEOF)
                continue;
            if (tk.value == c.value)
            {
                found = &tk;
                break;
            }
        }

        if (!found)
        {
            wchar_t msg[256];
            swprintf_s(msg, _countof(msg),
                L"Operator \"%s\" not found in tokenized output of: \"%s\"\n",
                c.value, c.source);
            TRACE(L"%s", msg);
            errorLog += CW2A(msg);
            ++errorCount;
            continue;
        }

        if (found->token != c.expected)
        {
            wchar_t msg[256];
            swprintf_s(msg, _countof(msg),
                L"Operator \"%s\" in \"%s\": expected token=%d but got token=%d\n",
                c.value, c.source, (int)c.expected, (int)found->token);
            TRACE(L"%s", msg);
            errorLog += CW2A(msg);
            ++errorCount;
        }
    }

    if (!errorLog.empty())
        errorLog = "Operator tokenization check FAILED!\n\n" + errorLog;
    else
        TRACE(L"Operator tokenization check passed.\n");

    return errorLog;
}
#endif

// ============================================================================
// captureFormatterVerbatimSource
//
// Snapshots the raw text of every line OnEditFormatSql() feeds to the tokenizer, so
// emitVerbatimFragment() can write a fragment straight from the source instead of reconstructing it
// from token offsets.
//
// The tokenizer deliberately discards whitespace and never emits etWHITESPACE tokens — only
// significant tokens and etEOL survive — so a reconstruction can only ever *approximate* the author's
// layout: it enforces a one-space minimum gap between adjacent tokens and re-anchors every
// continuation line. For text the formatter does not understand at all (an unrecognized root
// statement: a SQL*Plus command, CREATE TABLE/VIEW, a CREATE ... JAVA SOURCE unit) that approximation
// is a corruption — "SPOOL install.log" comes back as "SPOOL install . log", "@@ shared\defs.sql" as
// "@ @ shared \ defs . sql", and embedded Java gets re-indented. Returning the exact slice keeps such
// statements byte-identical.
//
// Lines are joined with '\n', matching FormatWriter::NewLine(). The buffer is owned by `lines`
// (supplied by the caller) and must outlive the returned function; the lambda captures it by reference
// only.
//
// `firstLineOffset` mirrors the offset the first line is handed to PlSqlParserEx::PutLine() with (a
// formatting request may start mid-line): token offsets on that line are relative to the *fed* text,
// so the captured snapshot must start at the same place or every slice from that line would be
// shifted. See the "Verbatim-source callback" section of OEPlsSqlFormatter.h.
// ============================================================================
static VerbatimSourceFn captureFormatterVerbatimSource (
    COEditorView& view, int firstLine, int lastLine, int firstLineOffset,
    std::vector<std::wstring>& lines)
{
    lines.clear();

    const int nlines = view.GetLineCount();
    if (lastLine >= nlines)
        lastLine = nlines - 1;

    for (int line = firstLine; line <= lastLine; line++)
    {
        OEStringW lineBuff;
        view.GetLineW(line, lineBuff);

        size_t skip = (line == firstLine && firstLineOffset > 0) ? (size_t)firstLineOffset : 0;
        if (skip > lineBuff.length())
            skip = lineBuff.length();

        lines.emplace_back(lineBuff.data() + skip, lineBuff.length() - skip);
    }

    return [&lines, firstLine](const Token& startToken, const Token& endToken) -> wstring
    {
        if (startToken.line > endToken.line)
            return wstring();

        const size_t startIdx = (size_t)(startToken.line - firstLine);
        const size_t endIdx   = (size_t)(endToken.line   - firstLine);
        if (startIdx >= lines.size() || endIdx >= lines.size())
            return wstring();

        const size_t startOff = (size_t)startToken.offset;
        if (startIdx == endIdx)
        {
            // Same-line fragment: a plain slice, when the token really does map
            // onto that line (a multi-line token — e.g. a block comment or a
            // quoted string containing newlines — carries the length of all its
            // lines but an offset inside its first one, so it cannot be sliced
            // and the caller falls back to reconstruction).
            const size_t endCol = (size_t)endToken.offset + (size_t)endToken.length;
            if (startOff > lines[startIdx].size() || endCol > lines[startIdx].size()
            ||  startOff > endCol)
                return wstring();

            return lines[startIdx].substr(startOff, endCol - startOff);
        }

        // Multi-line fragment: join the intervening lines exactly as they were
        // typed, then drop the tail of the last one that follows the end token.
        const size_t endOff = (size_t)endToken.offset + (size_t)endToken.length;
        if (startOff > lines[startIdx].size() || endOff > lines[endIdx].size())
            return wstring();

        wstring text = lines[startIdx].substr(startOff);
        for (size_t i = startIdx + 1; i <= endIdx; i++)
        {
            text += L"\n";
            text += lines[i];
        }

        const size_t tail = lines[endIdx].size() - endOff;
        if (tail >= text.size())
            return wstring();

        text.resize(text.size() - tail);
        return text;
    };
}


// ============================================================================
// FormatterProgressMonitor
//
// The ProgressFn OnEditFormatSql() installs below. Two jobs, both driven from the formatter's
// root-node checkpoints (see the "Progress callback" section of OEPlsSqlFormatter.h for what the
// stages and counters mean):
//
//   * a status line, throttled to one Global::SetStatusText(..., TRUE) per second. Checkpoints fire
//     per root node - thousands of times on a large script - and SetWindowText + UpdateWindow is the
//     expensive part, not the callback. A stage change always reports immediately, because
//     "parsing" -> "writing" is the one transition worth seeing even inside the throttle window.
//
//   * a wall-clock budget. Neither format pass can be interrupted below a root-node boundary, so
//     without this a pathological document would run unbounded with no way out. After
//     kFmtFirstBudgetMs the user is asked, with a CTaskDialog, to continue for 1 / 3 / 10 more minutes
//     or to cancel. Continuing re-arms the budget from *now*, so elapsing again asks again. Cancelling
//     (or ESC, or closing the dialog) throws a user exception, which unwinds the format; MFC reports
//     that silently, the same as every other cancelled operation in SQLTools.
//
// None of this runs during automated testing: OnEditFormatSql() below does not install the scope at
// all while formatterHeadless(), so an automated run gets neither the status line nor the prompt. (The
// test app is a full GUI instance with a live CMFCStatusBar, so the status line is *not* a no-op
// there.) The formatterHeadless() test on the prompt is therefore unreachable today; it stays as a
// guard for any future installer, because a modal dialog on the COM path would hang the caller until
// its own timeout and the corpus stress harness would record CRASH/TIMEOUT for a file that merely
// formatted slowly.
// ============================================================================
namespace
{
    const ULONGLONG kFmtFirstBudgetMs  = 60 * 1000;  // ask after 1 minute
    const ULONGLONG kFmtStatusPeriodMs = 1000;       // one status line / second

    // CTaskDialog command-control IDs. Arbitrary, but must not collide with the
    // common-button results (IDOK / IDCANCEL / ...), which DoModal() also
    // returns and which the switch below relies on to detect cancellation.
    const int kFmtContinue1  = 101;
    const int kFmtContinue3  = 102;
    const int kFmtContinue10 = 103;

    class FormatterProgressMonitor
    {
    public:
        FormatterProgressMonitor ()
            : m_start(::GetTickCount64()), m_budgetMs(kFmtFirstBudgetMs),
              m_lastStatus(0), m_lastStage(FormatStage::ParseTree), m_haveStage(false)
        {}

        void operator () (FormatStage stage, int current, int total)
        {
            const ULONGLONG now = ::GetTickCount64();

            const bool stageChanged = !m_haveStage || stage != m_lastStage;
            m_lastStage = stage;
            m_haveStage = true;

            if (stageChanged || now - m_lastStatus >= kFmtStatusPeriodMs)
            {
                m_lastStatus = now;

                wchar_t msg[128];
                if (stage == FormatStage::ParseTree)
                    swprintf_s(msg, _countof(msg),
                               L"Formatting: parsing tree (line %d of %d)...", current, total);
                else
                    swprintf_s(msg, _countof(msg),
                               L"Formatting: writing (statement %d of %d)...", current, total);

                Global::SetStatusText(msg, TRUE);
            }

            if (now - m_start >= m_budgetMs && !formatterHeadless())
                PromptToContinue();
        }

    private:
        // Returns with the budget re-armed, or throws when the user cancels.
        void PromptToContinue ()
        {
            const ULONGLONG elapsedSec = (::GetTickCount64() - m_start) / 1000;

            wchar_t content[512];
            swprintf_s(content, _countof(content),
                       L"Formatting this selection has been running for %I64u seconds and is "
                       L"not finished yet.\n\n"
                       L"The document is probably very large or unusually structured. You can "
                       L"let it continue, or cancel the operation.",
                       elapsedSec);

            // The Cancel *common* button (TDCBF_CANCEL_BUTTON) is the cancel
            // path: it also covers ESC and closing the dialog, and DoModal()
            // returns IDCANCEL for all three, which the switch below folds into
            // its default arm. Only the three "continue" choices are command
            // controls, so there is exactly one visible way to cancel.
            CTaskDialog dlg(L"", content, L"SQL Formatter is still working", TDCBF_CANCEL_BUTTON);
            dlg.SetMainIcon(TD_WARNING_ICON);
            dlg.AddCommandControl(kFmtContinue1,  L"Continue for 1 more minute");
            dlg.AddCommandControl(kFmtContinue3,  L"Continue for 3 more minutes");
            dlg.AddCommandControl(kFmtContinue10, L"Continue for 10 more minutes");

            ULONGLONG extraMs = 0;
            switch (dlg.DoModal())
            {
            case kFmtContinue1:  extraMs =  1 * 60 * 1000; break;
            case kFmtContinue3:  extraMs =  3 * 60 * 1000; break;
            case kFmtContinue10: extraMs = 10 * 60 * 1000; break;
            default:
                Global::SetStatusText(L"Formatting cancelled.", TRUE);
                // Unwinds through the emitters with the emission deliberately
                // incomplete; the flag stops the Debug range guards from
                // asserting on that. Cleared by ~FormatterProgressScope() once
                // the format has fully unwound.
                g_formatterCancelling = true;
                AfxThrowUserException();   // does not return
            }

            // Re-arm from *now*, not from the old deadline: DoModal() can sit
            // open for a while, and charging that against the next budget would
            // make the follow-up prompt fire almost immediately.
            m_start      = ::GetTickCount64();
            m_budgetMs   = extraMs;
            m_lastStatus = 0;   // force an immediate status line on resume
        }

        ULONGLONG   m_start;
        ULONGLONG   m_budgetMs;
        ULONGLONG   m_lastStatus;
        FormatStage m_lastStage;
        bool        m_haveStage;
    };
}

void COEditorView::OnEditFormatSqlWithSettings ()
{
    if (!IsSelectionEmpty())
        GetDocument()->ShowFormatSqlSettings(this);
}

// ============================================================================
// Selection seeding — md/plsql_formatter_selection_seed_plan.md. A fragment is
// formatted as the section the document tree says it is in; every gate fails closed.
// ============================================================================

// Section-lookup budget, mirroring kFindMatchParseBudgetMs in OEView_1.cpp: a
// bounded join on the background parse; a timeout declines rather than waits.
static const ULONGLONG kFormatSeedParseBudgetMs = 3000;

// A seed depth past this is a pathological tree, not a real nesting level, and
// would only produce a giant dedent (gate G6).
static const int kMaxSeedDepth = 32;

// The enclosing construct, carried for diagnostics only in P1: no emitter reads
// it, so a mapping gap degrades to None and cannot change layout.
static FormatContext::UnitKind SeedUnitKind (EOutlineItemType itemType)
{
    switch (itemType)
    {
    case oitProcedure:   return FormatContext::Procedure;
    case oitFunction:    return FormatContext::Function;
    case oitPackage:     return FormatContext::Package;
    case oitPackageBody: return FormatContext::PackageBody;
    case oitTrigger:     return FormatContext::Trigger;
    case oitType:        return FormatContext::TypeSpec;
    case oitTypeBody:    return FormatContext::TypeBody;
    // A type body's member subprogram is a procedure/function body as far as the
    // formatter is concerned; UnitKind has no "member" value and needs none while
    // this field is diagnostics-only.
    case oitTypeMemberProcedure: return FormatContext::Procedure;
    case oitTypeMemberFunction:  return FormatContext::Function;
    default:             return FormatContext::None;
    }
}

// The last character the selection actually covers, in index coordinates. A
// full-line selection ends at column 0 of the FOLLOWING line, so the probe walks
// back over trailing blank lines to the last line of the selection that has text.
static bool SelectionEndProbe (COEditorView& view, const Square& selection, int& line, int& offset)
{
    line   = selection.end.line;
    offset = selection.end.column;

    if (offset > 0)
    {
        offset--;
        return true;
    }

    while (line > selection.start.line)
    {
        line--;

        OEStringW text;
        view.GetLineW(line, text);

        const wchar_t* chars = text.data();
        int last = (int)text.length() - 1;
        while (last >= 0 && iswspace(chars[last]))
            last--;

        if (last >= 0)
        {
            offset = last;
            return true;
        }
    }

    return false;
}

// Outcome of the gate walk. The seed is the only part the formatter consumes; `decline`
// and the two hits exist so a `.FmtTraceTree=1` run can say WHY a fragment was formatted
// unseeded. Every gate fails closed, so without this the answer is indistinguishable
// from "the parse had not finished", and telling those apart is the whole job when a
// selection formats differently than expected.
struct SeedDecision
{
    FormatterSeed  seed;
    const wchar_t* decline = L"";   // empty when the fragment IS seeded
    SectionHit     start;
    SectionHit     end;
    // True once gate G5 has established this really is a fragment rather than the whole
    // document. The trace is reported only for these - see DescribeSeedDecision.
    bool           fragment = false;
};

// Gates G1-G6 of the plan: turn the tree's answer for BOTH ends of the selection into a
// seed, or decline. Both ends must agree, so a fragment spanning BEGIN is not seedable.
static SeedDecision MakeFormatterSeed (COEDocument& doc, const Square& selection,
                                       int lineCount, int endLine, int endOffset)
{
    SeedDecision d;

    // G5: a whole-document selection stays on the unseeded path bit-for-bit.
    if (selection.start.line == 0 && selection.start.column == 0
    &&  selection.end.line >= lineCount - 1)
    {
        d.decline = L"whole-document";
        return d;
    }

    d.fragment = true;

    // G1: the published tree must answer for both ends at all. A parse still in flight, a
    // truncated tail or a position in no construct declines here.
    if (!doc.GetFormatSeed(selection.start.line, selection.start.column, d.start))
    {
        d.decline = L"no-section-at-start";
        return d;
    }
    if (!doc.GetFormatSeed(endLine, endOffset, d.end))
    {
        d.decline = L"no-section-at-end";
        return d;
    }

    // G2: one section, one construct, one indent level across the whole fragment.
    if (d.start.section != d.end.section
    ||  d.start.owner   != d.end.owner
    ||  d.start.depth   != d.end.depth)
    {
        d.decline = L"ends-disagree";
        return d;
    }

    // G3: only a declaration, body, exception or type-member-list section is seedable.
    // GetFormatSeed already reports a header as a miss; this keeps the mapping explicit.
    FormatFragmentScope scope;
    if      (d.start.section == SyntaxNode::shDeclarations) scope = FormatFragmentScope::Declarations;
    else if (d.start.section == SyntaxNode::shBody)         scope = FormatFragmentScope::Body;
    else if (d.start.section == SyntaxNode::shException)    scope = FormatFragmentScope::Exception;
    else if (d.start.section == SyntaxNode::shTypeMembers)  scope = FormatFragmentScope::TypeMembers;
    else
    {
        d.decline = L"unseedable-section";
        return d;
    }

    // G4: refuse a start strictly inside a statement - a FROM… tail would otherwise be
    // re-parsed as a statement list. Containers are safe: a fragment starts inside one.
    const SyntaxNode* innermost = d.start.innermost;
    if (!innermost)
    {
        d.decline = L"no-innermost-node";
        return d;
    }

    const EOutlineItemType itemType = innermost->GetOutlineItemType();
    const bool container = innermost->OwnsSections()
                        || itemType == oitIf || itemType == oitCase || itemType == oitLoop;

    if (!container)
    {
        Square own;
        const bool atOwnStart = innermost->GetOwnExtent(own)
                             && (own.start.line >  selection.start.line
                              || (own.start.line == selection.start.line
                               && own.start.column >= selection.start.column));
        if (!atOwnStart)
        {
            d.decline = L"start-inside-statement";
            return d;
        }
    }

    // G6: a sane indent level, so the dedent in formatPlsql stays bounded.
    if (d.start.depth < 0 || d.start.depth > kMaxSeedDepth)
    {
        d.decline = L"depth-out-of-range";
        return d;
    }

    d.seed.scope = scope;
    d.seed.owner = SeedUnitKind(d.start.owner);
    d.seed.depth = d.start.depth;
    return d;
}

// One-line account of what a selection format decided, for `.FmtTraceTree=1`. Reached only
// from the selection branch, so a whole-file trace - and therefore every existing baseline
// that captures one - is untouched. `settled` says whether the background parse had
// finished when the gates ran: a decline with settled=0 means the tree was never
// consulted, which is a different problem from a gate saying no.
static std::wstring DescribeSeedDecision (const SeedDecision& d, bool settled)
{
    const wchar_t* scope =
        d.seed.scope == FormatFragmentScope::Declarations ? L"Declarations" :
        d.seed.scope == FormatFragmentScope::Body         ? L"Body" :
        d.seed.scope == FormatFragmentScope::Exception    ? L"Exception" :
        d.seed.scope == FormatFragmentScope::TypeMembers  ? L"TypeMembers" :
                                                             L"Document";
    wchar_t buf[256];
    swprintf_s(buf, _countof(buf),
               L"[SelectionSeed] settled=%d scope=%s owner=%d depth=%d decline=%s"
               L" start=%d/%d/%d end=%d/%d/%d\n",
               settled ? 1 : 0, scope, (int)d.seed.owner, d.seed.depth,
               *d.decline ? d.decline : L"none",
               (int)d.start.section, (int)d.start.owner, d.start.depth,
               (int)d.end.section, (int)d.end.owner, d.end.depth);
    return buf;
}




// DoEditFormatSql — the actual formatting body, parameterized on a
// FormatterPolicy rather than reading GetSettings() itself (see
// md/FormatterPolicy_proposal.md). OnEditFormatSql() below is the thin
// wrapper that builds the policy from the live editor settings;
// SqlFormatterHandler (automated testing) calls this overload directly with
// a deterministic policy so test output cannot shift when the developer's
// persisted Fmt* settings change.
void COEditorView::DoEditFormatSql (const OpenEditor::FormatterPolicy& policy)
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (!IsSelectionEmpty())
    {
        if (GetBlockMode() != EBlockMode::ebtStream)
        {
            AfxMessageBox(L"SQL Format is supported on steam selection only!", MB_OK | MB_ICONSTOP);
            AfxThrowUserException();
        }

        OpenEditor::SimpleSyntaxAnalyser analyser;
        OpenEditor::PlSqlParserEx parser(analyser);

        OpenEditor::Square selection;
        GetSelection(selection);
        selection.normalize();

        // convert positions to indexes (because of tabs)
        selection.start.column = PosToInx(selection.start.line, selection.start.column);
        selection.end.column   = PosToInx(selection.end.line, selection.end.column);

        // Which section of the DOCUMENT this fragment sits in, so that it is formatted the
        // way the same tokens are formatted inside the enclosing unit. Document, the
        // default, means "no enclosing section was established" and keeps the fragment-only
        // behaviour this command has always had.
        FormatterSeed seed;

        if (COEDocument* pDoc = GetDocument())
        {
            // Bounded join on the background parse, exactly as OnEditFindMatch does. A
            // timeout cancels nothing and simply leaves the seed at Document: a stale or
            // absent tree must never change how a selection is formatted.
            const bool settled =
                pDoc->WaitForParseQuiescent(::GetTickCount64() + kFormatSeedParseBudgetMs);

            int        endLine = 0, endOffset = 0;
            SeedDecision decision;

            // The gates need BOTH ends as line/offset pairs. The start already is one; the
            // end has to be recovered from the selection, and when that is not possible the
            // fragment is formatted unseeded rather than against a half-known range.
            if (SelectionEndProbe(*this, selection, endLine, endOffset))
                decision = MakeFormatterSeed(*pDoc, selection, GetLineCount(), endLine, endOffset);
            else
                decision.decline = L"no-text-at-end";

            seed = decision.seed;

            // Honour DescribeSeedDecision's contract: a fragment only. Every format of an
            // unselected document gets gate G5's whole-document decline, and reporting that
            // would add a line to every .FmtTraceTree=1 baseline - format_tree,
            // collection_type, compound_trigger, unknown_stmt - saying nothing about the tree
            // shape those suites exist to guard.
            if (formatterTreeTraceActive() && decision.fragment)
                appendFormatterTreeTrace(DescribeSeedDecision(decision, settled).c_str());
        }

        int line   = selection.start.line;
        int offset = selection.start.column;
        int nlines = GetLineCount();

        for (; line < nlines && line <= selection.end.line; line++)
        {
            OEStringW lineBuff;
            GetLineW(line, lineBuff);
            const wchar_t* str = lineBuff.data();
            int length = (int)lineBuff.length();

            if (line == selection.end.line) 
                length = min(length, selection.end.column);

            parser.PutLine(line, str + offset, length - offset);
            offset = 0;
        }

        auto tokens = analyser.GetTokens();

        // Unrecognized statements are written out from the exact source slice
        // rather than reconstructed from token offsets — see
        // captureFormatterVerbatimSource() above. The snapshot (`sourceLines`)
        // backs the returned callback by reference, so both must outlive
        // formatPlsql().
        std::vector<std::wstring> sourceLines;
        FormatterVerbatimSourceScope verbatimSource(
            captureFormatterVerbatimSource(*this, selection.start.line, selection.end.line,
                                           selection.start.column, sourceLines));

        // Progress + wall-clock budget for the format below (see FormatterProgressMonitor).
        // Deliberately NOT installed while headless, i.e. on the automated-testing COM path: there is
        // no user to read the status line, the "still working" prompt would hang the caller until its
        // own 10 s proxy timeout, and every checkpoint would be pure overhead in a run that formats
        // thousands of files back to back. See formatterHeadless() in OEPlsSqlFormatter.h.
        //
        // The monitor is declared first so it outlives the scope: the scope stores a std::function that
        // calls into it, and both must cover exactly the formatPlsql() call. A unique_ptr because
        // FormatterProgressScope is non-copyable and here conditionally constructed — the same idiom
        // SqlFormatterHandler uses for its tree-trace scope — and being declared after the monitor it
        // is also destroyed before it.
        FormatterProgressMonitor progressMonitor;
        std::unique_ptr<FormatterProgressScope> progress;
        if (!formatterHeadless())
            progress.reset(new FormatterProgressScope(
                [&progressMonitor](FormatStage stage, int current, int total)
                    { progressMonitor(stage, current, total); }));

        wstring buffer = formatPlsql(tokens, policy, seed);

        verifyFormatterLosslessness(tokens, buffer);

#ifdef _DEBUG
        std::string operatorErrors = verifyOperatorTokenization();
        if (!operatorErrors.empty())
        {
            if (formatterDiagnosticsActive())
            {
                *g_pFormatterDiagnostics += L"[OperatorTokenizationCheck]\n";
                *g_pFormatterDiagnostics += std::wstring(CA2W(operatorErrors.c_str()));
                *g_pFormatterDiagnostics += L"\n";
            }
            else
            {
                CErrorDlg(operatorErrors.c_str(), "OperatorTokenizationCheck").DoModal();
            }
        }

        // Whole-buffer layout invariants (a script '/' must stand alone on its
        // line and be followed by a blank line). Catches the "right tokens,
        // wrong line" bug class that the token-fidelity guards structurally
        // cannot see, because those strip whitespace before comparing.
        // See OEPlsSqlFormatterCommon.h.
        std::wstring layoutErrors = checkScriptSlashLayout(buffer);
        if (!layoutErrors.empty())
        {
            if (formatterDiagnosticsActive())
            {
                *g_pFormatterDiagnostics += L"[LayoutGuard]\n";
                *g_pFormatterDiagnostics += layoutErrors;
            }
            else
            {
                std::string narrow(CW2A(layoutErrors.c_str()));
                CErrorDlg(narrow.c_str(), "LayoutGuard").DoModal();
            }
        }
#endif
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        DeleteBlock(true);

        size_t end = buffer.find_last_not_of(L" \n\r");
        if (end != std::wstring::npos) {
            buffer.erase(end + 1);
        }

        // align formatted text by first token
        // Skip leading etEOL tokens: a comment that spans multiple lines is
        // buffered internally and only emitted once its closing delimiter is
        // found, so the etEOL tokens for those already-consumed lines are
        // pushed to the token stream first, ahead of the comment token
        // itself. tokens[0] can therefore be an EOL whose offset has nothing
        // to do with the real indentation of the first content token.
        auto firstContentTok = tokens.begin();
        while (firstContentTok != tokens.end() && firstContentTok->token == etEOL)
            ++firstContentTok;

        if (selection.start.column == 0 && firstContentTok != tokens.end())
        {
            std::wstring line, buffer2, indent(firstContentTok->offset, ' ');

            bool cr;
            int position = 0;
            const wchar_t* str = buffer.c_str();

            while (EditContext::getLine(str, position, line, cr))
            {
                if (!line.empty())
                    buffer2 += indent + line + L'\n';
                else if (cr)
                    buffer2 += L'\n';
            }

            buffer = buffer2;
        }
        else
            AlignCodeFragment(buffer.c_str(), buffer);

        InsertBlock(buffer.c_str(), false, true);

        Global::SetStatusText("Formatting: done.", TRUE);
    }
}

void COEditorView::OnEditFormatSql ()
{
    DoEditFormatSql(OpenEditor::FormatterPolicy::FromSettings(GetSettings()));
}
