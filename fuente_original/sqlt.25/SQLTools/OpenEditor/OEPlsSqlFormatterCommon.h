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
#pragma once

// ============================================================================
// OEPlsSqlFormatterCommon — shared low-level utilities for the PL/SQL
// formatter emitter (OEPlsSqlFormatterBlocks.cpp and future split-out
// emitter files — see md/plsql_formatter_decoupling_plan.md).
//
// Currently holds FormatWriter (see md/plsql_formatter_trace_writer_proposal.md):
// a thin wrapper around the output std::wstring accumulator that centralizes
// blank-line collapsing, indent-level bookkeeping, and gives a single named
// place (WriteToken/WriteText) to set a breakpoint on when tracing an
// emission bug back to its call stack.
//
// This header/cpp pair is intentionally minimal for now — it is the first
// piece extracted per the decoupling plan's Part 1 file split. More
// generic token-stream helpers (noSpaceBeforeToken, skipEol, indentText,
// etc.) will move here incrementally as call sites are migrated to
// FormatWriter; they are not moved wholesale up front to keep each change
// reviewable and buildable in small steps.
// ============================================================================

#include "OpenEditor/OEPlsSqlFormatter.h"

namespace OpenEditor {

// Prefixes every non-empty line of text with indent.
std::wstring indentText(const std::wstring& text, int indentSize);

#ifdef _DEBUG
// ----------------------------------------------------------------------------
// Layout-invariant checks (debug builds only).
//
// EmitRangeGuard proves an emitter wrote the right *tokens*. It deliberately
// strips whitespace before comparing (stripWhitespace/expectedRangeText), so it
// is structurally blind to "right tokens, wrong line". Two bugs of exactly that
// shape reached the baselines before these checks existed:
//
//   L1 — a script '/' terminator emitted on the header line instead of on a
//        line of its own;
//   L2 — a '/'-terminated unit glued to the comment that follows it, with no
//        blank line in between (commit 2bc0661a).
//
// Neither dropped, duplicated nor reordered a token, so no fidelity guard could
// observe either one. L1 is enforced inline in FormatWriter::WriteToken (it
// needs the token type, which only exists at emission time); L2 is a property
// of the finished buffer and is checked whole-buffer by
// checkScriptSlashLayout() next to verifyOperatorTokenization() in OEView_7.cpp.
//
// Deliberately NOT checked here: total line-break count over a span. The
// formatter reflows lines freely, so line structure is not an invariant it
// preserves — expectedRangeText() skips etEOL for exactly that reason. Only
// the two rules above are guaranteed by every emitter.
//
// reportLayoutViolation() is declared this early because WriteToken needs it at
// its point of definition. Routing mirrors EmitRangeGuard: when an automated
// test is running the report goes to the diagnostics sink, because _ASSERTE
// would pop the modal CRT dialog and deadlock the COM caller.
// ----------------------------------------------------------------------------
void reportLayoutViolation(const wchar_t* what, const std::wstring& detail);
#endif

// ----------------------------------------------------------------------------
// FormatWriter — traceable output accumulator for the PL/SQL emitter.
//
// Replaces the historical pattern of threading a bare `std::wstring& out`
// through every emitXxx() function and re-implementing blank-line
// collapsing inline at each call site (see md/plsql_formatter_trace_writer_proposal.md
// for the motivating bug and full design discussion).
//
// Token Fidelity Rule reinforcement: WriteToken() is the only sanctioned way
// to emit a keyword/punctuation character sourced from tokens[idx].value.
// WriteText() is for already-composed text (a joined flat statement, or the
// output of a recursive formatSQL()/formatPlsql() call) — kept distinct from
// WriteToken() so a reviewer/breakpoint can tell "raw token" apart from
// "composed text" emission. WriteIndent()/WriteSpaces()/NewLine()/
// EmptyLine() only ever append structural whitespace, never keyword or
// punctuation characters.
//
// Debugging workflow this enables: set a conditional breakpoint inside
// WriteToken() (e.g. on tok.value == L"TYPE", or on m_lastToken matching
// some condition) and get the exact call stack of whichever emitXxx()
// function produced a given piece of output, instead of manually bisecting
// emitStatementSequence()'s branches.

// ---------------------------------------------------------------------------
// E1 — IFormatSink: the pluggable output sink behind FormatWriter
// (md/sql_plsql_formatter_integration_plan.md, Phase E).
//
// FormatWriter used to own a bound `std::wstring&`. That works while every
// emitter writes text, but it forecloses "one writer for both formatters":
// the SQL formatter's output is a *token* buffer, because five retroactive
// alignment passes address already-emitted tokens by index and one of them
// inserts tokens (plan §E0). No std::wstring can express that, so the
// abstraction has to go the other way — FormatWriter becomes the front end
// and the buffer becomes a sink behind it.
//
// The interface is deliberately tiny: append text, append a token, and two
// read-only lookbehind primitives. Everything FormatWriter's layout checks
// and inspection helpers need is expressible over Length()/At(), so a sink
// never has to expose its internals.
//
// The contract is append-only, and every pre-E1 FormatWriter emission
// already satisfied it: WriteIndent(level) only ever appended spaces (it is
// now a one-line wrapper over WriteSpaces()), and WriteIndentText() appends
// an indented *copy* produced by indentText() rather than rewriting the
// buffer in place. So nothing had to be dropped to adopt the sink, and no
// emitter's output changes.
// ---------------------------------------------------------------------------
class IFormatSink
{
public:
	virtual ~IFormatSink() {}

	// Appends the n characters starting at p.
	virtual void AppendText(const wchar_t* p, size_t n) = 0;

	// Appends one already-formed token. A text sink appends tk.value; a
	// token-backed sink records the token itself, so the retroactive passes
	// can still see per-token boundaries.
	virtual void AppendToken(const OpenEditor::Token& tk) = 0;

	// Lookbehind support for the layout checks and inspection helpers.
	// At(i) is only ever called with i < Length().
	virtual size_t  Length() const = 0;
	virtual wchar_t At(size_t i) const = 0;

	// Whole-buffer view, for the callers that are not sink-agnostic yet:
	// EMIT_RANGE_GUARD's before-snapshot and emitParamList()'s
	// continuation-line column probe. Must stay consistent with Length()/At(),
	// and EmitRangeGuard holds it *by reference* across the guarded scope, so
	// a sink must append into the very string Str() returns.
	virtual const std::wstring& Str() const = 0;

	// Discards everything appended so far. Needed because FormatWriter::Clear()
	// is used to recycle the scratch writer in emitParamList().
	virtual void Clear() = 0;

	// Retracts the last `count` characters appended, reporting whether this
	// sink can retract at all. Deliberately NOT part of the append-only
	// contract above: it exists for one caller, the end-of-line-comment join
	// (FormatWriter::ResumeLineForEndComment), which has to undo the single
	// line break the preceding item emitted so the author's comment can stay
	// on the line it annotates. The default refuses, so a sink that cannot
	// retract (TokenStreamSink — its tokens are addressed by index by the
	// retroactive alignment passes) needs no change and its caller falls back
	// to the own-line behaviour.
	virtual bool Retract(size_t count) { (void)count; return false; }
};

// ---------------------------------------------------------------------------
// StringSink — the default sink, and byte-for-byte the pre-E1 behaviour:
// append to a caller-owned std::wstring. Every existing FormatWriter call
// site gets one of these implicitly, through FormatWriter's std::wstring&
// constructor, so E1 changes no emitter.
// ---------------------------------------------------------------------------
class StringSink : public IFormatSink
{
public:
	explicit StringSink(std::wstring& out) : m_out(out) {}

	void AppendText(const wchar_t* p, size_t n) override { m_out.append(p, n); }
	void AppendToken(const OpenEditor::Token& tk) override { m_out += tk.value; }
	size_t  Length() const override { return m_out.size(); }
	wchar_t At(size_t i) const override { return m_out.at(i); }
	const std::wstring& Str() const override { return m_out; }
	void Clear() override { m_out.clear(); }
	bool Retract(size_t count) override
	{
		if (count > m_out.size())
			return false;
		m_out.resize(m_out.size() - count);
		return true;
	}

private:
	std::wstring& m_out;
};

// ----------------------------------------------------------------------------
class FormatWriter
{
public:
	explicit FormatWriter(std::wstring& out, int spacesPerIndentLevel = 2,
						  bool layoutChecks = true)
		: m_sinkScratch(), m_strSink(out), m_sink(m_strSink),
		  m_spacesPerLevel(spacesPerIndentLevel)
	{
#ifdef _DEBUG
		m_layoutChecks = layoutChecks;
#else
		(void)layoutChecks;
#endif
	}

	// E1 — writes to a caller-supplied sink (e.g. the SQL formatter's
	// token-backed sink). m_strSink must be constructed before m_sink, so it
	// is parked on the unused m_sinkScratch; nothing ever appends there.
	explicit FormatWriter(IFormatSink& sink, int spacesPerIndentLevel = 2,
	                      bool layoutChecks = true)
		: m_sinkScratch(), m_strSink(m_sinkScratch), m_sink(sink),
		  m_spacesPerLevel(spacesPerIndentLevel)
	{
#ifdef _DEBUG
		m_layoutChecks = layoutChecks;
#else
		(void)layoutChecks;
#endif
	}

	// ---- Token / text emission ----

	// Emits a token's exact source text verbatim (Token Fidelity Rule: the
	// only sanctioned way to emit a keyword/punctuation character). Kept as
	// a distinct method (rather than folding into WriteText) so a
	// conditional breakpoint here can inspect tok.token/tok.value/tok.offset
	// and the call stack shows exactly which emitXxx() wrote it.
	void WriteToken(const OpenEditor::Token& tok)
	{
#ifdef _DEBUG
		// Layout invariant L1 — a script '/' terminator stands alone on its own
		// line. The lexer relies on the same invariant when it reclassifies
		// etSLASH to etEOS, and every emitter that writes one does so after
		// NewLine()+WriteCurrentIndent(). Writing it mid-line instead drops no
		// token, so EmitRangeGuard cannot see it; this catches it in the exact
		// frame responsible, with a stack trace.
		//
		// classifyTerminator() is the single canonical "is this a script slash"
		// test — do not re-inline `etEOS && value == L"/"` here. The scan is
		// gated on that test, so ordinary tokens pay nothing.
		if (m_layoutChecks
		&&  classifyTerminator(tok) == FormatContext::TermSlash
		&&  !AtLineStart())
			reportLayoutViolation(
				L"script '/' terminator written mid-line",
				L"it must stand alone on its own line; output tail was '"
					+ TailForReport() + L"'");
#endif
		m_lastToken = &tok;
		m_sink.AppendToken(tok);
	}

	// Emits already-composed text that is not a single token's raw value
	// (e.g. joinTokensSingleSpaced() output, or the result of a recursive
	// formatSQL()/formatPlsql() call already re-indented by the caller).
	void WriteText(const std::wstring& text)
	{
		m_sink.AppendText(text.c_str(), text.size());
	}

	void WriteIndentText(const std::wstring& text, int indentSize)
	{
		// indentText() returns a copy, so this stays a pure append.
		const std::wstring indented = indentText(text, indentSize);
		m_sink.AppendText(indented.c_str(), indented.size());
	}

	// ---- E2b: splicing a nested formatter's token run ----
	//
	// The token-run replacement for WriteIndentText(formatSQL(...), indentSize)
	// at the DML delegation sites. `run` is a nested formatter's output buffer
	// (formatSQL()'s return value); every non-empty line of it is prefixed with
	// indentSize spaces and the result is appended here.
	//
	// FIDELITY. The emitted characters are identical to
	// WriteIndentText(concatTokenValues(run), indentSize) by construction, not
	// by measurement: this walks the same segment boundaries indentText() does —
	// split on '\n', drop a trailing '\r' from each line, prefix a line only
	// when it has content — so a blank line stays blank instead of acquiring a
	// trailing indent, and a run ending in '\n' keeps that newline. What
	// changes is the token *shape*: the old path handed the sink one flat
	// string, which a token-backed sink re-split into etWHITESPACE/etEOL/
	// etUNKNOWN runs; this one appends the nested formatter's own tokens, so
	// their real types (etSELECT, etQUOTED_STRING, etCOMMENT, …) and therefore
	// their boundaries survive the splice. That is the point of E2b — see
	// md/sql_plsql_formatter_integration_plan.md §E2b.
	//
	// Three emission forms, one per token shape:
	//   - a token whose value has no '\n' is appended untouched via
	//     AppendToken(), with its leading indent written separately through
	//     WriteSpaces() like any other line start;
	//   - a LAYOUT token whose value spans lines (a multi-line block comment,
	//     or an etEOL/etWHITESPACE run) cannot have its *interior* line starts
	//     prefixed without splitting it, so its value is rebuilt with the
	//     prefix injected. This is the same whitespace-only mutation
	//     realign_comment()/align_comment() already perform inside a comment
	//     token, and it keeps the comment one token instead of the shard-run
	//     the string path produced;
	//   - a DATA token whose value spans lines (a string literal — '…',
	//     q'[…]', or a "…" quoted identifier) is appended VERBATIM. Its
	//     interior line starts are part of the literal's value, so prefixing
	//     them would silently rewrite the program's data: that is exactly the
	//     defect that turned json_table(cast, '\n$[*]' …) into
	//     '\n    $[*]' and grew a 7-char token to 11 chars, failing the
	//     losslessness check (corpus cases 1356 / 1374). Only the leading
	//     indent is added, and it goes out through WriteSpaces() — outside the
	//     value — so the literal itself survives byte-for-byte. Note this is a
	//     deliberate deviation from indentText(), which prefixed every
	//     non-empty line of the flattened text and therefore corrupted any
	//     multi-line literal in a delegated run.
	//
	// m_lastToken is deliberately not updated: spliced tokens are either
	// borrowed from a caller-owned run whose lifetime is shorter than the
	// token vector LastToken() otherwise points into, or rebuilt copies that
	// would dangle outright. LastToken() keeps naming the last token written
	// directly through WriteToken().
	void SpliceInto(const std::vector<OpenEditor::Token>& run, int indentSize)
	{
		if (run.empty()) return;

		const int indentChars = indentSize > 0 ? indentSize : 0;
		const std::wstring indent((size_t)indentChars, L' ');
		bool atLineStart = true;

		for (size_t ti = 0; ti < run.size(); ++ti)
		{
			const OpenEditor::Token& tk = run.at(ti);
			const std::wstring& v = tk.value;
			if (v.empty()) continue; // zero-length sentinel tokens contribute nothing

			const size_t firstNl = v.find(L'\n');

			if (firstNl == std::wstring::npos)
			{
				// Single-line token: one whole line segment.
				size_t len = v.size();
				if (v.at(len - 1) == L'\r') --len; // indentText() drops a trailing CR
				if (len == 0) continue;         // a lone '\r' is an empty line

				if (atLineStart)
				{
					WriteSpaces(indentChars);
					atLineStart = false;
				}
				if (len == v.size())
					m_sink.AppendToken(tk); // type and value preserved verbatim
				else
					WriteText(v.substr(0, len));
				continue;
			}

			// Multi-line token. Its own leading indent goes out through
			// WriteSpaces() so every line start is emitted the same way; only
			// the interior line starts of a LAYOUT token are folded into the
			// rebuilt value — a DATA token's interior is its own payload.
			if (atLineStart)
			{
				// firstNl may be 0 (the token opens with a line break), so the
				// CR probe has to be guarded rather than indexing v[-1].
				size_t firstLen = firstNl;
				if (firstLen > 0 && v.at(firstLen - 1) == L'\r') --firstLen;
				if (firstLen > 0)
				{
					WriteSpaces(indentChars);
					atLineStart = false;
				}
			}

			// Whose whitespace is this? A comment's or a whitespace/EOL run's
			// interior is layout, so re-indenting it only moves text around. A
			// string literal's or quoted identifier's interior is data: adding
			// the splice indent there changes the value the program will see,
			// so such a token goes out exactly as the nested formatter
			// produced it (Token Fidelity Rule).
			const bool interiorIsLayout =
				   tk.token == etCOMMENT
				|| tk.token == etEOL
				|| tk.token == etWHITESPACE;

			if (!interiorIsLayout)
			{
				m_sink.AppendToken(tk); // value preserved verbatim
				// The line-start bookkeeping still has to happen: the writer
				// sits at a fresh line start only when the value ended on a
				// newline (a lone trailing CR belongs to that newline).
				const size_t lastNl = v.rfind(L'\n'); // exists: firstNl != npos
				size_t lastLen      = v.size() - (lastNl + 1);
				if (lastLen > 0 && v.at(v.size() - 1) == L'\r') --lastLen;
				atLineStart = (lastLen == 0);
				continue;
			}

			OpenEditor::Token rebuiltTk = tk;
			std::wstring& rebuilt = rebuiltTk.value;
			rebuilt.clear();

			size_t pos = 0;
			bool firstSeg = true;
			for (;;)
			{
				const size_t nl = v.find(L'\n', pos);
				const size_t stop = (nl == std::wstring::npos) ? v.size() : nl;
				size_t len = stop - pos;
				if (len > 0 && v.at(pos + len - 1) == L'\r') --len;

				if (!firstSeg)
				{
					rebuilt += L'\n';
					if (len > 0) rebuilt += indent; // interior line start
				}
				firstSeg = false;
				rebuilt.append(v, pos, len);

				if (nl == std::wstring::npos)
				{
					atLineStart = (len == 0); // ended on a newline => fresh line
					break;
				}
				pos = nl + 1;
			}

			rebuiltTk.length = static_cast<OpenEditor::Token::size_pos>(rebuilt.size());
			m_sink.AppendToken(rebuiltTk);
		}
	}

	// Emits `level` indent levels' worth of spaces at the writer's
	// configured spacing (see SetIndentSpacing/PushIndent/PopIndent below
	// for the stack-based alternative).
	void WriteIndent(int level)
	{
		if (level > 0)
			WriteSpaces(level * m_spacesPerLevel);
	}

	// Emits the current indent (see PushIndent/PopIndent/CurrentIndentLevel/
	// SetIndentChars). Backed by m_indentChars (absolute character count),
	// not m_indentLevel directly — SetIndentLevel()/PushIndent()/PopIndent()
	// keep m_indentChars in sync for the normal level-based case, while
	// SetIndentChars() lets a caller (e.g. AlignParen mode) set an arbitrary
	// absolute column that isn't a clean multiple of m_spacesPerLevel.
	void WriteCurrentIndent()
	{
		WriteSpaces(m_indentChars);
	}

	// WriteCurrentIndent() for an emitter that may be entered mid-line, i.e.
	// when the caller has already put content on the current output line and
	// only the rest of it belongs to the emitter. A second indent there would
	// land in the middle of the line; skipping it is what lets
	// emitVarDeclGroup() hand a declaration's default expression over after
	// writing the aligned "name TYPE :=" prefix. At line start — every
	// statement-level and SQL-side caller — this is exactly
	// WriteCurrentIndent(), so nothing about their output changes.
	void WriteCurrentIndentIfLineStart()
	{
		if (AtLineStart())
			WriteCurrentIndent();
	}

	// Begins a wrapping emitter's first line and reports the column its first
	// token will occupy: the indent is written and m_indentChars returned when
	// the line is fresh, or the real buffer column when the caller already owns
	// the line's head (see WriteCurrentIndentIfLineStart). The emitters' greedy
	// width packing measures against that column, so a declaration default
	// wrapped after a wide aligned prefix is packed to the same right margin as
	// the same expression in a statement.
	int BeginWrappedLine()
	{
		if (!AtLineStart())
			return (int)CurrentLineWidth();
		WriteCurrentIndent();
		return m_indentChars;
	}

	// Emits the current indent plus one extra level's worth of spaces (or
	// `extraChars` spaces if given explicitly). This is the continuation-line
	// counterpart to WriteCurrentIndent() — the single replacement for the
	// old pattern of threading a separately-computed `contIndent` string
	// through emitConcatAwareStatement()/emitMathBoolAwareStatement(): those
	// continuation lines are always "current indent + 1 level", regardless of
	// whether the current indent is a normal level-based indent or an
	// AlignParen-style absolute column set via SetIndentChars().
	void WriteContinuationIndent(int extraChars = -1)
	{
		WriteSpaces(m_indentChars + (extraChars >= 0 ? extraChars : m_spacesPerLevel));
	}

	// Sinks take a pointer+count rather than a run length, so the spaces are
	// materialised here. The stack buffer covers every realistic indent; the
	// wstring fallback is only reached by pathologically deep nesting.
	void WriteSpaces(int count)
	{
		if (count <= 0) return;
		wchar_t buf[64];
		if ((size_t)count <= sizeof(buf) / sizeof(buf[0]))
		{
			for (int i = 0; i < count; ++i) buf[i] = L' ';
			m_sink.AppendText(buf, (size_t)count);
		}
		else
		{
			const std::wstring s((size_t)count, L' ');
			m_sink.AppendText(s.c_str(), s.size());
		}
	}

	// Ends the current line. Does not collapse/deduplicate — callers that
	// want "exactly one blank line, never two" must use EmptyLine().
	void NewLine()
	{
		m_sink.AppendText(L"\n", 1);
	}

	// Forces exactly one blank line before further content, collapsing so a
	// double blank line ("\n\n\n") never results from repeated calls. This
	// is the single replacement for every hand-rolled
	// `out.compare(out.size()-2, 2, L"\n\n") != 0` check previously
	// duplicated across emitDeclareSection()/emitStatementSequence().
	void EmptyLine()
	{
		if (EndsWithBlankLine())
			return;
		m_sink.AppendText(L"\n", 1);
	}

	// ---- Indent-level stack ----
	// Optional convenience so callers don't have to thread indentLevel
	// through every helper by hand. Existing code that still computes
	// `indentLevel * s.GetIndentSpacing()` locally continues to work
	// unchanged; new/migrated code can use PushIndent/PopIndent instead.
	void PushIndent() { ++m_indentLevel; m_indentChars = m_indentLevel * m_spacesPerLevel; }
	void PopIndent() { if (m_indentLevel > 0) --m_indentLevel; m_indentChars = m_indentLevel * m_spacesPerLevel; }
	int CurrentIndentLevel() const { return m_indentLevel; }
	void SetIndentLevel(int level) { m_indentLevel = level; m_indentChars = level * m_spacesPerLevel; }

	// Sets the current indent to an absolute character column, independent of
	// m_spacesPerLevel/m_indentLevel. Used by AlignParen-style callers that
	// align continuation lines to an arbitrary source column (e.g. right
	// after an opening '(') rather than to an indent-level multiple.
	// m_indentLevel is intentionally left unchanged (it no longer matches
	// m_indentChars in this mode); WriteCurrentIndent()/WriteContinuationIndent()
	// only ever consult m_indentChars, so this is safe.
	void SetIndentChars(int chars) { m_indentChars = chars; }
	int CurrentIndentChars() const { return m_indentChars; }
	int GetNextIndentChars() const { return m_indentChars + m_spacesPerLevel; }

	void SetIndentSpacing(int spacesPerLevel) { m_spacesPerLevel = spacesPerLevel; }
	int GetIndentSpacing() const { return m_spacesPerLevel; }

	// ---- Read-only lookbehind (migration helpers) ----
	// Escape hatches for call sites that still need to inspect the tail of
	// the buffer directly during the incremental migration. New code should
	// prefer EmptyLine()/EndsWithSemicolon() over hand-rolled equivalents.
	bool EndsWithBlankLine() const
	{
		const size_t n = m_sink.Length();
		return n >= 2 && m_sink.At(n - 1) == L'\n' && m_sink.At(n - 2) == L'\n';
	}

	// Mirrors emitStatementSequence()'s prevEndsWithSemicolon(): skips
	// trailing whitespace and checks for a trailing ';'.
	bool EndsWithSemicolon() const
	{
		size_t i = m_sink.Length();
		while (i > 0 && (m_sink.At(i - 1) == L'\n' || m_sink.At(i - 1) == L' ' || m_sink.At(i - 1) == L'\t' || m_sink.At(i - 1) == L'\r'))
			--i;
		return i > 0 && m_sink.At(i - 1) == L';';
	}

	// True when nothing but spaces/tabs has been written since the last line
	// break (or since the start of the buffer) — i.e. the next token begins a
	// fresh output line. Debug-only layout invariant L1 (see WriteToken).
	bool AtLineStart() const
	{
		size_t i = m_sink.Length();
		while (i > 0 && (m_sink.At(i - 1) == L' ' || m_sink.At(i - 1) == L'\t'))
			--i;
		return i == 0 || m_sink.At(i - 1) == L'\n';
	}

	// ---- End-of-line comment support ----
	// (md/plsql_formatter_endline_comments_proposal.md)

	// Visual width of the current output line: characters appended since the
	// last line break (or since the start of the buffer). Same backward scan
	// over Length()/At() as AtLineStart() above.
	//
	// The end-of-line-comment width veto measures this, not
	// FormatContext::formattedLen: formattedLen estimates a *tree node*, which
	// is neither the emitted line (a node may have been wrapped onto several
	// lines, or spliced together with its neighbours) nor a character count
	// (it is a width estimate). Measuring the real buffer is what keeps the
	// veto honest — proposal Q3.
	size_t CurrentLineWidth() const
	{
		size_t i = m_sink.Length();
		while (i > 0 && m_sink.At(i - 1) != L'\n')
			--i;
		return m_sink.Length() - i;
	}

	// Resumes the current output line so the next write continues it instead
	// of starting a new one. This is the mechanism behind keeping an author's
	// end-of-line comment on the line it annotates, rather than promoting it
	// to a line of its own where it silently reads as describing the *next*
	// item.
	//
	// Two states are resumable:
	//   - the line is still open (a spliced DML run or a flat-token fallback
	//     ended without a break) — nothing to undo;
	//   - the preceding item closed it with NewLine() — that one '\n' is
	//     retracted through IFormatSink::Retract(). This is a local undo of
	//     the break written one loop iteration earlier by the same function
	//     that is now asking for it, not a post-pass over the finished output
	//     buffer (which the proposal rules out).
	//
	// `reserve` is the character count the caller is about to add to that line
	// (separator plus comment); `maxCol` is the width veto, where <= 0 means
	// unlimited. Returns false — leaving the output untouched, so the caller
	// can fall back to emitting the comment on its own line — when the line
	// cannot host a trailing comment: nothing emitted yet, the last line is
	// blank or carries only indentation, it is a lone script '/' (layout
	// invariant L1 gives that line to itself), the reserve does not fit inside
	// maxCol, or the sink cannot retract.
	bool ResumeLineForEndComment(size_t reserve, int maxCol)
	{
		// Locate the line the comment would be appended to: [lineStart,lineEnd).
		auto blank = [this](size_t k)
		{
			const wchar_t c = m_sink.At(k);
			return c == L' ' || c == L'\t' || c == L'\r';
		};

		const size_t n = m_sink.Length();
		size_t lineEnd = n;
		bool   retract = false;

		if (AtLineStart())
		{
			// The preceding item already closed its line — the normal case,
			// since every statement arm ends with NewLine(). That break has to
			// be undone, one character, through the sink; a sink that cannot
			// retract refuses at the bottom of this function and the caller
			// falls back to emitting the comment on a line of its own.
			if (n == 0 || m_sink.At(n - 1) != L'\n')
				return false;
			lineEnd = n - 1;
			retract = true;
		}

		size_t lineStart = lineEnd;
		while (lineStart > 0 && m_sink.At(lineStart - 1) != L'\n')
			--lineStart;

		// The line has to carry content: a comment cannot be "trailing" on a
		// blank line, nor on one holding nothing but the indentation
		// WriteCurrentIndent() put there.
		size_t first = lineStart, last = lineEnd;
		while (first < last && blank(first))     ++first;
		while (last > first && blank(last - 1))  --last;
		if (first == last)
			return false;

		// Rule 3 — the script '/' line-start invariant. Layout guard L1 gives a
		// lone '/' its own line, and the lexer only reclassifies etSLASH to
		// etEOS at line start, so nothing may be joined onto that line: doing
		// it would make the terminator re-lex as division and cost losslessness.
		if (last - first == 1 && m_sink.At(first) == L'/')
			return false;

		// Rule 1 — the swallow invariant. If the line already carries a "--"
		// comment, everything appended after it vanishes on re-parse. The scan
		// is over output text, so a "--" inside a string literal is a false
		// positive; that costs a demotion to own-line and nothing more, while
		// the real cases are already excluded because every emitter closes the
		// line after writing a "--" (isEndLineComment()'s contract).
		for (size_t k = first; k + 1 < last; ++k)
			if (m_sink.At(k) == L'-' && m_sink.At(k + 1) == L'-')
				return false;

		// Rule 6 — the width veto, measured on the line actually emitted and
		// against the same separator width the caller is about to write.
		const size_t lineWidth = lineEnd - lineStart;
		if (maxCol > 0 && (int)(lineWidth + reserve) > maxCol)
			return false;

		if (retract && !m_sink.Retract(1))
			return false;
		return true;
	}

	// Last few characters of the buffer, for diagnostic messages. Newlines are
	// shown as '\\' so a multi-line tail stays readable on one report line.
	std::wstring TailForReport(size_t maxChars = 40) const
	{
		const size_t len = m_sink.Length();
		const size_t n = len < maxChars ? len : maxChars;
		std::wstring r;
		for (size_t i = len - n; i < len; ++i)
		{
			const wchar_t c = m_sink.At(i);
			r += (c == L'\n') ? L'\\' : c;
		}
		return r;
	}

	// True when the last non-blank output line consists of nothing but the
	// script terminator '/'. A lone '/' closes a compilation unit exactly as
	// ';' closes a statement, so blank-line separation rules keyed on "the
	// previous item is closed" must treat it the same way — otherwise a unit
	// terminated by '/' alone is glued to whatever comment follows it.
	//
	// The '/' is required to stand alone on its line, matching the invariant
	// the lexer itself uses when it reclassifies etSLASH to etEOS. That is
	// what keeps this from matching the '/' that ends a /* */ comment or a
	// division operator, neither of which ever sits alone on a line.
	bool EndsWithScriptSlash() const
	{
		size_t i = m_sink.Length();
		while (i > 0 && (m_sink.At(i - 1) == L'\n' || m_sink.At(i - 1) == L' ' || m_sink.At(i - 1) == L'\t' || m_sink.At(i - 1) == L'\r'))
			--i;
		if (i == 0 || m_sink.At(i - 1) != L'/')
			return false;
		--i; // step past the '/'
		while (i > 0 && (m_sink.At(i - 1) == L' ' || m_sink.At(i - 1) == L'\t'))
			--i;
		return i == 0 || m_sink.At(i - 1) == L'\n';
	}

	size_t Size() const { return m_sink.Length(); }

	// Whole-buffer view for the call sites that are not sink-agnostic yet
	// (EMIT_RANGE_GUARD's before-snapshot, emitParamList()'s continuation-line
	// column probe). E1 dropped the pre-existing non-const overload: handing
	// out a mutable std::wstring& is exactly the escape hatch a pluggable sink
	// exists to remove, and nothing was using it.
	const std::wstring& Str() const { return m_sink.Str(); }

	// Last token passed to WriteToken(), kept purely for breakpoint/trace
	// inspection — never used for emission decisions.
	const OpenEditor::Token* LastToken() const { return m_lastToken; }

	void Clear () { m_sink.Clear(); m_lastToken = nullptr; }

private:
	// E1 — sink plumbing. Declaration order matters: m_sinkScratch, then
	// m_strSink (bound either to the caller's string or to the scratch), then
	// m_sink (initialised from one of those two). The scratch is never
	// appended to when this writer was constructed from an IFormatSink&; it
	// exists only so m_strSink has something to bind to, and stays empty, so
	// it costs no heap allocation.
	std::wstring  m_sinkScratch;
	StringSink    m_strSink;
	IFormatSink&  m_sink;
	int m_spacesPerLevel;
	int m_indentLevel = 0;
	int m_indentChars = 0; // source of truth for WriteCurrentIndent()/WriteContinuationIndent()
	const OpenEditor::Token* m_lastToken = nullptr;
#ifdef _DEBUG
	// False for scratch/composing writers (joinTokensSingleSpaced, the
	// per-parameter buffer in emitParamList): those build a text *fragment*
	// that is later re-emitted through WriteText(), so a '/' inside one is not
	// a layout violation and checking it would only produce false positives.
	// The top-level formatPlsql() writer — and every emitter sharing its buffer
	// — leaves this true.
	bool m_layoutChecks = true;
#endif
};

// ----------------------------------------------------------------------------
// IndentGuard — RAII helper to manage FormatWriter indent level.
//
// Pushes indent on construction, pops on destruction. Enables clean nested
// indentation without manual push/pop bookkeeping:
//
//   {
//       IndentGuard guard(fw);  // fw.PushIndent() called here
//       fw.WriteCurrentIndent();
//       fw.WriteText(L"nested content\n");
//   }  // fw.PopIndent() called here automatically
//
// Supports pushing multiple levels at once:
//
//   IndentGuard guard(fw, 2);  // Pushes 2 levels; destructor pops 2
// ----------------------------------------------------------------------------
class IndentGuard
{
public:
	explicit IndentGuard(FormatWriter& fw, int levels = 1)
		: m_fw(fw), m_levels(levels)
	{
		for (int i = 0; i < m_levels; ++i)
			m_fw.PushIndent();
	}

	~IndentGuard()
	{
		for (int i = 0; i < m_levels; ++i)
			m_fw.PopIndent();
	}

	// Prevent accidental copies
	IndentGuard(const IndentGuard&) = delete;
	IndentGuard& operator=(const IndentGuard&) = delete;

private:
	FormatWriter& m_fw;
	int m_levels;
};

// ----------------------------------------------------------------------------
// IndentLevelGuard — RAII helper for the "fw.SetIndentLevel(indentLevel);"
// pattern used at the top of every emitXxx(..., int indentLevel, FormatWriter&)
// function (see md/plsql_formatter_trace_writer_proposal.md Phase 2).
//
// fw.SetIndentLevel() is an ABSOLUTE setter, not a stack push/pop like
// PushIndent()/PopIndent() above. Because every recursive emitXxx() call sets
// the writer's tracked level unconditionally on entry and callers commonly
// recurse with a different level (indentLevel+1, +2, ...) before continuing
// to emit more content at their own original level, a bare
// "fw.SetIndentLevel(indentLevel);" at function entry with no matching
// restore leaks the callee's level into the caller once the callee returns —
// every subsequent fw.WriteCurrentIndent() call (in the callee's caller, or
// its caller's caller, ...) then silently uses the wrong, stale level. This
// exact bug occurred in emitMathBoolAwareStatement()'s paren-interior
// recursion (fixed by hand once, see trace-writer proposal "Second bug fix").
//
// IndentLevelGuard makes the mistake structurally impossible: construct it
// in place of "fw.SetIndentLevel(indentLevel);" at the top of an emitXxx()
// function, and the writer's PREVIOUS level (whatever it was before this
// function ran, i.e. the caller's own level) is automatically restored when
// the function returns — including on every early "return" and on exception
// unwind — with no manual restore code needed at any call site.
//
// Internally this saves/restores both m_indentLevel and m_indentChars: the
// level must be restored too, otherwise a later PushIndent()/PopIndent()
// (which recompute m_indentChars from m_indentLevel) would recompute from a
// stale, still-too-deep level and leak extra indentation into everything
// emitted afterward at the caller's own level (e.g. a second WHEN clause
// after an EXCEPTION handler's body, once the body's own IndentLevelGuard
// has been destroyed).
//
//   void emitFoo(..., int indentLevel, FormatWriter& fw)
//   {
//       IndentLevelGuard indentGuard(fw, indentLevel); // replaces fw.SetIndentLevel(indentLevel);
//       ... fw.WriteCurrentIndent() ...
//       emitBar(..., indentLevel + 1, fw); // safe: emitBar has its own guard
//       ... fw.WriteCurrentIndent() ...          // still correct: guard hasn't been destroyed yet
//   } // indentGuard's destructor restores the caller's level here
// ----------------------------------------------------------------------------
class IndentLevelGuard
{
public:
	IndentLevelGuard(FormatWriter& fw, int newLevel)
		: m_fw(fw), m_savedLevel(fw.CurrentIndentLevel()), m_savedChars(fw.CurrentIndentChars())
	{
		if (newLevel != -1) // only for restore
			m_fw.SetIndentLevel(newLevel);
	}

	~IndentLevelGuard()
	{
		m_fw.SetIndentLevel(m_savedLevel);
		m_fw.SetIndentChars(m_savedChars);
	}

	// Prevent accidental copies
	IndentLevelGuard(const IndentLevelGuard&) = delete;
	IndentLevelGuard& operator=(const IndentLevelGuard&) = delete;

private:
	FormatWriter& m_fw;
	int m_savedLevel;
	int m_savedChars;
};

// ----------------------------------------------------------------------------
// Generic token-stream helpers (decoupling plan Group (a) — see
// md/plsql_formatter_decoupling_plan.md). These are pure, file-independent
// utilities used throughout the PL/SQL emitter (spacing rules, token
// classification, EOL/comment skipping, flat-emission measuring, etc.).
// Implementations live in OEPlsSqlFormatterCommon.cpp.
// ----------------------------------------------------------------------------

// Returns true when no space should be emitted BEFORE this token.
bool noSpaceBeforeToken(EToken t);

// Joins tokens[start..end) into a single-spaced string, skipping EOLs,
// using FormatWriter for consistent token-fidelity emission.
std::wstring joinTokensSingleSpaced(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end /*exclusive*/);

// Returns true when no space should be emitted AFTER this token.
bool noSpaceAfterToken(EToken t);
bool noSpaceAfterToken(int t);

// True for reserved words that are datatype names (VARCHAR2, NUMBER, …).
// Their precision/scale parens stay tight: VARCHAR2(100), NUMBER(10,2).
bool isDatatypeWord(const OpenEditor::Token& tk);

// Returns true when a space must be emitted between two adjacent tokens.
// Mirrors the rule used by the SQL formatter: reserved keywords (IN, NOT,
// BETWEEN, …) always get a space before a following '(' — except datatype
// names (isDatatypeWord), which keep their parens tight.
bool needSpaceBetween(const OpenEditor::Token& prev, const OpenEditor::Token& tk);

// Both "-- ..." end-of-line and "/* ... */" block comments come through
// as etCOMMENT. Distinguish by text: anything placed after a "--" comment
// on the same output line would be silently swallowed by it on re-parse.
bool isEndLineComment(const OpenEditor::Token& tk);

// True when the comment token at index `i` shares its source line with the
// code that precedes it — i.e. the author wrote it as an end-of-line comment
// annotating that code, rather than as a lead-in comment on a line of its own.
//
// This is a property of the *source*, so it is read from Token::line rather
// than inferred from the output: the emitter reflows freely and the tree
// carries no "same line" bit of its own. The scan steps back over the etEOL/
// etWHITESPACE run in front of the comment (an etEOL carries the line it
// terminates, so comparing against it would call every comment trailing) and
// stops at the first real token. False when nothing precedes the comment, so
// a leading comment is never trailing.
//
// One definition, used by buildTree() (which records it as
// FormatContext::trailing) and by the emitter sites that dispatch on the raw
// etCOMMENT token instead of on a Comment node, so the two can never disagree.
bool isTrailingComment(const std::vector<OpenEditor::Token>& tokens, int i);

// Returns true when a token is a quoted string literal (etQUOTED_STRING).
bool isStringLiteral(const OpenEditor::Token& tk);

// Appends a zero-length etEOS sentinel to a sub-vector that was sliced
// from the token stream without a trailing ';'. buildTree() closes all
// open scopes on etSEMICOLON or etEOS, so without it closeSrcIdx lands
// on the last real data token and calcWidths() under-counts the width,
// which can cause putSingleLine() to omit the final token.
void appendEosSentinel(std::vector<OpenEditor::Token>& sub);

// Returns true if tk is a low-priority arithmetic operator (+/-).
// + is etUNKNOWN with value "+"; - is etMINUS.
bool isLowPrioArith(const OpenEditor::Token& tk);

// Returns true if tk is a high-priority arithmetic operator (* or /).
bool isHighPrioArith(const OpenEditor::Token& tk);

// Returns true if tk is a boolean AND/OR operator.
bool isBoolOp(const OpenEditor::Token& tk);

// Returns true if tk is a statement-terminator keyword that must stay
// attached to the last condition line (THEN, LOOP).
bool isCondTerminator(const OpenEditor::Token& tk);

// Emits tokens[start..end] (inclusive) in a single flat line with
// minimal spacing, returning the visual width emitted.
int emitFlatTokens(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end,
	FormatWriter& fw);

// Measures the flat visual width of tokens start..end (inclusive).
int measureFlat(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end);

// True when [start,end) holds an end-of-line "--" comment.
//
// Any layout that joins a token range onto ONE physical line must refuse to do
// so when this is true: everything joined after a "--" comment lands on that
// comment's line and is silently swallowed on re-parse. Callers use it to pick
// the multi-line layout that replays comments explicitly instead - see
// emitSubprogramHeader()'s spanHasEolComment and emitParamListStatement().
// Losslessness outranks the length preference, so this applies even when
// wrapping is disabled.
bool rangeHasEndLineComment(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end /*exclusive*/);

// Emits tokens[start..end) (end EXCLUSIVE) with emitFlatTokens()'s spacing
// rules, but breaks the output line after every "--" comment so nothing written
// after it is swallowed. `indentChars` is the column those continuation lines
// are re-anchored to.
//
// Returns true when the output line is still open, i.e. the last token emitted
// was not an end-of-line comment. Callers use that to decide whether the next
// token needs a line break first.
bool emitJoinedKeepingComments(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end /*exclusive*/,
	int indentChars,
	FormatWriter& fw);

// One argument of a call's argument list, as split by splitAtTopLevelCommas().
//
// `lead` holds the comments the split stepped over before `start`, in source
// order. They are part of the argument list and must be reproduced: emitting
// the chunk without them silently deletes every comment a caller wrote between
// two arguments ("p_x => 1,  -- default"). This mirrors HeaderParam::lead in
// OEPlsSqlFormatterDecls.cpp, which does the same for a declaration's
// parameter list.
//
// A chunk that holds comments but no argument text at all (e.g. "f(a, --x\n)")
// is still returned, with an empty [start,end] range (start > end), so its
// comments are not lost either.
struct ParamSpan
{
	int start, end;
	std::vector<int> lead;
};

// Splits token range (parenOpen+1)..(parenClose-1) at top-level commas.
// Returns one ParamSpan per argument, exclusive of the comma, with leading
// EOLs/comments skipped - and those skipped comments handed back in
// ParamSpan::lead so the caller can reproduce them.
std::vector<ParamSpan> splitAtTopLevelCommas(
	const std::vector<OpenEditor::Token>& tokens,
	int parenOpen, int parenClose);

// Skips both etEOL and standalone etCOMMENT tokens. Used when scanning
// for the *next required token* in a declaration (e.g. the name after
// CURSOR, IS after the parameter list, etc.), so a comment placed
// between two significant tokens does not make the parse fail and fall
// back to swallowing the whole declaration verbatim. Any comment(s)
// skipped this way are appended (each on its own line) to skippedComments
// so callers can still reproduce them in the output.
int skipEolAndComments(
	const std::vector<OpenEditor::Token>& tokens, int i, int end,
	std::vector<int>* skippedComments = nullptr);

// Returns the index of the next ';' token after idx (skipping EOL only).
// Returns -1 if none found before end of token stream.
// Use to obtain the actual ';' token so its value can be emitted verbatim
// (token-fidelity rule: never synthesize punctuation as L";").
int nextSemicolon(
	const std::vector<OpenEditor::Token>& tokens,
	int idx);

// Returns the index of the first top-level ';' or etEOS in
// [pos, rangeEnd), tracking paren depth so parens are not searched
// inside. If no top-level ';' is found (e.g. malformed input with an
// unclosed paren), falls back to the first ';'/EOS at any depth so
// that broken input still produces a single statement rather than
// running on to the end of the range.
int scanToSemicolon(
	const std::vector<OpenEditor::Token>& tokens,
	int pos, int rangeEnd);

// Skips etEOL tokens starting at i, up to (not including) end.
int skipEol(const std::vector<OpenEditor::Token>& tokens, int i, int end);

// Returns true if k is one of the DML scope kinds (Select/Delete/Update/
// InsertSimple/InsertConditional/Merge/CteDml). CteDml is the root-level
// "WITH … SELECT/DML" compound statement: its emitter path is the same
// whole-range formatSQL() splice the other DML kinds use.
bool isDmlKind(FormatContext::Kind k);

// Emits the closing "END [qualifier] [name] ;\n" line shared by
// emitBlock, emitIfStmt, emitLoopStmt, and emitPackage.
void emitEndClause(
	const std::vector<OpenEditor::Token>& tokens,
	int closeSrcIdx,
	int indentLevel,
	FormatWriter& fw);

// Appends a blank line unless one is already present at the end of the
// writer's buffer (never produce a double blank line). Thin wrapper over
// FormatWriter::EmptyLine().
void appendBlankLineIfNeeded(FormatWriter& fw);

// Strips trailing whitespace from a formatted-SQL string in-place
// (the DML formatter leaves trailing spaces/newlines that would
// produce blank lines when we append our own "\n" after it).
void trimTrailingWs(std::wstring& s);

// ---- E2b: token-run views of a nested formatter's output ----
//
// formatSQL() returns a token run, but three of its consumers at the
// delegation sites need a *text* property of that run: the slice guard and
// TRACE want the characters, emitLoopStmt()'s flat-vs-wrap decision wants the
// newline test and the character count. These are the token-run forms of those
// three operations, each exactly equivalent to the std::wstring expression it
// replaces, so converting a site changes nothing it measures:
//
//   concatTokenValues(run)   == the string formatSQL() used to return
//   tokenRunIsMultiLine(run) == (formatted.find(L'\n') != std::wstring::npos)
//   tokenRunCharLength(run)  == formatted.size()
//
// concatTokenValues() is also what SQL_SLICE_GUARD compares against, which is
// why the guard needed no change of oracle: it already strips whitespace from
// both sides, and a concatenation carries the same characters as the run.

// Concatenates every token's value, in order. This is the text image of a
// token run — identical to what TokenStreamSink::flush() writes out.
std::wstring concatTokenValues(const std::vector<OpenEditor::Token>& run);

// True when any token's value contains a line break. Note this is a value
// scan rather than an etEOL test: a multi-line block comment carries '\n'
// inside a single etCOMMENT token, and emitLoopStmt()'s single-vs-multi-line
// decision has to see that too (the string form saw it for free, because the
// comment's interior newlines were just characters in the buffer).
bool tokenRunIsMultiLine(const std::vector<OpenEditor::Token>& run);

// Total character count of a run — the flat width used by emitLoopStmt() when
// it decides whether a subquery still fits on the header line.
size_t tokenRunCharLength(const std::vector<OpenEditor::Token>& run);

// Token-run overload of trimTrailingWs(std::wstring&): drops trailing
// whitespace-only tokens, then trims any trailing whitespace characters off
// the new last token's value. Same result as trimming the concatenated text,
// including the all-whitespace case (the run ends up empty). Zero-length
// appendEosSentinel() tokens at the tail are dropped too — they contributed no
// characters, so the string form trimmed them away silently.
void trimTrailingWs(std::vector<OpenEditor::Token>& run);

// Finds the first Block child of node whose openSrcIdx equals openIdx.
// Returns nullptr if not found.
const FormatContext* findBlockChild(
	const FormatContext& node, int openIdx);

// Finds the SubprogramForwardDecl child of `node` that starts at or after
// `pos`, or nullptr when there is none (i.e. `pos` is on an unrelated
// statement). Step 2.1 replacement for re-scanning raw tokens with
// tryParseNestedSubprogramHeader() to recognize a forward declaration.
//
// Normally the node's openSrcIdx == pos exactly (buildTree extends the span
// back over any CREATE [OR REPLACE] prefix and method modifiers). The lookup
// is tolerant of one extra case: a comment between a method modifier and the
// PROCEDURE/FUNCTION keyword stops that backward extension, so `pos` can sit a
// few tokens earlier — only EOL and method-modifier words are allowed in
// [pos, openSrcIdx), and the caller then passes `pos` as
// emitSubprogramForwardDecl's spanStart so nothing is dropped. A standalone
// comment at `pos` is deliberately NOT tolerated (it must keep falling through
// to the caller's own comment handling so it stays on its own line), and
// neither is any other real token — that is what stops `pos` on an unrelated
// statement from matching a forward decl further ahead.
// A node whose span is not fully inside [pos, rangeEnd) is rejected too: the
// caller's range does not own it, and emitting it would overshoot the range
// (mirroring tryParseNestedSubprogramHeader, which also stops at rangeEnd).
const FormatContext* findForwardDeclChild(
	const FormatContext& node,
	const std::vector<OpenEditor::Token>& tokens,
	int pos, int rangeEnd);

// ---------------------------------------------------------------------------
// Whitespace-insensitive text comparison helpers.
//
// stripWhitespace() removes every space, tab, CR and LF; expectedRangeText()
// concatenates the values of the tokens in `ranges` (skipping etEOL) and
// strips the result, so the two are directly comparable.
//
// Deliberately NOT inside the _DEBUG block below: EMIT_RANGE_GUARD uses them
// to prove an emitter consumed exactly its allotted range, and
// emitVerbatimFragment() uses the very same oracle in *every* build
// configuration to decide whether a caller-supplied exact-source slice is
// faithful enough to be written out as-is (see the Verbatim-source callback
// section of OEPlsSqlFormatter.h). A release build must not silently emit a
// slice that drops or duplicates a token, so the check cannot be compiled out.
// ---------------------------------------------------------------------------
inline std::wstring stripWhitespace(const std::wstring& s)
{
	std::wstring r;
	r.reserve(s.size());
	for (wchar_t c : s)
		if (c != L' ' && c != L'\n' && c != L'\r' && c != L'\t')
			r += c;
	return r;
}

inline std::wstring expectedRangeText(
	const std::vector<OpenEditor::Token>& tokens,
	const std::vector<std::pair<int, int>>& ranges)
{
	std::wstring r;
	const int n = (int)tokens.size();
	for (const auto& rg : ranges)
		for (int i = rg.first; i <= rg.second; ++i)
			if (i >= 0 && i < n && tokens.at(i).token != etEOL)
				r += tokens.at(i).value;
	return stripWhitespace(r);
}

// ---------------------------------------------------------------------------
// Debug-mode emission-range guard infrastructure.  Verifies in Debug builds
// that emitters consume exactly their allotted token range.  Moved here from
// OEPlsSqlFormatterBlocks.cpp so every formatter TU shares one copy of the
// macros and helpers.
// ---------------------------------------------------------------------------

#ifdef _DEBUG
// ------------------------------------------------------------------
// #8 — per-emitter token-range assertions (debug builds only).
//
// The token-fidelity rule (md/plsql_formatter_rules.md §1a) guarantees the
// only text an emitter may synthesize is whitespace; every keyword and
// punctuation character is copied verbatim from tokens[idx].value. That
// makes losslessness locally checkable: the text an emitter appends to
// `out`, with all whitespace removed, must equal the whitespace-stripped
// concatenation of the source token values in the range(s) it consumed.
//
// EmitRangeGuard is an RAII oracle that performs this check on scope exit.
// A dropped, duplicated, or reordered token trips the assert at the exact
// emitter responsible, instead of surfacing later in the whole-buffer
// re-tokenize comparison (verifyFormatterLosslessness). It is compiled out
// entirely in release builds.
// ------------------------------------------------------------------
// stripWhitespace() / expectedRangeText() live above the _DEBUG guard — see
// the comment there for why they are compiled in every configuration.

struct EmitRangeGuard
{
	const std::vector<OpenEditor::Token>& tokens;
	std::vector<std::pair<int, int>>      ranges;
	const std::wstring&                   out;
	size_t                                startLen;
	const wchar_t*                        who;

	EmitRangeGuard(const std::vector<OpenEditor::Token>& t,
				   int start, int end, const std::wstring& o, const wchar_t* w)
		: tokens(t), out(o), startLen(o.size()), who(w)
	{
		ranges.push_back({ start, end });
	}

	EmitRangeGuard(const std::vector<OpenEditor::Token>& t,
				   std::vector<std::pair<int, int>> r, const std::wstring& o, const wchar_t* w)
		: tokens(t), ranges(std::move(r)), out(o), startLen(o.size()), who(w)
	{
	}

	~EmitRangeGuard()
	{
		// A user-cancelled format stopped at a root-node checkpoint on purpose,
		// so what was emitted is a deliberate *prefix* of the expected range and
		// the comparison below can only fail. Asserting here would pop a modal
		// dialog on top of the cancellation the user just asked for. See
		// g_formatterCancelling in OEPlsSqlFormatter.h.
		if (g_formatterCancelling)
			return;

		std::wstring expected = expectedRangeText(tokens, ranges);
		std::wstring actual   = stripWhitespace(out.substr(startLen));
		if (expected != actual)
		{
			TRACE("PL/SQL formatter losslessness violation in %ls:\n"
				  "  expected tokens: '%ls'\n"
				  "  actual output  : '%ls'\n",
				  who, expected.c_str(), actual.c_str());
			if (formatterDiagnosticsActive())
			{
				// Automated test in progress (see OEPlsSqlFormatter.h): report
				// through the diagnostics sink. _ASSERTE would pop the modal
				// CRT assert dialog and deadlock the COM caller.
				std::wstring& rep = *g_pFormatterDiagnostics;
				rep += L"[EMIT_RANGE_GUARD] token fidelity violation in ";
				rep += who;
				rep += L"\n  expected tokens: '";
				rep += expected;
				rep += L"'\n  actual output  : '";
				rep += actual;
				rep += L"'\n";
			}
			else
			{
				_ASSERTE(!"PL/SQL emitter dropped/added/reordered a token (see TRACE output)");
			}
		}
	}
};

// Convenience macro: single contiguous [start,end] range.
#define EMIT_RANGE_GUARD(tokens, start, end, out) \
	EmitRangeGuard _emitRangeGuard((tokens), (start), (end), (out.Str()), _CRT_WIDE(__FUNCTION__))
// Convenience macro: explicit list of ranges (for non-contiguous emitters).
#define EMIT_RANGES_GUARD(tokens, ranges, out) \
	EmitRangeGuard _emitRangeGuard((tokens), (ranges), (out.Str()), _CRT_WIDE(__FUNCTION__))

// Returns the inclusive end index a whole-node guard should use for a
// structural scope (Block/IfStmt/LoopStmt) whose closer is emitted by
// emitEndClause. emitEndClause emits END plus the trailing qualifier
// (IF/LOOP) and terminating ';' — all of which live *after* closeSrcIdx —
// so the guarded range must extend to that ';' (or, if none, to the last
// non-EOL/EOS token emitted on the END line). Mirrors emitEndClause's own
// forward scan exactly so the guard's expected span matches what is emitted.
inline int endClauseGuardEnd(
	const std::vector<OpenEditor::Token>& tokens, int closeSrcIdx)
{
	int end = closeSrcIdx;
	int i = closeSrcIdx + 1;
	const int n = (int)tokens.size();
	while (i < n && tokens.at(i).token != etEOL && tokens.at(i).token != etEOS)
	{
		end = i;
		if (tokens.at(i).token == etSEMICOLON) break;
		++i;
	}
	return end;
}

// ------------------------------------------------------------------
// E3.1 — text-form fidelity guard for the formatSQL() delegation sites.
//
// EMIT_RANGE_GUARD compares what an emitter appended to a FormatWriter
// against the token range it consumed. The sites that delegate a DML slice
// to formatSQL() have no FormatWriter on the SQL side: the formatted
// statement comes back as an opaque std::wstring that WriteIndentText()
// re-indents, so no range guard can see it. This is the one place the
// token-fidelity rule (md/plsql_formatter_rules.md §1a) stops being
// enforced — see md/sql_plsql_formatter_integration_plan.md §0.6.
//
// checkSqlSliceFidelity() applies the same oracle across that boundary:
// the whitespace-stripped slice must equal the whitespace-stripped result.
// A dropped, duplicated or reordered token inside a nested DML statement
// now trips at the delegating emitter instead of surfacing later in
// verifyFormatterLosslessness()'s whole-buffer re-tokenize.
//
// stripWhitespace() removes every ' ', '\n', '\r' and '\t', so the
// whitespace-only re-indentation align_comment() performs on multi-line
// block comments (OEPlsSqlFormatterSqlSide.cpp:539) — the single input mutation
// formatSQL() makes — is neutralised automatically. That is why this guard
// needs no equivalent of OEView_7.cpp's normalizeValue(), which exists
// only because that comparison is token-by-token rather than
// whitespace-stripped.
//
// The slice is read *after* formatSQL() returns, which is safe for the
// same reason. Trailing appendEosSentinel() tokens carry an empty value
// and so contribute nothing to the expected text.
// ------------------------------------------------------------------
inline void checkSqlSliceFidelity(
	const std::vector<OpenEditor::Token>& slice,
	const std::wstring& formatted,
	const wchar_t* who)
{
	if (slice.empty()) return;

	std::vector<std::pair<int, int>> ranges;
	ranges.push_back({ 0, (int)slice.size() - 1 });

	std::wstring expected = expectedRangeText(slice, ranges);
	std::wstring actual   = stripWhitespace(formatted);
	if (expected == actual) return;

	TRACE("SQL formatter losslessness violation in %ls:\n"
		  "  expected tokens: '%ls'\n"
		  "  actual output  : '%ls'\n",
		  who, expected.c_str(), actual.c_str());
	if (formatterDiagnosticsActive())
	{
		// Automated test in progress (see OEPlsSqlFormatter.h): report
		// through the diagnostics sink. _ASSERTE would pop the modal CRT
		// assert dialog and deadlock the COM caller.
		std::wstring& rep = *g_pFormatterDiagnostics;
		rep += L"[SQL_SLICE_GUARD] token fidelity violation in ";
		rep += who;
		rep += L"\n  expected tokens: '";
		rep += expected;
		rep += L"'\n  actual output  : '";
		rep += actual;
		rep += L"'\n";
	}
	else
	{
		_ASSERTE(!"formatSQL() dropped/added/reordered a token (see TRACE output)");
	}
}

// Convenience macro for the formatSQL() delegation sites. A plain call
// rather than an RAII object so it can sit in scopes that already hold an
// _emitRangeGuard from EMIT_RANGE_GUARD.
#define SQL_SLICE_GUARD(slice, formatted) \
	checkSqlSliceFidelity((slice), (formatted), _CRT_WIDE(__FUNCTION__))


#else
#define EMIT_RANGE_GUARD(tokens, start, end, out)  ((void)0)
#define EMIT_RANGES_GUARD(tokens, ranges, out)     ((void)0)
#define SQL_SLICE_GUARD(slice, formatted)          ((void)0)
#endif

class Settings;

// ============================================================================
// FormatterPolicy — see md/FormatterPolicy_proposal.md.
//
// The single settings-derived value object read by both the SQL and PL/SQL
// emitters. Replaces the dual pattern of the PL/SQL side threading a bare
// `const Settings& s` through every emitXxx() and the SQL side pumping
// s.GetFmt*() into file-scope FMT_* globals in its constructor: both now
// build a FormatterPolicy once at the boundary (FromSettings()) and pass it
// down, and the SQL side's FMT_* globals become a transient mirror populated
// from the policy instead of reading Settings directly.
//
// Also folds in ExprWrapPolicy's four fields (expressionMaxLen, concatMaxLen,
// concatKeepMixedPairTogether, concatBreakBetweenLiterals), collapsing the
// `(s, pol)` dual-parameter pattern some emitters had into a single `pol`.
//
// FromSettings(const Settings&) is the ONLY place in the codebase that reads
// s.GetFmt*()/s.GetIndentSpacing() for formatter purposes; it is defined in
// OEPlsSqlFormatterStmts.cpp (already includes OESettings.h).
//
// Defaults() returns the same production defaults as OESettings.cpp's
// CMN_IMPL_PROPERTY registrations, hardcoded here so automated tests can
// build a policy that is immune to the developer's persisted live settings.
// ============================================================================
struct FormatterPolicy
{
	// ---- indent ----
	int  indentSpacing                       = 2;

	// ---- SQL-side settings (formerly the SQLFormatter FMT_* globals) ----
	bool simpleStmtEnabled                   = true;
	int  simpleStmtMaxLen                    = 60;
	bool selectShortListEnabled              = true;
	int  selectShortListMaxLen               = 40;
	bool fromShortListEnabled                = true;
	int  fromShortListMaxLen                 = 40;
	bool alignColumnAliases                  = true;
	int  blockShortMaxLen                    = 60;
	bool selectStmtEnabled                   = true;
	int  selectStmtMaxLen                    = 60;
	bool groupByShortListEnabled             = true;
	int  groupByShortListMaxLen              = 60;
	bool orderByShortListEnabled             = true;
	int  orderByShortListMaxLen              = 60;
	bool setOpBlankLine                      = false;
	int  caseWhenShortMaxLen                 = 40;
	int  returningMaxLen                     = 60;
	int  returningMaxItems                   = 5;
	int  logErrorsMaxLen                     = 40;
	bool alignSetAssignments                 = true;
	bool alignCaseInSet                      = true;
	bool brickListEnabled                    = true;
	int  brickListMaxLineLen                 = 80;
	int  brickListMaxLineItems               = 10;
	int  brickListMinItems                   = 5;
	bool brickListLinkedInsert               = true;
	bool brickListLinkedSetOp                = true;

	// ---- PL/SQL-side settings ----
	bool plsqlDeclBlankLineSeparator         = true;
	bool plsqlBlankLineAroundFlowStmts       = true;
	bool plsqlBlankLineAroundSubprograms     = true; // declared, not yet read anywhere (pre-existing)
	bool plsqlBlankLineAfterUnitHeader       = true;
	bool plsqlDmlBlankLineSeparator          = true;
	bool plsqlBlankLineBeforeComment         = true;
	bool plsqlIsOnNewLine                    = false;
	int  plsqlParamListMaxLen                = 80;
	bool plsqlParamListAlignParen            = false;

	// ---- end-of-line comment preservation ----
	// md/plsql_formatter_endline_comments_proposal.md. keepTrailing is the
	// master switch for rules 1-5/10-13: a comment the author wrote on the same
	// source line as the code it annotates stays on that output line instead of
	// being promoted to a line of its own (where it silently reads as describing
	// the *next* statement). maxCol is rules 6-7, the width veto: a preserved
	// comment whose end column would exceed it is demoted instead, because the
	// formatter's own re-indentation, list explosion and short-statement
	// collapsing all move code, so a source column is not an output column.
	// <= 0 disables the veto.
	bool plsqlEndLineCommentKeepTrailing     = true;
	int  plsqlEndLineCommentMaxCol           = 120;

	// ---- expression wrapping (formerly ExprWrapPolicy) ----
	// §9.1/§9.2 — math + boolean wrapping width. 0 disables wrapping.
	int  expressionMaxLen                    = 0;
	// §8 — || concatenation wrapping width. 0 disables wrapping.
	int  concatMaxLen                        = 0;
	// §8 — never break at a || that crosses a literal<->variable boundary.
	bool concatKeepMixedPairTogether         = false;
	// §8 — always break a literal || literal transition onto a new line.
	bool concatBreakBetweenLiterals          = false;

	// The reused-pl/sql-settings SQL expression/concat widths (see
	// ExprWrapPolicy::FromSql's comment — the late decision was to reuse the
	// PL/SQL FmtPlsqlExpressionMaxLen/FmtPlsqlConcatMaxLen for the SQL side
	// rather than add dedicated FmtSql* settings).
	int  sqlExpressionMaxLen                 = 80;
	int  sqlConcatMaxLen                     = 80;

	// Builds a policy from the developer's persisted/live settings. The
	// only place that calls Settings::Get*() for formatter purposes.
	// Defined in OEPlsSqlFormatterStmts.cpp (next to the old ExprWrapPolicy
	// factories, which this supersedes).
	static FormatterPolicy FromSettings(const Settings& s);

	// Hardcoded production defaults, independent of the user's live
	// settings. Used by the automated test harness so test output cannot
	// shift when the developer changes their formatter preferences.
	static FormatterPolicy Defaults() { return FormatterPolicy(); }
};

// ---------------------------------------------------------------------------
// End-of-line comment emission (md/plsql_formatter_endline_comments_proposal.md)
// ---------------------------------------------------------------------------
// Appends an author's trailing "-- ..." comment to the output line it
// annotates, one space behind the code, and returns true. Returns false —
// with the output untouched — when the comment has to go on a line of its own,
// which the caller then does with the ordinary blankBefore(true) /
// WriteCurrentIndent() / WriteToken() / NewLine() sequence. Callers therefore
// must NOT write the indent or the blank separator before trying this: both
// would land between the code and its comment.
//
// `trailing` is FormatContext::trailing, or isTrailingComment() at the sites
// that dispatch on a raw etCOMMENT token instead of on a Comment node. It is
// the source fact that decides whether the comment annotates the code before
// it; every other reason to decline is the writer's, and each is a refusal
// rather than a rewrite:
//   - plsqlEndLineCommentKeepTrailing off — rules 1-5 disabled wholesale;
//   - a "/* */" block comment — joinable in principle, but the feature is
//     scoped to "--" (proposal Q2) because a multi-line body cannot be
//     "trailing" at all;
//   - ResumeLineForEndComment() refuses — the swallow invariant (rule 2), the
//     lone script '/' (rule 3), a blank or indent-only line, the width veto
//     (rules 6-7), or a sink that cannot retract.
bool tryEmitTrailingEndComment(
	FormatWriter& fw,
	const OpenEditor::Token& tk,
	bool trailing,
	const FormatterPolicy& pol);

} // namespace OpenEditor
