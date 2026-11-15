/* 
	SQLTools is a tool for Oracle database developers and DBAs.
	Copyright (C) 1997-2026 Aleksey Kochetov

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
#include <map>
#include "OpenEditor/OEHelpers.h"
#include "Common/PlSqlParserEx.h"
#include "Common/StrHelpers.h"   // q_closer_for - shared with the other scanners

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace Common
{
namespace PlsSql
{
	using std::wstring;

	// Same delimiter set as PlSqlParser
	extern OpenEditor::DelimitersMap m_Delimiters;

// ---------------------------------------------------------------------------
// construction / reset
// ---------------------------------------------------------------------------

PlSqlParserEx::PlSqlParserEx (SyntaxAnalyser* analyzer,
							   TokenMapPtr     tokenMap,
							   ReservedPtr     reserved)
	: m_analyzer(analyzer)
	, m_tokenMap(tokenMap)
	, m_reserved(reserved)
	, m_strState(esNone)
	, m_qClose(0)
	, m_prevLineLastNonSpace(0)
	, m_lineRescan(0)
{
}

void PlSqlParserEx::Clear ()
{
	m_strState = esNone;
	m_qClose   = 0;
	m_strToken = Token();
	m_prevLineLastNonSpace = 0;
	m_lineRescan = 0;
}

void PlSqlParserEx::PutEOF (int line)
{
	Token tk;
	tk = etEOF;
	tk.line   = line;
	tk.offset = 0;
	tk.length = 0;
	m_analyzer->PutToken(tk);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

void PlSqlParserEx::emit (EToken type, int line, int offset, int length,
						   const wstring& value)
{
	Token tk;
	tk.token    = type;
	tk.line     = line;
	tk.offset   = static_cast<Token::size_pos>(offset);
	tk.length   = static_cast<Token::size_pos>(length);
	tk.reserved = 0;
	tk.value    = value;
	m_analyzer->PutToken(tk);
}

// Scan digits [. digits] [E [+-] digits] starting at digitStart.
// Returns true and sets tokenEnd past the last character on success.
bool PlSqlParserEx::scanNumberBody (const wchar_t* str, int length,
									 int digitStart, int& tokenEnd) const
{
	int i = digitStart;
	if (i >= length || !iswdigit(str[i])) return false;
	while (i < length && iswdigit(str[i])) ++i;
	if (i < length && str[i] == L'.')
	{
		++i;
		while (i < length && iswdigit(str[i])) ++i;
	}
	if (i < length && (str[i] == L'E' || str[i] == L'e'))
	{
		int save = i++;
		if (i < length && (str[i] == L'+' || str[i] == L'-')) ++i;
		if (i >= length || !iswdigit(str[i]))
			i = save; // not a valid exponent — backtrack
		else
			while (i < length && iswdigit(str[i])) ++i;
	}
	tokenEnd = i;
	return true;
}

// ---------------------------------------------------------------------------
// emit an EOL token
// ---------------------------------------------------------------------------
static void emitEol(SyntaxAnalyser* analyzer, int line, int length)
{
	Token eol;
	eol.token  = etEOL;
	eol.line   = line;
	eol.offset = static_cast<Token::size_pos>(length);
	eol.length = 0;
	eol.value  = L"\n";
	analyzer->PutToken(eol);
}

// ---------------------------------------------------------------------------
// SQL*Plus PRO[MPT] / REM[ARK] line commands
// ---------------------------------------------------------------------------
// Every abbreviation SQL*Plus itself accepts, and exactly the eight spellings the
// highlighter is already configured with for PL/SQL (Settings/Data/languages.xml,
// <StartLineComments>). Keeping the two lists in step is the point: a line the
// editor colours as a comment is now also a line the parser treats as one. They
// disagreed before, and only the parser's answer ever reached folding.
static bool isSqlPlusCommentWord (const wchar_t* word, int len)
{
	static const wchar_t* const s_words[] = {
		L"PRO",   L"PROM",  L"PROMP", L"PROMPT",
		L"REM",   L"REMA",  L"REMAR", L"REMARK",
	};

	for (const wchar_t* w : s_words)
		if ((size_t)len == wcslen(w) && _wcsnicmp(word, w, (size_t)len) == 0)
			return true;

	return false;
}

// ---------------------------------------------------------------------------
// SQL*Plus Tier 2 + Tier 3 command words: the ones that are ordinary identifiers
// or ordinary clause keywords too
// ---------------------------------------------------------------------------
// ACCEPT, APPEND, ATTRIBUTE, BREAK, CLEAR, COLUMN, COMPUTE, CONNECT, COPY,
// DEFINE, DESCRIBE, DISCONNECT, HOST, PRINT, RECOVER, RUN, SHOW, START,
// VARIABLE. Every one is
// already wired to SqlPlusCmdNode in g_recognitionSeq (OEPlsSqlSyntaxImpl2.cpp
// :160-211) and none is in s_plsqlTokens, so a statement-leading one lexes as
// etUNKNOWN, matches nothing, and its GenericSqlNode fallback runs to the next
// ';' - the EXECUTE/Tier 1 failure mode exactly.
//
// They cannot go in the token map the way the Tier 1 thirteen did. A map entry is
// context-free, and these ARE the column names a formatted column list indents
// onto a line start (COLUMN, DEFINE, CLEAR, PRINT, SHOW, VARIABLE, ATTRIBUTE), so
// retyping them everywhere would rewrite the token stream of ordinary DDL. That
// is the same reasoning that put prompt/remark behind AtStatementStart() above;
// the difference is that these words carry arguments worth keeping, so they get
// their real kind rather than being collapsed into a comment.
//
// minLength is SQL*Plus's own abbreviation rule, taken from the commented-out
// script-mode twin (OEPlsSqlParser.cpp:59-96) so both tokenizers agree - with two
// deliberate deviations. That table gives APPEND and RUN minLength 1, matching
// SQL*Plus's interactive A and R shortcuts, but a single letter is far too weak a
// signal to retype on: it would fire on any one-character line. Both are required
// to be spelled in full here. Nothing else deviates, and CLEAR keeps its 2 ("CL").
//
// Tier 3 adds START and CONNECT. An earlier note here excluded them on the grounds
// that START WITH and CONNECT BY "occur at a statement start too". They do not:
// both are clauses of a SELECT that is already open, so m_top is that statement's
// node and AtStatementStart() is false. No Oracle statement begins with START or
// CONNECT. Measured unchanged by these two entries - a hierarchical query in both
// clause orders, an identity column's "(START WITH 1 INCREMENT BY 1)", a top-level
// DECLARE whose local variable is named START, and CONNECT as a selected column.
//
// minLengths are the script-mode twin's own (OEPlsSqlParser.cpp:68 CONN, :90 STA),
// so both tokenizers still agree. STARTUP is unaffected: it is 7 characters, and
// len <= full rejects it here, so it keeps reaching its Tier 1 map entry.
//
// AT stays out, and not for the reason the old note gave - SQL*Plus has no AT
// command, and AT TIME ZONE is a mid-statement expression the gate never sees.
// etAT is the '@' script operator (OEPlsSqlParser.cpp:50): a delimiter, not a word,
// so this table cannot express it. The tokenizer hands '@' back as etAT_SIGN
// (:816), recognised in g_recognitionSeq instead - see the note there.
struct SqlPlusCmdWord
{
	const wchar_t* word;
	int            minLength;
	EToken         kind;
};

static const SqlPlusCmdWord s_sqlPlusCmdWords[] = {
	{ L"ACCEPT",     3, etACCEPT     },
	{ L"APPEND",     6, etAPPEND     },
	{ L"ATTRIBUTE",  9, etATTRIBUTE  },
	{ L"BREAK",      3, etBREAK      },
	{ L"CLEAR",      2, etCLEAR      },
	{ L"COLUMN",     3, etCOLUMN     },
	{ L"COMPUTE",    4, etCOMPUTE    },
	{ L"CONNECT",    4, etCONNECT    },
	{ L"COPY",       4, etCOPY       },
	{ L"DEFINE",     3, etDEFINE     },
	{ L"DESCRIBE",   4, etDESCRIBE   },
	{ L"DISCONNECT", 4, etDISCONNECT },
	{ L"HOST",       4, etHOST       },
	{ L"PRINT",      3, etPRINT      },
	{ L"RECOVER",    7, etRECOVER    },
	{ L"RUN",        3, etRUN        },
	{ L"SHOW",       3, etSHOW       },
	{ L"START",      3, etSTART      },
	{ L"VARIABLE",   3, etVARIABLE   },
};

// A word of length len is a command if it is a whole prefix of one entry and at
// least that entry's minLength - the same rule buildTokenMap expands, so "COL",
// "COLU" and "COLUMN" all answer etCOLUMN while "COLUMNS" answers etNONE.
static EToken sqlPlusCommandKind (const wchar_t* word, int len)
{
	for (const SqlPlusCmdWord& c : s_sqlPlusCmdWords)
	{
		int full = static_cast<int>(wcslen(c.word));
		if (len >= c.minLength && len <= full
			&& _wcsnicmp(word, c.word, static_cast<size_t>(len)) == 0)
			return c.kind;
	}

	return etNONE;
}

// ---------------------------------------------------------------------------
// PutLine — main entry point
//
// The tokenizer is fed one line at a time, so nothing inside PutLineImpl can
// look past the start of the current line. A '+'/'-' that begins a line would
// therefore always look like a unary sign prefix, even when it continues an
// expression from the previous line — e.g. a wrapped
//
//       endpoint_value - (LAG(...) OVER (ORDER BY endpoint_value) + 1)
//       + 1 AS vals_in_range
//
// where the second '+' is plainly binary. Because the signed-literal rule also
// tolerates whitespace between sign and digits, that mis-classification fuses
// "+ 1" into a single "+1" token and the formatted buffer no longer re-lexes to
// the original token stream (verifyFormatterLosslessness fails).
//
// Remembering the last non-space character of every line gives the next line
// the context it needs. Blank lines leave the value untouched so that
//
//       select a,
//
//              -1
//
// still sees the comma.
// ---------------------------------------------------------------------------
bool PlSqlParserEx::PutLine (int line, const wchar_t* str, int length)
{
	bool result = PutLineImpl(line, str, length);

	for (int k = length - 1; k >= 0; --k)
	{
		if (!iswspace(str[k]))
		{
			m_prevLineLastNonSpace = str[k];
			break;
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// PutLineTail — re-scan what follows a literal that closed mid-line
// ---------------------------------------------------------------------------
void PlSqlParserEx::PutLineTail (int line, const wchar_t* str, int length)
{
	++m_lineRescan;
	PutLine(line, str, length);
	--m_lineRescan;
}

// ---------------------------------------------------------------------------
// PutLineImpl — tokenise a single line
// ---------------------------------------------------------------------------
bool PlSqlParserEx::PutLineImpl (int line, const wchar_t* str, int length)
{
	// ------------------------------------------------------------------
	// Continuation: we are inside a multi-line token from a previous line.
	// ------------------------------------------------------------------
	if (m_strState == esSQuote)
	{
		m_strToken.value += L'\n';
		for (int i = 0; i < length; ++i)
		{
			m_strToken.value += str[i];
			if (str[i] == L'\'')
			{
				// closing quote
				m_strToken.length = static_cast<Token::size_pos>(m_strToken.value.size());
				m_analyzer->PutToken(m_strToken);
				m_strState = esNone;
				// recurse on whatever follows the closing quote on this line
				if (i + 1 < length)
					PutLineTail(line, str + i + 1, length - i - 1);
				else
					emitEol(m_analyzer, line, length);
				return true;
			}
		}
		// not closed on this line
		emitEol(m_analyzer, line, length);
		return true;
	}

	if (m_strState == esQQuote)
	{
		m_strToken.value += L'\n';
		for (int i = 0; i < length - 1; ++i)
		{
			m_strToken.value += str[i];
			if (str[i] == m_qClose && str[i+1] == L'\'')
			{
				m_strToken.value += str[i+1]; // closing '
				m_strToken.length = static_cast<Token::size_pos>(m_strToken.value.size());
				m_analyzer->PutToken(m_strToken);
				m_strState = esNone;
				int rest = i + 2;
				if (rest < length)
					PutLineTail(line, str + rest, length - rest);
				else
					emitEol(m_analyzer, line, length);
				return true;
			}
		}
		// append last char (can't be the two-char close sequence)
		if (length > 0)
			m_strToken.value += str[length - 1];
		emitEol(m_analyzer, line, length);
		return true;
	}

	if (m_strState == esComment)
	{
		m_strToken.value += L'\n';
		for (int i = 0; i < length; ++i)
		{
			m_strToken.value += str[i];
			if (i > 0 && str[i-1] == L'*' && str[i] == L'/')
			{
				m_strToken.length = static_cast<Token::size_pos>(m_strToken.value.size());
				m_analyzer->PutToken(m_strToken);
				m_strState = esNone;
				if (i + 1 < length)
					PutLineTail(line, str + i + 1, length - i - 1);
				else
					emitEol(m_analyzer, line, length);
				return true;
			}
		}
		emitEol(m_analyzer, line, length);
		return true;
	}

	// ------------------------------------------------------------------
	// Normal line scan
	// ------------------------------------------------------------------
	int i = 0;

	// ---- SQL*Plus PRO[MPT] / REM[ARK]: a line command, not a statement ----
	// Both end at end of line and both are pure text - SQL*Plus prints a PROMPT
	// and ignores a REMARK - so neither may open a statement. SqlPlusCmdNode is
	// exactly that node: it completes on etEOL as well as etEOS/etEOF
	// (OEPlsSqlSyntaxImpl.cpp:1664-1666). The trouble is that almost none of the
	// words that reach it are ever produced. s_scriptTokens, the table listing
	// all 37 command words the factory maps to that node
	// (OEPlsSqlSyntaxImpl2.cpp:159-211), is commented out at
	// OEPlsSqlParser.cpp:48-159, and the map actually in use - s_plsqlTokens,
	// :162-369 - kept precisely one of them, EXIT at :184, until EXECUTE joined it
	// at :300 for the EXEC folding fix and thirteen more at :348-360 for Tier 1.
	// So the map reaches the node through those fourteen, and Tier 2 below adds
	// seventeen more by kind rather than by map entry. etPROMPT and etREMARK are
	// still never emitted as such - this probe turns them into etCOMMENT - so the
	// "ignore ; in REM and PROMPTS" exemption at :1667-1675, the one rule written
	// specifically for those two commands, still cannot fire.
	//
	// A leading "prompt" therefore arrives as etUNKNOWN,
	// ScriptFactory::RecognizeAndCreate matches nothing, and its fallback
	// GenericSqlNode runs to the next ';'. On an APEX export, which opens
	//
	//     prompt application/set_environment
	//     ----------------------------...
	//     --
	//     -- ORACLE Application Express (APEX) export file
	//     ----------------------------...
	//     begin
	//       null;
	//     end;
	//     /
	//
	// that swallowed the banner AND the block opener into one oitStatement region
	// spanning prompt..null; : the begin..end got no fold of its own, the comment
	// run started a line late, and GetLastOutlineQuality still said oqClean, so
	// nothing downstream complained. Measured, not inferred - see
	// md/CodeFolding_tests.md §7.
	//
	// Emitting the line as a comment fixes both halves at once. It keeps the text
	// out of the tree entirely, so there is no node left to swallow anything, and
	// it lands in PlSqlAnalyzer's CommentSpan side table, where AppendCommentRuns
	// folds it together with the '--' banner below it into a single run - the
	// grouping the highlighter has always shown.
	//
	// Three guards, each covering a distinct false positive. Tier 2 sits inside
	// the same three, and inheriting them is precisely what makes it safe:
	//  - AtStatementStart(): "prompt" and "remark" are ordinary identifiers
	//    everywhere else, and a column list indents them onto a line start all the
	//    time (CREATE TABLE t ( / prompt VARCHAR2(10) / );). Only a
	//    statement-leading one is a command. This is why the probe lives here and
	//    not in the token map: a map entry is context-free and would retype every
	//    such column name in the file. It is also why the formatter is untouched -
	//    SimpleSyntaxAnalyser inherits the false default and keeps its own
	//    Root-scope probe instead.
	//  - m_lineRescan == 0: a continuation branch re-entered PutLine on the tail
	//    of a line after a multi-line literal closed mid-line. That tail is not a
	//    line start, and on a statement whose FIRST token is the literal
	//    AtStatementStart() would still say true.
	//  - whitespace or end of line after the word, which is what makes it a
	//    command word rather than the front of an identifier ("prompt_log" scans
	//    as one word anyway, but "rem(1)" and "prompt.foo" must not match).
	// Tier 2 state. Set by this probe, consumed by the word branch of the main
	// scan below, because only that branch knows it really did reach a word: if
	// the first non-space character turns out to be a quote, a number or a
	// two-character operator, the override is simply never applied and the line
	// lexes exactly as it did before. -1 / etNONE mean "nothing to override".
	//
	//  - leadCmdStart/leadCmdKind: the line's first word is a Tier 2 command.
	//  - setWordStart: the line's first word is SET, so the word after it sits in
	//    GenericSetNode's parameter position and is looked up there too. That is
	//    not a statement start - SET is - so without this second hook
	//    "SET DEFINE OFF" would keep arriving as etUNKNOWN and fall to the
	//    ePendingSql arm at OEPlsSqlSyntaxImpl.cpp:1780, which completes only on
	//    a semicolon. Same swallow, different row: the Tier 1 SET case fixed
	//    TIMING and PAUSE by giving them map entries, and DEFINE and DESCRIBE are
	//    the two parameters that cannot get one.
	EToken leadCmdKind  = etNONE;
	int    leadCmdStart = -1;
	int    setWordStart = -1;
	bool   setParamNext = false;

	if (m_lineRescan == 0 && m_analyzer->AtStatementStart())
	{
		int w = 0;
		while (w < length && iswspace(str[w])) ++w;

		int e = w;
		while (e < length && !m_Delimiters[str[e]] && !iswspace(str[e])) ++e;

		if (e > w && (e == length || iswspace(str[e])))
		{
			if (isSqlPlusCommentWord(str + w, e - w))
			{
				wstring val(str + w, length - w);
				emit(etCOMMENT, line, w, length - w, val);
				emitEol(m_analyzer, line, length);
				return true;
			}

			leadCmdKind = sqlPlusCommandKind(str + w, e - w);

			if (leadCmdKind != etNONE)
				leadCmdStart = w;
			else if (e - w == 3 && _wcsnicmp(str + w, L"SET", 3) == 0)
				setWordStart = w; // SET itself is in the token map; only arm the slot
		}
	}

	while (i < length)
	{
		// skip whitespace — same as original PlSqlParser: emit nothing
		if (iswspace(str[i]))
		{
			while (i < length && iswspace(str[i])) ++i;
			continue;
		}

		wchar_t ch = str[i];

		// ---- q-quoted string: q'X...X' or nq'X...X'  (X is a bracket or any char) ----
		// 'n' spells the national-character-set variant; the quoting rules are
		// identical, so both prefixes share one code path.
		//
		// The prefix has to be a whole token. Requiring a delimiter, whitespace or
		// start-of-line immediately before it is what keeps "myq'[x]'" an
		// identifier followed by a string rather than a q-literal. The word scan
		// below would normally swallow "myq" whole anyway, but stating the rule
		// here keeps it independent of scan order.
		int qPos = -1;
		if (ch == L'q' || ch == L'Q')
			qPos = i;
		else if ((ch == L'n' || ch == L'N') && i + 1 < length
				 && (str[i+1] == L'q' || str[i+1] == L'Q'))
			qPos = i + 1;

		if (qPos >= 0
			&& (i == 0 || iswspace(str[i-1]) || m_Delimiters[str[i-1]])
			&& qPos + 2 < length && str[qPos+1] == L'\''
			&& Common::q_closer_for(str[qPos+2]) != 0)
		{
			wchar_t close = Common::q_closer_for(str[qPos+2]);
			// search for close+' on this line
			int start = i;
			wstring val(str + i, qPos + 3 - i); // q'X or nq'X
			i = qPos + 3;
			bool found = false;
			while (i < length)
			{
				val += str[i];
				if (str[i] == close && i + 1 < length && str[i+1] == L'\'')
				{
					val   += str[i+1];
					i     += 2;
					found = true;
					break;
				}
				++i;
			}
			if (found)
			{
				emit(etQUOTED_STRING, line, start, i - start, val);
			}
			else
			{
				// multi-line q-literal
				m_strState        = esQQuote;
				m_qClose          = close;
				m_strToken        = Token();
				m_strToken.token  = etQUOTED_STRING;
				m_strToken.line   = line;
				m_strToken.offset = static_cast<Token::size_pos>(start);
				m_strToken.length = 0;
				m_strToken.value  = val;
				emitEol(m_analyzer, line, length);
				return true;
			}
			continue;
		}

		// ---- regular single-quoted string: handles '' escapes and multi-line ----
		if (ch == L'\'')
		{
			int start = i;
			wstring val;
			val += ch;
			++i;
			bool closed = false;
			while (i < length)
			{
				val += str[i];
				if (str[i] == L'\'')
				{
					if (i + 1 < length && str[i+1] == L'\'')
					{
						// escaped '' — consume both
						++i;
						val += str[i];
						++i;
					}
					else
					{
						++i;
						closed = true;
						break;
					}
				}
				else
					++i;
			}
			if (closed)
			{
				emit(etQUOTED_STRING, line, start, i - start, val);
			}
			else
			{
				// multi-line string
				m_strState        = esSQuote;
				m_strToken        = Token();
				m_strToken.token  = etQUOTED_STRING;
				m_strToken.line   = line;
				m_strToken.offset = static_cast<Token::size_pos>(start);
				m_strToken.length = 0;
				m_strToken.value  = val;
				// BugFix: do not see any reason to emitEol because it is internal to a multi-line string
				// emitEol(m_analyzer, line, length);
				return true;
			}
			continue;
		}

		// ---- double-quoted identifier ----
		if (ch == L'"')
		{
			int start = i;
			wstring val;
			val += ch;
			++i;
			bool closed = false;
			while (i < length)
			{
				val += str[i];
				if (str[i] == L'"') { ++i; closed = true; break; }
				++i;
			}
			if (closed)
				emit(etDOUBLE_QUOTED_STRING, line, start, i - start, val);
			// if not closed (unusual) just drop — identifier can't span lines
			continue;
		}

		// ---- end-line comment: -- ----
		if (ch == L'-' && i + 1 < length && str[i+1] == L'-')
		{
			wstring val(str + i, length - i);
			emit(etCOMMENT, line, i, length - i, val);
			emitEol(m_analyzer, line, length);
			return true;
		}

		// ---- block comment: /* ... */ ----
		if (ch == L'/' && i + 1 < length && str[i+1] == L'*')
		{
			int start = i;
			wstring val;
			val += ch; val += str[i+1];
			i += 2;
			bool closed = false;
			while (i < length)
			{
				val += str[i];
				if (str[i] == L'/' && i > 0 && str[i-1] == L'*')
				{
					++i; closed = true; break;
				}
				++i;
			}
			if (closed)
				emit(etCOMMENT, line, start, i - start, val);
			else
			{
				m_strState        = esComment;
				m_strToken        = Token();
				m_strToken.token  = etCOMMENT;
				m_strToken.line   = line;
				m_strToken.offset = static_cast<Token::size_pos>(start);
				m_strToken.length = 0;
				m_strToken.value  = val;
				emitEol(m_analyzer, line, length);
				return true;
			}
			continue;
		}

		// ---- signed numeric literal: [+-][ws]digits[.digits][E[+-]digits] ----
		if (ch == L'+' || ch == L'-')
		{
			int digitStart = i + 1;
			while (digitStart < length && iswspace(str[digitStart])) ++digitStart;

			if (digitStart < length && iswdigit(str[digitStart]))
			{
				// Unary context: sign is a prefix to a literal, not a binary operator.
				// True when the nearest non-space char before the sign is a keyword
				// letter, comma, opening paren, comparison operator, another sign, a
				// comment end or arithmetic operator, or there is no such character
				// anywhere before the sign at all (it is the first token of the
				// statement).
				//
				// "Nearest non-space char" may sit on a previous line: PutLine only
				// ever sees one line, so m_prevLineLastNonSpace supplies the context
				// a line-leading sign would otherwise lose. A line break does not
				// turn a binary operator into a unary sign.
				bool unary = false;
				int k = i - 1;
				while (k >= 0 && iswspace(str[k])) --k;
				wchar_t p = (k >= 0) ? str[k] : m_prevLineLastNonSpace;
				if (p == 0)
				{
					// No significant character before the sign — start of statement
					unary = true;
				}
				else
				{
					// A sign is a unary prefix unless the character before it can
					// end an operand. '/' and '*' cover a closing comment — as in
					// "SELECT /*+rule*/" with "-1" on the next line — as well as the
					// divide and multiply operators; '|' covers '||'; ';' covers a
					// sign that begins a new statement.
					unary = (iswalpha(p) || p==L','||p==L'('||p==L'='
										 ||p==L'<'||p==L'>'||p==L'+'||p==L'-'
										 ||p==L'/'||p==L'*'||p==L'|'||p==L';');
				}
				if (unary)
				{
					int tokenEnd = 0;
					if (scanNumberBody(str, length, digitStart, tokenEnd))
					{
						// value: sign + number digits (no internal whitespace)
						wstring val;
						val += ch;
						val += wstring(str + digitStart, tokenEnd - digitStart);
						emit(etUNKNOWN, line, i, tokenEnd - i, val);
						i = tokenEnd;
						continue;
					}
				}
			}
		}

		// ---- SQL*Plus / on its own line (etEOS) ----
		if (ch == L'/')
		{
			// check if entire line (before and after) is blank
			bool blankBefore = true, blankAfter = true;
			for (int k = 0; k < i; ++k)
				if (!iswspace(str[k])) { blankBefore = false; break; }
			for (int k = i + 1; k < length; ++k)
				if (!iswspace(str[k])) { blankAfter = false; break; }

			if (blankBefore && blankAfter)
			{
				emit(etEOS, line, i, 1, wstring(1, ch));
				++i;
				continue;
			}
			// ordinary slash — fall through to keyword/delimiter scan
		}

		// ---- two-character operators (must be checked before single-char delimiters) ----
		// Pairs: >= <= <> != ~= ^= := => || ** .. << >>
		if (m_Delimiters[ch] && i + 1 < length)
		{
			wchar_t ch2 = str[i + 1];
			bool isTwoChar = false;
			EToken twoType = etUNKNOWN;

			switch (ch)
			{
			case L'>': if (ch2 == L'=') { twoType = etGREATER_EQUAL; isTwoChar = true; }
					   else if (ch2 == L'>') { twoType = etLABEL_END; isTwoChar = true; } break; // >= >>
			case L'<': if (ch2 == L'=') { twoType = etLESS_EQUAL; isTwoChar = true; }
					   else if (ch2 == L'>') { twoType = etNOT_EQUAL; isTwoChar = true; }
					   else if (ch2 == L'<') { twoType = etLABEL_BEGIN; isTwoChar = true; } break; // <= <> <<
			case L'!': if (ch2 == L'=') { twoType = etNOT_EQUAL; isTwoChar = true; } break; // !=
			case L'~': if (ch2 == L'=') { twoType = etNOT_EQUAL; isTwoChar = true; } break; // ~=
			case L'^': if (ch2 == L'=') { twoType = etNOT_EQUAL; isTwoChar = true; } break; // ^=
			case L':': if (ch2 == L'=') { twoType = etASSIGN; isTwoChar = true; } break; // :=
			case L'=': if (ch2 == L'>') { twoType = etASSOC; isTwoChar = true; } break; // =>
			case L'|': if (ch2 == L'|') { twoType = etCONCAT_OP; isTwoChar = true; } break; // ||
			case L'*': if (ch2 == L'*') { twoType = etEXPONENT; isTwoChar = true; } break; // **
			case L'.': if (ch2 == L'.') { twoType = etRANGE; isTwoChar = true; } break; // ..
			}

			if (isTwoChar)
			{
				wstring val(str + i, 2);
				emit(twoType, line, i, 2, val);
				i += 2;
				continue;
			}
		}

		// ---- keyword or plain delimiter ----
		if (m_Delimiters[ch])
		{
			// single-character delimiter
			wstring val(1, ch);
			wstring upper(1, towupper(ch));

			// look up in token map
			auto it = m_tokenMap->find(upper);
			if (it != m_tokenMap->end())
			{
				EToken type = static_cast<EToken>(it->second);
				emit(type, line, i, 1, val);
			}
			else
			{
				// Map common punctuation to their enum values directly
				EToken type = etUNKNOWN;
				switch (ch) {
					case L',': type = etCOMMA;               break;
					case L'.': type = etDOT;                 break;
					case L';': type = etSEMICOLON;           break;
					case L'(': type = etLEFT_ROUND_BRACKET;  break;
					case L')': type = etRIGHT_ROUND_BRACKET; break;
					case L'=': type = etEQUAL;               break;
					case L'<': type = etLESS;                break;
					case L'>': type = etGREATER;             break;
					case L'+': type = etUNKNOWN;             break;
					case L'-': type = etMINUS;               break;
					case L'*': type = etSTAR;                break;
					case L'/': type = etSLASH;               break;
					case L'%': type = etPERCENT_SIGN;        break;
					case L'@': type = etAT_SIGN;             break;
					case L':': type = etCOLON;               break;
					default:   type = etUNKNOWN;             break;
				}
				emit(type, line, i, 1, val);
			}
			++i;
			continue;
		}

		// ---- word token (keyword or identifier) ----
		{
			int start = i;
			wstring val, upper;
			while (i < length && !m_Delimiters[str[i]] && !iswspace(str[i]))
			{
				val   += str[i];
				upper += towupper(str[i]);
				++i;
			}

			// look up reserved words
			int reserved = 0;
			auto resIt = m_reserved->find(upper);
			if (resIt != m_reserved->end())
				reserved = resIt->second;

			auto it = m_tokenMap->find(upper);
			EToken type = it != m_tokenMap->end()
				? static_cast<EToken>(it->second)
				: etUNKNOWN;

			// ---- Tier 2 SQL*Plus command words, applied only where the probe
			// established the context. Matching on `start` rather than on "the
			// first word seen" is what keeps this self-limiting: the override can
			// only ever land on the exact character range the probe recognised.
			if (start == leadCmdStart)
			{
				type         = leadCmdKind;
				leadCmdStart = -1;
			}
			else if (start == setWordStart)
			{
				setParamNext = true; // this word IS the SET; arm the next one
				setWordStart = -1;
			}
			else if (setParamNext)
			{
				setParamNext = false; // parameter position only, never further
				if (type == etUNKNOWN)
				{
					EToken k = sqlPlusCommandKind(str + start, i - start);
					if (k != etNONE)
						type = k;
				}
			}

			Token tk;
			tk.token    = type;
			tk.line     = line;
			tk.offset   = static_cast<Token::size_pos>(start);
			tk.length   = static_cast<Token::size_pos>(i - start);
			tk.reserved = reserved;
			tk.value    = val;
			m_analyzer->PutToken(tk);
			continue;
		}
	}

	// end of line
	emitEol(m_analyzer, line, length);
	return true;
}

}; // namespace PlsSql
}; // namespace Common
