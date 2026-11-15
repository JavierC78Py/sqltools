/* 
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2015 Aleksey Kochetov

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
    16.03.2003 bug fix, plsql match sometimes fails after some edit operations
    13.06.2014 bug fix, prompt with ' is not handled properly
*/

#include "stdafx.h"
#include <map>
#include "OpenEditor/OEHelpers.h"
#include <COMMON/ExceptionHelper.h>
#include <COMMON/StrHelpers.h>
#include "SQLWorksheet/PlsSqlParser.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


    using std::map;
	OpenEditor::DelimitersMap m_Delimiters(" \t\'\"\\()[]{}+-*/.,!?;:=><%|@&^");

SimplePlsSqlParser::SimplePlsSqlParser (SyntaxAnalyser* analyzer, TokenMapPtr tokenMap)
	: m_analyzer(analyzer),
    m_tokenMap(tokenMap),
    m_sequenceOfLength(0),
    m_qClose(0)
{
    m_sequenceOf = eNone;
    SetTokenMap(m_tokenMap);
}

// 16.03.2003 bug fix, plsql match sometimes fails after some edit operations
void SimplePlsSqlParser::Clear ()
{
    m_sequenceOf = eNone;
    m_qClose = 0;
}

void SimplePlsSqlParser::SetTokenMap (TokenMapPtr tokenMap)
{
    m_tokenMap = tokenMap;
}

void SimplePlsSqlParser::PutEOF (int line)
{
	Token token;
	token = etEOF;
    token.line = line;
    token.offset = 0;
	token.length = 0;
	m_analyzer->PutToken(token);
}

bool SimplePlsSqlParser::PutLine (int line, const char* str, int length)
{
    //TRACE("PutLine: %4d:%s\n", line+1, string(str, length).c_str());

    Token token;

	for (int offset = 0; offset < length; )
    {
        token = etUNKNOWN;
        token.line = line;
        token.offset = offset;
		token.length = 0;
        token.value.clear();

        string buffer;

        if (m_sequenceOf == eNone)
        {
            // skip white space
            for (; offset < length && isspace(str[offset]); offset++)
                {}

            // check EOL
            if (!(offset < length))
                break;

			token.offset = offset;
            // read string token
            if (m_Delimiters[str[offset]])
                buffer += str[offset++];
            else
                for (; offset < length && !m_Delimiters[str[offset]]; offset++)
                    buffer += toupper(str[offset]);

			token.length = buffer.length();
            token.value = buffer;

            // Oracle alternative quoting: q'X...X' and nq'X...X'. The word scan
            // above stops at the delimiter "'", so buffer holds the complete
            // upper-cased word and offset sits on that quote. Comparing the whole
            // word is what supplies the identifier boundary for free: "myq'[x]'"
            // accumulates as "MYQ", fails the test, and stays an identifier plus
            // an ordinary string.
            if ((buffer == "Q" || buffer == "NQ")
            && offset + 1 < length && str[offset] == '\'')
            {
                // whitespace or "'" cannot delimit a q-literal, so q_closer_for
                // returns 0 and this is an ordinary identifier after all
                char closer = Common::q_closer_for(str[offset + 1]);

                if (closer != 0)
                {
                    int prefixLen = buffer.length() + 2;   // q'X or nq'X

                    m_sequenceOf = eQQuotedString; // start literal accumulation
                    m_qClose = closer;
                    m_sequenceToken = etQUOTED_STRING;
                    m_sequenceToken.line = token.line;
                    m_sequenceToken.offset = token.offset;
                    m_sequenceToken.length = prefixLen;
                    m_sequenceToken.value.assign(str + token.offset, prefixLen);
                    m_sequenceOfLength = prefixLen;

                    offset += 2;      // past the opening quote and the delimiter
                    continue;         // the literal body is consumed below
                }
            }
        }
        else
        {
            switch (m_sequenceOf)
            {
            case eEndLineComment:
                m_sequenceOfLength += length - offset;
                offset = length;
                m_sequenceOf = eNone;
				token = m_sequenceToken;
                break;
            case eQuotedString:
                for (; offset < length; offset++)
                {
                    m_sequenceOfLength++;
                    m_sequenceToken.value += str[offset];
                    if (str[offset] == '\'')
                    {
                        offset++;
                        m_sequenceOf = eNone;
						token = m_sequenceToken;
                        break;
                    }
                }
                break;
            case eQQuotedString:
                // Inside q'X...X' every character is literal text. There is no ''
                // escape, and ';', '/', '--', '/*' and brackets carry no meaning,
                // so none of them may reach the token stream and split a
                // statement. The literal ends at m_qClose followed immediately by
                // the closing quote; an unterminated one runs to EOF exactly like
                // an unterminated ordinary string.
                for (; offset < length; offset++)
                {
                    m_sequenceOfLength++;
                    m_sequenceToken.value += str[offset];
                    if (str[offset] == m_qClose
                    && offset + 1 < length && str[offset + 1] == '\'')
                    {
                        offset++;                        // onto the closing quote
                        m_sequenceOfLength++;
                        m_sequenceToken.value += str[offset];
                        offset++;                        // past it, as the cases above
                        m_sequenceOf = eNone;            // end of the q-literal
                        token = m_sequenceToken;
                        break;
                    }
                }
                break;

            case eDblQuotedString:
                for (; offset < length; offset++)
                {
                    m_sequenceOfLength++;
                    m_sequenceToken.value += str[offset];
                    if (str[offset] == '\"')
                    {
                        offset++;
                        m_sequenceOf = eNone;
						token = m_sequenceToken;
                        break;
                    }
                }
                break;
            case eComment:
                for (; offset < length; offset++)
                {
                    m_sequenceOfLength++;
                    if (offset && str[offset-1] == '*' && str[offset] == '/')
                    {
                        offset++;
                        m_sequenceOf = eNone;
						token = m_sequenceToken;
                        break;
                    }
                }
                break;
            }

            // sequence end
            if (m_sequenceOf == eNone)
            {
                token.length = m_sequenceOfLength;
                m_sequenceOfLength = 0;
            }
        }


        if (token == etUNKNOWN && !buffer.empty())
        {
            std::map<string, int>::const_iterator it = m_tokenMap->find(buffer);

            if (it != m_tokenMap->end())
            {
				token = (EToken)it->second;

                switch (token)
                {
                case etQUOTE:
                    m_sequenceOf = eQuotedString; // start string accumulation
					m_sequenceToken = etQUOTED_STRING;
					m_sequenceToken.line = token.line;
					m_sequenceToken.offset = token.offset;
					m_sequenceToken.length = token.length;
                    m_sequenceToken.value = "'";
                    m_sequenceOfLength = 1;
                    break;
                case etDOUBLE_QUOTE:
                    m_sequenceOf = eDblQuotedString; // start string accumulation
					m_sequenceToken = etDOUBLE_QUOTED_STRING;
					m_sequenceToken.line = token.line;
					m_sequenceToken.offset = token.offset;
					m_sequenceToken.length = token.length;
                    m_sequenceToken.value = "\"";
                    m_sequenceOfLength = 1;
                    break;
                case etMINUS:
                    if (offset < length && str[offset] == '-')
                    {
                        m_sequenceOf = eEndLineComment; // skip line remainer
						m_sequenceToken = etCOMMENT;
						m_sequenceToken.line = token.line;
						m_sequenceToken.offset = token.offset;
						m_sequenceToken.length = token.length;
                        m_sequenceToken.value.clear();
                        m_sequenceOfLength = 2;
                        offset++;
                    }
                    break;
                case etSLASH:
                    if (offset < length && str[offset] == '*')
                    {
                        m_sequenceOf = eComment; // skip comment
						m_sequenceToken = etCOMMENT;
						m_sequenceToken.line = token.line;
						m_sequenceToken.offset = token.offset;
						m_sequenceToken.length = token.length;
                        m_sequenceToken.value.clear();
                        m_sequenceOfLength = 2;
                        offset++;
                    }
                    break;
                }
            }
        }

        if (m_sequenceOf == eNone)
        {
            /*
            1234/67    /
            length=7   1
            offset=4   0
            seg(0,4)   0,0
            seg(5,2)   1,0
            */
            if (token == etSLASH 
            && Common::is_blank_line(str,  token.offset)
            && Common::is_blank_line(str + token.offset + token.length, length - token.offset - token.length))
                token = etEOS;
			m_analyzer->PutToken(token);
        }
    }

    if (m_sequenceOf == eEndLineComment)
        m_sequenceOf = eNone;
	
//    if (m_sequenceOf == eNone) 13.06.2014 bug fix, prompt with ' is not handled properly
    {
        token = etEOL;
	    token.line = line;
	    token.offset = length;
	    m_sequenceToken.length = 0;
	    m_analyzer->PutToken(token);
    }

    return true;
}
