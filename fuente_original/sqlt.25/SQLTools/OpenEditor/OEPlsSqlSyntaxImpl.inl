/* 
    Copyright (C) 2003,2005 Aleksey Kochetov

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

#include "OpenEditor/OEPlsSqlSyntax.h"

namespace OpenEditor
{ 
    template <class TokenArray>
    bool SyntaxNodeImpl<TokenArray>::FindToken (int line, int offset, const SyntaxNode*& node, int& index) const
    {
        if (m_tokens.size())
        {
            const Token& front = m_tokens.front();  
            const Token& back  = m_tokens.back();  
            TRACE("SyntaxNodeImpl::FindToken, Node=%s, line=%d, offset=%d\n", typeid(*this).name(), line, offset);
            TRACE("\tm_tokens.front().line=%d, m_tokens.front().offset=%d\n", front.line, front.offset);
            TRACE("\tm_tokens.back().line=%d, m_tokens.back().offset=%d\n", back.line, back.offset);

            if ((front.line < line 
                || (front.line == line && front.offset <= offset))
            && (back.line > line
                || (back.line == line && (back.offset + back.length) > offset)))
            {
                for (TokenArray::const_iterator it = m_tokens.begin();
                    it != m_tokens.end();
                    ++it)
                {
                    if (it->line == line && (it->offset <= offset && offset < it->offset + it->length))
                    {
                        node = this;
                        index = it - m_tokens.begin();
                        return true;
                    }
                }

                if (const SyntaxNode* child = GetChild())
                {
                    while (child && child->GetHitCode(line, 1) != ABOVE) // inverted to GetLineStatus
                    {
                        if (child->FindToken(line, offset, node, index))
                            return true;
                        child = child->GetSibling();
                    }
                }
            }
            // zero length closing token - EOS/EOF
            else if (!offset && !back.length 
            && line == back.line && offset == back.offset)
            {
                node = this;
                index = m_tokens.size()-1;
                return true;
            }
        }
        return false;
    }

    //TODO: close all parent construction where a child fails
    template <class TokenArray>
    void SyntaxNodeImpl<TokenArray>::GetLineStatus (LineStatusMap& statusMap, int& recursion) const
    {
//#ifdef _DEBUG
//    DbgPrintNode();
//#endif
        if (++recursion > RECURSION_LIMIT)
        {
            --recursion; // TODO: add a recursion controller class
            return;      // return silently because this method is called in background
        }

        int line = statusMap.GetBaseLine();
        int count = statusMap.GetLineCount();
        EHitCode hitCode = SyntaxNodeImpl<TokenArray>::GetHitCode(line, count);

        if (hitCode == HIT)
        {
            int begin = m_tokens.front().line;
            int end   = m_tokens.back().line;
            //TRACE("Name=%s, Line=%d, first=%d, last=%d\n",  typeid(*this).name(), line, begin,  end);

            if (typeid(*this) != typeid(ExpressionNode))
            {
                if (line <= begin && begin < line + count) 
                    statusMap[begin].beginCounter++;

                if (line <= end && end < line + count) 
                    statusMap[end].endCounter++;
                
                for (int i = max(begin, line), limit = min(end + 1, line + count); i < limit; i++)
                {
                    statusMap[i].level++;
                    //TRACE("i=%d, statusMap[i].level=%d\n",  i,  statusMap[i].level);
                    if (IsFailed() && GeFailureLine() <= i) 
                        statusMap[i].syntaxError = true;
                }
            }

            if (const SyntaxNode* child = GetChild())
            {
                while (child && child->GetHitCode(line, count) != ABOVE)//BELOW)
                {
                    child->GetLineStatus(statusMap, recursion);
                    child = child->GetSibling();
                }
            }
        }

        --recursion;
    }

    template <class TokenArray>
    void SyntaxNodeImpl<TokenArray>::GetToken (int index, int& line, int& offset, int& length) const
    {
        line   = m_tokens.at(index).line;
        offset = m_tokens.at(index).offset;
        length = m_tokens.at(index).length;
    }

    // always stop on the first and last token
    //   or on token with "stop sign"
    template <class TokenArray>
    void SyntaxNodeImpl<TokenArray>::GetNextToken (int index, int& line, int& offset, int& length, bool& backward) const
    {
        if (++index >= static_cast<int>(m_tokens.size()))
        {
            backward = true;
            index = 0;
        }
        else
        {
            backward = false;

            for (; index < static_cast<int>(m_tokens.size()); index++)
            {
                if (StopSign(m_tokens[index]))
                    break;
            }

            if (static_cast<int>(m_tokens.size()) == index)
                --index;
        }

        line   = m_tokens.at(index).line;
        offset = m_tokens.at(index).offset;
        length = m_tokens.at(index).length;
    }

    template <class TokenArray>
    void SyntaxNodeImpl<TokenArray>::Colapse (const Token& token)
    {
        if (!IsCompleted())
        {
            m_tokens.push_back(token);
            Failure(token.line);
        }
        //else
        //    ASSERT(0);
    }

    template <class TokenArray>
    bool SyntaxNodeImpl<TokenArray>::MatchesEndLabel (const Token& token, int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_tokens.size()))
            return false;

        return m_tokens.at(index).value == token.value;
    }

    // See SyntaxNode::RejectsEndLabel for why this exists at all.
    template <class TokenArray>
    bool SyntaxNodeImpl<TokenArray>::RejectsEndLabel (const Token& token) const
    {
        // Only an identifier can BE a label. Anything else arriving after a held
        // END - a `;`, LOOP, IF, CASE, RECORD, a compound trigger's BEFORE EACH
        // ROW - makes no claim about whose END this is, so it is never rejected and
        // both tokens reach this node exactly as they did before deferral existed.
        if (token.token != etUNKNOWN
        &&  token.token != etDOUBLE_QUOTED_STRING)
            return false;

        // No declared name captured, so nothing to compare against. Accepting is
        // the only safe answer: rejecting would pop a node that may well own this
        // END, and the analyzer has no way to put it back.
        if (m_endLabel1 < 0)
            return false;

        if (MatchesEndLabel(token, m_endLabel1))
            return false;

        // A schema-qualified declaration records both parts, and either one is
        // accepted: `END pkg;` for `CREATE PACKAGE BODY sch.pkg` is what people
        // write, and being lenient here can only ever under-reject, never steal a
        // terminator from a construct that really owns it.
        if (MatchesEndLabel(token, m_endLabel2))
            return false;

        return true;
    }

    // Index of this node's last own token at or before (line, offset), or -1 when the
    // position precedes all of them. Tokens are appended in buffer order.
    template <class TokenArray>
    int SyntaxNodeImpl<TokenArray>::OwnTokenIndexAtOrBefore (int line, int offset) const
    {
        int found = -1;

        for (int i = 0; i < static_cast<int>(m_tokens.size()); i++)
        {
            const Token& tk = m_tokens.at(i);

            if (tk.line > line || (tk.line == line && tk.offset > offset))
                break;

            found = i;
        }

        return found;
    }

    // See SyntaxNode::GetSectionAt. One implementation for every section-owning node
    // type, answered from the boundary indices NoteSectionStart recorded.
    template <class TokenArray>
    SyntaxNode::ESectionHit SyntaxNodeImpl<TokenArray>::GetSectionAt (int line, int offset) const
    {
        if (!OwnsSections())
            return shNone;

        // A compound trigger has no seedable section, and nothing nested inside one
        // may be reported either: FindSection relies on this to stop descending.
        if (BlocksSeedDescent())
            return shNone;

        const int idx = OwnTokenIndexAtOrBefore(line, offset);

        // Before this construct's first own token, so the position is not inside it.
        if (idx < 0)
            return shNone;

        // A caret ON a boundary keyword declines: BEGIN/EXCEPTION/END must never be
        // handed to the emitter that formats the section they bracket. A caret after
        // one - the normal case, sections start on the next line - is inside a section.
        const Token& tk = m_tokens.at(idx);
        const bool onBoundary =
               (idx == m_declStart || idx == m_bodyStart || idx == m_excStart || idx == m_endStart)
            && tk.line == line
            && offset >= tk.offset
            && offset <  tk.offset + (int)tk.length;

        if (onBoundary)
            return shNone;

        if (m_endStart  >= 0 && idx >= m_endStart)  return shNone;   // END, ';', label
        if (m_excStart  >= 0 && idx >= m_excStart)  return shException;
        if (m_bodyStart >= 0 && idx >= m_bodyStart) return shBody;
        if (m_declStart >= 0 && idx >= m_declStart) return DeclarationSectionKind();

        return shHeader;
    }

    template <class TokenArray>
    SyntaxNode::EHitCode SyntaxNodeImpl<TokenArray>::GetHitCode (int line, int count) const
    {
        EHitCode retVal = UNKNOWN; //, ABOVE, HIT, BELOW

        if (!m_tokens.empty())
        {
            int begin = m_tokens.front().line;
            int end   = m_tokens.back().line;

            if ( ((line <= begin) && (begin <= (line + count - 1)))
            || (begin <= line) && (line <= (end)))
                retVal = HIT;
            else if ((line + count - 1) < begin) // a window is above this statement
                retVal = ABOVE;
            else if (line > end) // a window is below this statement
                retVal = BELOW;
        }

        return retVal;
    }

    template <class TokenArray>
    void SyntaxNodeImpl<TokenArray>::DbgPrintNode () const
    {
	    std::string indent;
	    indent.append(GetLevel(), '>');

	    if (m_tokens.size())
            TRACE("%4d%s%s%s%4d\n", m_tokens.front().line + 1,  indent.c_str(), typeid(*this).name(), IsFailed() ? " FAILURE" : "", IsFailed() ? GeFailureLine() : -1);
	    else
		    TRACE("%4d%s%s%s%4d\n", -1, indent.c_str(), typeid(*this).name(), IsFailed() ? " FAILURE" : "", IsFailed() ? GeFailureLine() : -1);

	    if (const SyntaxNode* node = GetChild())
            node->DbgPrintNode();

	    indent.clear();
	    indent.append(GetLevel(), '<');

	    if (m_tokens.size())
		    TRACE("%4d%s%s\n", m_tokens.back().line + 1,  indent.c_str(), typeid(*this).name());
	    else
		    TRACE("%4d%s%s\n", -1, indent.c_str(), typeid(*this).name());

	    if (const SyntaxNode* node = GetSibling())
            node->DbgPrintNode();
    }

};
