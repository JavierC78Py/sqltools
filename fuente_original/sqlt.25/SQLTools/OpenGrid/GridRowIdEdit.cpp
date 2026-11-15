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

#include "GridRowIdEdit.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace OG2
{

namespace
{

bool SameNoCase (const std::string& s, const char* word)
{
    const size_t n = strlen(word);
    if (s.size() != n)
        return false;

    for (size_t i = 0; i < n; ++i)
        if (toupper(static_cast<unsigned char>(s[i])) != toupper(static_cast<unsigned char>(word[i])))
            return false;

    return true;
}

bool IsNameChar (char c)
{
    return isalnum(static_cast<unsigned char>(c)) != 0
        || c == '_' || c == '$' || c == '#'
        || static_cast<unsigned char>(c) >= 0x80;
}

// After the last unquoted dot, unquoted: S.T, "S"."T" and s.t all give T.
std::string TrailingName (const std::string& s)
{
    size_t start = 0;
    bool inQuotes = false;

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '"')
            inQuotes = !inQuotes;
        else if (s[i] == '.' && !inQuotes)
            start = i + 1;
    }

    std::string name = s.substr(start);
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);

    return name;
}

struct Token
{
    std::string text;   // verbatim, quotes included
    bool quoted;        // a "..." identifier
    bool literal;       // a '...' string
    bool word;          // a bare identifier, keyword or number

    Token () : quoted(false), literal(false), word(false) {}
};

// False on anything unterminated, so a malformed query refuses rather than half-parses.
bool Tokenize (const std::string& sql, std::vector<Token>& out)
{
    const size_t n = sql.size();
    size_t i = 0;

    while (i < n)
    {
        const char c = sql[i];

        if (isspace(static_cast<unsigned char>(c)))
        {
            ++i;
            continue;
        }

        if (c == '-' && i + 1 < n && sql[i + 1] == '-')
        {
            while (i < n && sql[i] != '\n')
                ++i;
            continue;
        }

        if (c == '/' && i + 1 < n && sql[i + 1] == '*')
        {
            const size_t end = sql.find("*/", i + 2);
            if (end == std::string::npos)
                return false;
            i = end + 2;
            continue;
        }

        if (c == '\'' || c == '"')
        {
            Token t;
            t.quoted = (c == '"');
            t.literal = (c == '\'');
            t.text = c;

            size_t j = i + 1;
            for (;;)
            {
                if (j >= n)
                    return false;

                t.text += sql[j];

                if (sql[j] == c)
                {
                    if (j + 1 < n && sql[j + 1] == c)     // '' and "" are an escaped quote
                    {
                        t.text += c;
                        j += 2;
                        continue;
                    }
                    ++j;
                    break;
                }
                ++j;
            }

            out.push_back(t);
            i = j;
            continue;
        }

        if (IsNameChar(c))
        {
            size_t j = i;
            while (j < n && IsNameChar(sql[j]))
                ++j;

            Token t;
            t.word = true;
            t.text = sql.substr(i, j - i);
            out.push_back(t);
            i = j;
            continue;
        }

        Token t;
        t.text = std::string(1, c);
        out.push_back(t);
        ++i;
    }

    return true;
}

bool IsKeyword (const Token& t, const char* word)
{
    return t.word && SameNoCase(t.text, word);
}

bool IsAnyKeyword (const Token& t, const char* const* words, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (IsKeyword(t, words[i]))
            return true;
    return false;
}

// Where a FROM clause can legally stop. Deliberately not a comma and not a JOIN keyword: those are
// answered by falling through to the refusal below, not by being listed here.
const char* const g_clauseEnd[] =
{
    "WHERE", "GROUP", "ORDER", "HAVING", "CONNECT", "START", "FETCH", "FOR", "OFFSET",
    "MINUS", "UNION", "INTERSECT", "SAMPLE", "PARTITION"
};

bool IsClauseEnd (const Token& t)
{
    if (IsAnyKeyword(t, g_clauseEnd, sizeof(g_clauseEnd) / sizeof(g_clauseEnd[0])))
        return true;

    return (!t.word && !t.quoted && !t.literal) && (t.text == ")" || t.text == ";");
}

const char* const g_setOp[] = { "UNION", "MINUS", "INTERSECT" };

}

int DetectRowIdColumn (const std::vector<std::string>& rawHeaders)
{
    int found = ercNone;

    for (size_t i = 0; i < rawHeaders.size(); ++i)
    {
        if (!SameNoCase(TrailingName(rawHeaders[i]), "ROWID"))
            continue;

        if (found != ercNone)
            return ercAmbiguous;

        found = static_cast<int>(i);
    }

    return found;
}

std::string ResolveSingleTable (const std::string& sql)
{
    std::vector<Token> tok;
    if (!Tokenize(sql, tok) || tok.empty())
        return std::string();

    if (!IsKeyword(tok[0], "SELECT"))
        return std::string();

    // One pass for the three things that decide it: the first FROM outside any parenthesis, balanced
    // parentheses, and no set operator - a UNION row could have come from either side of it.
    size_t from = tok.size();
    int depth = 0;
    bool balanced = true;

    for (size_t i = 1; i < tok.size(); ++i)
    {
        const Token& t = tok[i];

        if (!t.word && !t.quoted && !t.literal)
        {
            if (t.text == "(") { ++depth; continue; }
            if (t.text == ")") { if (--depth < 0) balanced = false; continue; }
        }

        if (depth != 0)
            continue;

        if (IsAnyKeyword(t, g_setOp, sizeof(g_setOp) / sizeof(g_setOp[0])))
            return std::string();

        if (from == tok.size() && IsKeyword(t, "FROM"))
            from = i;
    }

    if (!balanced || depth != 0 || from == tok.size())
        return std::string();

    size_t i = from + 1;
    if (i >= tok.size() || (!tok[i].word && !tok[i].quoted))
        return std::string();

    std::string table = tok[i].text;
    ++i;

    while (i + 1 < tok.size() && !tok[i].word && !tok[i].quoted && !tok[i].literal && tok[i].text == "."
        && (tok[i + 1].word || tok[i + 1].quoted))
    {
        table += ".";
        table += tok[i + 1].text;
        i += 2;
    }

    if (i < tok.size() && IsKeyword(tok[i], "AS"))
    {
        ++i;
        if (i >= tok.size() || (!tok[i].word && !tok[i].quoted))
            return std::string();
        ++i;
    }
    else if (i < tok.size() && (tok[i].word || tok[i].quoted) && !IsClauseEnd(tok[i]))
    {
        ++i;        // a bare alias
    }

    // Past the alias only a clause end or the end of the query is acceptable, so a comma and a JOIN
    // both land here and are refused - which is what stops a two-table query writing to the first one.
    if (i < tok.size() && !IsClauseEnd(tok[i]))
        return std::string();

    return table;
}

namespace {

// A format model reaches the server as a string literal, so a quote inside it has to be doubled or it
// would close the literal early and leave the rest of the picture as SQL for the server to parse.
std::string QuoteLiteral (const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out += '\'';

    for (size_t i = 0; i < text.size(); ++i)
    {
        if ('\'' == text[i])
            out += '\'';
        out += text[i];
    }

    return out + '\'';
}

}

std::string BuildValueExpr (const std::string& bind, const std::string& func,
                            const std::string& picture, const std::string& nlsParam)
{
    // No function named means no conversion wanted, and an empty bind has nothing to wrap. Either way
    // the caller gets the bind back exactly as it stands.
    if (bind.empty() || func.empty())
        return bind;

    // An empty picture still wraps: besides naming the format model, the wrapper gives the SQLT_LVC bind
    // a character argument position, Oracle not coercing one into NUMBER/DATE/INTERVAL on its own.
    if (picture.empty())
        return func + "(" + bind + ")";

    std::string expr = func + "(" + bind + ", " + QuoteLiteral(picture);

    if (!nlsParam.empty())
        expr += ", " + QuoteLiteral(nlsParam);

    return expr + ")";
}

std::string BuildUpdateSql (const std::string& table, const std::string& column, bool returningRowId,
                            const std::string& valueExpr)
{
    if (table.empty() || column.empty())
        return std::string();

    std::string sql = "UPDATE " + table + " SET " + column + " = "
                    + (valueExpr.empty() ? std::string(":newval") : valueExpr)
                    + " WHERE ROWID = CHARTOROWID(:rid)";

    if (returningRowId)
        sql += " RETURNING ROWIDTOCHAR(ROWID) INTO :newrid";

    return sql;
}

std::string BuildDeleteSql (const std::string& table)
{
    if (table.empty())
        return std::string();

    return "DELETE FROM " + table + " WHERE ROWID = CHARTOROWID(:rid)";
}

std::string BuildLockSql (const std::string& table)
{
    if (table.empty())
        return std::string();

    // Addressed by CHARTOROWID(:rid) for the same reason the writes are: the grid holds the ROWID as
    // display text. NOWAIT keeps a locked row an instant answer, not a wait on the shared connection.
    return "SELECT ROWID FROM " + table + " WHERE ROWID = CHARTOROWID(:rid) FOR UPDATE NOWAIT";
}

std::string BuildInsertSql (const std::string& table, const std::vector<std::string>& columns,
                            const std::vector<std::string>& valueExprs)
{
    if (table.empty() || columns.empty())
        return std::string();

    std::string names;
    std::string values;

    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (columns[i].empty())
            return std::string();

        if (i)
        {
            names += ", ";
            values += ", ";
        }

        names += columns[i];

        // The bind name stays the same either way, which is what lets the caller bind by name without
        // knowing whether a conversion ended up wrapped around it.
        values += (i < valueExprs.size() && !valueExprs[i].empty())
                ? valueExprs[i]
                : ":v" + std::to_string(i + 1);
    }

    return "INSERT INTO " + table + " (" + names + ") VALUES (" + values
        + ") RETURNING ROWIDTOCHAR(ROWID) INTO :newrid";
}

std::vector<int> SelectInsertColumns (const std::vector<std::string>& rawHeaders,
                                      int rowIdColumn,
                                      const std::vector<std::string>& staged)
{
    std::vector<int> cols;

    // Indexed by table column, and `staged` may be shorter than the header list - a row staged
    // before every column was known - so the walk stops at whichever ran out first.
    const size_t n = std::min(rawHeaders.size(), staged.size());

    for (size_t c = 0; c < n; ++c)
    {
        // Blank is "the user never filled this in", which is left out rather than bound as NULL so
        // the column's own DEFAULT still applies. Writing NULL here would silently override it.
        if (staged[c].empty())
            continue;

        // The address comes from the server, so it is never a column an INSERT may name.
        if (static_cast<int>(c) == rowIdColumn)
            continue;

        // The same test SET applies: an unaliased expression, a qualified name or ROWID has no place here.
        if (!IsUpdatableColumnName(rawHeaders[c]))
            continue;

        cols.push_back(static_cast<int>(c));
    }

    return cols;
}

bool IsUpdatableColumnName (const std::string& rawHeader)
{
    if (rawHeader.empty())
        return false;

    std::string name;

    if ('"' == rawHeader.front())
    {
        // Quoted, so the bytes between the quotes are the whole name. A doubled quote is Oracle's
        // escape rather than a terminator, and is refused: a name that unusual is not worth guessing.
        if (rawHeader.size() < 3 || '"' != rawHeader.back())
            return false;

        name = rawHeader.substr(1, rawHeader.size() - 2);

        if (name.empty() || std::string::npos != name.find('"'))
            return false;
    }
    else
    {
        // Bare, so it must be exactly one identifier. IsNameChar admits no dot, space, parenthesis
        // or operator, which turns `UPPER(x)`, `a b` and `t.COL` all into refusals.
        if (isdigit(static_cast<unsigned char>(rawHeader.front())))
            return false;

        for (size_t i = 0; i < rawHeader.size(); ++i)
            if (!IsNameChar(rawHeader[i]))
                return false;

        name = rawHeader;
    }

    // ROWID is the address this module updates by, so it is never a column SET may target - and the
    // grid's ROWID column is not data the user should be able to retype.
    return !SameNoCase(name, "ROWID");
}

}

