/* 
    Copyright (C) 2002-2015 Aleksey Kochetov

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
    06.03.2003 bug fix, stack overflow on closing a big pl/sql file with syntax errors
    03.06.2003 bug fix, sql find match fails on select/insert/... if there is no ending ';'
    2011.09.19 bug fix, PL/SQL Analyzer fails on auth/wrapped packages
    2011.09.21 bug fix, PL/SQl Analyzer fails on grant/revoke 
*/

#include "stdafx.h"
#include <map>
#include <COMMON/AppGlobal.h>
#include <COMMON/ExceptionHelper.h>
#include "OpenEditor/OEPlsSqlSyntaxImpl.h"
#include "OpenEditor/OEPlsSqlSyntaxImpl.inl"
#include "OpenEditor/OEPlsSqlSyntaxImpl2.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define SET_AND_RET(token, TK, stage) \
        if (token == TK) { \
            append(stage, token); \
            return; \
        }
#define SET_STATE_AND_RET(token, TK, stage) \
        if (token == TK) { \
            m_state = stage; \
            return; \
        }


namespace OpenEditor
{

    // weird - I really don't like this unregular code
    // but don't see a good solution
    static
    void close_driver_node  (SyntaxNode* node, const Token& token) 
    {
        if (node)
        {    
            std::unique_ptr<SyntaxNode> dummy;

            if (SyntaxNode* parent = node->GetParent())
            {
                const type_info& pt = typeid(*parent);

                if (pt == typeid(ExpressionNode)
                || pt == typeid(SelectNode)    
                || pt == typeid(InsertNode)    
                || pt == typeid(UpdateNode)    
                || pt == typeid(DeleteNode)    
                || pt == typeid(MergeNode)
                || pt == typeid(OpenCursorNode)
                || pt == typeid(GenericSqlNode))
                    parent->PutToken(token, dummy);
            }
        }
    }

ScriptNode::~ScriptNode ()
{
    try { EXCEPTION_FRAME;

        Clear();
    }
    _DESTRUCTOR_HANDLER_;
}

void ScriptNode::Clear ()
{
    _ASSERTE(!GetSibling());
   clear();
}

void ScriptNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child) 
{ 
    switch (token)
    {
    case etCOMMENT:
        return;
    case etEOS:
    case etEOF:
    case etSEMICOLON:
        if (!m_tokens.empty())
            ScriptFactory::Create(m_tokens, child);
        break;
    default:
        if (m_tokens.size() == m_tokens.max_size())
            m_tokens.erase(m_tokens.begin());
        m_tokens.push_back(token);
        ScriptFactory::RecognizeAndCreate(m_tokens, child);
        break;
    }

    if (child.get())
        m_tokens.clear();
};

bool ScriptNode::FindToken (int line, int offset, const SyntaxNode*& node, int& index) const
{
    // don't check recursion here because this implementation is called once 
    if (const SyntaxNode* child = GetChild())
    {
        while (child)
        {
            EHitCode hc = child->GetHitCode(line, 1);
            if (hc == HIT)
            {
                if (child->FindToken(line, offset, node, index))
                    return true;
            }
            else if (hc == ABOVE) // a window is above this statement
                break;
            // else if BELOW, just skip
            child = child->GetSibling();
        }
    }

    return false;
}

void ScriptNode::GetLineStatus (LineStatusMap& statusMap, int& recursion) const
{
    // don't check recursion here because this implementation is called once 
    if (const SyntaxNode* child = GetChild())
    {
        while (child)
        {
            EHitCode hc = child->GetHitCode(statusMap.GetBaseLine(), statusMap.GetLineCount());
            if (hc == HIT)
                child->GetLineStatus(statusMap, recursion);
            else if (hc == ABOVE) // a window is above this statement
                break;
            // else if BELOW, just skip
            child = child->GetSibling();
        }
    }
}

void BlockNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etDECLARE, eDeclare);
        SET_AND_RET(token, etBEGIN, eBegin);
        Failure(token.line);
        break;
    case eDeclare:
        SET_AND_RET(token, etBEGIN, eBegin);
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);
        // let's skip some tokens between end and ;
        // it is needed for compound trigger parsing
        //Failure(token.line);
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

// 2011.09.19 bug fix, PL/SQL Analyzer fails on auth/wrapped packages
void PackageNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        SET_AND_RET(token, etPACKAGE, ePackage); 
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_AND_RET(token, etPACKAGE, ePackage); 
        if (token.token == etUNKNOWN 
            && (token.value == L"EDITIONABLE" || token.value == L"NONEDITIONABLE"))
            break;
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etPACKAGE, ePackage); 
        if (token.token == etUNKNOWN 
        && (token.value == L"EDITIONABLE" || token.value == L"NONEDITIONABLE"))
            break;
        Failure(token.line);
        break;
    case ePackage:
        SET_AND_RET(token, etBODY, eBody);
        if (!token.reserved)
        {
            append(eName1, token);
            return;
        }
        Failure(token.line);
        break;
    case eBody:
        SET_STATE_AND_RET(token, etBODY, eBody);
        if (!token.reserved)
        {
            append(eName1, token);
            return;
        }
        Failure(token.line);
        break;
    case eName1:
        SET_AND_RET(token, etDOT, eNameSeparator);
        SET_STATE_AND_RET(token, etWRAPPED, eWrapped); 
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1); 
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        if (token.token == etUNKNOWN && token.value == L"ACCESSIBLE") {
            m_state = eAccessible;
            break;
        }
        Failure(token.line);
        break;
    case eNameSeparator:
        if (!token.reserved)
        {
            append(eName1, token);
            return;
        }
        break;
    case eName2:
        SET_STATE_AND_RET(token, etWRAPPED, eWrapped); 
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1); 
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        if (token.token == etUNKNOWN && token.value == L"ACCESSIBLE") {
            m_state = eAccessible;
            break;
        }
        Failure(token.line);
        break;
    case eAuthid1:
        SET_AND_RET(token, etCURRENT_USER, eAuthid2);
        SET_AND_RET(token, etDEFINER, eAuthid2);
        Failure(token.line);
        break;
    case eAuthid2:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        if (token.token == etUNKNOWN && token.value == L"ACCESSIBLE") {
            m_state = eAccessible;
            break;
        }
        Failure(token.line);
        break;
    case eAccessible:
        // skip until IS/AS or AUTHID
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1); 
        break;
    case eIs:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etEND, eEnd); // if it's not body, BEGIN is optional
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the package name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the package name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    case eWrapped:
        SET_AND_RET(token, etEOS, eComplete);   
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

EOutlineItemType PackageNode::GetOutlineItemType () const
{
    auto& tokens = GetTokens();
    for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
        if (tokens[i].token == etBODY)
            return oitPackageBody;
    return oitPackage;
}

void TypeBodyNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        SET_AND_RET(token, etTYPE, eType); 
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_AND_RET(token, etTYPE, eType); 
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etTYPE, eType); 
        Failure(token.line);
        break;
    case eType:
        SET_STATE_AND_RET(token, etBODY, eBody); 
        SET_AND_RET(token, etUNKNOWN, eName1); 
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName1); 
        Failure(token.line);
        break;
    case eBody:
        SET_AND_RET(token, etUNKNOWN, eName1); 
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName1); 
        Failure(token.line);
        break;
    case eName1:
        SET_AND_RET(token, etDOT, eNameSeparator);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        Failure(token.line);
        break;
    case eNameSeparator:
        SET_AND_RET(token, etUNKNOWN, eName2); 
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName2); 
        break;
    case eName2:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        Failure(token.line);
        break;
    case eIs:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etEND, eEnd); // if it's not body, BEGIN is optional
        // Member subprogram introducer lookahead: "MEMBER", "STATIC",
        // "CONSTRUCTOR", "MAP MEMBER", "ORDER MEMBER" - all followed by
        // PROCEDURE/FUNCTION. Only the first lead token and the PROCEDURE/
        // FUNCTION token are forwarded to the spawned TypeBodyMemberNode so
        // navigation/selection includes the prefix.
        if (token.token == etUNKNOWN
            && (!_wcsicmp(token.value.c_str(), L"MEMBER")
                || !_wcsicmp(token.value.c_str(), L"STATIC")
                || !_wcsicmp(token.value.c_str(), L"CONSTRUCTOR")))
        {
            m_memberLeadToken = token;
            m_state = eMemberLeadReady;
            return;
        }
        if (token.token == etUNKNOWN
            && (!_wcsicmp(token.value.c_str(), L"MAP")
                || !_wcsicmp(token.value.c_str(), L"OVERRIDING")))
        {
            m_memberLeadToken = token;
            m_state = eMemberLeadNeedMember;
            return;
        }
        if (token == etORDER)
        {
            m_memberLeadToken = token;
            m_state = eMemberLeadNeedMember;
            return;
        }
        if (token == etPROCEDURE || token == etFUNCTION)
        {
            // no lead - spawn a member node and re-deliver the introducer token
            child.reset(new TypeBodyMemberNode);
            std::unique_ptr<SyntaxNode> dummy;
            child->PutToken(token, dummy);
            return;
        }
        break;
    case eMemberLeadNeedMember:
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"MEMBER"))
        {
            m_state = eMemberLeadReady;
            return;
        }
        // "MAP"/"ORDER" here wasn't part of a MEMBER lead - reset and re-scan
        m_state = eIs;
        PutToken(token, child);
        return;
    case eMemberLeadReady:
        if (token == etPROCEDURE || token == etFUNCTION)
        {
            child.reset(new TypeBodyMemberNode);
            std::unique_ptr<SyntaxNode> dummy;
            child->PutToken(m_memberLeadToken, dummy);
            child->PutToken(token, dummy);
            m_state = eIs;
            return;
        }
        // unexpected token after a recognized lead - reset and re-scan
        m_state = eIs;
        PutToken(token, child);
        return;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the package name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the package name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

// One MEMBER/STATIC/MAP MEMBER/ORDER MEMBER/CONSTRUCTOR subprogram body
// inside a TypeBodyNode. Mirrors ProcedureNode/FunctionNode body parsing
// (name/arguments/[RETURN]/AUTHID/IS-AS/BEGIN/EXCEPTION/END/LANGUAGE) but
// also owns an optional lead introducer token so it's included in
// navigation/selection.
void TypeBodyMemberNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)
{
    switch (m_state)
    {
    case eUndef:
        // TypeBodyNode already validated the full lead sequence; it forwards
        // either just the PROCEDURE/FUNCTION token (no lead), or the first
        // lead token followed by the PROCEDURE/FUNCTION token.
        if (token == etPROCEDURE) { m_isFunction = false; append(eProcOrFunc, token); return; }
        if (token == etFUNCTION)  { m_isFunction = true;  append(eProcOrFunc, token); return; }
        append(eLead, token);
        return;
    case eLead:
        if (token == etPROCEDURE) { m_isFunction = false; append(eProcOrFunc, token); return; }
        if (token == etFUNCTION)  { m_isFunction = true;  append(eProcOrFunc, token); return; }
        Failure(token.line);
        break;
    case eProcOrFunc:
        if (!token.reserved)
        {
            append(eName1, token);
            return;
        }
        Failure(token.line);
        break;
    case eName1:
        if (token == etLEFT_ROUND_BRACKET) {
            m_state = eArguments;
            break; // it's parsed as an expression!!!
        }
        SET_AND_RET(token, etSEMICOLON, eComplete);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        if (m_isFunction)
            SET_STATE_AND_RET(token, etRETURN, eReturn);
        Failure(token.line);
        break;
    case eArguments:
        SET_AND_RET(token, etSEMICOLON, eComplete);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        if (m_isFunction)
            SET_STATE_AND_RET(token, etRETURN, eReturn);
        Failure(token.line); // ???
        break;
    case eReturn:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        return; // skip anything else - assume that's the return type
    case eIs:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etLANGUAGE, eLanguage);
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the member name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the member name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    case eLanguage:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

    struct CommitOnExit {
        Token& _prevToken;
        const Token& _token;
        CommitOnExit (Token& prevToken, const Token& token)
            : _prevToken(prevToken), _token(token) 
        {}
        ~CommitOnExit () 
        { _prevToken = _token; }
    };
// One MEMBER/STATIC/MAP MEMBER/ORDER MEMBER/CONSTRUCTOR subprogram spec.
// Only used inside a TypeNode member list, so it never sees a real BEGIN/END
// block - the spec is just a header, no body.
void TypeMemberNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)
{
    CommitOnExit commit(m_prevToken, token);

    // Don't consume the enclosing member-list's closing paren - complete
    // with a synthetic empty token and re-deliver the real one to the
    // parent TypeNode (same pattern SelectNode uses for closing an inner
    // query's parenthesis).
    auto completeAndRedeliverToParent = [this, &token, &child] ()
    {
        //Token tk;
        //tk = etUNKNOWN;
        //tk.line = token.line;
        //tk.offset = token.offset;
        append(eComplete, m_prevToken);
        GetParent()->PutToken(token, child); // weird
    };

    switch (m_state)
    {
    case eUndef:
        // TypeNode already validated the full lead sequence; it forwards
        // either just the PROCEDURE/FUNCTION token (no lead), or the first
        // lead token followed by the PROCEDURE/FUNCTION token.
        if (token == etPROCEDURE) { m_isFunction = false; append(eProcOrFunc, token); return; }
        if (token == etFUNCTION)  { m_isFunction = true;  append(eProcOrFunc, token); return; }
        append(eLead, token);
        return;
    case eLead:
        if (token == etPROCEDURE) { m_isFunction = false; append(eProcOrFunc, token); return; }
        if (token == etFUNCTION)  { m_isFunction = true;  append(eProcOrFunc, token); return; }
        Failure(token.line);
        break;
    case eProcOrFunc:
        if (!token.reserved)
        {
            append(eName, token);
            return;
        }
        Failure(token.line);
        break;
    case eName:
        if (token == etLEFT_ROUND_BRACKET)
        {
            m_parenDepth = 1;
            m_state = eArguments;
            break;
        }
        SET_AND_RET(token, etRETURN, eReturn);
        if (token == etCOMMA)
        {
            append(eComplete, m_prevToken);
            return;
        }
        if (token == etRIGHT_ROUND_BRACKET)
        {
            completeAndRedeliverToParent();
            return;
        }
        Failure(token.line);
        break;
    case eArguments:
        if (token == etLEFT_ROUND_BRACKET)
            m_parenDepth++;
        else if (token == etRIGHT_ROUND_BRACKET)
        {
            m_parenDepth--;
            if (m_parenDepth == 0)
            {
                m_state = eName; // fall through to look for RETURN/,/)
                break;
            }
        }
        break;
    case eReturn:
        // return type name, plus optional trailing clauses that may contain
        // their own nested parens, e.g. PIPELINED PARALLEL_ENABLE
        // (PARTITION p_cursor BY ANY) - track depth so an inner ')' doesn't
        // get mistaken for the member list's closing paren.
        if (token == etLEFT_ROUND_BRACKET)
        {
            m_parenDepth++;
            break;
        }
        if (m_parenDepth > 0)
        {
            if (token == etRIGHT_ROUND_BRACKET)
                m_parenDepth--;
            break;
        }
        if (token == etCOMMA)
        {
            append(eComplete, m_prevToken);
            return;
        }
        if (token == etRIGHT_ROUND_BRACKET)
        {
            completeAndRedeliverToParent();
            return;
        }
        break;
    }
}

void TypeNode::ResetMemberLead ()
{
    m_memberLead = mlNone;
    m_memberLeadToken = Token();
}

// Handles tokens once we're scanning the "( attribute_list, member_spec... )"
// member list of a CREATE TYPE ... AS OBJECT. Plain attribute declarations
// are skipped (no navigation value); member/static/constructor specs are
// spawned as real TypeMemberNode children so they keep keyboard navigation.
// Only the first lead token and the PROCEDURE/FUNCTION token are forwarded
// to the spawned child - intermediate lead keywords are not stored.
void TypeNode::HandleMemberListToken (const Token& token, std::unique_ptr<SyntaxNode>& child)
{
    if (token == etCOMMA)
    {
        ResetMemberLead();
        return;
    }

    switch (m_memberLead)
    {
    case mlNone:
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"MEMBER"))
        {
            m_memberLead = mlReady;
            m_memberLeadToken = token;
            return;
        }
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"STATIC"))
        {
            m_memberLead = mlReady;
            m_memberLeadToken = token;
            return;
        }
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"CONSTRUCTOR"))
        {
            m_memberLead = mlReady;
            m_memberLeadToken = token;
            return;
        }
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"MAP"))
        {
            m_memberLead = mlMap;
            m_memberLeadToken = token;
            return;
        }
        if (token == etORDER)
        {
            m_memberLead = mlOrder;
            m_memberLeadToken = token;
            return;
        }
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"NOT"))
        {
            m_memberLead = mlNot;
            m_memberLeadToken = token;
            return;
        }
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"OVERRIDING"))
        {
            m_memberLead = mlOverriding;
            m_memberLeadToken = token;
            return;
        }
        if (token == etPROCEDURE || token == etFUNCTION)
        {
            // no lead - spawn a member node and re-deliver the introducer token
            child.reset(new TypeMemberNode);
            std::unique_ptr<SyntaxNode> dummy;
            child->PutToken(token, dummy);
            return;
        }
        // skip attribute declarations (name/type/etc.) token by token
        return;
    case mlNot:
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"OVERRIDING"))
        {
            m_memberLead = mlOverriding;
            // keep the first lead token (NOT), don't overwrite with OVERRIDING
            return;
        }
        // "NOT" here wasn't part of an OVERRIDING lead (e.g. attribute decl) - reset
        ResetMemberLead();
        return HandleMemberListToken(token, child);
    case mlOverriding:
    case mlMap:
    case mlOrder:
        if (token.token == etUNKNOWN && !_wcsicmp(token.value.c_str(), L"MEMBER"))
        {
            m_memberLead = mlReady;
            return;
        }
        ResetMemberLead();
        return HandleMemberListToken(token, child);
    case mlReady:
        if (token == etPROCEDURE || token == etFUNCTION)
        {
            child.reset(new TypeMemberNode);
            std::unique_ptr<SyntaxNode> dummy;
            child->PutToken(m_memberLeadToken, dummy);
            child->PutToken(token, dummy);
            ResetMemberLead();
            return;
        }
        // unexpected token after a recognized lead - reset and re-scan
        ResetMemberLead();
        return HandleMemberListToken(token, child);
    }
}

// CREATE [OR REPLACE] TYPE [schema.]name [AUTHID ...] [UNDER supertype]
//   AS OBJECT ( attribute_list, member_spec... )
//   [[NOT] FINAL] [[NOT] INSTANTIABLE] [[NOT] PERSISTABLE] ;
void TypeNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)
{
    CommitOnExit commit(m_prevToken, token);

    // simplified handling for collections
	if (m_state >= eName1 && m_state <= eAs)
    {
		if (token.token == etUNKNOWN)
            if (!_wcsicmp(token.value.c_str(), L"TABLE")
                || !_wcsicmp(token.value.c_str(), L"VARRAY")
				|| !_wcsicmp(token.value.c_str(), L"VARYING")
				|| !_wcsicmp(token.value.c_str(), L"ARRAY"))
				m_state = eCollection;
    }
    else if (m_state == eCollection)
    {
        SET_AND_RET(token, etSEMICOLON, eComplete);
        //SET_AND_RET(token, etEOS, eComplete);
        if (token == etEOS) {
            append(eComplete, m_prevToken); // exclude EOS 
            return;
        }
    }

    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr);
        SET_AND_RET(token, etTYPE, eType);
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace);
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etTYPE, eType);
        Failure(token.line);
        break;
    case eType:
        if (!token.reserved)
        {
            append(eName1, token);
            return;
        }
        Failure(token.line);
        break;
    case eName1:
        SET_AND_RET(token, etDOT, eNameSeparator);
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        SET_AND_RET(token, etUNDER, eUnder);
        SET_AND_RET(token, etIS, eAs);
        SET_AND_RET(token, etAS, eAs);
        Failure(token.line);
        break;
    case eNameSeparator:
        if (!token.reserved)
        {
            append(eName2, token);
            return;
        }
        Failure(token.line);
        break;
    case eName2:
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        SET_AND_RET(token, etUNDER, eUnder);
        SET_AND_RET(token, etIS, eAs);
        SET_AND_RET(token, etAS, eAs);
        Failure(token.line);
        break;
    case eAuthid1:
        SET_AND_RET(token, etCURRENT_USER, eAuthid2);
        SET_AND_RET(token, etDEFINER, eAuthid2);
        Failure(token.line);
        break;
    case eAuthid2:
        SET_AND_RET(token, etUNDER, eUnder);
        SET_AND_RET(token, etIS, eAs);
        SET_AND_RET(token, etAS, eAs);
        Failure(token.line);
        break;
    case eUnder:
        // skip the supertype name (possibly schema.name)
        SET_AND_RET(token, etIS, eAs);
        SET_AND_RET(token, etAS, eAs);
        // [AS OBJECT] is optional
        SET_AND_RET(token, etLEFT_ROUND_BRACKET, eMemberList);
        break;
    case eAs:
        SET_AND_RET(token, etOBJECT, eObject);
        Failure(token.line);
        break;
    case eObject:
        SET_AND_RET(token, etLEFT_ROUND_BRACKET, eMemberList);
        Failure(token.line);
        break;
    case eMemberList:
        // nested paren inside an attribute declaration, e.g. varchar2(100)
        if (token == etLEFT_ROUND_BRACKET)
            m_memberListParenDepth++;

        if (!m_memberListParenDepth)
        {
            HandleMemberListToken(token, child);
            SET_AND_RET(token, etRIGHT_ROUND_BRACKET, eAfterObject);
        }

        if (token == etRIGHT_ROUND_BRACKET)
            m_memberListParenDepth--;

        break;
    case eAfterObject:
        // trailing [[NOT] FINAL] [[NOT] INSTANTIABLE] [[NOT] PERSISTABLE] ;
        SET_AND_RET(token, etSEMICOLON, eComplete);
        //SET_AND_RET(token, etEOS, eComplete);
        if (token == etEOS) {
            append(eComplete, m_prevToken); // exclude EOS 
            return;
        }
        break;
    }
}

void ProcedureNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        SET_AND_RET(token, etPROCEDURE, eProcedure);
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_AND_RET(token, etPROCEDURE, eProcedure); 
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etPROCEDURE, eProcedure); 
        Failure(token.line);
        break;
    case eProcedure:
        {
            SyntaxNode* parent = GetParent();
            if (!parent 
            || typeid(*parent) == typeid(ScriptNode))
            {
                if (!token.reserved)
                {
                    append(eName1, token);
                    return;
                }
            }
            else
            {

                if (!((1<<1) & token.reserved))
                {
                    append(eName1, token);
                    return;
                }
            }
        }
        //SET_AND_RET(token, etUNKNOWN, eName1); 
        //SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName1); 
        Failure(token.line);
        break;
    case eName1:
        if (token == etLEFT_ROUND_BRACKET) {
            m_state = eArguments;
            break; // it's parsed as an expression!!!
        }
        SET_AND_RET(token, etDOT, eNameSeparator);
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        //m_declaration = true;
        Failure(token.line);
        break;
    case eNameSeparator:
        {
            SyntaxNode* parent = GetParent();
            if (!parent 
            || typeid(*parent) == typeid(ScriptNode))
            {
                if (!token.reserved)
                {
                    append(eName1, token);
                    return;
                }
            }
            else
            {
                if (!((1<<1) & token.reserved))
                {
                    append(eName1, token);
                    return;
                }
            }
        }
        //SET_AND_RET(token, etUNKNOWN, eName2); 
        //SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName2); 
        break;
    case eName2:
        if (token == etLEFT_ROUND_BRACKET) {
            m_state = eArguments;
            break; // it's parsed as an expression!!!
        }
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        //m_declaration = true;
        Failure(token.line);
        break;
    case eArguments:
        SET_AND_RET(token, etSEMICOLON, eComplete);
        //m_declaration = true;
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        Failure(token.line); // ???
        break;
    case eAuthid1:
        SET_AND_RET(token, etCURRENT_USER, eAuthid2);
        SET_AND_RET(token, etDEFINER, eAuthid2);
        Failure(token.line);
        break;
    case eAuthid2:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        Failure(token.line);
        break;
    case eIs:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etLANGUAGE, eLanguage);
        //Failure(token.line); // ???
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the package name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the package name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    case eLanguage:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

void FunctionNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        SET_AND_RET(token, etFUNCTION, eFunction);
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_AND_RET(token, etFUNCTION, eFunction); 
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etFUNCTION, eFunction); 
        Failure(token.line);
        break;
    case eFunction:
        {
            SyntaxNode* parent = GetParent();
            if (!parent 
            || typeid(*parent) == typeid(ScriptNode))
            {
                if (!token.reserved)
                {
                    append(eName1, token);
                    return;
                }
            }
            else
            {
                if (!((1<<1) & token.reserved))
                {
                    append(eName1, token);
                    return;
                }
            }
        }
        //SET_AND_RET(token, etUNKNOWN, eName1); 
        //SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName1); 
        Failure(token.line);
        break;
    case eName1:
        if (token == etLEFT_ROUND_BRACKET) {
            m_state = eArguments;
            break; // it's parsed as an expression!!!
        }
        SET_AND_RET(token, etDOT, eNameSeparator);
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        SET_STATE_AND_RET(token, etRETURN, eReturn); 
        //m_declaration = true;
        Failure(token.line);
        break;
    case eNameSeparator:
        {
            SyntaxNode* parent = GetParent();
            if (!parent 
            || typeid(*parent) == typeid(ScriptNode))
            {
                if (!token.reserved)
                {
                    append(eName1, token);
                    return;
                }
            }
            else
            {
                if (!((1<<1) & token.reserved))
                {
                    append(eName1, token);
                    return;
                }
            }
        }
        //SET_AND_RET(token, etUNKNOWN, eName2); 
        //SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName2); 
        break;
    case eName2:
        if (token == etLEFT_ROUND_BRACKET) {
            m_state = eArguments;
            break; // it's parsed as an expression!!!
        }
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        SET_STATE_AND_RET(token, etRETURN, eReturn); 
        //m_declaration = true;
        Failure(token.line);
        break;
    case eArguments:
        SET_STATE_AND_RET(token, etRETURN, eReturn); 
        Failure(token.line);
        break;
    case eReturn:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        SET_STATE_AND_RET(token, etAUTHID, eAuthid1);
        return; // skip anything else - assume thats is return type
    case eAuthid1:
        SET_AND_RET(token, etCURRENT_USER, eAuthid2);
        SET_AND_RET(token, etDEFINER, eAuthid2);
        Failure(token.line);
        break;
    case eAuthid2:
        SET_AND_RET(token, etIS, eIs);
        SET_AND_RET(token, etAS, eIs);
        Failure(token.line);
        break;
    case eIs:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etLANGUAGE, eLanguage);
        //Failure(token.line); // ???
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the package name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the package name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    case eLanguage:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

void JavaNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& /*child*/)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_STATE_AND_RET(token, etAND, eAnd); 
        SET_STATE_AND_RET(token, etJAVA, eJava); 
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eAnd:
        SET_STATE_AND_RET(token, etCOMPILE, eCompile); 
        Failure(token.line);
        break;
    case eReplace:
        SET_STATE_AND_RET(token, etAND, eAnd); 
        SET_STATE_AND_RET(token, etJAVA, eJava); 
        Failure(token.line);
    case eCompile:
        SET_STATE_AND_RET(token, etJAVA, eJava); 
        Failure(token.line);
        break;
    case eJava:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        break;
    }

    // no children
    //PlSqlFactory::RecognizeAndCreate(token, child);
}

void TriggerNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCREATE, eCreate);
        SET_AND_RET(token, etTRIGGER, eTrigger);
        Failure(token.line);
        break;
    case eCreate:
        SET_STATE_AND_RET(token, etOR, eOr); 
        SET_AND_RET(token, etTRIGGER, eTrigger); 
        Failure(token.line);
        break;
    case eOr:
        SET_STATE_AND_RET(token, etREPLACE, eReplace); 
        Failure(token.line);
        break;
    case eReplace:
        SET_AND_RET(token, etTRIGGER, eTrigger); 
        Failure(token.line);
        break;
    case eTrigger:
        SET_AND_RET(token, etUNKNOWN, eName1); 
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName1); 
        Failure(token.line);
        break;
    case eName1:
        SET_AND_RET(token, etDOT, eNameSeparator);
        // else
        append(eHeader, token);
        return;
        break;
    case eNameSeparator:
        SET_AND_RET(token, etUNKNOWN, eName2); 
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eName2); 
        break;
    case eName2:
        append(eHeader, token);
        return;
        break;
    case eHeader:
        SET_AND_RET(token, etBEGIN, eBegin);
        SET_AND_RET(token, etDECLARE, eDeclare);
        SET_AND_RET(token, etTRIGGER, eBegin); // for compound triggers
        break;
    case eDeclare:
        SET_AND_RET(token, etBEGIN, eBegin);
        break;
    case eBegin:
        SET_AND_RET(token, etEXCEPTION, eException);
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eException:
        SET_AND_RET(token, etEND, eEnd);   
        break;
    case eEnd:
        SET_AND_RET(token, etSEMICOLON, eComplete);   
        SET_AND_RET(token, etUNKNOWN,   eEnd); // skipping the package name  
        SET_AND_RET(token, etDOUBLE_QUOTED_STRING, eEnd); // skipping the package name  
        SET_AND_RET(token, etSEMICOLON, eEnd);   
        Failure(token.line);
        break;
    }

    switch (m_state)
    {
    case eDeclare: 
    case eBegin: 
    case eException:
    case eEnd:
    case eComplete:
        PlSqlFactory::RecognizeAndCreate(token, child);
    }
}

void SelectionNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etIF, eIf);
        Failure(token.line);
        break;
    case eIf:
    case eElsIf:
        SET_AND_RET(token, etELSIF, eElsIf);
        SET_AND_RET(token, etELSE,  eElse); 
        SET_AND_RET(token, etEND,   eEnd);  
        break;
    case eElse:
        SET_AND_RET(token, etEND, eEnd);  
        break;
    case eEnd:
        SET_AND_RET(token, etIF, eEndIf);  
        Failure(token.line);
        break;
    case eEndIf:
        SET_AND_RET(token, etSEMICOLON, eComplete);  
        Failure(token.line);
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

void CaseNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCASE, eCase);
        Failure(token.line);
        break;
    case eCase:
    case eWhen:
        SET_AND_RET(token, etWHEN,  eWhen);
        SET_AND_RET(token, etELSE,  eElse); 
        SET_AND_RET(token, etEND,   eEnd);  
        break;
    case eElse:
        SET_AND_RET(token, etEND, eEnd);  
        break;
    case eEnd:
        SET_AND_RET(token, etCASE, eEndCase); 
        //Failure(token.line);
        // let's assume it is an expression
        {
            m_state = eComplete;
            return;
        } 
    case eEndCase:
        SET_AND_RET(token, etSEMICOLON, eComplete);  
        Failure(token.line);
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

void SqlCaseNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etCASE, eCase);
        Failure(token.line);
        break;
    case eCase:
    case eWhen:
        SET_AND_RET(token, etWHEN,  eWhen);
        SET_AND_RET(token, etELSE,  eElse); 
        SET_AND_RET(token, etEND,   eComplete);  
        break;
    case eElse:
        SET_AND_RET(token, etEND, eComplete);  
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void IterationNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etWHILE, eWhile);
        SET_AND_RET(token, etFOR  , eFor  );
        SET_AND_RET(token, etLOOP , eLoop );
        Failure(token.line);
        break;
    case eWhile:
    case eFor:
        if (!token.reserved && m_varName.empty())
        {
            m_varName = token.value;
            return;
        }
        SET_AND_RET(token, etLOOP, eLoop);
        break;
    case eLoop:
        SET_AND_RET(token, etEND, eEnd);
        break;
    case eEnd:
        SET_AND_RET(token, etLOOP, eEndLoop);
        Failure(token.line);
        break;
    case eEndLoop:
        SET_AND_RET(token, etSEMICOLON, eComplete);
        if (m_varName == token.value)
            return;
        Failure(token.line);
        break;
    }

    PlSqlFactory::RecognizeAndCreate(token, child);
}

void ExpressionNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etLEFT_ROUND_BRACKET, eOpen);
        Failure(token.line);
        break;
    case eOpen: 
        SET_AND_RET(token, etRIGHT_ROUND_BRACKET, eComplete);
        if (token == etSEMICOLON)
        {
            append(eComplete, token);
            Failure(token.line);
            close_driver_node(this, token); // weird
        }
        break;
    }

    ExpressionFactory::RecognizeAndCreate(token, child);
}

void SelectNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etSELECT, eSelect);
        SET_AND_RET(token, etWITH, eWith);
        Failure(token.line);
        break;
    case eWith:
        SET_AND_RET(token, etSELECT, eSelect);
        break;
    case eSelect:
        SET_AND_RET(token, etFROM, eFrom);
        break;
    case eFrom:
        SET_AND_RET(token, etWHERE, eWhere);
        // continue
    case eWhere:
    case eNextPosibleSet:
        SET_AND_RET(token,  etUNION,     eUnion);
        SET_AND_RET(token,  etINTERSECT, eIntersect);
        SET_AND_RET(token,  etMINUS_SQL, eMinus);
        switch (token)
        {
        case etEOS:
        case etEOF:
        case etSEMICOLON:
            append(eComplete, token);
            close_driver_node(this, token); // weird
            return;
        case etRIGHT_ROUND_BRACKET:
            // for inner queries
            {
                Token tk;
                tk = etUNKNOWN;
                tk.line = token.line;
                tk.offset = token.offset;
                append(eComplete, tk);
                // close an expression
                GetParent()->PutToken(token, child); // weird
            }
            break;
        }
        break;
    case eUnion: 
        SET_AND_RET(token, etSELECT, eSelect);
        if(token == etLEFT_ROUND_BRACKET) 
            break;
        if (token == etALL) return;
        Failure(token.line);
        break;
    case eIntersect: 
    case eMinus:
        SET_AND_RET(token, etSELECT, eSelect);
        if(token == etLEFT_ROUND_BRACKET) 
            break;
        Failure(token.line);
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void SelectNode::OnChildCompletion (SyntaxNode* child, const Token& token)
{
    if (child)
    {    
        const type_info& pt = typeid(*child);

        if (pt == typeid(ExpressionNode))
        {
            switch (m_state)
            {
            case eUnion:
            case eIntersect:
            case eMinus:
                m_state = eComplete;
                {
                    Token tk = token;
                    tk = etUNKNOWN;
                    tk.offset = tk.endCol();
                    tk.length = 0;
                    append(eComplete, tk);
                }
            }
        }
    }
}

void InsertNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etINSERT, eInsert);
        Failure(token.line);
        break;
    case eInsert:
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void UpdateNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etUPDATE, eUpdate);
        Failure(token.line);
        break;
    case eUpdate:
        SET_AND_RET(token, etSET, eSet);
        break;
    case eSet:
        SET_AND_RET(token, etWHERE, eWhere);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    case eWhere:
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void DeleteNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etDELETE, eDelete);
        Failure(token.line);
        break;
    case eDelete:
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    case eWhere:
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

// The arm's own UPDATE / INSERT / DELETE are deliberately left unrecognised:
// DmlSqlFactory has no case for them, and claiming one would nest an UpdateNode
// inside the MERGE and hand folding two regions for a single statement.
void MergeNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)
{
    switch (m_state)
    {
    case eUndef:
        SET_AND_RET(token, etMERGE, eMerge);
        Failure(token.line);
        break;
    case eMerge:
    case eWhen:
        SET_AND_RET(token, etWHEN, eWhen);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void OpenCursorNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etOPEN, eOpen);
        Failure(token.line);
        break;
    case eOpen:
        SET_AND_RET(token, etFOR, eFor);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    case eFor:
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }

    DmlSqlFactory::RecognizeAndCreate(token, child);
}

void SqlPlusCmdNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>&)  
{
    switch (m_state) 
    {
    case eUndef:
        append(ePending, token); 
        break;
    case ePending:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOL, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        if // ignore ; in REM and PROMPTS
        (
            token == etSEMICOLON
            && (
                m_tokens.empty()
                || (*m_tokens.begin() != etREMARK && *m_tokens.begin() != etPROMPT)
            )
        )
            SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }
}

void GenericSqlNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>& child)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etGRANT, eGrant);
        SET_AND_RET(token, etREVOKE, eRevoke);
        append(ePending, token); 
        break;

    case ePending:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;

    case eGrant:
    case eRevoke:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        return; // don't want any children!
    }

    DdlSqlFactory::RecognizeAndCreate(token, child);
}

void GenericSetNode::PutToken (const Token& token, std::unique_ptr<SyntaxNode>&)  
{
    switch (m_state) 
    {
    case eUndef:
        SET_AND_RET(token, etSET, eSet);
        Failure(token.line);
        break;
    case eSet:
        SET_STATE_AND_RET(token, etAT,                   ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etAPPINFO,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etARRAYSIZE,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etAUTOCOMMIT,           ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etAUTOPRINT,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etAUTORECOVERY,         ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etAUTOTRACE,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etBLOCKTERMINATOR,      ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCMDSEP,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCOLSEP,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCOMPATIBILITY,        ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCONCAT,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCOPYCOMMIT,           ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etCOPYTYPECHECK,        ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etDEFINE,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etDESCRIBE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etECHO,                 ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etEDITFILE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etEMBEDDED,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etESCAPE,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etFEEDBACK,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etFLAGGER,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etFLUSH,                ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etHEADING,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etHEADSEP,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etINSTANCE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etLINESIZE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etLOBOFFSET,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etLOGSOURCE,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etLONG,                 ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etLONGCHUNKSIZE,        ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etMARKUP,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etNEWPAGE,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etNULL,                 ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etNUMFORMAT,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etNUMWIDTH,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etPAGESIZE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etPAUSE,                ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etRECSEP,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etRECSEPCHAR,           ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSCAN,                 ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSERVEROUTPUT,         ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSHIFTINOUT,           ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSHOWMODE,             ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLBLANKLINES,        ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLCASE,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLCONTINUE,          ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLNUMBER,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLPLUSCOMPATIBILITY, ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLPREFIX,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLPROMPT,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSQLTERMINATOR,        ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etSUFFIX,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTAB,                  ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTERMOUT,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTIME,                 ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTIMING,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTRIMOUT,              ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etTRIMSPOOL,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etUNDERLINE,            ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etVERIFY,               ePendingSqlPlusCmd);
        SET_STATE_AND_RET(token, etWRAP,                 ePendingSqlPlusCmd);
        // SET TRANSACTION and SET ROLE are the only SQL statements starting with
        // SET, and the only ones that may be wrapped, so they alone keep the
        // semicolon-terminated arm.
        if (token.token == etUNKNOWN
            && (!_wcsicmp(token.value.c_str(), L"TRANSACTION")
             || !_wcsicmp(token.value.c_str(), L"ROLE")))
        {
            append(ePendingSql, token);
            break;
        }
        // else: a SQL*Plus system variable, which ends at end of line.
        append(ePendingSqlPlusCmd, token);
        // add known sql cases and comment out the next line
        // Failure(token.line); 
        break;
    case ePendingSql:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    case ePendingSqlPlusCmd:
        SET_AND_RET(token, etEOS, eComplete);
        SET_AND_RET(token, etEOL, eComplete);
        SET_AND_RET(token, etEOF, eComplete);
        SET_AND_RET(token, etSEMICOLON, eComplete);
        break;
    }
}

}; // namespace OpenEditor