#include "stdafx.h"
#include "SqlFormatterHandler.h"
#include "SQLWorksheet/SQLWorksheetDoc.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OEPlsSqlFormatter.h"
#include "OpenEditor/OEPlsSqlFormatterCommon.h"
#include "App/SQLTools.h"
#include "Common/StackTracer.h"
#include <memory>
#include <exception>
#include <string>

// ----------------------------------------------------------------------------
// A fixed FormatterPolicy snapshot, built from a captured Settings XML export
// (a developer's persisted config) rather than from FormatterPolicy::Defaults()
// or the live GlobalSettings. Useful for a test/baseline run that must
// reproduce a *specific* saved settings profile regardless of what the
// current developer's live settings or the hardcoded defaults are.
//
// Only the FmtXxx / IndentSpacing values are pulled out of the XML fragment;
// everything unrelated to formatting (print margins, MDI tabs, file backup,
// etc.) is not part of FormatterPolicy and is intentionally ignored. Two XML
// keys have no corresponding field and are likewise ignored:
//   - FmtPlsqlBlankLineBetweenFlowStmts / FmtPlsqlBlankLineBetweenSubprograms
//     are legacy/renamed duplicates of the "Around" variants below, which are
//     the ones FormatterPolicy::FromSettings() actually reads.
//   - FmtPlsqlConcatSkipLiteralPairBreak has no FormatterPolicy field; the
//     equivalent switch is FmtPlsqlConcatKeepMixedPairTogether.
static OpenEditor::FormatterPolicy MakeFormatterPolicyFromCapturedSettingsXml()
{
    OpenEditor::FormatterPolicy pol;

    // ---- indent ----
    int  indentSpacing                   = 2;


    // ---- SQL-side settings ----
    pol.simpleStmtEnabled                = true;   // FmtSimpleStmtEnabled="1"
    pol.simpleStmtMaxLen                 = 40;     // FmtSimpleStmtMaxLen="40"
    pol.selectShortListEnabled           = true;   // FmtSelectShortListEnabled="1"
    pol.selectShortListMaxLen            = 45;     // FmtSelectShortListMaxLen="45"
    pol.fromShortListEnabled             = true;   // FmtFromShortListEnabled="1"
    pol.fromShortListMaxLen              = 26;     // FmtFromShortListMaxLen="26"
    pol.alignColumnAliases               = true;   // FmtAlignColumnAliases="1"
    pol.blockShortMaxLen                 = 60;     // FmtBlockShortMaxLen="60"
    pol.selectStmtEnabled                = true;   // FmtSelectStmtEnabled="1"
    pol.selectStmtMaxLen                 = 50;     // FmtSelectStmtMaxLen="50"
    pol.groupByShortListEnabled          = true;   // FmtGroupByShortListEnabled="1"
    pol.groupByShortListMaxLen           = 60;     // FmtGroupByShortListMaxLen="60"
    pol.orderByShortListEnabled          = true;   // FmtOrderByShortListEnabled="1"
    pol.orderByShortListMaxLen           = 60;     // FmtOrderByShortListMaxLen="60"
    pol.setOpBlankLine                   = false;  // FmtSetOpBlankLine="0"
    pol.caseWhenShortMaxLen              = 40;     // FmtCaseWhenShortMaxLen="40"
    pol.returningMaxLen                  = 40;     // FmtReturningMaxLen="40"
    pol.returningMaxItems                = 5;      // FmtReturningMaxItems="5"
    pol.logErrorsMaxLen                  = 40;     // FmtLogErrorsMaxLen="40"
    pol.alignSetAssignments              = true;   // FmtAlignSetAssignments="1"
    pol.alignCaseInSet                   = true;   // FmtAlignCaseInSet="1"
    pol.brickListEnabled                 = false;  // FmtBrickListEnabled="0"
    pol.brickListMaxLineLen              = 80;     // FmtBrickListMaxLineLen="80"
    pol.brickListMaxLineItems            = 10;     // FmtBrickListMaxLineItems="10"
    pol.brickListMinItems                = 5;      // FmtBrickListMinItems="5"
    pol.brickListLinkedInsert            = true;   // FmtBrickListLinkedInsert="1"
    pol.brickListLinkedSetOp             = true;   // FmtBrickListLinkedSetOp="1"

    // ---- PL/SQL-side settings ----
    pol.plsqlDeclBlankLineSeparator      = true;   // FmtPlsqlDeclBlankLineSeparator="1"
    pol.plsqlBlankLineAroundFlowStmts    = true;   // FmtPlsqlBlankLineAroundFlowStmts="1"
    pol.plsqlBlankLineAroundSubprograms  = true;   // FmtPlsqlBlankLineAroundSubprograms="1"
    pol.plsqlBlankLineAfterUnitHeader    = true;   // FmtPlsqlBlankLineAfterUnitHeader="1"
    pol.plsqlDmlBlankLineSeparator       = true;   // FmtPlsqlDmlBlankLineSeparator="1"
    pol.plsqlBlankLineBeforeComment      = true;   // FmtPlsqlBlankLineBeforeComment="1"
    pol.plsqlIsOnNewLine                 = false;  // FmtPlsqlIsOnNewLine="0"
    pol.plsqlParamListMaxLen             = 80;     // FmtPlsqlParamListMaxLen="80"
    pol.plsqlParamListAlignParen         = false;  // FmtPlsqlParamListAlignParen="0"
    // Not present in the captured XML (they post-date it), so these carry the
    // compiled defaults from OESettings.cpp - which is exactly what an
    // installation that has never touched them in the UI gets.
    pol.plsqlEndLineCommentKeepTrailing  = true;   // FmtPlsqlEndLineCommentKeepTrailing="1"
    pol.plsqlEndLineCommentMaxCol        = 120;    // FmtPlsqlEndLineCommentMaxCol="120"

    // ---- expression wrapping (PL/SQL-side source values) ----
    pol.expressionMaxLen                 = 80;     // FmtPlsqlExpressionMaxLen="80"
    pol.concatMaxLen                     = 80;     // FmtPlsqlConcatMaxLen="80"
    pol.concatKeepMixedPairTogether      = true;   // FmtPlsqlConcatKeepMixedPairTogether="1"
    pol.concatBreakBetweenLiterals       = true;   // FmtPlsqlConcatBreakBetweenLiterals="1"

    // ---- SQL-side expression/concat widths (reuse the PL/SQL values, per
    // FormatterPolicy::FromSettings' existing convention) ----
    pol.sqlExpressionMaxLen              = 80;     // FmtPlsqlExpressionMaxLen="80"
    pol.sqlConcatMaxLen                  = 80;     // FmtPlsqlConcatMaxLen="80"

    return pol;
}

// ----------------------------------------------------------------------------
// ".FmtSelection=" support: format only part of the document, which is what drives
// DoEditFormatSql's selection seeding. The spec is 1-based: "<firstLine>-<lastLine>"
// for whole lines, or "<l1>:<c1>-<l2>:<c2>" with an exclusive end column.
// ----------------------------------------------------------------------------
static bool ParseFmtSelection (const std::wstring& spec, int& startLine, int& startCol,
                               int& endLine, int& endCol)
{
    const size_t dash = spec.find(L'-');
    if (dash == std::wstring::npos)
        return false;

    // One side of the range: "line" or "line:column". A side without a column
    // starts at column 0, and an end without one means "to the end of that line".
    auto parseSide = [](const std::wstring& text, int& line, int& column) -> bool
    {
        const size_t colon = text.find(L':');

        try {
            line   = std::stoi(Common::str(text.substr(0, colon))) - 1;
            column = (colon == std::wstring::npos)
                   ? 0
                   : std::stoi(Common::str(text.substr(colon + 1))) - 1;
        }
        catch (...) { return false; }

        return line >= 0 && column >= 0;
    };

    if (!parseSide(spec.substr(0, dash), startLine, startCol))
        return false;
    if (!parseSide(spec.substr(dash + 1), endLine, endCol))
        return false;

    // Whole-line form: the end line is part of the selection, so it ends at
    // column 0 of the line after it - the shape a line selection has in the editor.
    if (spec.find(L':') == std::wstring::npos)
        endCol = 0, endLine++;

    return startLine < endLine || (startLine == endLine && startCol < endCol);
}

// Move the view's selection onto the parsed range. Columns are index-based here
// and converted to display positions, exactly as ColumnAutocompleteHandler does.
static bool ApplyFmtSelection (COEditorView& view, const std::wstring& spec)
{
    int startLine = 0, startCol = 0, endLine = 0, endCol = 0;

    if (!ParseFmtSelection(spec, startLine, startCol, endLine, endCol))
        return false;

    const int nlines = view.GetLineCount();
    if (startLine >= nlines || endLine > nlines)
        return false;

    OpenEditor::Square sel;
    sel.start.line   = startLine;
    sel.start.column = view.InxToPos(startLine, startCol);
    sel.end.line     = endLine;
    sel.end.column   = view.InxToPos(endLine, endCol);

    view.SetSelection(sel);
    return true;
}

bool SqlFormatterHandler::Execute(const std::wstring& payload, std::wstring& out)
{
    out.clear();

    // Parse optional leading parameter lines from the wide payload.
    // Recognised keys (case-sensitive):
    //  .FmtBrickListEnabled=1
    //  .FmtBrickListMaxLineLen=80
    //  .FmtBrickListMaxLineItems=10
    //  .FmtBrickListMinItems=3
    //  .FmtBrickListLinkedInsert=1
    //  .FmtBrickListLinkedSetOp=1
    //  .FmtPlsqlParamListAlignParen=1
    //  .FmtPlsqlExpressionMaxLen=80
    //  .FmtPlsqlConcatMaxLen=80
    //  .FmtPlsqlConcatKeepMixedPairTogether=1
    //  .FmtPlsqlConcatBreakBetweenLiterals=1
    //  .FmtPlsqlEndLineCommentKeepTrailing=1
    //  .FmtPlsqlEndLineCommentMaxCol=120
    //  .FmtSelectShortListMaxLen=45
    //  .FmtTraceTree=1
    //  .FmtSelection=12-20        (1-based lines; also accepts 12:3-20:8)
    // Unrecognised or non-'.' starting line stops the param scan.
    //
    // -1 means "not specified by this test" - leave the live setting alone.
    // That is distinct from 0 for the four *MaxLen keys, where 0 is a meaningful
    // value (= wrapping disabled), so a test can pin expression/concat wrapping
    // off explicitly instead of inheriting whatever the developer's saved
    // settings happen to say. format_expression.ps1 depends on this: it formats
    // one PL/SQL corpus at 0 and at 80 and fails the suite if the two baselines
    // come out byte-identical, which is the only way to detect that the limits
    // have stopped having any effect.
    //
    // These four are PL/SQL-only settings, read solely on the PL/SQL statement
    // path. Pinning them in a pure-SQL suite is inert - that was tried and the
    // pins removed.
    //
    // The two .FmtSql* keys below are their SQL-side counterparts (Phase F3,
    // increment 2), added because an FmtPlsql* pin cannot reach a SQL document.
    // They are registered and pinnable NOW, ahead of F2, so the SQL suites can
    // pin them the moment an injection point exists - both format_expression.ps1
    // and format_delete.ps1 say to do exactly that when F lands.
    //
    // Until F2 wires an injection point nothing reads them, so a pin is inert
    // rather than wrong: output is unchanged and the suites still compare
    // byte-identically. That is deliberate - it lets the keys be exercised by the
    // harness (round-tripped through Settings, saved and restored) before any
    // formatter behaviour depends on them.
    // See F3/F4 in md/sql_plsql_formatter_integration_plan.md.
    //
    // .FmtTraceTree=1 — like .FmtSelection= below — is NOT a Settings pin: it is a
    // request to this handler to capture formatPlsql()'s syntax-tree dump
    // (OpenEditor::FormatterTreeTraceScope) and prepend it to the returned
    // text wrapped in <<<SYNTAX-TREE>>> ... <<<END-SYNTAX-TREE>>> markers,
    // so a suite can baseline the parsed tree shape itself - e.g. that a
    // package's "end pkg_example;" closes the Block instead of leaving a
    // stray "UnknownStmt [pkg_example ;]" sibling behind it.
    const int kMaxParamLines = 20;    // was 3, which could not express F's two
                                      // wrapping limits alongside the brick-list
                                      // keys in a single run. Raised to 20 now
                                      // that 18 keys are recognised, so a suite
                                      // can pin all of them with margin. This
                                      // limit has to stay ahead of the key count
                                      // because exceeding it is silent - the scan
                                      // just stops and later keys are ignored.
    int fmtBrickListEnabled      = -1;
    int fmtBrickListMaxLineLen    = -1;
    int fmtBrickListMaxLineItems  = -1;
    int fmtBrickListMinItems      = -1;
    int fmtBrickListLinkedInsert  = -1;
    int fmtBrickListLinkedSetOp   = -1;
    int fmtPlsqlParamListAlignParen = -1;
    int fmtPlsqlParamListMaxLen     = -1;
    int fmtPlsqlExpressionMaxLen    = -1;
    int fmtPlsqlConcatMaxLen        = -1;
    int fmtPlsqlConcatKeepMixedPairTogether = -1;
    int fmtPlsqlConcatBreakBetweenLiterals  = -1;
    int fmtPlsqlEndLineCommentKeepTrailing  = -1;
    int fmtPlsqlEndLineCommentMaxCol        = -1;
    int fmtSelectShortListMaxLen            = -1;
    int fmtTraceTree = -1;

    // Empty means "no .FmtSelection directive": format the whole document, as
    // before. Non-empty is handed to ApplyFmtSelection instead of SelectAll().
    std::wstring fmtSelection;

    size_t sqlStartPos = 0; // index in payload where SQL text starts
    {
        size_t pos = 0;
        int parsed = 0;
        while (parsed < kMaxParamLines && pos < payload.size())
        {
            // find end of current line (include only '\n' as delimiter; handle CR if present)
            size_t nl = payload.find(L'\n', pos);
            size_t lineEnd = (nl == std::wstring::npos) ? payload.size() : nl;
            // extract and trim line
            std::wstring line = payload.substr(pos, lineEnd - pos);
            // trim leading spaces
            size_t a = 0;
            while (a < line.size() && iswspace(line[a])) ++a;
            size_t b = line.size();
            while (b > a && iswspace(line[b-1])) --b;
            if (a >= b)
            {
                // empty line — treat as end of param block
                sqlStartPos = pos;
                break;
            }
            std::wstring trimmed = line.substr(a, b - a);

            bool consumed = false;
            if (!trimmed.empty() && trimmed[0] == L'.')
            {
                // parse key=value
                size_t eq = trimmed.find(L'=');
                if (eq != std::wstring::npos && eq+1 < trimmed.size())
                {
                    std::wstring key = trimmed.substr(0, eq);
                    std::wstring val = trimmed.substr(eq+1);

                    // The only non-integer directive, so it is captured verbatim
                    // here and the integer parse below is skipped for it.
                    if (key == L".FmtSelection") { fmtSelection = val; consumed = true; }

                    // simple integer parse
                    if (!consumed) try {
                        int ival = std::stoi(Common::str(val));
                        if (key == L".FmtBrickListEnabled") { fmtBrickListEnabled = ival; consumed = true; }
                        else if (key == L".FmtBrickListMaxLineLen") { fmtBrickListMaxLineLen = ival; consumed = true; }
                        else if (key == L".FmtBrickListMaxLineItems") { fmtBrickListMaxLineItems = ival; consumed = true; }
                        else if (key == L".FmtBrickListMinItems") { fmtBrickListMinItems = ival; consumed = true; }
                        else if (key == L".FmtBrickListLinkedInsert") { fmtBrickListLinkedInsert = ival; consumed = true; }
                        else if (key == L".FmtBrickListLinkedSetOp") { fmtBrickListLinkedSetOp = ival; consumed = true; }
                        else if (key == L".FmtPlsqlParamListAlignParen") { fmtPlsqlParamListAlignParen = ival; consumed = true; }
                        else if (key == L".FmtPlsqlParamListMaxLen") { fmtPlsqlParamListMaxLen = ival; consumed = true; }
                        else if (key == L".FmtPlsqlExpressionMaxLen") { fmtPlsqlExpressionMaxLen = ival; consumed = true; }
                        else if (key == L".FmtPlsqlConcatMaxLen") { fmtPlsqlConcatMaxLen = ival; consumed = true; }
                        else if (key == L".FmtPlsqlConcatKeepMixedPairTogether") { fmtPlsqlConcatKeepMixedPairTogether = ival; consumed = true; }
                        else if (key == L".FmtPlsqlConcatBreakBetweenLiterals") { fmtPlsqlConcatBreakBetweenLiterals = ival; consumed = true; }
                        else if (key == L".FmtPlsqlEndLineCommentKeepTrailing") { fmtPlsqlEndLineCommentKeepTrailing = ival; consumed = true; }
                        else if (key == L".FmtPlsqlEndLineCommentMaxCol") { fmtPlsqlEndLineCommentMaxCol = ival; consumed = true; }
                        else if (key == L".FmtSelectShortListMaxLen") { fmtSelectShortListMaxLen = ival; consumed = true; }
                        else if (key == L".FmtTraceTree") { fmtTraceTree = ival; consumed = true; }
                    }
                    catch (...) { /* ignore parse errors */ }
                }
            }

            if (!consumed)
            {
                // this line is not a parameter — SQL starts here
                sqlStartPos = pos;
                break;
            }

            // consumed this parameter line — move to next
            ++parsed;
            if (nl == std::wstring::npos)
            {
                // no more lines
                sqlStartPos = payload.size();
                break;
            }
            pos = nl + 1;
            // skip a single trailing CR if present at start of next line (handled by trim)
            sqlStartPos = pos;
        }
    }

    // Create a new worksheet document to avoid touching the active one
    CDocTemplate* pDocTemplate = theApp.GetPLSDocTemplate();
    if (!pDocTemplate)
        return false;

    CPLSWorksheetDoc* pDoc = (CPLSWorksheetDoc*)pDocTemplate->OpenDocumentFile(NULL);
    if (!pDoc)
        return false;

    // Use only the SQL part after any parsed header parameters
    std::string text = Common::str(payload.substr(sqlStartPos));
    pDoc->DefaultFileFormat();
    pDoc->SetText(text.c_str(), text.length());

    COEditorView* pEditor = pDoc->GetFirstEditorView();
    if (pEditor)
    {
        // ".FmtSelection=" narrows the format to part of the document; a malformed
        // or out-of-range spec falls back to the whole document rather than failing.
        if (fmtSelection.empty() || !ApplyFmtSelection(*pEditor, fmtSelection))
            pEditor->SelectAll();

        // Deterministic policy — see md/FormatterPolicy_proposal.md. Starting
        // from FormatterPolicy::Defaults() (the hardcoded production defaults)
        // rather than the developer's live GlobalSettings means test output
        // cannot shift when the developer changes their own formatter
        // preferences; the parsed ".Fmt*=" pins are applied directly onto the
        // policy fields below, with no GlobalSettings mutation/restore at all.
        OpenEditor::FormatterPolicy policy = MakeFormatterPolicyFromCapturedSettingsXml();


        // Formatter self-checks (EmitRangeGuard, verifyFormatterLosslessness,
        // verifyOperatorTokenization) report a failure through a modal dialog,
        // which blocks the UI thread and makes this COM-driven handler time out
        // (SQLTools.Proxy waits 10 s). The scope redirects those reports into
        // `diagnostics` and also mutes the CRT assert dialog for anything else
        // raised further down.
        std::wstring diagnostics;
        OpenEditor::FormatterDiagnosticsScope diagScope(diagnostics);

        // With the .at() indexing hardening in the OEPlsSqlFormatter* sources,
        // a stale token index in the formatter now arrives here as a catchable
        // std::out_of_range instead of crashing the UI process. Record what()
        // plus a stack trace into `diagnostics`, which flows into the returned
        // text (<<<FORMATTER-DIAGNOSTICS>>>) and thus into the corpus-stress
        // artifact for the failing file. Caveat: the trace is captured at the
        // catch point - the throwing frames are already unwound by then - so
        // it proves the exception kind and the path through the handler, but
        // not the exact .at() site; pinpoint that with a debugger session
        // using first-chance exceptions (Debug > Windows > Exception Settings).
        auto recordFormatterException = [&diagnostics](const std::exception* e)
        {
            std::string trace;
            Common::StackTracer::Trace(trace);  // local PDBs only, no symbol server

            diagnostics += L"[EXCEPTION] ";
            if (e)
            {
                diagnostics += L"std::exception during OnEditFormatSql: ";
                diagnostics += (LPCWSTR)CStringW(e->what());
            }
            else
            {
                diagnostics += L"unknown exception during OnEditFormatSql";
            }
            diagnostics += L"\n[STACK TRACE at catch point]\n";
            diagnostics += (LPCWSTR)CStringW(trace.c_str());
            diagnostics += L"\n";
        };

        // ".FmtTraceTree=1" is not a Settings pin but a request to this
        // handler: while the scope below is alive, formatPlsql() captures its
        // syntax-tree dump into `treeTrace` (in ANY build configuration),
        // which is then returned ahead of the formatted text wrapped in
        // <<<SYNTAX-TREE>>> markers. That lets a suite baseline the *parsed
        // tree shape* - e.g. that "end pkg_example;" closes the package
        // Block instead of leaving a stray UnknownStmt node behind it.
        std::wstring treeTrace;
        std::unique_ptr<OpenEditor::FormatterTreeTraceScope> treeScope;
        if (fmtTraceTree == 1)
            treeScope.reset(new OpenEditor::FormatterTreeTraceScope(treeTrace));

        try
        {
            if (fmtBrickListEnabled != -1)
                policy.brickListEnabled = fmtBrickListEnabled ? true : false;
            if (fmtBrickListMaxLineLen != -1)
                policy.brickListMaxLineLen = fmtBrickListMaxLineLen;
            if (fmtBrickListMaxLineItems != -1)
                policy.brickListMaxLineItems = fmtBrickListMaxLineItems;
            if (fmtBrickListMinItems != -1)
                policy.brickListMinItems = fmtBrickListMinItems;
            if (fmtBrickListLinkedInsert != -1)
                policy.brickListLinkedInsert = fmtBrickListLinkedInsert ? true : false;
            if (fmtBrickListLinkedSetOp != -1)
                policy.brickListLinkedSetOp = fmtBrickListLinkedSetOp ? true : false;
            if (fmtPlsqlParamListAlignParen != -1)
                policy.plsqlParamListAlignParen = fmtPlsqlParamListAlignParen ? true : false;
            if (fmtPlsqlParamListMaxLen != -1)
                policy.plsqlParamListMaxLen = fmtPlsqlParamListMaxLen;
            if (fmtPlsqlExpressionMaxLen != -1)
            {
                policy.expressionMaxLen = fmtPlsqlExpressionMaxLen;
                policy.sqlExpressionMaxLen = fmtPlsqlExpressionMaxLen;
            }
            if (fmtPlsqlConcatMaxLen != -1)
            {
                policy.concatMaxLen = fmtPlsqlConcatMaxLen;
                policy.sqlConcatMaxLen = fmtPlsqlConcatMaxLen;
            }
            if (fmtPlsqlConcatKeepMixedPairTogether != -1)
                policy.concatKeepMixedPairTogether = fmtPlsqlConcatKeepMixedPairTogether ? true : false;
            if (fmtPlsqlConcatBreakBetweenLiterals != -1)
                policy.concatBreakBetweenLiterals = fmtPlsqlConcatBreakBetweenLiterals ? true : false;
            if (fmtPlsqlEndLineCommentKeepTrailing != -1)
                policy.plsqlEndLineCommentKeepTrailing = fmtPlsqlEndLineCommentKeepTrailing ? true : false;
            if (fmtPlsqlEndLineCommentMaxCol != -1)
                policy.plsqlEndLineCommentMaxCol = fmtPlsqlEndLineCommentMaxCol;
            // SQL-side key, so it lands on the SQL half of the policy. It was
            // already fixed in the captured-settings snapshot above but not
            // pinnable per-run, which meant a suite could not exercise the
            // short-list limit the way format_expression.ps1 exercises the
            // *MaxLen wrapping keys.
            if (fmtSelectShortListMaxLen != -1)
                policy.selectShortListMaxLen = fmtSelectShortListMaxLen;

            pEditor->DoEditFormatSql(policy);
        }
        catch (const std::exception& e)
        {
            // .at() hardening: formatter index bugs surface here (typically
            // std::out_of_range) - report them instead of swallowing silently.
            recordFormatterException(&e);
        }
        catch (...)
        {
            recordFormatterException(nullptr);
        }

        pEditor->GetBlock(out);

        if (!treeTrace.empty())
        {
            // Requested via .FmtTraceTree=1: emit the captured dump ahead of
            // the formatted text. The <<<FORMATTED-OUTPUT>>> marker mirrors
            // the diagnostics layout below; the two can coexist (diagnostics
            // then wrap everything, and the run fails on the self-check).
            out = L"<<<SYNTAX-TREE>>>\n"
                + treeTrace
                + L"<<<END-SYNTAX-TREE>>>\n"
                + L"<<<FORMATTED-OUTPUT>>>\n"
                + out;
        }

        if (!diagnostics.empty())
        {
            // Surface the self-check failure to the caller instead of silently
            // returning broken output; baseline diffs then show it directly.
            out = L"<<<FORMATTER-DIAGNOSTICS>>>\n"
                + diagnostics
                + L"<<<END-FORMATTER-DIAGNOSTICS>>>\n"
                + L"<<<FORMATTED-OUTPUT>>>\n"
                + out;
            //return true;
        }

        pDoc->SetModifiedFlag(FALSE);
        pEditor->GetParentFrame()->SendMessage(WM_CLOSE);

        return true;
    }
    
    return false;
}