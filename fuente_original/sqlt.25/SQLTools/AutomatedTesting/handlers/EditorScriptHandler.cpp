#include "stdafx.h"
#include "EditorScriptHandler.h"

#include "SQLWorksheet/SQLWorksheetDoc.h"
#include "OpenEditor/OEDocument.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OESettings.h"
#include "OpenEditor/OEStorage.h"
#include "OpenEditor/OEFolding.h"
#include "OpenEditor/OEHelpers.h"
#include "OpenEditor/OEPlsSqlSupport.h"
#include "Common/MyUtf.h"
#include "Common/AppUtilities.h"
#include "App/SQLTools.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// Script grammar
// =============================================================================
//
// UTF-16 payload, one directive per line. `#` starts a comment - a whole line, or
// trailing after a directive's arguments. Only a trailing CR is stripped: spaces
// are never trimmed, because `type |...|` may legitimately want them.
//
// An unknown name, a malformed argument or a failed check is a hard ERROR that
// aborts the run rather than a line to skip, and the transcript ends with a
// machine-readable `RESULT checks=<n> pass=<n> fail=<n>` that the PowerShell
// library parses - so a script error can never masquerade as a green baseline.
//
//   .case <name>            "CASE <name>" banner; resets the document, caret, block
//                           mode and overtype. Settings are NOT reset - pin per case.
//   .lang <ClassName>       Switch the document's language class. Echoes back the class
//                           the document actually ended up with.
//   .text |...|             Replace the whole document. `\n` separates lines.
//   .caret <L>:<C>          Move the caret (1-based line:column).
//   .sel <L1>:<C1> <L2>:<C2>
//                           Stream selection, 1-based: inclusive start, exclusive end.
//   .colsel <L1>:<C1> <L2>:<C2>
//                           The same square, entered in column block mode first.
//   .mode stream|column     Set the block mode without touching the selection.
//   .overtype on|off        Press VK_INSERT - the flag's only writer - and only when the
//                           state differs, because the flag is process-wide.
//   .type |...|             Send the text one WM_CHAR per UTF-16 code unit, which is what
//                           a keyboard delivers: a surrogate pair arrives as two messages.
//   .key <Name>             Send one key as WM_KEYDOWN+WM_KEYUP, or as WM_CHAR for the
//                           keys a keyboard delivers that way (Tab, Return).
//   .key <Mod>+<Name>       The same with a modifier held down - `Shift+Tab`, `Ctrl+U`,
//                           `Alt+Shift+Left`; Shift/Ctrl/Control/Alt, in any order and any
//                           combination. The view decides chords by calling GetKeyState rather
//                           than by reading the message's key flags, so the modifier is published
//                           into the thread key-state array for the duration of the dispatch and
//                           restored afterwards. Without this a scripted Shift+Tab is
//                           indistinguishable from a plain Tab.
//   .cmd <Name>             Post a named editor command as WM_COMMAND on the view's parent
//                           frame - the route a menu item or accelerator takes.
//   .undo [N] / .redo [N]   Sugar for `cmd EditUndo` / `cmd EditRedo`, N times (default 1).
//   .settings <k>=<v>[,...]  Pin global settings, by the names in the bool and int tables
//                           below, as 0|1 or on|off. Restored at run end, on abort too.
//   .indenttype none|auto|smart
//                           Pin the DOCUMENT's IndentType - an OpenEditor::EIndent. It is a
//                           class setting, not a global one, which is why it is not a
//                           `.settings` key. Sticky across cases, restored at run end.
//   .clipboard |...| [code][,columnar]
//                           Put a payload on the clipboard shaped like SQLTools' own copy
//                           output; the two options add the markers paste reacts to.
//   .clipboard clear        Empty the clipboard - no formats at all, so EditPaste disables.
//                           Distinct from `.clipboard ||`, which leaves an empty text format.
//   .supportmark            Record the installed PlSqlSupport's identity for `.expect
//                           supportsame`. Cleared by `.case`; a script error without PL/SQL.
//   .foldsettle [N]         Settle the background parse and let UpdateFoldsFromOutline build
//                           regions, looping the idle path up to N times (default 64). Every
//                           fold and outline directive below needs this first.
//   .fold <L> collapse|expand|toggle
//                           Put the region anchored at 1-based line L into a known state via the
//                           production path a gutter click takes. `toggle` on a non-anchor is a no-op.
//   .paneoutline            Digest of the PANE product: what the Code Outline pane would be
//                           handed from the same extraction folding read.
//   .outlinequality         Digest of the committed FOLD product's quality - the only oracle
//                           for oqTruncated and oqSyntaxError.
//   .outlinetrust           Per-item trust of the committed FOLD product, beside the parse cut
//                           line it is measured against. Measurement only.
//   .linestatus             Per-buffer-line gutter status read over the WHOLE buffer, so the
//                           numbers cannot depend on the scroll position. Measurement only.
//   .search |text|          Set the find pattern without opening the modeless dialog. Search
//                           options are persisted settings, so a case pins them via `.settings`.
//   .replaceall |replacement|
//                           Whole-document replace. The argument is the REPLACEMENT, not the
//                           pattern: the pattern is whatever `.search` set last.
//   .echo <text>            Emit a free-form transcript line, prefixed with "#".
//
// Assertions - each emits "expect <what> <expected> -> PASS" or, on mismatch,
// "expect <what> expected=... actual=... -> FAIL (line N)". Both sides are rendered to
// strings first, so the FAIL line shows the difference verbatim.
//
//   .expect text |...|      Whole document text. A trailing newline in the expectation
//                           matches the document's implicit final line break.
//   .expect caret <L>:<C>   Caret position.
//   .expect sel <L1>:<C1> <L2>:<C2>
//                           Selection square, compared after normalize() so it does not
//                           depend on drag direction. Also takes the "-" joined spelling.
//   .expect nosel           The selection is empty - rendered as a sentinel, so it cannot
//                           read the same as a selection OF empty text.
//   .expect seltext |...|   The selection as GetBlock() reports it, i.e. column-aware,
//                           CRLF-folded first so `\n` means what it means in `.text`.
//   .expect canundo 0|1     Storage::CanUndo.
//   .expect canredo 0|1     Storage::CanRedo.
//   .expect modified 0|1    Storage::IsModified.
//   .expect mode stream|column
//   .expect overtype 0|1
//   .expect clipboard |...| CF_UNICODETEXT as it reads back, or `<empty>` when the format is
//                           ABSENT - not the same state as an empty string. Not CRLF-folded.
//   .expect clipboardcode 0|1
//                           CF_PRIVATEFIRST is present and reads "{code}".
//   .expect clipboardcolumnar 0|1
//                           The OpenEditor.ColumnarText tag is on the clipboard, i.e. the
//                           next paste switches the view to column mode.
//
// Folding - lines are 1-based buffer lines, rows 0-based visual rows, the model 0-based:
//
//   .expect regions <n>     FoldModel::Count() - how many regions exist at all.
//   .expect region <i> <start>:<end>:<collapsed>
//                           One region's geometry, 1-based inclusive, plus its flag. An
//                           out-of-range <i> is a script bug and aborts.
//   .expect anchor <L> 0|1  Line L is a fold anchor, i.e. carries a [+]/[-] glyph.
//   .expect collapsed <L> 0|1
//                           The region anchored at L is collapsed.
//   .expect currentregion <L> <start>:<end>|none
//                           The innermost available region spanning L - the rows the
//                           gutter's current-region tint fills when the caret is on L.
//   .expect hidden <L> 0|1  L does not occupy a visual row.
//   .expect visrows <n>     Buffer lines minus the UNION of hidden ranges: the value the
//                           scrollbar is sized from.
//   .expect rowof <L> <row> The visual row L renders on, or its marker's row when hidden.
//   .expect topmost <L>     The view's first visible line, 1-based.
//
// Outline quality and trust - these read the live document's PL/SQL support, so they need
// PlSqlOutlineEnabled pinned on and a `.lang PL/SQL` before them:
//
//   .expect quality <name>  oqClean | oqTruncated | oqSyntaxError, by enumerator NAME so a
//                           fourth value fails loudly instead of arriving as a numeric diff.
//   .expect items <n>       Size of the COMMITTED snapshot folding is handed, not the pane's.
//   .expect modelclean 0|1  FoldModel::IsSnapshotClean(), the far end of the chain.
//   .expect untrusted <n>   Committed items carrying rangeTrusted == false: the subtree rule.
//   .expect pastcut <n>     Committed items ending at or past the parse cut line: the line
//                           rule, computed here so the two can be asserted against each other.
//   .expect cutline <n>|-   GetParseCutLine(), 1-based, or `-` for a parse that reached the
//                           end of the buffer - the spelling `.outlinetrust` prints.
//   .expect unparsed <n>    Buffer lines carrying LineStatus::unparsed. A COUNT, so `-` is
//                           rejected: 0 is the honest "none", and a second spelling would let
//                           a truncation expectation pass two ways.
//   .expect unparsedfrom <n>|-
//                           The first such line, 1-based, or `-`: the boundary the gutter
//                           draws its one horizontal rule at.
//   .expect unparsedboundary <n>|-
//                           The line carrying LineStatus::unparsedBoundary, or `-`. Equal to
//                           `unparsedfrom` by construction, and asserted separately because
//                           the two diverge in exactly the ways worth catching.
//
// Language-support identity - the oracles for "a settings change must not reparse the
// document". The support is rebuilt only when the KIND the settings now ask for differs
// from the kind installed; every other change is forwarded to the installed object. For a
// settled document the two behaviours converge on the same regions and outline, so
// identity is the only observable difference and it gets pinned.
//
//   .expect supportkind plsql|common
//                           LanguageSupport::GetKind() by NAME. With PlSqlKeywordNavigation
//                           off it must read `common`; `plsql` means an object was kept that
//                           the settings no longer ask for.
//   .expect supportsame 0|1 Whether the installed PlSqlSupport is STILL the one `.supportmark`
//                           recorded: 1 = refreshed in place, 0 = replaced.
// =============================================================================

namespace
{
    using OpenEditor::Position;
    using OpenEditor::Square;
    using OpenEditor::EBlockMode;

    // ---------------------------------------------------------------- text ---

    // Renders a string for the transcript: TAB and the control range become \-escapes
    // so every reported value stays on one readable, diffable line. A TAB in
    // particular must not be invisible in the baseline.
    std::wstring Render(const std::wstring& s)
    {
        std::wstring o;
        o.reserve(s.size() + 8);

        for (size_t i = 0; i < s.size(); i++)
        {
            const wchar_t c = s[i];

            switch (c)
            {
            case L'\\': o += L"\\\\"; break;
            case L'\t': o += L"\\t";  break;
            case L'\r': o += L"\\r";  break;
            case L'\n': o += L"\\n";  break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    wchar_t b[16];
                    swprintf_s(b, _countof(b), L"\\x%04X", static_cast<unsigned>(c));
                    o += b;
                }
                else
                    o += c;
            }
        }

        return o;
    }

    // Inverse of Render for script arguments: \\ \n \r \t \0 and \uXXXX.
    // Anything else after a backslash is an error rather than a silent
    // pass-through - a typo in a baseline expectation must not quietly change
    // what the test asserts.
    bool Unescape(const std::wstring& s, std::wstring& out, std::wstring& err)
    {
        out.clear();

        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] != L'\\')
            {
                out += s[i];
                continue;
            }

            if (i + 1 >= s.size())
            {
                err = L"trailing backslash";
                return false;
            }

            const wchar_t e = s[++i];

            switch (e)
            {
            case L'\\': out += L'\\'; break;
            case L'n':  out += L'\n'; break;
            case L'r':  out += L'\r'; break;
            case L't':  out += L'\t'; break;
            case L'0':  out += L'\0'; break;
            case L'u':
                {
                    if (i + 4 >= s.size())
                    {
                        err = L"\\u needs four hex digits";
                        return false;
                    }

                    unsigned v = 0;
                    for (int k = 0; k < 4; k++)
                    {
                        const wchar_t h = s[i + 1 + k];
                        v <<= 4;
                        if (h >= L'0' && h <= L'9')                   v |= static_cast<unsigned>(h - L'0');
                        else if (h >= L'a' && h <= L'f')              v |= static_cast<unsigned>(h - L'a' + 10);
                        else if (h >= L'A' && h <= L'F')              v |= static_cast<unsigned>(h - L'A' + 10);
                        else { err = L"\\u needs four hex digits"; return false; }
                    }

                    out += static_cast<wchar_t>(v);
                    i += 4;
                    break;
                }
            default:
                {
                    wchar_t b[64];
                    swprintf_s(b, _countof(b), L"unknown escape '\\%c'", e);
                    err = b;
                    return false;
                }
            }
        }

        return true;
    }

    // Splits on '\n' and drops a single trailing '\r' per line. Nothing else is
    // trimmed: leading indentation is significant for the tab-related cases and
    // trailing spaces are significant for `type`.
    std::vector<std::wstring> SplitLines(const std::wstring& text)
    {
        std::vector<std::wstring> v;
        size_t pos = 0;

        while (pos <= text.size())
        {
            const size_t nl = text.find(L'\n', pos);
            std::wstring line = (nl == std::wstring::npos)
                ? text.substr(pos)
                : text.substr(pos, nl - pos);

            if (!line.empty() && line[line.size() - 1] == L'\r')
                line.erase(line.size() - 1);

            v.push_back(line);

            if (nl == std::wstring::npos)
                break;

            pos = nl + 1;
        }

        return v;
    }

    // First whitespace-separated token; `rest` receives what follows it (one
    // leading space removed, nothing else touched).
    std::wstring Head(const std::wstring& s, std::wstring& rest)
    {
        size_t i = 0;
        while (i < s.size() && s[i] != L' ' && s[i] != L'\t')
            i++;

        rest = (i < s.size()) ? s.substr(i + 1) : std::wstring();
        return s.substr(0, i);
    }

    bool ParseInt(const std::wstring& s, int& out, std::wstring& err)
    {
        if (s.empty())
        {
            err = L"expected a number";
            return false;
        }

        for (size_t i = 0; i < s.size(); i++)
            if (!iswdigit(s[i]))
            {
                err = L"'" + s + L"' is not a number";
                return false;
            }

        out = _wtoi(s.c_str());
        return true;
    }

    // "L:C", 1-based, as every script and transcript uses. Position itself is
    // 0-based with `column` declared first (OEHelpers.h), so the
    // conversion happens here and nowhere else.
    bool ParsePos(const std::wstring& s, Position& out, std::wstring& err)
    {
        const size_t c = s.find(L':');

        if (c == std::wstring::npos)
        {
            err = L"'" + s + L"' is not a L:C position";
            return false;
        }

        int line = 0, col = 0;

        if (!ParseInt(s.substr(0, c), line, err) || !ParseInt(s.substr(c + 1), col, err))
            return false;

        if (line < 1 || col < 1)
        {
            err = L"'" + s + L"' is not 1-based";
            return false;
        }

        out.line = line - 1;
        out.column = col - 1;
        return true;
    }

    std::wstring FmtPos(const Position& p)
    {
        wchar_t b[32];
        swprintf_s(b, _countof(b), L"%d:%d", p.line + 1, p.column + 1);
        return b;
    }

    std::wstring FmtSquare(const Square& s)
    {
        return FmtPos(s.start) + L"-" + FmtPos(s.end);
    }

    // "|...|" delimited argument, unescaped. The delimiters keep arguments that
    // begin or end with a space expressible, which `type` and `text` need.
    bool ParseDelimited(const std::wstring& s, std::wstring& out, std::wstring& err)
    {
        if (s.size() < 2 || s[0] != L'|' || s[s.size() - 1] != L'|')
        {
            err = L"argument must be delimited as |...|";
            return false;
        }

        return Unescape(s.substr(1, s.size() - 2), out, err);
    }

    // Strips a trailing "# comment" from a directive line. A '#' inside a
    // |...| argument is left alone, so `type |#1|` still works.
    std::wstring StripComment(const std::wstring& line)
    {
        bool inDelim = false;

        for (size_t i = 0; i < line.size(); i++)
        {
            if (line[i] == L'|')
                inDelim = !inDelim;
            else if (line[i] == L'#' && !inDelim)
                return line.substr(0, i);
        }

        return line;
    }

    std::wstring TrimEnd(const std::wstring& s)
    {
        size_t n = s.size();
        while (n && (s[n - 1] == L' ' || s[n - 1] == L'\t'))
            --n;
        return s.substr(0, n);
    }

    bool EqualsNoCase(const std::wstring& a, const wchar_t* b)
    {
        return _wcsicmp(a.c_str(), b) == 0;
    }

    std::wstring TrimStart(const std::wstring& s)
    {
        size_t i = 0;
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t'))
            i++;
        return s.substr(i);
    }

    std::wstring Trim(const std::wstring& s)
    {
        return TrimStart(TrimEnd(s));
    }

    std::wstring Lower(const std::wstring& s)
    {
        std::wstring r = s;
        for (size_t i = 0; i < r.size(); i++)
            r[i] = static_cast<wchar_t>(towlower(r[i]));
        return r;
    }

    std::wstring Num(int n)
    {
        wchar_t b[24];
        swprintf_s(b, _countof(b), L"%d", n);
        return b;
    }

    // Splits on a separator, keeping empty fields (so "a,,b" is three items and
    // a trailing comma is a visible script bug rather than a silent no-op).
    std::vector<std::wstring> SplitOn(const std::wstring& s, wchar_t sep)
    {
        std::vector<std::wstring> v;
        size_t pos = 0;

        while (pos <= s.size())
        {
            const size_t next = s.find(sep, pos);

            v.push_back((next == std::wstring::npos)
                ? s.substr(pos)
                : s.substr(pos, next - pos));

            if (next == std::wstring::npos)
                break;

            pos = next + 1;
        }

        return v;
    }

    // Splits on runs of spaces/tabs, dropping empties.
    std::vector<std::wstring> SplitWs(const std::wstring& s)
    {
        std::vector<std::wstring> v;
        size_t i = 0;

        while (i < s.size())
        {
            while (i < s.size() && (s[i] == L' ' || s[i] == L'\t'))
                i++;

            const size_t start = i;

            while (i < s.size() && s[i] != L' ' && s[i] != L'\t')
                i++;

            if (i > start)
                v.push_back(s.substr(start, i - start));
        }

        return v;
    }

    // "L1:C1 L2:C2" or "L1:C1-L2:C2" - both spellings are accepted so a script
    // can write the same form in `.sel` (action) and `.expect sel` (assertion).
    bool ParseSquare(const std::wstring& s, Square& out, std::wstring& err)
    {
        std::vector<std::wstring> toks = SplitWs(s);

        if (toks.size() == 1)
            toks = SplitOn(toks[0], L'-');

        if (toks.size() != 2)
        {
            err = L"expected 'L1:C1 L2:C2', got '" + s + L"'";
            return false;
        }

        if (!ParsePos(Trim(toks[0]), out.start, err) ||
            !ParsePos(Trim(toks[1]), out.end,   err))
            return false;

        return true;
    }

    bool ParseBool(const std::wstring& s, bool& out, std::wstring& err)
    {
        const std::wstring v = Lower(Trim(s));

        if (v == L"1" || v == L"on"  || v == L"true"  || v == L"yes") { out = true;  return true; }
        if (v == L"0" || v == L"off" || v == L"false" || v == L"no")  { out = false; return true; }

        err = L"'" + s + L"' is not a boolean (use 0/1 or on/off)";
        return false;
    }

    // --------------------------------------------------------- editor state ---

    // GetLineW, not GetBlock: with no square GetBlock returns the *selection*, and with
    // one it honours the block mode, so neither reports a column-mode document faithfully.
    //
    // The separator goes BETWEEN lines, not after every one: appending it after the last
    // line as well would make `.text |abc|` and `.expect text |abc|` disagree about the
    // same document, and an empty document would read as "\n" rather than "".
    std::wstring DocText(COEditorView* v)
    {
        std::wstring t;
        const int n = v->GetLineCount();

        for (int i = 0; i < n; i++)
        {
            if (i)
                t += L'\n';

            OpenEditor::OEStringW line;
            v->GetLineW(i, line);
            t.append(line.data(), line.length());
        }

        return t;
    }

    // In-place CRLF -> LF, for editor strings compared against script text written with
    // `\n`. A lone CR is left alone: it is not a separator here, and Render escapes it so
    // it stays visible in the transcript instead of being folded away.
    void FoldCRLF(std::wstring& s)
    {
        size_t w = 0;

        for (size_t r = 0; r < s.size(); r++)
        {
            if (s[r] == L'\r' && r + 1 < s.size() && s[r + 1] == L'\n')
                continue;

            s[w++] = s[r];
        }

        s.resize(w);
    }

    // -------------------------------------------------------------------------
    // Clipboard plumbing for `.clipboard` and the three `.expect clipboard*` checks.
    //
    // The Common helpers cannot be used standalone: neither opens a clipboard session, and
    // SetClipboardData fails outright when the clipboard is not open. The session is managed
    // here and the helpers are used inside it, as the production copy and paste paths do.
    //
    // Every format has to go up in ONE session. A helper that EmptyClipboard()s per call
    // would leave only the last format behind, and a case written to test the "{code}" paste
    // path would quietly test a plain foreign-text paste - still green, still meaningless.
    //
    // The three formats a paste can observe:
    //
    //   CF_UNICODETEXT              the payload, and the only format OnEditPaste reads or
    //                               OnUpdate_EditPaste enables the command for - a clipboard
    //                               holding only CF_TEXT leaves EditPaste greyed out.
    //   CF_PRIVATEFIRST = "{code}"  routes the payload through AlignCodeFragment first. Set by
    //                               the object tree, the code outline and the result grid, and
    //                               never by the editor's own copy - which is precisely why a
    //                               script has to be able to fake it.
    //   OpenEditor.ColumnarText     a bare tag that makes paste switch into alt-columnar mode.
    //                               Its id is read through COEditorView's own accessor rather
    //                               than a second RegisterClipboardFormat here, which would
    //                               agree with it only by luck.
    // -------------------------------------------------------------------------

    // OpenClipboard fails whenever some other window holds the clipboard open - a clipboard
    // manager, Windows clipboard history, an RDP redirector, another application mid-copy.
    // That is transient contention rather than an error, and the documented response is to ask
    // again. Without the retry a `.cmd EditCopy` can leave the clipboard untouched and a case
    // fails on a payload nobody wrote: intermittent, and looking like an editor regression.
    //
    // A read that keeps failing is reported as an ERROR by the callers below rather than as an
    // absent format; see EClipboardRead.
    bool OpenClipboardRetry (HWND owner, int attempts = 25, DWORD sleepMs = 20)
    {
        for (int i = 0; i < attempts; i++)
        {
            if (::OpenClipboard(owner))
                return true;

            ::Sleep(sleepMs);
        }

        return false;
    }

    // Publishes one format inside an already-open session. On success ownership of
    // the handle passes to the clipboard; on failure it is freed here,
    // SetClipboardData having declined it.
    bool SetClipboardFormat (UINT format, const void* data, size_t bytes,
                             std::wstring& err)
    {
        HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
        void*   p = h ? ::GlobalLock(h) : 0;

        if (!p)
        {
            if (h)
                ::GlobalFree(h);

            err = L"GlobalAlloc/GlobalLock failed";
            return false;
        }

        memcpy(p, data, bytes);
        ::GlobalUnlock(h);

        if (!::SetClipboardData(format, h))
        {
            ::GlobalFree(h);

            wchar_t b[64];
            swprintf_s(b, _countof(b), L"SetClipboardData failed for format %u", format);
            err = b;
            return false;
        }

        return true;
    }

    // Opens, empties, fills and closes - one session, every format. `text` is
    // published even when empty, so `.clipboard ||` means "paste is enabled and
    // inserts nothing" rather than "paste is disabled".
    bool SetClipboardPayload (HWND owner, const std::wstring& text, bool code,
                              bool columnar, std::wstring& err)
    {
        if (!OpenClipboardRetry(owner))
        {
            err = L"OpenClipboard failed after retries - another window is holding "
                  L"the clipboard open";
            return false;
        }

        ::EmptyClipboard();

        bool ok = SetClipboardFormat(CF_UNICODETEXT, text.c_str(),
                                     (text.size() + 1) * sizeof(wchar_t), err);

        if (ok && code)
            // Narrow and NUL-terminated: sizeof("{code}") is 7, and the marker is
            // compared byte-wise against that same literal in OEView_1.cpp.
            ok = SetClipboardFormat(CF_PRIVATEFIRST, "{code}", sizeof("{code}"), err);

        if (ok && columnar)
        {
            const UINT fmt = COEditorView::GetAltColumnarTextFormat();

            if (!fmt)
            {
                // Registered when the first editor view is created. Zero means the
                // harness got here before any view existed; report it rather than drop
                // the tag and let the case test the non-columnar path by accident.
                err = L"the OpenEditor.ColumnarText format is not registered";
                ok  = false;
            }
            else
            {
                // Narrow, as DoEditCopy writes it. No consumer reads the bytes -
                // paste and drop both only ask IsClipboardFormatAvailable.
                static const char tag[] = "dummy";

                ok = SetClipboardFormat(fmt, tag, sizeof(tag), err);
            }
        }

        ::CloseClipboard();
        return ok;
    }

    bool ClearClipboard (HWND owner, std::wstring& err)
    {
        if (!OpenClipboardRetry(owner))
        {
            err = L"OpenClipboard failed after retries - another window is holding "
                  L"the clipboard open";
            return false;
        }

        ::EmptyClipboard();
        ::CloseClipboard();
        return true;
    }

    // Reading the clipboard has THREE outcomes and collapsing them to two is what
    // makes a harness lie. "The format is absent" is a state a script asserts -
    // `.expect clipboard |<empty>|` after `.clipboard clear`, and the whole point of
    // keeping that distinct from `.clipboard ||`. "The clipboard could not be
    // opened" is a harness failure: nothing is known about the format either way,
    // and reporting it as `absent` would fail a correct case with a message that
    // reads like an editor regression. So the callers below switch on all three.
    enum EClipboardRead
    {
        eClipboardReadFailed = -1,
        eClipboardAbsent     =  0,
        eClipboardPresent    =  1
    };

    // CF_UNICODETEXT, or eClipboardAbsent when the format is not there at all.
    EClipboardRead ReadClipboardText (std::wstring& out)
    {
        out.clear();

        if (!OpenClipboardRetry(NULL))
            return eClipboardReadFailed;

        CString text;
        const bool ok = Common::AppGetClipboardText(text, CF_UNICODETEXT);

        ::CloseClipboard();

        if (ok)
            out.assign((LPCWSTR)text, (size_t)text.GetLength());

        return ok ? eClipboardPresent : eClipboardAbsent;
    }

    // The exact production test in OEView_1.cpp: the marker format has to
    // be present AND read "{code}".
    EClipboardRead ReadClipboardCodeMarker ()
    {
        if (!OpenClipboardRetry(NULL))
            return eClipboardReadFailed;

        std::string info;

        const bool ok = Common::AppGetClipboardText(info, CF_PRIVATEFIRST)
                     && info == "{code}";

        ::CloseClipboard();
        return ok ? eClipboardPresent : eClipboardAbsent;
    }

    EClipboardRead ReadClipboardColumnarTag ()
    {
        const UINT fmt = COEditorView::GetAltColumnarTextFormat();

        // IsClipboardFormatAvailable is the one clipboard query that needs no open
        // session - and the one OnEditPaste itself uses - so unlike the two
        // readers above it cannot lose a race and never returns eClipboardReadFailed.
        return (fmt && ::IsClipboardFormatAvailable(fmt)) ? eClipboardPresent
                                                          : eClipboardAbsent;
    }

    // Which keys reach the view as WM_CHAR and which as WM_KEYDOWN. Getting this wrong
    // fails silently: the message is delivered, the view discards it, and the transcript
    // merely shows nothing having happened.
    //
    // Tab and Enter are OnChar's to handle. Backspace and Escape are NOT, even though a real
    // keyboard delivers them as WM_CHAR too: OnChar throws both away ("it's processed
    // already") because OnKeyDown does the work, so a WM_CHAR carrying them is a no-op.
    // Everything else (arrows, Home/End, Delete, Insert, F-keys) is WM_KEYDOWN.
    bool IsCharKey(UINT vk)
    {
        return vk == VK_TAB || vk == VK_RETURN;
    }

    // lParam for WM_KEYDOWN: repeat count 1 plus the extended-key flag for the
    // keys that carry it on a real keyboard. The view's OnKeyDown handlers here
    // only look at nChar/nRepCnt, so a zero scan code is harmless.
    LPARAM KeyLParam(UINT vk)
    {
        LPARAM lp = 1;

        switch (vk)
        {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_HOME: case VK_END:  case VK_PRIOR: case VK_NEXT:
        case VK_DELETE: case VK_INSERT:
            lp |= 0x01000000;
            break;
        default:
            break;
        }

        return lp;
    }

    // WinUser.h defines MK_SHIFT and MK_CONTROL but has NO Alt counterpart: the Alt bit is
    // documented only as a WM_*BUTTON* key-flag value, so it gets a name here rather than a bare
    // 0x0020 in the two places that have to agree about it (KeyStateGuard and g_mods).
    const UINT MK_ALTKEY = 0x0020;

    // Holds a thread's key-state array with one or more modifier bytes set down, for the lifetime
    // of one synthetic keystroke.
    //
    // This exists because production code decides a chord by calling GetKeyState, not by reading
    // the message's key flags: OnChar's `case '\t'` tests GetKeyState(VK_SHIFT) to tell Tab from
    // Shift+Tab (OEView_1.cpp:1820), and OnKeyDown does the same for Shift/Ctrl/Alt on every
    // navigation key (:857-859). GetKeyState reads the CALLING THREAD's key-state array, so a
    // WM_CHAR posted with no state behind it is indistinguishable from the unmodified key - which
    // is why Shift+Tab could not be scripted before this class existed.
    //
    // SendMessage to a window owned by this thread dispatches inline, so writing the array here
    // is what the view then reads. AttachThreadInput covers a window owned by another thread, and
    // attaching shares the key-state array between the two - hence the attach happens BEFORE the
    // snapshot, and the restore before the detach.
    class KeyStateGuard
    {
    public:
        KeyStateGuard (UINT mods, HWND hwnd)
            : m_thisThread(::GetCurrentThreadId())
            , m_viewThread(::GetWindowThreadProcessId(hwnd, NULL))
            , m_attached(false)
        {
            m_attached = m_viewThread != m_thisThread
                      && ::AttachThreadInput(m_thisThread, m_viewThread, TRUE);

            BYTE state[256];
            ::GetKeyboardState(state);

            ::CopyMemory(m_saved, state, sizeof(m_saved));
            ::CopyMemory(m_state, state, sizeof(m_state));

            if (mods & MK_SHIFT)   m_state[VK_SHIFT]   = 0x80;
            if (mods & MK_CONTROL) m_state[VK_CONTROL] = 0x80;
            if (mods & MK_ALTKEY)  m_state[VK_MENU]    = 0x80;

            ::SetKeyboardState(m_state);
        }

        // Restores unconditionally, on the throwing path too: key state is thread-wide, so a
        // modifier left down would silently turn every later keystroke in the run into a chord.
        ~KeyStateGuard ()
        {
            ::SetKeyboardState(m_saved);

            if (m_attached)
                ::AttachThreadInput(m_thisThread, m_viewThread, FALSE);
        }

    private:
        KeyStateGuard (const KeyStateGuard&);                // not copyable
        KeyStateGuard& operator= (const KeyStateGuard&);

        DWORD m_thisThread, m_viewThread;
        bool  m_attached;
        BYTE  m_state[256];
        BYTE  m_saved[256];
    };

    // -------------------------------------------------------------- tables ---

    // Command names for `cmd <Name>`. These are the editor commands a script
    // needs, both the ones a feature under test interacts with and the ones that
    // build a scenario (select-all, delete, format). Every entry is a real MFC
    // command ID resolved from resource.h / OpenEditorInc.rh, never a hard-coded
    // accelerator: the point is to drive the same handlers a menu item drives.
    struct CmdEntry { const wchar_t* name; UINT id; };

    const CmdEntry g_cmds[] =
    {
        // MFC-standard edit commands, all handled by COEditorView's own message map.
        { L"EditUndo",           ID_EDIT_UNDO           },
        { L"EditRedo",           ID_EDIT_REDO           },
        { L"EditCut",            ID_EDIT_CUT            },
        { L"EditCopy",           ID_EDIT_COPY           },
        { L"EditPaste",          ID_EDIT_PASTE          },
        { L"EditSelectAll",      ID_EDIT_SELECT_ALL     },

        // The copy/cut variants COEditorView's own map wires up. They belong in a
        // clipboard test because DoEditCopy's `append` argument is the only thing
        // separating them from plain EditCopy: with it the previous clipboard text is
        // read back and concatenated AHEAD of the new block instead of being emptied
        // away, and the "Bookmarked" pair copies the bookmarked lines rather than the
        // selection.
        { L"EditCopyAndAppend",  ID_EDIT_COPY_N_APPEND    },  // 50031
        { L"EditCutAndAppend",   ID_EDIT_CUT_N_APPEND     },  // 50033
        { L"EditCopyBookmarked", ID_EDIT_COPY_BOOKMARKED  },  // 50030
        { L"EditCutBookmarked",  ID_EDIT_CUT_BOOKMARKED   },  // 50032

        // OpenEditor commands from OpenEditorInc.rh.
        { L"EditDelete",         ID_EDIT_DELETE         },  // 50035
        { L"DeleteLine",         ID_EDIT_DELETE_LINE    },  // 50037
        { L"StreamSel",          ID_EDIT_STREAM_SEL     },  // 50028
        { L"ColumnSel",          ID_EDIT_COLUMN_SEL     },  // 50029
        { L"TogglesSel",         ID_EDIT_TOGGLES_SEL    },  // 50067
        { L"Indent",             ID_EDIT_INDENT         },  // 50050
        { L"Undent",             ID_EDIT_UNDENT         },  // 50069
        { L"Comment",            ID_EDIT_COMMENT        },  // 50064
        { L"Uncomment",          ID_EDIT_UNCOMMENT      },  // 50068
        { L"Tabify",             ID_EDIT_TABIFY         },  // 50065
        { L"Untabify",           ID_EDIT_UNTABIFY       },  // 50070
        { L"DupLineOrSelection", ID_EDIT_DUP_LINE_OR_SELECTION }, // 50168
        { L"Autocomplete",       ID_EDIT_AUTOCOMPLETE   },  // 50167
        { L"FormatSql",          ID_EDIT_FORMAT_SQL     },  // 50412

        // Outlining commands (see md/CodeFolding.md), handled by COEditorView's own
        // message map.
        { L"FoldCollapseCurrent", ID_EDIT_FOLD_COLLAPSE_CURRENT }, // 50430
        { L"FoldExpandCurrent",   ID_EDIT_FOLD_EXPAND_CURRENT   }, // 50431
        { L"FoldToggleCurrent",   ID_EDIT_FOLD_TOGGLE_CURRENT   }, // 50432
        { L"FoldExpandSubtree",   ID_EDIT_FOLD_EXPAND_SUBTREE   }, // 50433
        { L"FoldCollapseAll",     ID_EDIT_FOLD_COLLAPSE_ALL     }, // 50434
        { L"FoldExpandAll",       ID_EDIT_FOLD_EXPAND_ALL       }, // 50435
        { L"FoldToggleAll",       ID_EDIT_FOLD_TOGGLE_ALL       }, // 50436
        { L"FoldCollapseToLevel1", ID_EDIT_FOLD_COLLAPSE_LEVEL1 }, // 50437
        { L"FoldCollapseToLevel2", ID_EDIT_FOLD_COLLAPSE_LEVEL2 }, // 50438
        { L"FoldCollapseToLevel3", ID_EDIT_FOLD_COLLAPSE_LEVEL3 }, // 50439
        // The gutter context menu's PlSqlOutlineFoldingEnabled flip - outside
        // the two update ranges on purpose (always enabled, check state).
        { L"FoldToggleEnabled",   ID_EDIT_FOLD_TOGGLE_ENABLED   }, // 50440

        // The Find/Replace dialog cannot itself be driven from a script - it is modeless and
        // its OnOK() calls DestroyWindow(), so nothing survives to press a second button. These
        // reach the same RepeadSearch funnel its Find Next button does. Set the pattern with
        // `.search` first; Replace All is `.replaceall`.
        { L"FindNext",           ID_EDIT_FIND_NEXT      },
        { L"FindPrevious",       ID_EDIT_FIND_PREVIOUS  },
    };

    bool LookupCommand(const std::wstring& name, UINT& id)
    {
        for (size_t i = 0; i < _countof(g_cmds); i++)
            if (EqualsNoCase(name, g_cmds[i].name))
            {
                id = g_cmds[i].id;
                return true;
            }

        // Also accept the resource symbol verbatim ("ID_EDIT_UNDO") and a bare numeric ID, so a
        // suite is never blocked by a missing alias - but no synthetic symbol generation: an
        // unknown name must fail loudly rather than dispatch some other command.
        if (!name.empty())
        {
            if (name[0] == L'I' || name[0] == L'i')
            {
                const std::wstring tail = name.substr(3);   // past "ID_"
                for (size_t i = 0; i < _countof(g_cmds); i++)
                    if (EqualsNoCase(tail, g_cmds[i].name))
                    {
                        id = g_cmds[i].id;
                        return true;
                    }
            }

            bool numeric = true;
            for (size_t i = 0; i < name.size(); i++)
                if (!iswdigit(name[i]))
                    numeric = false;

            if (numeric)
            {
                id = static_cast<UINT>(_wtoi(name.c_str()));
                return true;
            }
        }

        return false;
    }

    // Key names for `key <Name>`.
    struct KeyEntry { const wchar_t* name; UINT vk; };

    const KeyEntry g_keys[] =
    {
        { L"Left",     VK_LEFT     }, { L"Right",    VK_RIGHT    },
        { L"Up",       VK_UP       }, { L"Down",     VK_DOWN     },
        { L"Home",     VK_HOME     }, { L"End",      VK_END      },
        { L"PgUp",     VK_PRIOR    }, { L"PgDn",     VK_NEXT     },
        { L"Delete",   VK_DELETE   }, { L"Del",      VK_DELETE   },
        { L"Insert",   VK_INSERT   }, { L"Ins",      VK_INSERT   },
        { L"Back",     VK_BACK     }, { L"Backspace",VK_BACK     },
        { L"Tab",      VK_TAB      }, { L"Return",   VK_RETURN   },
        { L"Enter",    VK_RETURN   }, { L"Escape",   VK_ESCAPE   },
        { L"Esc",      VK_ESCAPE   }, { L"Space",    VK_SPACE    },
        { L"F1",       VK_F1       }, { L"F2",       VK_F2       },
        { L"F3",       VK_F3       }, { L"F4",       VK_F4       },
        { L"F5",       VK_F5       }, { L"F6",       VK_F6       },
    };

    bool LookupKey(const std::wstring& name, UINT& vk)
    {
        for (size_t i = 0; i < _countof(g_keys); i++)
            if (EqualsNoCase(name, g_keys[i].name))
            {
                vk = g_keys[i].vk;
                return true;
            }

        // Single printable character as a key name, e.g. `key (`.
        if (name.size() == 1 && name[0] >= 0x20)
        {
            vk = static_cast<UINT>(towupper(name[0]));
            return true;
        }

        return false;
    }

    // Modifier prefixes for `key <Mod>+<Key>` - `Shift+Tab`, `Ctrl+U`, `Alt+Shift+Left`. Spelled
    // the way the shipped .keymap files spell them (default.keymap:222 `Shift+F7`), so a script
    // and a keymap entry describing the same chord read the same. MK_ALTKEY is declared up by
    // KeyStateGuard, which needs the same constant.
    struct ModEntry { const wchar_t* name; UINT flag; };

    const ModEntry g_mods[] =
    {
        { L"Shift",   MK_SHIFT   },
        { L"Ctrl",    MK_CONTROL },
        { L"Control", MK_CONTROL },
        { L"Alt",     MK_ALTKEY  },
    };

    bool LookupModifier(const std::wstring& name, UINT& flag)
    {
        for (size_t i = 0; i < _countof(g_mods); i++)
            if (EqualsNoCase(name, g_mods[i].name))
            {
                flag = g_mods[i].flag;
                return true;
            }

        return false;
    }

    // Splits a `.key` argument into modifiers plus a key name. The key name is the LAST
    // '+'-separated token and every token before it must be a modifier, so `Shift+Tab` yields
    // MK_SHIFT and "Tab". Tokens are trimmed, which makes `Shift + Tab` equivalent.
    //
    // Returns false only when a prefix is not a modifier name; LookupKey still has the final say
    // on the key name itself, so an unknown key keeps reporting through its existing message.
    //
    // Anything with no usable split is passed through whole, which is what preserves two existing
    // spellings: `key +` is the single-character form LookupKey resolves to a VK for '+', and a
    // bare `key Tab` has nothing to strip.
    bool ParseKeyCombo(const std::wstring& combo, std::wstring& keyName, UINT& mods)
    {
        mods    = 0;
        keyName = combo;

        // Empty tokens are dropped rather than kept, so a doubled or trailing '+' cannot
        // manufacture a blank modifier name and fail with a confusing message.
        std::vector<std::wstring> parts;

        for (size_t at = 0; at <= combo.size(); )
        {
            const size_t next = combo.find(L'+', at);

            const std::wstring one =
                Trim(combo.substr(at, next == std::wstring::npos ? std::wstring::npos : next - at));

            if (!one.empty())
                parts.push_back(one);

            if (next == std::wstring::npos)
                break;

            at = next + 1;
        }

        if (parts.size() < 2)
            return true;

        keyName = parts.back();

        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            UINT flag = 0;

            if (!LookupModifier(parts[i], flag))
                return false;

            mods |= flag;
        }

        return true;
    }

    // ----------------------------------------------------------- settings ---

    // The global settings `.settings` can pin. This table is the handler's only
    // feature-specific part: a suite needing another switch adds one row here and nothing else.
    // Pinning is what lets a case state its whole precondition matrix instead of inheriting the
    // developer's profile - it is what makes "everything off" expressible at all.
    //
    // Read and written through GlobalSettings, the single shared instance behind every
    // document's Settings aggregate, always with notify = true so OnSettingsChanged runs just
    // as it does when the options dialog closes.
    typedef const bool& (OpenEditor::GlobalSettings::*BoolGetter)() const;
    typedef void (OpenEditor::GlobalSettings::*BoolSetter)(const bool&, bool);

    struct SettingEntry
    {
        const wchar_t* name;
        BoolGetter     get;
        BoolSetter     set;
    };

    const SettingEntry g_settings[] =
    {
        { L"AutoCloseBrackets",
          &OpenEditor::GlobalSettings::GetAutoCloseBrackets,
          &OpenEditor::GlobalSettings::SetAutoCloseBrackets },
        { L"AutoCloseBeforeWhitespaceOnly",
          &OpenEditor::GlobalSettings::GetAutoCloseBeforeWhitespaceOnly,
          &OpenEditor::GlobalSettings::SetAutoCloseBeforeWhitespaceOnly },
        { L"AutoSurroundQuotes",
          &OpenEditor::GlobalSettings::GetAutoSurroundQuotes,
          &OpenEditor::GlobalSettings::SetAutoSurroundQuotes },
        { L"AutoSurroundBrackets",
          &OpenEditor::GlobalSettings::GetAutoSurroundBrackets,
          &OpenEditor::GlobalSettings::SetAutoSurroundBrackets },
        // Replaces the two gates above for the ' / { / [ keys in languages with
        // AlternativeQuotes. Pinned both off and on, per key, in SURROUND.stp.
        { L"AutoSurroundPlsqlQuoting",
          &OpenEditor::GlobalSettings::GetAutoSurroundPlsqlQuoting,
          &OpenEditor::GlobalSettings::SetAutoSurroundPlsqlQuoting },
        { L"BlockTypingOverwrite",
          &OpenEditor::GlobalSettings::GetBlockTypingOverwrite,
          &OpenEditor::GlobalSettings::SetBlockTypingOverwrite },
        // Whether PLAIN Tab with a MULTI-LINE selection indents the block or types over it
        // (OEView_1.cpp `case '\t'`). Shift+Tab deliberately does NOT consult this: outdenting is a
        // whitespace operation on whole lines, so its routing depends only on the selection's shape
        // (empty -> ApplyManualUndent, columnar -> ColumnarUndent, else UndentBlock). BLOCK_INDENT.stp
        // pins this in both states precisely to prove that - the same Shift+Tab case must produce the
        // same transcript whichever way the switch is set.
        { L"BlockTabIndent",
          &OpenEditor::GlobalSettings::GetBlockTabIndent,
          &OpenEditor::GlobalSettings::SetBlockTabIndent },
        { L"ColBlockEditMode",
          &OpenEditor::GlobalSettings::GetColBlockEditMode,
          &OpenEditor::GlobalSettings::SetColBlockEditMode },
        // Gates the model-side reveal ColumnarInsert does through the virtual
        // EditContext::RevealLine hook (OEContext_3.cpp). Default true; pinned here
        // so a folding case can prove BOTH halves - that a columnar insert across a
        // collapsed region expands it, and that turning this off leaves it collapsed.
        { L"ColInsertRevealLine",
          &OpenEditor::GlobalSettings::GetColInsertRevealLine,
          &OpenEditor::GlobalSettings::SetColInsertRevealLine },
        { L"BlockDelAndBSDelete",
          &OpenEditor::GlobalSettings::GetBlockDelAndBSDelete,
          &OpenEditor::GlobalSettings::SetBlockDelAndBSDelete },

        // The three switches a clipboard case cannot leave to the developer's profile: each one
        // changes what a copy/cut/paste LEAVES BEHIND rather than what it does, so a suite that
        // inherited them could keep asserting true while measuring a different product.
        //
        //   BlockKeepMarking                 The only thing InsertBlock's `hideSelection`
        //                                    argument ever gets from a paste. Off, every paste
        //                                    ends in ClearSelection(true) - and for an
        //                                    ALT-columnar selection that call also drops the
        //                                    view back to ebtStream. One bool therefore decides
        //                                    both whether a paste leaves its own text selected
        //                                    and whether a columnar paste stays in column mode.
        //   ColBlockCursorToStartAfterPaste  Its name says the OPPOSITE of what it does: true
        //                                    moves the caret to the END of the pasted rectangle.
        //                                    The options page documents this rather than fixes it.
        //   ColBlockDeleteSpaceAfterMove     On, the columns close up after a columnar cut; off,
        //                                    the same width is re-inserted as spaces, so the
        //                                    document is left exactly as wide as it was.
        { L"BlockKeepMarking",
          &OpenEditor::GlobalSettings::GetBlockKeepMarking,
          &OpenEditor::GlobalSettings::SetBlockKeepMarking },
        { L"ColBlockCursorToStartAfterPaste",
          &OpenEditor::GlobalSettings::GetColBlockCursorToStartAfterPaste,
          &OpenEditor::GlobalSettings::SetColBlockCursorToStartAfterPaste },
        { L"ColBlockDeleteSpaceAfterMove",
          &OpenEditor::GlobalSettings::GetColBlockDeleteSpaceAfterMove,
          &OpenEditor::GlobalSettings::SetColBlockDeleteSpaceAfterMove },


        // The columnar half of the fold rule is the ONE place a cursor-range setting changes the
        // answer: a position pulled onto a fold anchor keeps its column verbatim with
        // CursorBeyondEOL on, but is clamped to the anchor's EOL with it off - which then
        // coincides with the stream rule, so the columnar case is only observable pinned on. The
        // STREAM rule needs no entry: GetLineLength(anchor) is a fixed point of that same clamp.
        { L"CursorBeyondEOL",
          &OpenEditor::GlobalSettings::GetCursorBeyondEOL,
          &OpenEditor::GlobalSettings::SetCursorBeyondEOL },

        // The three-setting conjunction COEDocument::IsFoldingEnabled() gates on:
        // PlSqlKeywordNavigation drives the parser that produces the outline,
        // PlSqlOutlineEnabled the snapshot itself, and PlSqlOutlineFoldingEnabled is the
        // feature. All three must be pinned for a folding case to have any regions at all -
        // folding ships OFF, so a suite that did not pin it would silently test nothing on
        // every machine but the one that happened to enable it.
        { L"PlSqlKeywordNavigation",
          &OpenEditor::GlobalSettings::GetPlSqlKeywordNavigation,
          &OpenEditor::GlobalSettings::SetPlSqlKeywordNavigation },
        { L"PlSqlOutlineEnabled",
          &OpenEditor::GlobalSettings::GetPlSqlOutlineEnabled,
          &OpenEditor::GlobalSettings::SetPlSqlOutlineEnabled },
        { L"PlSqlOutlineFoldingEnabled",
          &OpenEditor::GlobalSettings::GetPlSqlOutlineFoldingEnabled,
          &OpenEditor::GlobalSettings::SetPlSqlOutlineFoldingEnabled },

        // NOT part of the conjunction above: the current-region gutter tint's own switch,
        // paint-only by design. Model state and the fold commands answer exactly the same with it
        // off, which is what case fold-highlight-setting pins.
        { L"PlSqlOutlineFoldingHighlightCurrentRegion",
          &OpenEditor::GlobalSettings::GetPlSqlOutlineFoldingHighlightCurrentRegion,
          &OpenEditor::GlobalSettings::SetPlSqlOutlineFoldingHighlightCurrentRegion },
    };

    const SettingEntry* FindSetting(const std::wstring& name)
    {
        for (size_t i = 0; i < _countof(g_settings); i++)
            if (EqualsNoCase(name, g_settings[i].name))
                return &g_settings[i];

        return 0;
    }

    // Integer-valued settings, in a second table rather than folded into the one above: the
    // member-pointer types differ (const int& vs const bool&), and a tagged union of them would
    // put a dead field on all ten existing rows to serve three new ones. DoSettings tries the
    // bool table first and falls through to this one, so a script reads the same either way.
    //
    // Two of these decide whether a parse is reported truncated at all - the line and token
    // limits, which default to 3000 and 15000 - so without a pin the only way to reach
    // oqTruncated is a 3001-line fixture. The third is the Code Outline pane's item budget,
    // which no fixture has come close to hitting: the largest reached 51 regions against 500.
    typedef const int& (OpenEditor::GlobalSettings::*IntGetter)() const;
    typedef void (OpenEditor::GlobalSettings::*IntSetter)(const int&, bool);

    struct IntSettingEntry
    {
        const wchar_t* name;
        IntGetter      get;
        IntSetter      set;
    };

    const IntSettingEntry g_intSettings[] =
    {
        { L"PlSqlFileMaxLines",
          &OpenEditor::GlobalSettings::GetPlSqlFileMaxLines,
          &OpenEditor::GlobalSettings::SetPlSqlFileMaxLines },
        { L"PlSqlFileMaxTokens",
          &OpenEditor::GlobalSettings::GetPlSqlFileMaxTokens,
          &OpenEditor::GlobalSettings::SetPlSqlFileMaxTokens },
        // 0 means "no budget" and is mapped to INT_MAX by PlSqlSupport, so a case
        // that wants the cap OFF pins 0 and a case that wants it ON pins the count.
        { L"PlSqlOutlineMaxTopItems",
          &OpenEditor::GlobalSettings::GetPlSqlOutlineMaxTopItems,
          &OpenEditor::GlobalSettings::SetPlSqlOutlineMaxTopItems },
    };

    const IntSettingEntry* FindIntSetting(const std::wstring& name)
    {
        for (size_t i = 0; i < _countof(g_intSettings); i++)
            if (EqualsNoCase(name, g_intSettings[i].name))
                return &g_intSettings[i];

        return 0;
    }

    // "k=v,k=v" -> readable form for the transcript, e.g. "AutoCloseBrackets=1". Currently
    // unreferenced: DoSettings emits only the keys the script pinned, which is the shorter and
    // more useful line. Covers BOTH tables, so a whole-profile dump gets the integer
    // preconditions with it rather than a matrix that silently omits the parse limits.
    std::wstring RenderSettings(const OpenEditor::GlobalSettings& gs)
    {
        std::wstring s;

        for (size_t i = 0; i < _countof(g_settings); i++)
        {
            if (i)
                s += L' ';

            s += g_settings[i].name;
            s += L'=';
            s += ((gs.*(g_settings[i].get))() ? L'1' : L'0');
        }

        for (size_t i = 0; i < _countof(g_intSettings); i++)
        {
            s += L' ';
            s += g_intSettings[i].name;
            s += L'=';
            s += Num((gs.*(g_intSettings[i].get))());
        }

        return s;
    }

    // --------------------------------------------------------------- runner ---

    // Replays a script against one worksheet editor and accumulates the transcript. One instance
    // per Execute call; the dtor restores every global setting the script pinned, so even a run
    // that aborts mid-script cannot leave the live SQLTools session mutated.
    //
    // Abort policy: the first malformed directive or unknown key/command sets m_abort and every
    // remaining line is skipped, so a broken script produces one ERROR and one RESULT line
    // rather than a cascade - the baseline diff then points at the real cause.
    class Runner
    {
    public:
        Runner (CPLSWorksheetDoc* doc, COEditorView* view, std::wstring& out)
            : m_doc(doc), m_view(view), m_out(out)
            , m_hwnd(view->GetSafeHwnd())
            , m_checks(0), m_pass(0), m_fail(0), m_errors(0)
            , m_abort(false)
            , m_markedSupportId(0)
            , m_savedIndentType(false)
            , m_origIndentType(0)
        {
        }

        ~Runner ()
        {
            RestoreSettings();
        }

        void Run (const std::vector<std::wstring>& lines);

        int Checks () const { return m_checks; }
        int Passes () const { return m_pass; }
        int Fails  () const { return m_fail + m_errors; }

    private:
        Runner (const Runner&);                 // not copyable
        Runner& operator = (const Runner&);

        // transcript
        bool Error (int lineNo, const std::wstring& verb, const std::wstring& msg);
        void Emit  (const std::wstring& line);
        void Check (int lineNo, const std::wstring& what,
                    const std::wstring& expected, const std::wstring& actual);

        // editor primitives
        void ResetDoc ();
        void SetDocumentText (const std::wstring& text);
        void SendChar    (wchar_t c);
        void SendKey     (UINT vk);
        void SendKey     (UINT vk, UINT mods);   // mods: MK_SHIFT / MK_CONTROL / MK_MENU
        void SendCommand (UINT id);
        void SetOvertype (bool want);
        OpenEditor::GlobalSettings& Globals ();

        // verbs
        bool DoCase     (int lineNo, const std::wstring& args);
        bool DoLang     (int lineNo, const std::wstring& args);
        bool DoText     (int lineNo, const std::wstring& args);
        bool DoCaret    (int lineNo, const std::wstring& args);
        bool DoSel      (int lineNo, const std::wstring& args, bool columnar);
        bool DoMode     (int lineNo, const std::wstring& args);
        bool DoOvertype (int lineNo, const std::wstring& args);
        bool DoType     (int lineNo, const std::wstring& args);
        bool DoKey      (int lineNo, const std::wstring& args);
        bool DoCmd      (int lineNo, const std::wstring& args);
        bool DoUndo     (int lineNo, const std::wstring& args, bool redo);
        bool DoSettings (int lineNo, const std::wstring& args);
        bool DoIndentType (int lineNo, const std::wstring& args);
        bool DoClipboard (int lineNo, const std::wstring& args);
        bool DoSupportMark (int lineNo, const std::wstring& args);
        bool DoFoldSettle (int lineNo, const std::wstring& args);
        bool DoFold     (int lineNo, const std::wstring& args);
        bool DoPaneOutline (int lineNo, const std::wstring& args);
        bool DoOutlineQuality (int lineNo, const std::wstring& args);
        bool DoOutlineTrust (int lineNo, const std::wstring& args);
        bool DoLineStatus (int lineNo, const std::wstring& args);

        // The whole-buffer LineStatusMap extraction shared by `.linestatus` and the
        // three `.expect unparsed*` checks. Returns the PL/SQL support it came from, or
        // 0 when the document has none - which both callers turn into a script error.
        // See the definition for why it is whole-buffer rather than the view's paint
        // window, and why the settled check lives here instead of in the callers.
        OpenEditor::PlSqlSupport* FillWholeBufferLineStatus (int lineNo,
                                                             const std::wstring& verb,
                                                             OpenEditor::LineStatusMap&,
                                                             int& lines);
        bool DoSearch     (int lineNo, const std::wstring& args);
        bool DoReplaceAll (int lineNo, const std::wstring& args);
        bool DoEcho     (int lineNo, const std::wstring& args);
        bool DoExpect   (int lineNo, const std::wstring& args);

        // One entry per pinned key, tagged by which table it came from: the bool and
        // int setters have different member-pointer types, so RestoreSettings cannot
        // call one through the other. Exactly one of entry / intEntry is non-null.
        struct Saved
        {
            const SettingEntry*    entry;
            const IntSettingEntry* intEntry;
            bool                   bvalue;
            int                    ivalue;
        };

        void RestoreSettings ();

        CPLSWorksheetDoc* m_doc;
        COEditorView*     m_view;
        std::wstring&     m_out;
        HWND              m_hwnd;

        int  m_checks, m_pass, m_fail, m_errors;
        bool m_abort;

        // The PlSqlSupport::GetInstanceId() recorded by the last `.supportmark`, or 0
        // for "no mark taken in this case". 0 is not a reachable instance id - the
        // counter is PRE-incremented from 0 in PlSqlSupport's constructor
        // (OEPlsSqlSupport.cpp) - so it doubles as the unset spelling and lets
        // `.expect supportsame` reject a missing mark instead of silently comparing
        // against whatever the first object built in the process happened to get.
        // An id and not a pointer: see `.expect supportsame`.
        int  m_markedSupportId;

        std::wstring m_case;              // current `.case` name, for error text
        std::wstring m_searchText;        // pattern set by `.search`, for `.replaceall`
        std::vector<Saved> m_saved;       // settings snapshot, first save wins

        // `.indenttype` writes an override on the DOCUMENT's own Settings, which neither
        // table above can reach, so it keeps its own one-slot snapshot.
        bool m_savedIndentType;
        int  m_origIndentType;
    };

    OpenEditor::GlobalSettings& Runner::Globals ()
    {
        return *COEDocument::GetSettingsManagerForUpdate().GetGlobalSettings();
    }

    void Runner::Emit (const std::wstring& line)
    {
        m_out += line;
        m_out += L'\n';
    }

    bool Runner::Error (int lineNo, const std::wstring& verb, const std::wstring& msg)
    {
        Emit(L"ERROR line " + Num(lineNo) + L" [" + m_case + L"] " + verb + L": " + msg);
        m_errors++;
        m_abort = true;
        return false;
    }

    void Runner::Check (int lineNo, const std::wstring& what,
                        const std::wstring& expected, const std::wstring& actual)
    {
        m_checks++;

        if (expected == actual)
        {
            m_pass++;
            Emit(L"expect " + what + L" " + expected + L" -> PASS");
        }
        else
        {
            m_fail++;
            Emit(L"expect " + what
               + L" expected=" + expected
               + L" actual="   + actual
               + L" -> FAIL (line " + Num(lineNo) + L")");
        }
    }

    // ------------------------------------------------------ editor plumbing ---

    void Runner::ResetDoc ()
    {
        // Wipe everything a case could inherit from the previous one: text,
        // caret, selection, block mode, overtype mode, undo history, modified
        // flag. Settings are deliberately NOT reset here - `.settings` pins are
        // sticky across cases so a suite can state the whole matrix once.
        SetDocumentText(std::wstring());
        SetOvertype(false);

        // The search pattern is product state that outlives the document, so clear
        // it too: a `.replaceall` in one case must not silently run against a
        // pattern another case left behind. IsSearchTextEmpty() is not reachable
        // from here - COEditorView inherits EditContext protected and only
        // re-exposes the members it needs - hence the Runner's own mirror.
        m_view->SetSearchText(L"");
        m_searchText.clear();

        // So is the support mark. It identifies an OBJECT rather than a document
        // property, and the object a `.lang` builds in this case has a different id
        // from the one the previous case marked, so carrying the mark over could only
        // ever produce a `0` (no) that looks like a finding about the product.
        m_markedSupportId = 0;
    }

    void Runner::SetDocumentText (const std::wstring& text)
    {
        // Bulk replace, deliberately NOT typing: `.text` establishes a
        // precondition, so it must not run through OnChar and trigger the very
        // auto-pairing under test. Stream mode first, because InsertBlock in
        // column mode fills a rectangle instead of replacing the document.
        m_view->SetBlockMode(OpenEditor::ebtStream);
        m_view->SelectAll();
        m_view->DeleteBlock();

        if (!text.empty())
            m_view->InsertBlock(text.c_str(), true);   // hideSelection

        Position home;
        home.line = 0;
        home.column = 0;
        m_view->MoveTo(home);
        m_view->ClearSelection();

        // The setup itself must not be on the undo stack - a case asserting `canundo 0`
        // straight after `.text` would otherwise fail for a reason of its own - and the
        // document must not look modified to the user.
        m_doc->GetStorage().ClearUndo();
        m_doc->SetModifiedFlag(FALSE);
    }

    void Runner::SendChar (wchar_t c)
    {
        // OnChar throws both of these away because OnKeyDown does the work (see IsCharKey),
        // so a WM_CHAR carrying one is a silent no-op: the document simply stays as it was.
        ASSERT(c != VK_BACK && c != VK_ESCAPE);

        ::SendMessage(m_hwnd, WM_CHAR, static_cast<WPARAM>(c), 0);
    }

    void Runner::SendKey (UINT vk)
    {
        if (IsCharKey(vk))
        {
            // A real keyboard delivers these as WM_CHAR; their OnKeyDown branch never runs.
            ::SendMessage(m_hwnd, WM_CHAR, static_cast<WPARAM>(vk), 0);
            return;
        }

        const LPARAM lp = KeyLParam(vk);
        ::SendMessage(m_hwnd, WM_KEYDOWN, static_cast<WPARAM>(vk), lp);
        ::SendMessage(m_hwnd, WM_KEYUP,   static_cast<WPARAM>(vk), lp | 0xC0000000);
    }

    void Runner::SendKey (UINT vk, UINT mods)
    {
        // No modifiers means nothing to hold down, and taking the plain path keeps every existing
        // caller's behaviour - and the key-state array - untouched.
        if (!mods)
        {
            SendKey(vk);
            return;
        }

        // A real keyboard delivers the chord as the modifier's own WM_KEYDOWN, then the key. Only
        // the second half is replayed here: the view has no VK_SHIFT / VK_CONTROL / VK_MENU case in
        // OnKeyDown (they fall to `default:` and return), so the first half carries no information
        // the view keeps - what it reads is the key state, which the guard sets.
        const KeyStateGuard guard(mods, m_hwnd);

        SendKey(vk);
    }

    void Runner::SendCommand (UINT id)
    {
        // Through the parent frame rather than straight at the view: that is how a menu item
        // or an accelerator arrives, and OnCmdMsg offers it to the view, then the document,
        // then the frame. A direct SendMessage to the view would skip the document handler.
        CFrameWnd* pFrame = m_view->GetParentFrame();

        if (pFrame && pFrame->GetSafeHwnd())
            pFrame->SendMessage(WM_COMMAND, MAKEWPARAM(id, 0), 0);
    }

    void Runner::SetOvertype (bool want)
    {
        // m_isOverWriteMode is a single process-wide flag whose ONLY writer is the VK_INSERT
        // branch of OnKeyDown, so the only honest way to reach a wanted state is to read it and
        // press Insert once if it differs - which also keeps the status-bar indicator in step.
        if (COEditorView::IsOvertypeMode() != (want ? TRUE : FALSE))
            SendKey(VK_INSERT);
    }

    void Runner::RestoreSettings ()
    {
        if (m_savedIndentType)
        {
            m_doc->GetSettings().SetIndentType(m_origIndentType, false /*notify*/);
            m_savedIndentType = false;
        }

        if (m_saved.empty())
            return;

        OpenEditor::GlobalSettings& gs = Globals();

        // Reverse order, so a key pinned twice in one script ends back at its
        // original value. Each write notifies (ResetAutoPairs runs), mirroring
        // the options dialog's own path rather than shortcutting it.
        for (size_t i = m_saved.size(); i-- > 0; )
        {
            if (m_saved[i].intEntry)
                (gs.*(m_saved[i].intEntry->set))(m_saved[i].ivalue, true);
            else
                (gs.*(m_saved[i].entry->set))(m_saved[i].bvalue, true);
        }

        m_saved.clear();
    }

    // ------------------------------------------------------------- dispatch ---

    void Runner::Run (const std::vector<std::wstring>& lines)
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (m_abort)
                break;

            const int lineNo = static_cast<int>(i) + 1;

            // Leading indentation is insignificant for a directive. Spaces
            // *inside* a |...| argument are not, and those never reach Trim
            // because ParseDelimited reads them out of the original line.
            const std::wstring line = Trim(StripComment(lines[i]));

            if (line.empty())
                continue;

            // Every directive starts with '.'; anything else is a syntax error
            // rather than something to guess at.
            if (line[0] != L'.')
            {
                Error(lineNo, line, L"expected a directive starting with '.'");
                break;
            }

            std::wstring rest;
            const std::wstring verb = Lower(Head(line, rest));
            const std::wstring args = TrimEnd(rest);

            bool ok = true;

            if      (verb == L".case")      ok = DoCase(lineNo, args);
            else if (verb == L".lang")      ok = DoLang(lineNo, args);
            else if (verb == L".text")      ok = DoText(lineNo, args);
            else if (verb == L".caret")     ok = DoCaret(lineNo, args);
            else if (verb == L".sel")       ok = DoSel(lineNo, args, false);
            else if (verb == L".colsel")    ok = DoSel(lineNo, args, true);
            else if (verb == L".mode")      ok = DoMode(lineNo, args);
            else if (verb == L".overtype")  ok = DoOvertype(lineNo, args);
            else if (verb == L".type")      ok = DoType(lineNo, args);
            else if (verb == L".key")       ok = DoKey(lineNo, args);
            else if (verb == L".cmd")       ok = DoCmd(lineNo, args);
            else if (verb == L".undo")      ok = DoUndo(lineNo, args, false);
            else if (verb == L".redo")      ok = DoUndo(lineNo, args, true);
            else if (verb == L".settings")  ok = DoSettings(lineNo, args);
            else if (verb == L".indenttype") ok = DoIndentType(lineNo, args);
            else if (verb == L".clipboard") ok = DoClipboard(lineNo, args);
        else if (verb == L".supportmark") ok = DoSupportMark(lineNo, args);
            else if (verb == L".foldsettle") ok = DoFoldSettle(lineNo, args);
            else if (verb == L".fold")      ok = DoFold(lineNo, args);
            else if (verb == L".paneoutline") ok = DoPaneOutline(lineNo, args);
        else if (verb == L".outlinequality") ok = DoOutlineQuality(lineNo, args);
            else if (verb == L".outlinetrust")  ok = DoOutlineTrust(lineNo, args);
            else if (verb == L".linestatus")    ok = DoLineStatus(lineNo, args);
            else if (verb == L".search")     ok = DoSearch(lineNo, args);
            else if (verb == L".replaceall") ok = DoReplaceAll(lineNo, args);
            else if (verb == L".echo")      ok = DoEcho(lineNo, args);
            else if (verb == L".expect")    ok = DoExpect(lineNo, args);
            else
                ok = Error(lineNo, verb, L"unknown directive");

            if (!ok)
                break;
        }
    }

    // ---------------------------------------------------------------- verbs ---

    bool Runner::DoCase (int lineNo, const std::wstring& args)
    {
        if (args.empty())
            return Error(lineNo, L".case", L"missing case name");

        m_case = args;
        Emit(L"CASE " + args);
        ResetDoc();
        return true;
    }

    bool Runner::DoLang (int lineNo, const std::wstring& args)
    {
        if (args.empty())
            return Error(lineNo, L".lang", L"missing language class name");

        // SettingsManager::FindByName throws for an unknown class name
        // (OESettings.cpp), and a silent fallback to Plain text would make
        // every in-string/in-comment case pass for the wrong reason - so an
        // unknown name is reported as a script error, not ignored.
        try
        {
            m_doc->SetClassSetting(Common::str(args).c_str());
        }
        catch (const std::exception& e)
        {
            return Error(lineNo, L".lang", Common::wstr(e.what()));
        }
        catch (...)
        {
            return Error(lineNo, L".lang", L"unknown language class '" + args + L"'");
        }

        // Echo back what the document actually ended up with: SetClassSetting ->
        // Settings::NotifySettingsChanged -> Storage::OnSettingsChanged rebuilds
        // the LanguageSupport from exactly this value (OEStorage.cpp) and
        // forwards to every view, which is also what re-runs ResetAutoPairs.
        const std::string actual = m_doc->GetSettings().GetName();

        Emit(L"lang " + args + L" actual=" + Common::wstr(actual));

        if (!EqualsNoCase(Common::wstr(actual), args.c_str()))
            return Error(lineNo, L".lang",
                         L"class '" + args + L"' resolved to '" + Common::wstr(actual) + L"'");

        return true;
    }

    bool Runner::DoText (int lineNo, const std::wstring& args)
    {
        std::wstring text, err;

        if (!ParseDelimited(args, text, err))
            return Error(lineNo, L".text", err);

        SetDocumentText(text);
        Emit(L"text |" + Render(text) + L"|");
        return true;
    }

    bool Runner::DoCaret (int lineNo, const std::wstring& args)
    {
        Position pos;
        std::wstring err;

        if (!ParsePos(args, pos, err))
            return Error(lineNo, L".caret", err);

        m_view->MoveTo(pos);
        m_view->ClearSelection();

        Emit(L"caret " + args);
        return true;
    }

    bool Runner::DoSel (int lineNo, const std::wstring& args, bool columnar)
    {
        Square sel;
        std::wstring err;

        if (!ParseSquare(args, sel, err))
            return Error(lineNo, columnar ? L".colsel" : L".sel", err);

        // The block mode must be entered BEFORE the selection is made: a
        // multi-line square set while in stream mode is normalised to a stream
        // selection, so the column case would silently test the wrong thing.
        m_view->SetBlockMode(columnar ? OpenEditor::ebtColumn : OpenEditor::ebtStream);
        m_view->SetSelection(sel);

        Emit((columnar ? L"colsel " : L"sel ") + FmtSquare(sel));
        return true;
    }

    bool Runner::DoMode (int lineNo, const std::wstring& args)
    {
        const std::wstring v = Lower(args);

        if      (v == L"stream") m_view->SetBlockMode(OpenEditor::ebtStream);
        else if (v == L"column") m_view->SetBlockMode(OpenEditor::ebtColumn);
        else
            return Error(lineNo, L".mode", L"expected 'stream' or 'column', got '" + args + L"'");

        Emit(L"mode " + v);
        return true;
    }

    bool Runner::DoOvertype (int lineNo, const std::wstring& args)
    {
        bool want;
        std::wstring err;

        if (!ParseBool(args, want, err))
            return Error(lineNo, L".overtype", err);

        SetOvertype(want);

        // Read it back: if the Insert press did not land (view not attached,
        // focus stolen) every overtype case below would be testing insert mode
        // and still look plausible.
        const bool actual = COEditorView::IsOvertypeMode() ? true : false;

        if (actual != want)
            return Error(lineNo, L".overtype",
                         L"could not reach overtype=" + Num(want ? 1 : 0)
                       + L", still " + Num(actual ? 1 : 0));

        Emit(std::wstring(L"overtype ") + (want ? L"on" : L"off"));
        return true;
    }

    bool Runner::DoType (int lineNo, const std::wstring& args)
    {
        std::wstring text, err;

        if (!ParseDelimited(args, text, err))
            return Error(lineNo, L".type", err);

        // One WM_CHAR per UTF-16 code unit, which is what the keyboard delivers:
        // a surrogate pair arrives as two WM_CHARs, and OnChar's auto-pair logic
        // sees each half separately. Sending the whole string in one message
        // would exercise a path no user can reach.
        for (size_t i = 0; i < text.size(); i++)
            SendChar(text[i]);

        Emit(L"type |" + Render(text) + L"|");
        return true;
    }

    bool Runner::DoKey (int lineNo, const std::wstring& args)
    {
        std::wstring rest;
        const std::wstring name = Head(args, rest);

        if (name.empty())
            return Error(lineNo, L".key", L"missing key name");

        int repeat = 1;
        std::wstring err;

        if (!Trim(rest).empty() && (!ParseInt(Trim(rest), repeat, err) || repeat < 1))
            return Error(lineNo, L".key", L"bad repeat count '" + Trim(rest) + L"'");

        UINT vk = 0, mods = 0;
        std::wstring keyName;

        if (!ParseKeyCombo(name, keyName, mods))
            return Error(lineNo, L".key", L"unknown modifier in '" + name + L"'");

        if (!LookupKey(keyName, vk))
            return Error(lineNo, L".key", L"unknown key name '" + name + L"'");

        for (int i = 0; i < repeat; i++)
            SendKey(vk, mods);

        Emit(L"key " + name + (repeat > 1 ? L" " + Num(repeat) : std::wstring()));
        return true;
    }

    bool Runner::DoCmd (int lineNo, const std::wstring& args)
    {
        std::wstring rest;
        const std::wstring name = Head(args, rest);

        if (name.empty())
            return Error(lineNo, L".cmd", L"missing command name");

        int repeat = 1;
        std::wstring err;

        if (!Trim(rest).empty() && (!ParseInt(Trim(rest), repeat, err) || repeat < 1))
            return Error(lineNo, L".cmd", L"bad repeat count '" + Trim(rest) + L"'");

        UINT id = 0;

        if (!LookupCommand(name, id))
            return Error(lineNo, L".cmd", L"unknown command name '" + name + L"'");

        for (int i = 0; i < repeat; i++)
            SendCommand(id);

        Emit(L"cmd " + name + (repeat > 1 ? L" " + Num(repeat) : std::wstring()));
        return true;
    }

    bool Runner::DoUndo (int lineNo, const std::wstring& args, bool redo)
    {
        const wchar_t* verb = redo ? L".redo" : L".undo";

        int repeat = 1;
        std::wstring err;

        if (!args.empty() && (!ParseInt(args, repeat, err) || repeat < 1))
            return Error(lineNo, verb, L"bad repeat count '" + args + L"'");

        // Through the real command, not Storage::Undo/Redo directly: OnEditUndo is
        // what clears the pending auto-pair flag and re-syncs the selection, so "one
        // Ctrl+Z undoes the pair as a unit" is meaningless if the harness bypasses
        // the handler.
        for (int i = 0; i < repeat; i++)
            SendCommand(redo ? ID_EDIT_REDO : ID_EDIT_UNDO);

        Emit(std::wstring(redo ? L"redo " : L"undo ") + Num(repeat));
        return true;
    }

    bool Runner::DoEcho (int lineNo, const std::wstring& args)
    {
        (void)lineNo;
        Emit(L"# " + args);
        return true;
    }

    bool Runner::DoSettings (int lineNo, const std::wstring& args)
    {
        if (args.empty())
            return Error(lineNo, L".settings", L"missing 'key=value,...' list");

        OpenEditor::GlobalSettings& gs = Globals();
        const std::vector<std::wstring> items = SplitOn(args, L',');
        std::wstring applied;

        for (size_t i = 0; i < items.size(); i++)
        {
            const std::wstring item = Trim(items[i]);

            if (item.empty())
                continue;

            const size_t eq = item.find(L'=');

            if (eq == std::wstring::npos)
                return Error(lineNo, L".settings", L"expected 'key=value', got '" + item + L"'");

            const std::wstring key = Trim(item.substr(0, eq));
            const std::wstring val = Trim(item.substr(eq + 1));

            const SettingEntry*    entry    = FindSetting(key);
            const IntSettingEntry* intEntry = entry ? 0 : FindIntSetting(key);

            if (!entry && !intEntry)
                return Error(lineNo, L".settings", L"unknown setting '" + key + L"'");

            // Snapshot on the FIRST write to this key only, so RestoreSettings
            // puts back the value the session had before this run rather than an
            // intermediate one this same script set. Each row is compared against
            // its OWN table's pointer, since a bool and an int entry are different
            // types and could never alias.
            bool saved = false;

            for (size_t s = 0; s < m_saved.size(); s++)
                if ((entry    && m_saved[s].entry    == entry)
                 || (intEntry && m_saved[s].intEntry == intEntry))
                    saved = true;

            std::wstring rendered;

            if (entry)
            {
                bool want;
                std::wstring err;

                if (!ParseBool(val, want, err))
                    return Error(lineNo, L".settings", key + L": " + err);

                if (!saved)
                {
                    Saved sv;
                    sv.entry    = entry;
                    sv.intEntry = 0;
                    sv.bvalue   = (gs.*(entry->get))();
                    sv.ivalue   = 0;
                    m_saved.push_back(sv);
                }

                // notify = true: the setter walks GlobalSettings -> Settings ->
                // Storage -> every COEditorView, and the view's OnSettingsChanged is
                // what re-runs ResetAutoPairs. Pinning the backing field directly
                // would leave a stale pair table and make the case test nothing.
                (gs.*(entry->set))(want, true);

                rendered = Num(want ? 1 : 0);
            }
            else
            {
                // Same notify rule. The integer keys a folding case pins are the
                // ones PlSqlSupport reads in its constructor, so the write has to
                // land before `.lang PL/SQL` builds the support object - pinning
                // them afterwards is silently a no-op for that document.
                int want;
                std::wstring err;

                if (!ParseInt(val, want, err))
                    return Error(lineNo, L".settings", key + L": " + err);

                if (!saved)
                {
                    Saved sv;
                    sv.entry    = 0;
                    sv.intEntry = intEntry;
                    sv.bvalue   = false;
                    sv.ivalue   = (gs.*(intEntry->get))();
                    m_saved.push_back(sv);
                }

                (gs.*(intEntry->set))(want, true);

                rendered = Num(want);
            }

            if (!applied.empty())
                applied += L",";

            applied += key + L"=" + rendered;
        }

        Emit(L"settings " + applied);
        return true;
    }

    // ---------------------------------------------------------------- indent type ---

    bool Runner::DoIndentType (int lineNo, const std::wstring& args)
    {
        const std::wstring v = Lower(args);
        int type;

        if      (v == L"none")  type = OpenEditor::eiNone;
        else if (v == L"auto")  type = OpenEditor::eiAuto;
        else if (v == L"smart") type = OpenEditor::eiSmart;
        else
            return Error(lineNo, L".indenttype", L"expected none|auto|smart, got '" + args + L"'");

        // Snapshot on the FIRST write only, like `.settings`, so a script that pins the
        // type twice still ends back at the value the session had.
        if (!m_savedIndentType)
        {
            m_origIndentType  = m_doc->GetSettings().GetIndentType();
            m_savedIndentType = true;
        }

        // notify=false: nothing caches IndentType - EditContext::GetIndentType() reads it
        // off Storage's Settings on every Enter - and notifying would rebuild the
        // LanguageSupport and re-run ResetAutoPairs under a case that never asked for it.
        m_doc->GetSettings().SetIndentType(type, false /*notify*/);

        Emit(L"indenttype " + v);
        return true;
    }

    // ---------------------------------------------------------------- clipboard ---

    bool Runner::DoClipboard (int lineNo, const std::wstring& args)
    {
        std::wstring err;
        const HWND   owner = m_view->GetSafeHwnd();

        if (EqualsNoCase(Trim(args), L"clear"))
        {
            if (!ClearClipboard(owner, err))
                return Error(lineNo, L".clipboard", err);

            Emit(L"clipboard clear");
            return true;
        }

        // The payload is pipe-delimited like every other text directive, but the
        // options sit AFTER the closing pipe - and ParseDelimited stops at the
        // last '|' in the string, so it would swallow them. Hence the split here.
        const size_t open  = args.find(L'|');
        const size_t close = args.find_last_of(L'|');

        if (open == std::wstring::npos)
            return Error(lineNo, L".clipboard",
                         L"expected '|text|' or 'clear', got '" + args + L"'");

        if (close == open)
            return Error(lineNo, L".clipboard", L"missing the closing '|'");

        std::wstring text;

        if (!Unescape(args.substr(open + 1, close - open - 1), text, err))
            return Error(lineNo, L".clipboard", err);

        bool code = false, columnar = false;

        const std::vector<std::wstring> opts = SplitOn(args.substr(close + 1), L',');

        for (size_t i = 0; i < opts.size(); i++)
        {
            const std::wstring opt = Trim(opts[i]);

            if (opt.empty())
                continue;

            if      (EqualsNoCase(opt, L"code"))     code     = true;
            else if (EqualsNoCase(opt, L"columnar")) columnar = true;
            else
                return Error(lineNo, L".clipboard",
                             L"unknown option '" + opt + L"' (expected 'code' or 'columnar')");
        }

        if (!SetClipboardPayload(owner, text, code, columnar, err))
            return Error(lineNo, L".clipboard", err);

        // Read it back, the way DoOvertype reads the Insert press back: a payload
        // that never landed - another process holding the clipboard, a refused
        // SetClipboardData - would otherwise surface much later as a paste that did
        // nothing, with the cause several cases away.
        std::wstring actual;

        const EClipboardRead rd = ReadClipboardText(actual);

        if (rd == eClipboardReadFailed)
            return Error(lineNo, L".clipboard",
                         L"the clipboard could not be read back - OpenClipboard failed "
                         L"after retries, so the payload's state is unknown");

        if (rd == eClipboardAbsent)
            return Error(lineNo, L".clipboard",
                         L"read back <empty> - CF_UNICODETEXT is not on the clipboard "
                         L"at all, expected |" + Render(text) + L"|");

        // Compared as written, not through FoldCRLF: this is the raw CF_UNICODETEXT
        // buffer, so a `\r\n` in the script has to survive as one. Folding here
        // would let a payload with the wrong line endings pass unnoticed.
        if (actual != text)
            return Error(lineNo, L".clipboard",
                         L"read back |" + Render(actual) + L"|, expected |" + Render(text) + L"|");

        std::wstring rendered = L"clipboard |" + Render(text) + L"|";

        if (code)     rendered += L" code";
        if (columnar) rendered += L" columnar";

        Emit(rendered);
        return true;
    }

    // --------------------------------------------- language-support identity ---

    bool Runner::DoSupportMark (int lineNo, const std::wstring& args)
    {
        if (!args.empty())
            return Error(lineNo, L".supportmark",
                         L"takes no arguments, got '" + args + L"'");

        auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                           m_doc->GetStorage().GetLanguageSupport().get());

        if (!pPlSql)
            return Error(lineNo, L".supportmark", L"document has no PL/SQL support");

        // Recorded, not emitted: an id in a shared baseline would differ on every run. The
        // constant line is what tells a reader of the baseline where the mark was taken.
        m_markedSupportId = pPlSql->GetInstanceId();
        Emit(L"supportmark");
        return true;
    }

    // ------------------------------------------------------------- folding ---
    //
    // Both things folding needs from a script - a way to make regions EXIST and a way to put one
    // into a known state - go through the production paths rather than around them: a case that
    // poked the model directly would be asserting the test's own bookkeeping, not the editor's.

    // Effectively unbounded, and NOT the 3000 ms the production FindMatch path uses: a settle
    // that quietly gave up yields a transcript that looks plausible and is wrong. It is finite at
    // all only so a wedged worker shows up as one ERROR line instead of a hung test run.
    static const ULONGLONG kFoldSettleDeadlineMs = 10 * 60 * 1000;

    bool Runner::DoFoldSettle (int lineNo, const std::wstring& args)
    {
        // Regions are built by COEDocument::UpdateFoldsFromOutline, which in the product runs only
        // from the view's idle tick and returns early until the parser reports IsDone(). This
        // handler runs synchronously on the UI thread inside a message, so MFC idle never fires
        // while a script replays: without this directive a `.text` followed by a fold assertion
        // sees zero regions.
        //
        // Determinism comes from WaitForParseQuiescent: it BLOCKS on the worker's completion event
        // and then drives OnIdle(0) itself, so UpdateFoldsFromOutline always reads a tree the parse
        // actually published. Machine speed changes how long a tick takes, never what it produces.
        //
        // Deliberately the production primitive rather than a test-only synchronous mode: a sync
        // build switch would have the suite exercise a processText path the product never runs.
        int maxTicks = 64;
        std::wstring err;

        if (!args.empty() && !ParseInt(args, maxTicks, err))
            return Error(lineNo, L".foldsettle", err);

        if (maxTicks < 1)
            return Error(lineNo, L".foldsettle",
                         L"tick bound must be >= 1, got " + Num(maxTicks));

        int  ticks   = 0;
        bool pending = true;

        while (ticks < maxTicks)
        {
            ticks++;

            // COEDocument::OnIdle -> PlSqlSupport::OnIdle. In the product this SUBMITS
            // asynchronously, so its return value is computed before the batch it queued has
            // published and it cannot be used as `pending` on its own - that would make the final
            // batch report "more work left" and add a tick to every case. It is kept as the GATE
            // SIGNAL: COEDocument::OnIdle returns FALSE immediately when PlSqlKeywordNavigation is
            // off, one of the three settings a folding case pins, so an unpinned key surfaces here
            // as an immediate FALSE instead of a mysteriously empty model. That is why `pending` is
            // `submitted &&` and not a bare `!IsParseDone()`, which would spin all 64 ticks.
            const bool submitted = m_doc->OnIdle(0) ? true : false;

            // BLOCK until the parse has published everything it is going to, then build the regions
            // from that. The failure is a HARD error, not a degradation: FindMatch may silently fall
            // back to a text scan, but a settle that quietly gave up yields a transcript that looks
            // plausible and is wrong - the one thing a test harness must not produce.
            if (!m_doc->WaitForParseQuiescent(::GetTickCount64() + kFoldSettleDeadlineMs))
                return Error(lineNo, L".foldsettle",
                             L"background parse did not settle within "
                             + Num((int)kFoldSettleDeadlineMs) + L" ms");

            // Idempotent and near-free unless a parse just completed; this is the
            // call that turns the finished syntax tree into fold regions.
            m_doc->UpdateFoldsFromOutline();

            pending = submitted && !m_doc->IsParseDone();

            if (!pending)
                break;
        }

        // The whole consequence of settling on one line: a diff against the baseline then points
        // straight at "no regions were built" rather than at a dozen knock-on caret assertions.
        const int lines = m_doc->GetStorage().GetLineCount();
        const int rows  = m_doc->GetVisibleLineCount();

        Emit(L"foldsettle ticks="  + Num(ticks)
           + L" settled="          + Num(pending ? 0 : 1)
           + L" folding="          + Num(m_doc->IsFoldingEnabled() ? 1 : 0)
           + L" regions="          + Num(m_doc->GetFoldModel().Count())
           + L" lines="            + Num(lines)
           + L" rows="             + Num(rows)
           + L" hidden="           + Num(lines - rows));

        return true;
    }

    bool Runner::DoFold (int lineNo, const std::wstring& args)
    {
        // .fold <line> collapse|expand|toggle
        //
        // Routed through COEditorView::SetFoldCollapsed, NOT through FoldModel::SetCollapsedAt,
        // because the interesting half of a fold change is not the flag - it is ApplyFoldChange():
        // mapper cache dropped, caret pulled onto the anchor at column 0 when it sat inside the
        // region, m_Topmost and the split sibling's snapped onto visible lines, scrollbar resized.
        // Setting the flag alone would leave the view stale, and the navigation cases would then
        // measure the test's own state rather than the editor's.
        //
        // `changed` is REPORTED, not asserted, so this stays a command and the `.expect` lines
        // remain the only oracle - nothing is asserted twice.
        std::wstring rest;
        const std::wstring head = Head(args, rest);
        const std::wstring what = Lower(Trim(rest));

        int line = 0;
        std::wstring err;

        if (!ParseInt(head, line, err))
            return Error(lineNo, L".fold", err);

        if (line < 1)
            return Error(lineNo, L".fold", L"line is 1-based, got " + head);

        if (what != L"collapse" && what != L"expand" && what != L"toggle")
            return Error(lineNo, L".fold",
                         L"expected collapse|expand|toggle, got '" + what + L"'");

        OpenEditor::FoldModel& folds = m_doc->GetFoldModel();
        const int  line0    = line - 1;
        const bool toggling = (what == L"toggle");
        bool       want     = (what == L"collapse");

        if (toggling)
        {
            if (!folds.IsAnchorAt(line0))
            {
                // Not an anchor, so there is no state to invert. Reported as a no-op rather than an
                // Error(): aborting the run here would hide every later case behind one script typo,
                // and the case's own `.expect anchor` is what should fail.
                Emit(L"fold " + Num(line) + L" toggle -> noop (not an anchor)");
                return true;
            }

            want = !folds.IsCollapsedAt(line0);
        }

        const bool changed = m_view->SetFoldCollapsed(line0, want);

        Emit(L"fold " + Num(line) + L" " + what
           + L" -> " + (changed ? std::wstring(L"changed") : std::wstring(L"noop")));

        return true;
    }

    // -------------------------------------------------------- pane projection ---
    //
    // Folding and the Code Outline pane read ONE extraction with deliberately different
    // granularity: blocks, comment runs and every multiline statement go to folding only, so the
    // gutter gets a region for them while the pane keeps listing exactly what it listed before
    // those item types existed. That invariant is the entire reason the folding-only
    // EOutlineItemType values were APPENDED instead of reusing the pane's, and it lived inside
    // CodeOutlineWnd::DoRefreshTree - a window method no script could reach - untested.
    //
    // Both lists are read through PlSqlSupport, which is the point: a test that rebuilt either one
    // itself would only ever prove that the test can build a list.

    // Enumerator NAMES rather than raw values: a digest of numbers would diff just as well but
    // nobody would read it, and an unread baseline gets re-blessed without being understood. An
    // unlisted enumerator prints as "?", which is itself a diff - a new type cannot pass unnamed.
    const wchar_t* OutlineTypeName (OpenEditor::EOutlineItemType type)
    {
        switch (type)
        {
        case OpenEditor::oitBlock:               return L"Block";
        case OpenEditor::oitFunction:            return L"Function";
        case OpenEditor::oitProcedure:           return L"Procedure";
        case OpenEditor::oitPackage:             return L"Package";
        case OpenEditor::oitPackageBody:         return L"PackageBody";
        case OpenEditor::oitTypeBody:            return L"TypeBody";
        case OpenEditor::oitType:                return L"Type";
        case OpenEditor::oitTypeMemberFunction:  return L"TypeMemberFunction";
        case OpenEditor::oitTypeMemberProcedure: return L"TypeMemberProcedure";
        case OpenEditor::oitSelect:              return L"Select";
        case OpenEditor::oitFromClause:          return L"FromClause";
        case OpenEditor::oitWhereClause:         return L"WhereClause";
        case OpenEditor::oitComment:             return L"Comment";
        case OpenEditor::oitIf:                  return L"If";
        case OpenEditor::oitCase:                return L"Case";
        case OpenEditor::oitLoop:                return L"Loop";
        case OpenEditor::oitInsert:              return L"Insert";
        case OpenEditor::oitUpdate:              return L"Update";
        case OpenEditor::oitDelete:              return L"Delete";
        case OpenEditor::oitOpenCursor:          return L"OpenCursor";
        case OpenEditor::oitTrigger:             return L"Trigger";
        case OpenEditor::oitStatement:           return L"Statement";
        case OpenEditor::oitMerge:               return L"Merge";
        case OpenEditor::oitUnknown:             return L"Unknown";
        }

        // Unreachable while every enumerator is listed above; present so that a newly added one
        // degrades to a visible "?" instead of to undefined behaviour.
        return L"?";
    }

    bool Runner::DoPaneOutline (int lineNo, const std::wstring& args)
    {
        if (!args.empty())
            return Error(lineNo, L".paneoutline",
                         L"takes no arguments, got '" + args + L"'");

        OpenEditor::Storage& storage = m_doc->GetStorage();
        auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                          storage.GetLanguageSupport().get());

        if (!pPlSql)
            return Error(lineNo, L".paneoutline", L"document has no PL/SQL support");

        // Two reads of ONE extraction: PlSqlSupport holds two products of the single walk -
        // FoldCandidates for folding, OutlineItems for the pane - and both counts below must come
        // from that one pass or `filtered = total - visible` stops being arithmetic on one
        // snapshot and becomes a difference between two parses. Both still come from PlSqlSupport,
        // which is the point: a test that re-filtered the vector itself would only ever prove that
        // the test can filter a vector.
        const OpenEditor::FoldCandidates& all  = pPlSql->GetLastFolds();
        const OpenEditor::OutlineItems&   pane = pPlSql->GetLastPaneOutline();

        // `done` is REPORTED, not enforced: with PlSqlOutlineEnabled or PlSqlKeywordNavigation
        // pinned off there is no parse and therefore no outline at all, and an empty projection is
        // the correct answer for those gate cases rather than an error. Printing the parser's own
        // flag is what keeps an unexpectedly empty digest explainable from the transcript alone.
        const int done = pPlSql->IsDone() ? 1 : 0;

        // Depth histogram, ascending, last bucket grouping everything deeper: a
        // bare count cannot see a pane item that kept its type but gained or lost
        // a level, and that is precisely the shape a projection bug produces.
        const int depthBuckets = 8;
        int  perDepth[depthBuckets] = { 0 };
        int  maxDepth = 0;
        bool typeSeen[OpenEditor::oitUnknown + 1] = { false };
        std::wstring leaked;

        for (const auto& it : pane)
        {
            int d = it.depth;

            if (d < 0)
                d = 0;
            else if (d >= depthBuckets)
                d = depthBuckets - 1;

            perDepth[d]++;

            if (it.depth > maxDepth)
                maxDepth = it.depth;

            if (it.type >= 0 && it.type <= OpenEditor::oitUnknown)
                typeSeen[it.type] = true;

            // IsFoldingOnlyOutlineType is the same predicate the outline walk
            // routes by, so this fails on a type added to the folding-only list
            // without the walk being taught to keep it out of the pane - the one
            // way the invariant can break quietly. First offender only: naming
            // every leaked item would bury the digest, and one is enough to act on.
            if (leaked.empty() && OpenEditor::IsFoldingOnlyOutlineType(it.type))
            {
                // +1 because pos.start.line is 0-based in the snapshot, printed
                // 1-based the way `.expect region` prints its own lines.
                leaked = std::wstring(OutlineTypeName(it.type)) + L" @line "
                       + Num(it.pos.start.line + 1);
            }
        }

        std::wstring depths = L"none";
        std::wstring types  = L"none";

        if (!pane.empty())
        {
            depths.clear();

            for (int d = 0; d < depthBuckets; d++)
            {
                if (!perDepth[d])
                    continue;

                if (!depths.empty())
                    depths += L",";

                depths += Num(d) + L":" + Num(perDepth[d]);
            }

            types.clear();

            for (int t = 0; t <= OpenEditor::oitUnknown; t++)
            {
                if (!typeSeen[t])
                    continue;

                if (!types.empty())
                    types += L",";

                types += OutlineTypeName(static_cast<OpenEditor::EOutlineItemType>(t));
            }
        }

        Emit(L"paneoutline done="  + Num(done)
           + L" total="            + Num(static_cast<int>(all.size()))
           + L" visible="          + Num(static_cast<int>(pane.size()))
           + L" filtered="         + Num(static_cast<int>(all.size())
                                       - static_cast<int>(pane.size()))
           + L" maxdepth="         + Num(maxDepth)
           + L" depths="           + depths
           + L" types="            + types);

        // The transcript diff is the quiet oracle; this is the loud one, so a
        // regression reads "If @line 4" instead of an unexplained types= change.
        Check(lineNo, L"pane-nofolding-only", std::wstring(L"none"),
              leaked.empty() ? std::wstring(L"none") : leaked);

        return true;
    }

    // ---------------------------------------------------- outline quality ---
    //
    // OutlineQuality is the gate the whole folding design hangs on: on oqTruncated/oqSyntaxError
    // keep the prior regions and only update the quality flag, so folds never thrash on a partial
    // parse. Two consumers act on it - UpdateFoldsFromOutline passes `quality == oqClean` into
    // RebuildFromSnapshot, and the pane's status bar switches on it.
    //
    // OEFolding.cpp's self-tests hand RebuildFromSnapshot a literal `false` built from a synthetic
    // vector, so they prove the model refuses what it is told to refuse. Left uncovered is the
    // production half - EnsureOutline()'s quality computation and its three-way commit rule, which
    // decides whether a fresh snapshot replaces the old one or is discarded. A reordering of that
    // if/else would pass every other suite.

    // Enumerator NAMES for the reason OutlineTypeName uses them. A value not listed here prints "?",
    // which is both a diff and a failure of the loud check below.
    const wchar_t* OutlineQualityName (OpenEditor::PlSqlSupport::OutlineQuality q)
    {
        switch (q)
        {
        case OpenEditor::PlSqlSupport::oqClean:       return L"oqClean";
        case OpenEditor::PlSqlSupport::oqTruncated:   return L"oqTruncated";
        case OpenEditor::PlSqlSupport::oqSyntaxError: return L"oqSyntaxError";
        }

        return L"?";
    }

    bool Runner::DoOutlineQuality (int lineNo, const std::wstring& args)
    {
        if (!args.empty())
            return Error(lineNo, L".outlinequality",
                         L"takes no arguments, got '" + args + L"'");

        OpenEditor::Storage& storage = m_doc->GetStorage();
        auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                           storage.GetLanguageSupport().get());

        if (!pPlSql)
            return Error(lineNo, L".outlinequality", L"document has no PL/SQL support");

        // Both getters warm the same lazy cache, so reading the snapshot and then the quality cannot
        // tear: the first call performs the extraction and clears m_outlineStale, the second returns
        // what that call committed. `items` is the FOLD-CANDIDATE count, not the pane's: this
        // directive states what folding was handed, which is the committed product.
        const OpenEditor::FoldCandidates& all = pPlSql->GetLastFolds();
        const OpenEditor::PlSqlSupport::OutlineQuality quality =
            pPlSql->GetLastOutlineQuality();

        const wchar_t* qname = OutlineQualityName(quality);
        const int done = pPlSql->IsDone() ? 1 : 0;

        const OpenEditor::FoldModel& folds = m_doc->GetFoldModel();

        Emit(L"outlinequality quality=" + std::wstring(qname)
           + L" done="                   + Num(done)
           + L" items="                  + Num(static_cast<int>(all.size()))
           + L" regions="                + Num(folds.Count())
           + L" folding="                + Num(m_doc->IsFoldingEnabled() ? 1 : 0)
           + L" modelclean="             + Num(folds.IsSnapshotClean() ? 1 : 0));

        // `items` against `regions` is the whole chain made visible: every quality advances, so an
        // unclean parse still commits its fold candidates and the model still rebuilds from them -
        // the two numbers move together rather than both freezing at the last clean parse.
        // `modelclean` is the far end of the same chain, read back from FoldModel::IsSnapshotClean(),
        // and is what makes this an end-to-end measurement of support -> document -> model rather
        // than one flag in isolation. It is only meaningful with folding enabled; with it off
        // UpdateFoldsFromOutline never runs and the model sits at its constructed default of true,
        // which `folding=0` says out loud.
        //
        // Loud oracle, same pattern as pane-nofolding-only: a fourth OutlineQuality value would
        // otherwise arrive as an unexplained quality= change in a diff.
        const std::wstring known = (std::wstring(qname) == L"?") ? L"?" : L"known";

        Check(lineNo, L"outline-quality-known", std::wstring(L"known"), known);

        return true;
    }

    // ------------------------------------------------------- outline trust ---
    //
    // Per-item visibility for OutlineItem::rangeTrusted and for the parse cut that flag is measured
    // against. `.outlinequality` answers "was this snapshot clean?" about the WHOLE buffer; this is
    // the narrower question - which individual ranges in an unclean snapshot have an invented end.
    //
    // A MEASUREMENT directive: it asserts nothing about trust itself, its only check being the same
    // loud known-value one `.outlinequality` makes. `trusted` beside `pastcut` on every item prints
    // the two candidate rules side by side, so a rule that is too broad or too narrow shows up as a
    // disagreement rather than as a guess - and they do diverge on a comment run past the cut, where
    // `pastcut` is a false positive (outline-trust-comment-run-beside-a-syntax-error in FOLDING.stp).
    //
    // Reads the COMMITTED fold candidates through GetLastFolds(), the product folding acts on, so
    // what this prints is what a folding change would actually be handed. `.paneoutline` reads that
    // same vector beside the pane's; the two products differ by design and a digest that mixed them
    // would measure neither.
    bool Runner::DoOutlineTrust (int lineNo, const std::wstring& args)
    {
        if (!args.empty())
            return Error(lineNo, L".outlinetrust",
                         L"takes no arguments, got '" + args + L"'");

        OpenEditor::Storage& storage = m_doc->GetStorage();
        auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                           storage.GetLanguageSupport().get());

        if (!pPlSql)
            return Error(lineNo, L".outlinetrust", L"document has no PL/SQL support");

        // One lazy cache, warmed by the first read and reused by the rest, so the
        // four below cannot end up describing two different extractions.
        const OpenEditor::FoldCandidates& all = pPlSql->GetLastFolds();
        const OpenEditor::PlSqlSupport::OutlineQuality quality =
            pPlSql->GetLastOutlineQuality();
        const int cutLine = pPlSql->GetParseCutLine();
        const int done    = pPlSql->IsDone() ? 1 : 0;

        const wchar_t* qname = OutlineQualityName(quality);

        // Two counts of the same suspicion, reached two ways: `untrusted` is what the
        // outline walk stamped per item, `pastcut` is what a whole-buffer line rule
        // would stamp. Equal counts would suggest the rules agree; equal counts with
        // DIFFERENT members would not, which is why the per-item lines follow.
        int untrusted = 0;
        int pastCut   = 0;

        for (const auto& it : all)
        {
            if (!it.rangeTrusted)
                untrusted++;

            if (cutLine >= 0 && it.pos.end.line >= cutLine)
                pastCut++;
        }

        Emit(L"outlinetrust done=" + Num(done)
           + L" quality="          + std::wstring(qname)
           + L" items="            + Num(static_cast<int>(all.size()))
           + L" untrusted="        + Num(untrusted)
           + L" pastcut="          + Num(pastCut)
           + L" cutline="          + (cutLine < 0 ? std::wstring(L"-")
                                                  : Num(cutLine + 1)));

        // Loud oracle, same shape as outline-quality-known: without it a fourth
        // OutlineQuality value would arrive only as an unexplained quality= diff.
        Check(lineNo, L"outline-quality-known", std::wstring(L"known"),
              (std::wstring(qname) == L"?") ? std::wstring(L"?")
                                            : std::wstring(L"known"));

        // One line per item, in snapshot order - NOT sorted by position, because the order itself is
        // what the walk produced and re-sorting here would hide a change in it. Index printed so a
        // diff reads as "item 3 changed" rather than by counting lines. FmtSquare renders 1-based
        // line:col exactly as `.expect region` does, so a range here and a region there are the same
        // spelling of the same thing.
        //
        // No `|label|` field: this directive reads the COMMITTED FOLD CANDIDATES and a FoldCandidate
        // carries geometry only - the label is pane payload, built by FoldStatementLabel and never
        // read by folding. Printing one would mean matching the pane vector up by index: two products
        // on one transcript line, and a false impression that the fold list has labels.
        for (size_t i = 0; i < all.size(); i++)
        {
            const auto& it = all[i];

            Emit(L"outlinetrustitem " + Num(static_cast<int>(i))
               + L" "                 + OutlineTypeName(it.type)
               + L" "                 + FmtSquare(it.pos)
               + L" depth="           + Num(it.depth)
               + L" trusted="         + Num(it.rangeTrusted ? 1 : 0)
               + L" pastcut="         + Num((cutLine >= 0
                                          && it.pos.end.line >= cutLine) ? 1 : 0));
        }

        return true;
    }

    // ---------------------------------------------------------- line status ---

    // The extraction `.linestatus` and the three `.expect unparsed*` checks share.
    //
    // Fills `map` over the ENTIRE buffer, base line 0, and hands back the PL/SQL support it came
    // from - or 0 when the document has none, which both callers turn into the same script error.
    // One extraction, four reads, so the directive's transcript and the loud checks cannot end up
    // describing two different parses.
    //
    // The WHOLE buffer, not the view's paint window, is what makes the numbers mean anything:
    // GetLineStatus's contract is a window, and against a window `unparsed=` would count whatever
    // happened to be visible and `boundary=` would come and go as the view scrolled. Over the buffer
    // the cut line is always inside the window, so the marker is always reported and always sits at
    // `from=`; a disagreement between those two is then a real finding, not a scrolling artifact.
    //
    // A fresh local map and NOT COEView::m_statusMap: that one belongs to a view this handler is not
    // standing in, and the provider-action shortcut at the top of GetLineStatus would hand back a
    // partial fill on any repeat call at the same base and count.
    //
    // The settled check is LOUD and lives HERE rather than in the two callers, because
    // HasUnparsedTail() is gated on m_done on purpose - an in-flight parse legitimately reports no
    // tail, which is how the band avoids flashing over every large file. Without this a `.linestatus`
    // placed before its `.foldsettle` would print `tail=0 unparsed=0` and `.expect unparsed 0` would
    // PASS, for a reason that has nothing to do with the gutter.
    OpenEditor::PlSqlSupport* Runner::FillWholeBufferLineStatus (
        int lineNo, const std::wstring& verb,
        OpenEditor::LineStatusMap& map, int& lines)
    {
        OpenEditor::Storage& storage = m_doc->GetStorage();
        auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                           storage.GetLanguageSupport().get());

        lines = 0;

        if (!pPlSql)
            return 0;

        lines = storage.GetLineCount();

        map.Reset(0, lines);
        pPlSql->GetLineStatus(map, 0, lines);

        Check(lineNo, verb + L"-parse-settled", std::wstring(L"1"),
              Num(pPlSql->IsDone() ? 1 : 0));

        return pPlSql;
    }

    // `.linestatus` - print the per-buffer-line status the syntax gutter paints from, with the
    // unparsed tail called out explicitly.
    //
    // A MEASUREMENT directive, like `.outlinetrust` and `.outlinequality`: its only assertion is the
    // loud parse-settled one above, everything else is transcript, and the numbers are pinned by
    // `.expect unparsed|unparsedfrom|unparsedboundary` in the cases that carry them so re-blessing a
    // baseline cannot quietly drop a regression. Fields are printed in the order the gutter consumes
    // them.
    //
    //   lines      - the buffer's line count, the upper bound of every other number.
    //   done       - the parse is finished. Always 1 here, see the check above; printed anyway
    //                because it is the field that makes `tail` interpretable.
    //   processed  - GetProcessedLineCount(): how many lines the parser consumed, and therefore the
    //                0-based index of the first it never looked at. Rendered 1-based so it lines up
    //                with `from` - the same quantity measured off the map instead of off the counter
    //                that produced it, so a disagreement is a real defect, not a rendering one.
    //   tail       - HasUnparsedTail(): a configured limit cut a FINISHED parse short.
    //   unparsed   - how many buffer lines carry LineStatus::unparsed.
    //   from       - the first such line, 1-based, or `-` when there is none.
    //   boundary   - the line carrying LineStatus::unparsedBoundary, or `-`.
    //   cutline    - GetParseCutLine()+1 or `-`, printed beside the two above on purpose: they answer
    //                the same question from different sources, the live counter and the outline
    //                cache. With the outline on and the stop a truncation they agree; a case where
    //                they do not is exactly the case that justified not coupling them.
    bool Runner::DoLineStatus (int lineNo, const std::wstring& args)
    {
        if (!args.empty())
            return Error(lineNo, L".linestatus",
                         L"takes no arguments, got '" + args + L"'");

        OpenEditor::LineStatusMap map;
        int lines = 0;
        OpenEditor::PlSqlSupport* pPlSql =
            FillWholeBufferLineStatus(lineNo, L".linestatus", map, lines);

        if (!pPlSql)
            return Error(lineNo, L".linestatus", L"document has no PL/SQL support");

        const int processed = pPlSql->GetProcessedLineCount();
        const int tail      = pPlSql->HasUnparsedTail() ? 1 : 0;
        const int cutLine   = pPlSql->GetParseCutLine();

        int unparsed = 0;
        int from     = -1;
        int boundary = -1;

        // Contiguous runs of the flag, collected during the same scan. Runs rather than one line per
        // buffer line because the flag is a SUFFIX by construction - everything from m_currLine to
        // the end - so the run count is 0 or 1 however long the document is and the transcript
        // cannot scale with the truncation it is measuring. That is also what makes the count a
        // structural oracle rather than a summary: GetLineStatus marking a NON-contiguous set of
        // lines would arrive as runs=2 in the baseline instead of as a silent band in the middle of
        // a file nobody happened to be looking at.
        std::vector<std::pair<int,int>> runs;   // [first, last], 0-based inclusive

        for (int i = 0; i < lines; i++)
        {
            const OpenEditor::LineStatus& st = map[i];

            if (st.unparsed)
            {
                if (from < 0)
                    from = i;

                unparsed++;

                if (!runs.empty() && runs.back().second == i - 1)
                    runs.back().second = i;
                else
                    runs.push_back(std::make_pair(i, i));
            }

            if (st.unparsedBoundary)
                boundary = i;
        }

        Emit(L"linestatus lines="    + Num(lines)
           + L" done="               + Num(pPlSql->IsDone() ? 1 : 0)
           + L" processed="          + Num(processed + 1)
           + L" tail="               + Num(tail)
           + L" unparsed="           + Num(unparsed)
           + L" from="               + (from     < 0 ? std::wstring(L"-") : Num(from + 1))
           + L" boundary="           + (boundary < 0 ? std::wstring(L"-") : Num(boundary + 1))
           + L" cutline="            + (cutLine  < 0 ? std::wstring(L"-") : Num(cutLine + 1)));

        Emit(L"linestatusruns " + Num(static_cast<int>(runs.size())));

        // Index printed so a diff reads as "run 0 moved" rather than by counting lines,
        // matching `.outlinetrust`'s outlinetrustitem. Bounds 1-based inclusive, the
        // spelling `.expect region` uses for a span.
        for (size_t i = 0; i < runs.size(); i++)
            Emit(L"linestatusrun " + Num(static_cast<int>(i))
               + L" "              + Num(runs[i].first + 1)
               + L"-"              + Num(runs[i].second + 1));

        return true;
    }

    // ------------------------------------------------------------- searching ---

    // `.search |text|` - set the find pattern the search commands use, without opening the modeless
    // Find/Replace dialog. Same call the dialog makes in UpdateData, and it lands in
    // Searcher::m_strText, which is the PATTERN side of a search; the replacement side is
    // FindCtx::m_text, supplied per call - see `.replaceall`.
    //
    // Search OPTIONS (case, whole word, regexp, all-windows) are the persisted settings on the
    // shared Searcher, so a case that depends on one pins it with `.settings` like every other
    // setting-sensitive verb.
    //
    // A miss is reported on the status line rather than blocking the run, but it is still useless
    // here: no match means no movement and no selection to assert on. Only search for text the case
    // has itself just put in the document.
    bool Runner::DoSearch (int lineNo, const std::wstring& args)
    {
        std::wstring text, err;

        if (!ParseDelimited(args, text, err))
            return Error(lineNo, L".search", err);

        if (text.empty())
            return Error(lineNo, L".search",
                         L"pattern must not be empty - an empty one makes "
                         L"RepeadSearch end in a modal message box");

        m_view->SetSearchText(text.c_str());
        m_searchText = text;

        Emit(L"search |" + Render(text) + L"|");
        return true;
    }

    // `.replaceall |replacement|` - whole-document Replace All through the same
    // EditContext::SearchBatch(esbReplace) the dialog's whole-file branch calls.
    //
    // The argument is the REPLACEMENT, not the pattern: that is SearchBatch's contract, and the
    // dialog passes its own replace string the same way. The pattern is whatever `.search` set last.
    // The occurrence count is echoed so a run that replaced nothing says so on its own line rather
    // than only through a later `.expect text`.
    bool Runner::DoReplaceAll (int lineNo, const std::wstring& args)
    {
        std::wstring text, err;

        if (!ParseDelimited(args, text, err))
            return Error(lineNo, L".replaceall", err);

        // Searcher::Replace throws on CR or LF in the replacement. Reporting that as a script error
        // keeps the transcript readable instead of letting an app exception abort the run - and it is
        // also why line numbers collected before the call cannot go stale: a batch replace never
        // changes the line count.
        if (text.find_first_of(L"\r\n") != std::wstring::npos)
            return Error(lineNo, L".replaceall",
                         L"replacement must not contain CR or LF - the product "
                         L"rejects a multi-line replace string");

        if (m_searchText.empty())
            return Error(lineNo, L".replaceall",
                         L"no search text set - use `.search` first");

        const int count = m_view->SearchBatch(text.c_str(), OpenEditor::esbReplace);

        Emit(L"replaceall |" + Render(text) + L"| count=" + Num(count));
        return true;
    }

    // ---------------------------------------------------------- assertions ---

    // Every check is rendered to a string and compared as a string, so the FAIL
    // line in the baseline shows both sides of the difference verbatim instead
    // of a bare "mismatch". A malformed `.expect` is a *script* bug, not a test
    // failure: it goes through Error() and aborts the run, because continuing
    // would only produce a cascade of knock-on FAILs that hide the real cause.
    bool Runner::DoExpect (int lineNo, const std::wstring& args)
    {
        if (args.empty())
            return Error(lineNo, L".expect", L"missing check name");

        std::wstring rest;
        const std::wstring what = Lower(Head(args, rest));
        const std::wstring want = Trim(rest);

        // ---- .expect text |...| -------------------------------------------
        if (what == L"text")
        {
            std::wstring expected, err;

            if (!ParseDelimited(want, expected, err))
                return Error(lineNo, L".expect text", err);

            Check(lineNo, L"text",
                  L"|" + Render(expected)        + L"|",
                  L"|" + Render(DocText(m_view)) + L"|");
            return true;
        }

        // ---- .expect caret L:C --------------------------------------------
        if (what == L"caret")
        {
            Position expected;
            std::wstring err;

            if (!ParsePos(want, expected, err))
                return Error(lineNo, L".expect caret", err);

            Check(lineNo, L"caret", FmtPos(expected), FmtPos(m_view->GetPosition()));
            return true;
        }

        // ---- .expect sel L:C-L:C ------------------------------------------
        if (what == L"sel")
        {
            Square expected;
            std::wstring err;

            if (!ParseSquare(want, expected, err))
                return Error(lineNo, L".expect sel", err);

            // GetSelection is an out-parameter (OEContext.h), not a getter.
            Square actual;
            m_view->GetSelection(actual);

            Check(lineNo, L"sel", FmtSquare(expected), FmtSquare(actual));
            return true;
        }

        // ---- .expect nosel -------------------------------------------------
        if (what == L"nosel")
        {
            const bool hasSel = !m_view->IsSelectionEmpty();

            Check(lineNo, L"nosel", L"1", Num(hasSel ? 0 : 1));
            return true;
        }

        // ---- .expect seltext |...| ----------------------------------------
        if (what == L"seltext")
        {
            std::wstring expected, err;

            if (!ParseDelimited(want, expected, err))
                return Error(lineNo, L".expect seltext", err);

            // An empty selection and a selection of empty text must not read the
            // same in the baseline, so the former renders as a sentinel.
            std::wstring actual;

            if (m_view->IsSelectionEmpty())
            {
                actual = L"<none>";
            }
            else
            {
                m_view->GetBlock(actual);

                // GetBlock joins lines with CRLF in both block modes, while DocText -
                // and therefore `.text` and `.expect text` - joins them with a bare LF.
                // Left as it stands, identical document content would have to be spelled
                // two different ways in one script depending only on whether it happens
                // to be selected, and every multi-line `.expect seltext` would fail on
                // the separator rather than on anything the feature did. Fold, so `\n`
                // means the same thing in every directive.
                FoldCRLF(actual);
            }

            Check(lineNo, L"seltext",
                  L"|" + Render(expected) + L"|",
                  L"|" + Render(actual)   + L"|");
            return true;
        }

        // ---- .expect clipboard |...| --------------------------------------
        // The raw CF_UNICODETEXT buffer: the only text format OnEditPaste reads,
        // and the only one OnUpdate_EditPaste enables the command for.
        if (what == L"clipboard")
        {
            std::wstring expected, err;

            if (!ParseDelimited(want, expected, err))
                return Error(lineNo, L".expect clipboard", err);

            std::wstring actual;

            // Three outcomes, not two - see EClipboardRead. An absent format and an
            // empty one are different states and must not render the same:
            // `.clipboard clear` and `.clipboard ||` produce them respectively, and a
            // paste is disabled after the first but enabled after the second. A read
            // that FAILED is neither of those and must not be dressed up as one:
            // without this branch a transient OpenClipboard failure shows up as
            // `expected=|sentinel| actual=|<empty>|`, which reads exactly like the
            // editor having cleared the clipboard when it should not have.
            const EClipboardRead rd = ReadClipboardText(actual);

            if (rd == eClipboardReadFailed)
                return Error(lineNo, L".expect clipboard",
                             L"the clipboard could not be read - OpenClipboard failed "
                             L"after retries, so its contents are unknown");

            if (rd == eClipboardAbsent)
                actual = L"<empty>";

            Check(lineNo, what,
                  L"|" + Render(expected) + L"|",
                  L"|" + Render(actual)   + L"|");
            return true;
        }

        // ---- .expect clipboardcode|clipboardcolumnar 0|1 --------------------
        // The two private markers, one question each. Which of them is present
        // decides whether a paste re-indents the payload (`{code}`) and whether it
        // switches the view into column mode (the Alt-drag tag), so a case that
        // asserted only the text would pass with both routing decisions wrong.
        if (what == L"clipboardcode" || what == L"clipboardcolumnar")
        {
            bool expected;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect " + what, err);

            const EClipboardRead actual = (what == L"clipboardcode")
                                        ? ReadClipboardCodeMarker()
                                        : ReadClipboardColumnarTag();

            // Only the {code} read can fail - the columnar tag goes through
            // IsClipboardFormatAvailable, which needs no open session. Reported as an
            // ERROR rather than as "0" for the same reason as above: "the marker is
            // not there" and "we could not look" are different claims.
            if (actual == eClipboardReadFailed)
                return Error(lineNo, L".expect " + what,
                             L"the clipboard could not be read - OpenClipboard failed "
                             L"after retries, so the marker's presence is unknown");

            Check(lineNo, what, Num(expected ? 1 : 0),
                  Num(actual == eClipboardPresent ? 1 : 0));
            return true;
        }

        // ---- .expect canundo / canredo 0|1 --------------------------------
        if (what == L"canundo" || what == L"canredo")
        {
            bool expected;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect " + what, err);

            // Through Storage rather than EditContext::CanUndo: COEditorView
            // inherits EditContext *protected* and re-exports only the
            // navigation/selection members (the `using` block at OEView.h),
            // so the context-level undo accessors are not reachable from here.
            // Storage is the same source EditContext reads (OEContext.h)
            // minus the IsLocked() term, which cannot be set on a worksheet this
            // harness just created and owns for the whole run.
            const bool actual = (what == L"canundo") ? m_doc->GetStorage().CanUndo()
                                                     : m_doc->GetStorage().CanRedo();

            Check(lineNo, what, Num(expected ? 1 : 0), Num(actual ? 1 : 0));
            return true;
        }

        // ---- .expect modified 0|1 -----------------------------------------
        if (what == L"modified")
        {
            bool expected;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect modified", err);

            const bool actual = m_doc->IsModified() ? true : false;

            Check(lineNo, L"modified", Num(expected ? 1 : 0), Num(actual ? 1 : 0));
            return true;
        }

        // ---- .expect mode stream|column -----------------------------------
        if (what == L"mode")
        {
            if (want != L"stream" && want != L"column")
                return Error(lineNo, L".expect mode",
                             L"expected 'stream' or 'column', got '" + want + L"'");

            // Reported through GetBlockMode rather than inferred from the shape
            // of the selection: ebtUndefined is a real third state
            // (OEHelpers.h) and folding it into "stream" would hide a view
            // whose block mode was never initialised.
            const EBlockMode bm = m_view->GetBlockMode();
            const std::wstring actual = (bm == OpenEditor::ebtColumn) ? L"column"
                                      : (bm == OpenEditor::ebtStream) ? L"stream"
                                      :                                 L"undefined";

            Check(lineNo, L"mode", want, actual);
            return true;
        }

        // ---- .expect overtype 0|1 -----------------------------------------
        if (what == L"overtype")
        {
            bool expected;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect overtype", err);

            // Read through the same static flag the production VK_INSERT handler
            // writes. An overtype assertion is about the editor's own idea of the
            // mode agreeing with what the feature under test saw, so caching the
            // value here would test nothing.
            const bool actual = COEditorView::IsOvertypeMode() ? true : false;

            Check(lineNo, L"overtype", Num(expected ? 1 : 0), Num(actual ? 1 : 0));
            return true;
        }

        // ---- .expect anchor|collapsed|hidden <line> 0|1 ----------------------
        // One question per assertion, each answered by the model or the mapper
        // rather than by a copy of either: `anchor` and `collapsed` are both model
        // questions now - the collapsed state lives in the model's own layer, not
        // in a flag on a region - and `hidden` is the mapper's. So a disagreement
        // between the region set and the visibility map (the C-class bug the
        // stage 2 self-test guards) still shows up as two FAILs pointing at both
        // sides.
        // Lines are 1-based to match every other directive in a script; the model
        // is 0-based buffer lines.
        if (what == L"anchor" || what == L"collapsed" || what == L"hidden")
        {
            std::wstring tail;
            const std::wstring head = Head(want, tail);
            int  line     = 0;
            bool expected = false;
            std::wstring err;

            if (!ParseInt(head, line, err))
                return Error(lineNo, L".expect " + what, err);

            if (line < 1)
                return Error(lineNo, L".expect " + what, L"line is 1-based, got " + head);

            if (!ParseBool(Trim(tail), expected, err))
                return Error(lineNo, L".expect " + what, err);

            const OpenEditor::FoldModel& folds = m_doc->GetFoldModel();
            const int  line0 = line - 1;
            bool actual;

            if (what == L"anchor")
                actual = folds.IsAnchorAt(line0);
            else if (what == L"collapsed")
                // A line that is not an anchor is not a collapsed anchor either -
                // reporting the model's own answer avoids inventing a second
                // meaning for a line inside a region body. IsCollapsedAt gives
                // that answer directly, where the old form had to find the region
                // first and then read a flag off it.
                actual = folds.IsCollapsedAt(line0);
            else
                actual = m_doc->GetLineMapper().IsLineHidden(line0);

            Check(lineNo, what + L" " + Num(line),
                  Num(expected ? 1 : 0), Num(actual ? 1 : 0));
            return true;
        }

        // ---- .expect region <i> <start>:<end>:<collapsed> --------------------
        // Pins the geometry of one region on one line: 1-based inclusive [start..end] plus the
        // flag. An off-by-one on endLine (a region that hides one line too many, the `end;` of its
        // own block) or a buffer-line index applied where a position was meant lands here as a
        // visible number, instead of only as a caret assertion three cases away. The expected side
        // is compared verbatim, so a malformed expectation FAILs showing both strings rather than
        // aborting the run.
        //
        // Indexes the joined enumeration FoldModel exposes: every range the model knows about, in
        // the order the snapshot lists them, orphans last. The third field is a separate model
        // question rather than a member read - it is the folded mark JoinedView pairs with that
        // enumeration entry.
        if (what == L"region")
        {
            std::wstring tail;
            const std::wstring head = Head(want, tail);
            int i = 0;
            std::wstring err;

            if (!ParseInt(head, i, err))
                return Error(lineNo, L".expect region", err);

            const OpenEditor::FoldModel& folds = m_doc->GetFoldModel();

            // ONE derivation for the three questions this directive asks - how
            // many entries exist, what entry i is, and whether it is folded.
            // Count(), Get(i) and the now-retired IsCollapsed(i) each rebuilt the
            // joined enumeration from the two layers, so asking all three paid for
            // it three times per assertion, and `.expect region` runs 129 times in
            // FOLDING.stp. Taking the view once also makes the index space
            // coherent by construction: the bounds test and the two reads cannot
            // disagree about which enumeration they are indexing. It is also what
            // left IsCollapsed(i) with no reader anywhere - the folded mark now
            // arrives alongside the entry instead of being asked for a third time.
            OpenEditor::FoldModel::Regions joined;
            std::vector<char>              marks;
            folds.JoinedView(joined, marks);

            // An out-of-range index is a script bug, not a finding: aborting names
            // it directly, where Check() could only report "0 != 3" against a region
            // that does not exist.
            if (i < 0 || i >= (int)joined.size())
                return Error(lineNo, L".expect region",
                             L"index " + head + L" out of range, Count()="
                             + Num((int)joined.size()));

            // References into `joined`, which outlives both reads. Get(i) had to
            // return by value because the enumeration it indexed was a temporary
            // built inside the call; holding the view is what makes them stable.
            const OpenEditor::FoldRegion& r      = joined.at((size_t)i);
            const bool                    folded = marks.at((size_t)i) != 0;

            Check(lineNo, L"region " + Num(i), Trim(tail),
                  Num(r.startLine + 1) + L":" + Num(r.endLine + 1) + L":"
                + Num(folded ? 1 : 0));
            return true;
        }

        // ---- .expect currentregion <L> <start>:<end>|none ---------------------
        // The innermost available region spanning line L - what the gutter tint paints.
        if (what == L"currentregion")
        {
            std::wstring tail;
            const std::wstring head = Head(want, tail);
            int line = 0;
            std::wstring err;

            if (!ParseInt(head, line, err))
                return Error(lineNo, L".expect currentregion", err);

            if (line < 1)
                return Error(lineNo, L".expect currentregion",
                             L"line is 1-based, got " + head);

            const OpenEditor::FoldRegion* r =
                m_doc->GetFoldModel().FindEnclosingAvailable(line - 1);

            const std::wstring actual = r
                ? Num(r->startLine + 1) + L":" + Num(r->endLine + 1)
                : std::wstring(L"none");

            Check(lineNo, L"currentregion " + Num(line), Trim(tail), actual);
            return true;
        }

        // ---- .expect regions|visrows|topmost <n> ------------------------------
        // The three whole-document numbers a folding case needs, each with a
        // different owner: `regions` is the model's Count(), `visrows` the
        // document's visible-line count (buffer lines minus the UNION of hidden
        // ranges - the value the scrollbar is sized from, so it catches a mapper
        // that summed per-region hidden counts instead of unioning them, the
        // mistake LineMapper::Rebuild's run merge exists to prevent: a parent
        // folded over an already folded child would hide the child's lines
        // twice), and `topmost` the view's first visible row, reported 1-based.
        if (what == L"regions" || what == L"visrows" || what == L"topmost")
        {
            int expected = 0;
            std::wstring err;

            if (!ParseInt(want, expected, err))
                return Error(lineNo, L".expect " + what, err);

            const int actual =
                  (what == L"regions") ? m_doc->GetFoldModel().Count()
                : (what == L"visrows") ? m_doc->GetVisibleLineCount()
                :                        m_view->GetTopmostLine() + 1;

            Check(lineNo, what, Num(expected), Num(actual));
            return true;
        }

        // ---- .expect rowof <line> <row> --------------------------------------
        // LineToRow is BufferToVisual (OEView.h): the 0-based visual row a
        // buffer line occupies, or the row of the collapsing marker when the line
        // is hidden. This is the assertion that a hidden line did not merely
        // report itself hidden but actually stopped occupying a row - the
        // navigation and reveal cases are meaningless without it. Rows stay
        // 0-based because that is what the rulers index with; lines are 1-based as
        // everywhere else in a script.
        if (what == L"rowof")
        {
            std::wstring tail;
            const std::wstring head = Head(want, tail);
            int line     = 0;
            int expected = 0;
            std::wstring err;

            if (!ParseInt(head, line, err))
                return Error(lineNo, L".expect rowof", err);

            if (line < 1)
                return Error(lineNo, L".expect rowof", L"line is 1-based, got " + head);

            if (!ParseInt(Trim(tail), expected, err))
                return Error(lineNo, L".expect rowof", err);

            Check(lineNo, L"rowof " + Num(line), Num(expected),
                  Num(m_view->LineToRow(line - 1)));
            return true;
        }

        // ---- .expect lexstate <line> <state>:<quoteId> -----------------------
        // The cross-line lexical state Paint seeds the highlighter with before it tokenizes a line:
        // the raw pair ScanMultilineQuotes reports for that buffer line, uninterpreted, so the
        // assertion cannot drift from what COEditorView::Paint hands to SetMultilineQuotesState.
        //
        // `state` is MultilineQuotesScanner::EQuoteState - 0 qsPlain, 1 qsMultiline,
        // 2 qsAlternative - and `quoteId` the positional slot (0 comment, 1 string, 2 char) that
        // SetMultilineQuotesState remaps onto the language's pairs; quoteId is meaningless for
        // qsAlternative. So 0:0 is code, 1:0 is inside a multi-line comment and 1:1 inside a string.
        //
        // The guard for the folded-comment highlight bug: Paint walks VISIBLE rows and calls
        // NextLine once per row, and NextLine resets the state only when it is NOT a multi-line
        // one, so a collapsed comment run hid its own */ and every row below the fold inherited
        // eComment. The fix re-seeds from storage at each fold boundary, correct only because
        // MultilineQuotesScanner walks STORAGE and so still sees the hidden */. Were that ever made
        // row-aware or visible-only, the re-seed would quietly seed the wrong state and the bug
        // would return with nothing else to catch it.
        //
        // Lines are 1-based as everywhere else in a script, the scanner is 0-based.
        if (what == L"lexstate")
        {
            std::wstring tail;
            const std::wstring head = Head(want, tail);
            int line = 0;
            std::wstring err;

            if (!ParseInt(head, line, err))
                return Error(lineNo, L".expect lexstate", err);

            if (line < 1)
                return Error(lineNo, L".expect lexstate", L"line is 1-based, got " + head);

            int state(0), quoteId(0);
            bool parsing(false);
            wchar_t qClose(0); // q-literal delimiter, see COEditorView::Paint
            // Through the document's Storage, which is the very instance the
            // view's EditContext holds as m_pStorage - so this reads the same
            // configured scanner COEditorView::Paint seeds from. COEditorView
            // inherits EditContext protected, so EditContext::ScanMultilineQuotes
            // is not reachable from here; Storage's own is public and identical.
            m_doc->GetStorage().ScanMultilineQuotes(line - 1, state, quoteId, parsing, qClose);

            Check(lineNo, L"lexstate " + Num(line), Trim(tail),
                  Num(state) + L":" + Num(quoteId));
            return true;
        }

        // ---- .expect pane total|visible|filtered <n> ---------------------------
        // The three counts `.paneoutline` prints, assertable individually. They come from the SAME
        // two reads the digest uses - GetLastFolds() for every fold candidate the walk reached and
        // GetLastPaneOutline() for the pane entries it allowed through, both products of one
        // extraction - so an expectation cannot disagree with the transcript by re-deriving
        // anything.
        //
        // `total` is the FOLD count and `visible` the PANE count, which is what keeps
        // `filtered = total - visible` meaningful: the difference is exactly the set the walk
        // decided the pane may not list.
        //
        // Needed because a transcript-only pane oracle proved insufficient: PlSqlOutlineMaxTopItems
        // was once inert (its cap test was hoisted above the loop that spends the budget) and a case
        // written to catch exactly that passed green - a budget of 2 and a budget of "off" produced
        // byte-identical digests. Only the two phases differing from each other showed it up.
        // `visible`/`filtered` pinned by name turn that into a failure with a number in it.
        if (what == L"pane")
        {
            std::wstring tail;
            const std::wstring which = Lower(Head(want, tail));
            int expected = 0;
            std::wstring err;

            if (!ParseInt(Trim(tail), expected, err))
                return Error(lineNo, L".expect pane", err);

            auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                               m_doc->GetStorage().GetLanguageSupport().get());

            if (!pPlSql)
                return Error(lineNo, L".expect pane", L"document has no PL/SQL support");

            const int total   = static_cast<int>(pPlSql->GetLastFolds().size());
            const int visible = static_cast<int>(pPlSql->GetLastPaneOutline().size());

            const int actual =
                  (which == L"total")    ? total
                : (which == L"visible")  ? visible
                : (which == L"filtered") ? total - visible
                : -1;

            if (actual < 0)
                return Error(lineNo, L".expect pane",
                             L"unknown measure '" + which
                           + L"', want total, visible or filtered");

            Check(lineNo, L"pane " + which, Num(expected), Num(actual));
            return true;
        }

        // ---- .expect quality|items|modelclean ---------------------------------
        // The loud oracles for the anti-thrash rule, reading the same three values
        // .outlinequality prints. `quality` is the one that matters most: a
        // transcript line can be re-blessed without being read, but a named
        // expectation fails as "expected oqSyntaxError got oqClean" and cannot be
        // blessed away. `items` pins that an unclean parse DISCARDED its own
        // snapshot rather than committing it - the count staying put across a
        // syntax error IS the rule, and `regions` alone would not show it, since
        // the fold model keeps its regions one step removed. `modelclean` reads
        // FoldModel::IsSnapshotClean at the far end of support -> document ->
        // model, so a regression in the `quality == oqClean` argument OEDocument.cpp
        // passes fails here instead of only in a geometry diff.
        if (what == L"quality" || what == L"items" || what == L"modelclean")
        {
            auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                               m_doc->GetStorage().GetLanguageSupport().get());

            if (!pPlSql)
                return Error(lineNo, L".expect " + what, L"document has no PL/SQL support");

            if (what == L"quality")
            {
                Check(lineNo, L"quality", want,
                      OutlineQualityName(pPlSql->GetLastOutlineQuality()));
                return true;
            }

            if (what == L"items")
            {
                int expected = 0;
                std::wstring err;

                if (!ParseInt(want, expected, err))
                    return Error(lineNo, L".expect items", err);

                Check(lineNo, L"items", Num(expected),
                      Num(static_cast<int>(pPlSql->GetLastFolds().size())));
                return true;
            }

            bool expected = false;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect modelclean", err);

            Check(lineNo, L"modelclean", Num(expected ? 1 : 0),
                  Num(m_doc->GetFoldModel().IsSnapshotClean() ? 1 : 0));
            return true;
        }

        // ---- .expect untrusted|pastcut|cutline --------------------------------
        // The loud oracles for `.outlinetrust`'s trust measurement, reading the same three values it
        // prints. They exist because the choice of folding trust rule rests on one measured
        // disagreement - the comment run in outline-trust-comment-run-beside-a-syntax-error, where
        // the subtree rule says trusted and the line rule says past the cut - and a disagreement
        // that lives only in a transcript line can be re-blessed without being read. `untrusted` and
        // `pastcut` pin the two counts, so the rules can neither silently converge (the
        // discriminating case stopped discriminating) nor silently diverge further (a new class of
        // range started disagreeing and nobody looked).
        if (what == L"untrusted" || what == L"pastcut" || what == L"cutline")
        {
            auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(
                               m_doc->GetStorage().GetLanguageSupport().get());

            if (!pPlSql)
                return Error(lineNo, L".expect " + what, L"document has no PL/SQL support");

            const int cutLine = pPlSql->GetParseCutLine();

            if (what == L"cutline")
            {
                // `-` is the printed spelling of "the parse reached the end of the
                // buffer". Accepted as an expectation too, so a clean phase is pinned
                // in the same terms the transcript reports it rather than in a numeric
                // form that never appears there.
                const std::wstring actual = (cutLine < 0) ? std::wstring(L"-")
                                                          : Num(cutLine + 1);

                if (want == L"-")
                {
                    Check(lineNo, L"cutline", want, actual);
                    return true;
                }

                int expected = 0;
                std::wstring err;

                if (!ParseInt(want, expected, err))
                    return Error(lineNo, L".expect cutline", err);

                Check(lineNo, L"cutline", Num(expected), actual);
                return true;
            }

            // The same walk `.outlinetrust` makes over the same COMMITTED fold
            // candidates, so the number asserted here is the number printed there by
            // construction and not a second opinion about it. Which of the two candidate
            // rules is being counted is the only difference between the branches.
            const bool countingUntrusted = (what == L"untrusted");
            int count = 0;

            for (const auto& it : pPlSql->GetLastFolds())
            {
                const bool flagged = countingUntrusted
                                         ? !it.rangeTrusted
                                         : (cutLine >= 0 && it.pos.end.line >= cutLine);
                if (flagged)
                    count++;
            }

            int expected = 0;
            std::wstring err;

            if (!ParseInt(want, expected, err))
                return Error(lineNo, L".expect " + what, err);

            Check(lineNo, what, Num(expected), Num(count));
            return true;
        }

        // ---- .expect unparsed|unparsedfrom|unparsedboundary ------------------------
        // The loud oracles for the syntax gutter's unparsed band, reading the same whole-buffer
        // LineStatusMap `.linestatus` prints through the same FillWholeBufferLineStatus call, so the
        // number asserted here is the number printed there by construction.
        //
        // They exist for the same reason the trust pair does: the band is a PAINT, and nothing else
        // in the product or in this suite observes it. Without these the band rests on a transcript
        // line, which can be re-blessed without being read - and a re-blessed baseline is exactly
        // how a gutter that silently stops marking a truncated tail survives into a release.
        //
        // `unparsed` pins the extent, `unparsedfrom` the boundary, `unparsedboundary` the marker.
        // All three, because they fail in different ways: a marking that starts one line early or
        // late moves `from` and leaves the count alone; one that stops short moves the count and
        // leaves `from` alone; one that stamps the marker on every banded row instead of only the
        // first leaves both and moves `boundary` past `from`.
        if (what == L"unparsed" || what == L"unparsedfrom" || what == L"unparsedboundary")
        {
            OpenEditor::LineStatusMap map;
            int lines = 0;
            auto* pPlSql = FillWholeBufferLineStatus(lineNo, L".expect " + what, map, lines);

            if (!pPlSql)
                return Error(lineNo, L".expect " + what, L"document has no PL/SQL support");

            int count = 0;
            int first = -1;
            int mark  = -1;

            // The same scan DoLineStatus makes, over the same map, so `unparsed` here
            // and `unparsed=` there cannot disagree.
            for (int i = 0; i < lines; i++)
            {
                if (map[i].unparsed)
                {
                    if (first < 0)
                        first = i;

                    count++;
                }

                if (map[i].unparsedBoundary)
                    mark = i;
            }

            // `unparsed` is a COUNT over lines; the other two are LINE NUMBERS and are
            // rendered 1-based with `-` standing for "there is none", the spelling
            // `.linestatus` prints - so a clean phase is pinned in the terms the
            // transcript reports it rather than in a numeric form that never appears
            // there. A count of `-` is rejected rather than read as 0, because `0` is
            // the honest spelling of "no unparsed lines" and letting `-` mean the same
            // thing would give a truncated-tail expectation two ways to pass.
            const bool isCount = (what == L"unparsed");
            const int  actual  = isCount ? count : (what == L"unparsedfrom" ? first : mark);

            const std::wstring rendered = (isCount || actual >= 0)
                                              ? Num(isCount ? actual : actual + 1)
                                              : std::wstring(L"-");

            if (want == L"-")
            {
                if (isCount)
                    return Error(lineNo, L".expect " + what,
                                 L"a count cannot be '-', write 0 for no unparsed tail");

                Check(lineNo, what, want, rendered);
                return true;
            }

            int expected = 0;
            std::wstring err;

            if (!ParseInt(want, expected, err))
                return Error(lineNo, L".expect " + what, err);

            Check(lineNo, what, Num(expected), rendered);
            return true;
        }

        // ---- .expect supportkind|supportsame ----------------------------------
        // The two identity oracles; see the directive documentation at the top of this
        // file for the rule they pin.
        if (what == L"supportkind" || what == L"supportsame")
        {
            auto* pSupport = m_doc->GetStorage().GetLanguageSupport().get();

            if (!pSupport)
                return Error(lineNo, L".expect " + what,
                             L"document has no language support");

            if (what == L"supportkind")
            {
                const std::wstring actual =
                      (pSupport->GetKind() == OpenEditor::lskPlSql)
                          ? std::wstring(L"plsql")
                          : std::wstring(L"common");

                Check(lineNo, L"supportkind", Lower(Trim(want)), actual);
                return true;
            }

            // The derived type, for the id. The comparison is against a recorded ID and
            // not against a remembered POINTER, and that is load-bearing rather than
            // stylistic: an address can be reused by the next allocation, so a pointer
            // compared across a rebuild is free to report "same object" for two
            // different ones - precisely the false pass this check exists to prevent.
            // GetInstanceId comes from a monotonic counter, so it cannot. Being an int
            // also lets the mark outlive an interval in which the document had no
            // PlSqlSupport at all (a kind change to common and back), which is how a
            // script asserts that the replacement DID happen.
            auto* pPlSql = dynamic_cast<OpenEditor::PlSqlSupport*>(pSupport);

            if (!pPlSql)
                return Error(lineNo, L".expect supportsame",
                             L"document has no PL/SQL support");

            if (!m_markedSupportId)
                return Error(lineNo, L".expect supportsame", L"no preceding .supportmark");

            bool expected = false;
            std::wstring err;

            if (!ParseBool(want, expected, err))
                return Error(lineNo, L".expect supportsame", err);

            Check(lineNo, L"supportsame", Num(expected ? 1 : 0),
                  Num(pPlSql->GetInstanceId() == m_markedSupportId ? 1 : 0));
            return true;
        }

        return Error(lineNo, L".expect", L"unknown check '" + what + L"'");
    }

}   // namespace


// =============================================================================
//  Handler entry point
// =============================================================================

bool EditorScriptHandler::Execute(const std::wstring& payload, std::wstring& out)
{
    out.clear();

    if (Trim(payload).empty())
    {
        out = L"ERROR empty script: TestEditorScript was called with no payload\n"
              L"RESULT checks=0 pass=0 fail=1\n";
        return false;
    }

    CSQLToolsApp* pApp = (CSQLToolsApp*)AfxGetApp();
    ASSERT(pApp);

    // One isolated worksheet per run, created the same way SqlFormatterHandler
    // and the production "run PL/SQL block" path create it: OpenDocumentFile(NULL),
    // then title/format, then drive the view. Nothing here touches the developer's
    // own open worksheets.
    CPLSWorksheetDoc* pDoc    = 0;
    COEditorView*     pEditor = 0;

    POSITION pos = pApp->GetFirstDocTemplatePosition();

    if (CDocTemplate* pDocTemplate = pApp->GetNextDocTemplate(pos))
    {
        pDoc = (CPLSWorksheetDoc*)pDocTemplate->OpenDocumentFile(NULL);

        if (pDoc)
        {
            ASSERT_KINDOF(CPLSWorksheetDoc, pDoc);
            pEditor = pDoc->GetEditorView();
        }
    }

    if (!pDoc || !pEditor || !pEditor->GetSafeHwnd())
    {
        // Close whatever did open, so a failed run cannot leave a stray
        // worksheet tab behind in the live session.
        if (pDoc)
        {
            pDoc->SetModifiedFlag(FALSE);

            // CDocument::OnCloseDocument is protected, so the frame has to be
            // reached through a view. GetEditorView() returning null is the only
            // reason to fall back on the document's own view list, and an empty
            // list means there is no frame to close at all - leaving the untitled
            // worksheet open is then the least harmful outcome.
            CFrameWnd* frame = 0;

            if (pEditor)
                frame = pEditor->GetParentFrame();
            else if (POSITION vpos = pDoc->GetFirstViewPosition())
                if (CView* v = pDoc->GetNextView(vpos))
                    frame = v->GetParentFrame();

            if (frame)
                frame->SendMessage(WM_CLOSE);
        }

        out += L"ERROR could not open a worksheet editor for the step-script harness\n"
               L"RESULT checks=0 pass=0 fail=1\n";
        return false;
    }

    pDoc->SetTitle(L"SQLTools Automated Testing - Editor Script");
    pDoc->DefaultFileFormat();

    int checks = 0, pass = 0, fail = 0;

    {
        // Scoped so ~Runner - and therefore RestoreSettings - runs while the
        // document and its view are still alive. Restoring notifies
        // GlobalSettings, which walks down through Settings and Storage to every
        // registered COEditorView; doing that after WM_CLOSE would notify a
        // torn-down view, and never doing it at all would leak the script's pins
        // into the developer's live session.
        Runner runner(pDoc, pEditor, out);

        runner.Run(SplitLines(payload));

        checks = runner.Checks();
        pass   = runner.Passes();
        fail   = runner.Fails();
    }

    out += L"RESULT checks=" + Num(checks)
         + L" pass="         + Num(pass)
         + L" fail="         + Num(fail)
         + L"\n";

    pDoc->SetModifiedFlag(FALSE);
    pEditor->GetParentFrame()->SendMessage(WM_CLOSE);

    // The transcript is the real contract: MainFrm.cpp discards this return
    // value and only the RESULT line reaches PowerShell. It is still returned
    // honestly so an in-process caller can tell a clean run from a failed one.
    return fail == 0;
}
