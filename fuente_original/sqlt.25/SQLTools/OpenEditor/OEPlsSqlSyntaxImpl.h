/* 
    Copyright (C) 2003-2015 Aleksey Kochetov

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

#include "Common/FixedArray.h"
#include "OpenEditor/OEPlsSqlSyntax.h"

namespace OpenEditor
{ 
    using Common::FixedArray;

    // Index into a node's own m_tokens, or -1 when nothing was recorded. Every node
    // type below holds a FixedArray<Token, N> with N <= 14, and FixedArray's
    // limitcheck() throws rather than grows, so an index can never reach 127; the
    // four std::vector-backed types record no index at all, because they override
    // neither NoteNameToken nor NoteSectionStart. Narrow on purpose: the six of these
    // plus m_state fit in the padding that three ints used to need, which keeps every
    // node exactly the size it was before the section boundaries were added.
    // Signed, for the -1 sentinel. Prints as a glyph under operator<< - cast to int.
    typedef signed char token_inx;
    static_assert((token_inx)-1 < 0, "token_inx must stay signed: -1 is the 'absent' sentinel");

    // Capacity of a node's own token array, or -1 when it can grow. A partial
    // specialisation on FixedArray, so Common/FixedArray.h needs no change.
    template <class TokenArray> struct NodeTokenCapacity { enum { value = -1 }; };
    template <class T, int N> struct NodeTokenCapacity<FixedArray<T, N> > { enum { value = N }; };

    template <class TokenArray = FixedArray<Token, 10> >
    class SyntaxNodeImpl : public SyntaxNode
    {
    protected:
        SyntaxNodeImpl () : m_state(0) {}

    public:
        virtual bool WantsEOL () const { return false; }
        virtual bool FindToken (int line, int offset, const SyntaxNode*& node, int& index) const;
        virtual void GetToken (int index, int& line, int& offset, int& length) const;
        virtual void GetNextToken (int index, int& line, int& offset, int& length, bool& backward) const;
        virtual void GetLineStatus (LineStatusMap&, int& recursion) const;
        virtual void Colapse (const Token&);
        virtual ESectionHit GetSectionAt (int line, int offset) const;
        const TokenArray& GetTokens () const { return m_tokens; }

        // See SyntaxNode::GetOwnExtent. Tokens are appended in lex order, which
        // is buffer order, so the first and last of them bracket the node.
        virtual bool GetOwnExtent (Square& pos) const
        {
            if (m_tokens.empty()) return false;

            pos.start = { m_tokens.front().offset, m_tokens.front().line };
            pos.end   = { m_tokens.back().endCol(), m_tokens.back().line };
            return true;
        }

    public:
        // See SyntaxNode::RejectsEndLabel. Implemented once here because the
        // comparison is identical for every named node type; only WHICH token is
        // the declared name differs, and that is what NoteNameToken records.
        virtual bool RejectsEndLabel (const Token& token) const;

    protected:
        void append (int state, const Token& token)
        {
            m_state = state;
            m_tokens.push_back(token);
            NoteNameToken(state);
            NoteSectionStart(state);
        }
        virtual void clear ()
        {
            SyntaxNode::clear();
            m_tokens.clear();
            m_endLabel1 = m_endLabel2 = -1;
            m_declStart = m_bodyStart = m_excStart = m_endStart = -1;
        }
        virtual EHitCode GetHitCode (int line, int count) const;

        // Called by append() with the state just entered. The named node types
        // override it to record where their declared name landed in m_tokens, so a
        // later `END <label>` can be matched against it. The default does nothing,
        // which leaves m_endLabel1 at -1 and therefore WantsDeferredEnd() false -
        // unnamed constructs keep exactly the behaviour they have today.
        virtual void NoteNameToken (int /*state*/) {}

        // Called by append() with the state just entered, on the section-owning
        // node types, to record where each section starts. See GetSectionAt.
        virtual void NoteSectionStart (int /*state*/) {}

        // Shared body of those overrides: the four boundaries mean the same thing
        // for every block-shaped owner, and only the state that names the
        // declaration section differs. TypeNode does not use it - its member list
        // is bracketed by '(' and ')' rather than by keywords.
        void NoteSectionBoundary (int state, int declState, int beginState,
                                  int excState, int endState)
        {
            if      (state == declState)  m_declStart = LastTokenIndex();
            else if (state == beginState) m_bodyStart = LastTokenIndex();
            else if (state == excState)   m_excStart  = LastTokenIndex();
            else if (state == endState)   m_endStart  = LastTokenIndex();
        }

        // Index of this node's last own token at or before (line, offset), or -1 when
        // the position precedes all of them; a caret in a nested child still resolves.
        int OwnTokenIndexAtOrBefore (int line, int offset) const;

        // True when m_tokens[index] holds the same text as `token`. Both sides are
        // the lexer's own `value`: identifiers arrive upper-cased (PlSqlParser.cpp
        // :108) and quoted strings arrive verbatim, so this is a plain compare and
        // needs no Storage - and neither does BuildNodeLabel, which formats its
        // display label out of Token::value alone.
        bool MatchesEndLabel (const Token& token, int index) const;

        // Index of the token append() just pushed. Named node types use it from
        // NoteNameToken to record where their declared name landed, and the five
        // section owners from NoteSectionStart. Every write to a token_inx goes
        // through here, so the bound is checked in this one place.
        token_inx LastTokenIndex () const
        {
            // Non-virtual, and reached only from NoteNameToken / NoteSectionStart,
            // so this body is instantiated just for the node types that record an
            // index - all of them FixedArray-backed, where limitcheck() makes
            // size() <= N a certainty rather than a hope. A vector-backed type that
            // starts recording an index fails to compile here instead of truncating
            // silently at run time.
            static_assert(NodeTokenCapacity<TokenArray>::value > 0 &&
                          NodeTokenCapacity<TokenArray>::value <= 128,
                "token_inx is one byte: only a FixedArray-backed node (capacity <= 128) may record a token index");
            return static_cast<token_inx>(static_cast<int>(m_tokens.size()) - 1);
        }

    public:
        virtual void DbgPrintNode () const;

    protected:
        int m_state;
        // Indices into m_tokens of the declared name (m_endLabel1) and of its
        // schema qualifier's own part (m_endLabel2), or -1 when not captured.
        // Indices rather than copies of the text: a node then costs two bytes
        // instead of two wstrings, and m_tokens already owns the values. See
        // token_inx for why one byte each is enough.
        token_inx m_endLabel1 = -1;
        token_inx m_endLabel2 = -1;

        // Indices into m_tokens of this construct's section boundaries, or -1 when a
        // section is absent. Written by NoteSectionStart, read by GetSectionAt.
        token_inx m_declStart = -1;   // DECLARE, or the IS/AS of a named unit
        token_inx m_bodyStart = -1;   // BEGIN
        token_inx m_excStart  = -1;   // EXCEPTION
        token_inx m_endStart  = -1;   // the terminating END
    //private:
        TokenArray m_tokens;
    };

    // TODO: make sure that we really need ScriptNode
    class ScriptNode : public SyntaxNodeImpl<FixedArray<Token, 7> >
    {
    public:
        ~ScriptNode ();
        virtual bool IsScriptNode () const { return true; };

        void Clear ();

        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return false; }
        virtual bool FindToken (int line, int offset, const SyntaxNode*& node, int& index) const;
        virtual void GetLineStatus (LineStatusMap&, int& recursion) const;
    };

    class BlockNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eDeclare, eBegin, eException, eEnd, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitBlock; }

        // Section-owning: DECLARE…BEGIN…[EXCEPTION]…END; see GetSectionAt.
        virtual bool OwnsSections () const { return true; }

    protected:
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eDeclare, eBegin, eException, eEnd);
        }
    };

    class PackageNode : public SyntaxNodeImpl<FixedArray<Token, 14> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            ePackage, eWrapped, eAuthid1, eAuthid2, eAccessible, eBody, eName1, eNameSeparator, eName2, eIs, 
            eBegin, eException, eEnd, eComplete 
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        // PACKAGE vs PACKAGE BODY is only known once the BODY token (if any)
        // has been seen, so this scans the collected tokens at query time.
        virtual EOutlineItemType GetOutlineItemType () const;

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd. All three body
        // states are gated because a package SPEC accepts END straight from eIs -
        // BEGIN is optional there (`// if it's not body, BEGIN is optional`).
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eIs || m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning. A spec has no BEGIN, so IS…END is all declaration
        // section - which is what emitPackage does with it too.
        virtual bool OwnsSections () const { return true; }

    protected:
        virtual void NoteNameToken (int state)
        {
            if      (state == eName1) m_endLabel1 = LastTokenIndex();
            else if (state == eName2) m_endLabel2 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eIs, eBegin, eException, eEnd);
        }
    };

    class TypeBodyNode : public SyntaxNodeImpl<FixedArray<Token, 12> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            eType, eBody, eName1, eNameSeparator, eName2, eIs, 
            eBegin, eException, eEnd, eComplete,
            // lookahead states while scanning the "IS/AS ... END;" body for
            // a member subprogram's introducer keyword sequence, e.g.
            // "MEMBER", "STATIC", "CONSTRUCTOR", "MAP MEMBER", "ORDER MEMBER",
            // "OVERRIDING MEMBER" - all followed by PROCEDURE/FUNCTION. Same
            // idea as TypeNode's member-list lookahead, but simpler (no NOT
            // here - a type body's OVERRIDING member never carries NOT).
            eMemberLeadNeedMember, eMemberLeadReady
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitTypeBody; }

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd. eIs is gated for
        // the same reason as PackageNode's: a type body accepts END straight from
        // IS/AS when it has no BEGIN.
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eIs || m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning, and shaped exactly like PackageNode - which is no
        // coincidence: emitTypeBody is a one-line delegation to emitPackage, so
        // IS…END is a declare section, an optional BEGIN…[EXCEPTION]…END init
        // block is a statement run, and both are emitted one level below the
        // header. That is the same arithmetic FindSection performs for any
        // OwnsSections() node, so no formatter change comes with this override.
        virtual bool OwnsSections () const { return true; }

    protected:
        virtual void NoteNameToken (int state)
        {
            if      (state == eName1) m_endLabel1 = LastTokenIndex();
            else if (state == eName2) m_endLabel2 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eIs, eBegin, eException, eEnd);
        }

    private:
        Token m_memberLeadToken;
    };

    // One MEMBER/STATIC/MAP MEMBER/ORDER MEMBER/CONSTRUCTOR subprogram body
    // inside a "CREATE TYPE BODY ... IS/AS ... END;" block. Mirrors the
    // body-parsing states of ProcedureNode/FunctionNode (name/arguments/
    // [RETURN]/AUTHID/IS-AS/BEGIN/EXCEPTION/END/LANGUAGE) but also owns an
    // optional lead introducer keyword so it's included in navigation/
    // selection, same pattern as TypeMemberNode for the type spec's member
    // list. Only the first lead token and the PROCEDURE/FUNCTION token are
    // forwarded by TypeBodyNode - intermediate lead keywords (MEMBER when
    // preceded by MAP/ORDER) are not stored here.
    class TypeBodyMemberNode : public SyntaxNodeImpl<FixedArray<Token, 12> >
    {
        enum State { eUndef = 0, eLead, eProcOrFunc, eName1, eArguments, eReturn,
            eIs, eBegin, eException, eEnd, eLanguage, eComplete
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        // Only meaningful once eProcOrFunc has been reached (m_isFunction set).
        virtual EOutlineItemType GetOutlineItemType () const
        { return m_isFunction ? oitTypeMemberFunction : oitTypeMemberProcedure; }

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd. This node's eIs
        // is NOT gated: it accepts only BEGIN and LANGUAGE, never END.
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning, mirroring ProcedureNode: a type body's member
        // subprogram is a full IS/AS…BEGIN…[EXCEPTION]…END body, and the declare
        // section that contains it emits it exactly like a subprogram nested in a
        // package body. A selection inside such a member's declarations or
        // statements therefore seeds the same two scopes, one level deeper than
        // the type body's own payload.
        virtual bool OwnsSections () const { return true; }

    protected:
        // No eName2 in this node's machine - a type-body member is never declared
        // schema-qualified - so only the first slot is ever filled.
        virtual void NoteNameToken (int state)
        {
            if (state == eName1) m_endLabel1 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eIs, eBegin, eException, eEnd);
        }

    private:
        bool m_isFunction = false;
    };

    // One MEMBER/STATIC/MAP MEMBER/ORDER MEMBER/CONSTRUCTOR subprogram spec
    // inside a "CREATE TYPE ... AS OBJECT ( ... )" member list.
    // Completes on a COMMA (consumed) or a RIGHT_ROUND_BRACKET (NOT consumed;
    // re-delivered to the parent TypeNode, same pattern SelectNode uses for
    // closing an inner query's parenthesis).
    //
    // Owns the introducer keyword sequence, e.g. "NOT OVERRIDING MEMBER",
    // "MAP MEMBER", "ORDER MEMBER", "STATIC", "CONSTRUCTOR" - all followed by
    // PROCEDURE/FUNCTION. For navigation/selection purposes only the first
    // lead token and the PROCEDURE/FUNCTION token are kept in m_tokens
    // (plus name/arguments/end); the parent TypeNode still detects and
    // validates the full lead sequence but only forwards these two tokens -
    // the intermediate lead keywords (OVERRIDING/MEMBER/MAP/ORDER) are not
    // stored here.
    class TypeMemberNode : public SyntaxNodeImpl<FixedArray<Token, 12> >
    {
        enum State { eUndef = 0, eLead, eProcOrFunc, eName, eArguments, eReturn, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        // Only meaningful once eProcOrFunc has been reached (m_isFunction set).
        virtual EOutlineItemType GetOutlineItemType () const
        { return m_isFunction ? oitTypeMemberFunction : oitTypeMemberProcedure; }

    private:
        int m_parenDepth = 0;
        Token m_prevToken;
        bool m_isFunction = false;
    };

    // CREATE [OR REPLACE] TYPE [schema.]name [AUTHID ...] [UNDER supertype]
    //   AS OBJECT ( attribute_list, member_spec... )
    //   [[NOT] FINAL] [[NOT] INSTANTIABLE] [[NOT] PERSISTABLE] ;
    //
    // Note: unlike TypeBodyNode, this is a declarative spec, not a PL/SQL
    // block; member specs are attached as real TypeMemberNode children so
    // they keep full keyboard navigation/selection support, but plain
    // attribute declarations are just skipped (no navigation value).
    class TypeNode : public SyntaxNodeImpl<FixedArray<Token, 14> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            eType, eName1, eNameSeparator, eName2, 
            eAuthid1, eAuthid2, eUnder, 
            eAs, eObject, eMemberList, eAfterObject, eComplete,
            eCollection // VARRAY | [VARYING] ARRAY | TABLE OF
        };

        // Lookahead state for the introducer keyword sequence of a member
        // spec while scanning the member list, e.g. "NOT OVERRIDING MEMBER",
        // "MAP MEMBER", "ORDER MEMBER", "STATIC", "CONSTRUCTOR" - all
        // followed by PROCEDURE/FUNCTION. Only the first lead token is
        // remembered (m_memberLeadToken); once the lead is confirmed by
        // PROCEDURE/FUNCTION, a TypeMemberNode is spawned and fed just that
        // first lead token followed by the PROCEDURE/FUNCTION token.
        enum MemberLead { mlNone = 0, mlNot, mlOverriding, mlMap, mlOrder, mlReady };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitType; }

        // Section-owning: the "AS OBJECT ( … )" member list is this node's only
        // seedable part, recorded as the declaration run but reported as
        // shTypeMembers, because emitTypeMemberList lays a comma-separated member
        // list out and emitDeclareSection does not.
        //
        // The three shapes that never reach eMemberList record no boundary at all,
        // so every position in them answers shHeader and is declined exactly as
        // before: a forward declaration ("CREATE TYPE t;"), a collection type
        // (the eCollection detour drops every token after TABLE/VARRAY/ARRAY, so
        // its "VARRAY(5)" paren is never appended with eMemberList), and a
        // truncated unit whose parse stopped before the '('.
        virtual bool OwnsSections () const { return true; }
        virtual ESectionHit DeclarationSectionKind () const { return shTypeMembers; }

    protected:
        // The '(' that opens the member list and the ')' that closes it. Those are
        // the only two tokens appended with eMemberList / eAfterObject: attributes
        // are skipped token by token and member specs become TypeMemberNode
        // children, so the two indices bracket the whole list without help.
        virtual void NoteSectionStart (int state)
        {
            if      (state == eMemberList)  m_declStart = LastTokenIndex();
            else if (state == eAfterObject) m_endStart  = LastTokenIndex();
        }

    private:
        void HandleMemberListToken (const Token&, std::unique_ptr<SyntaxNode>&);
        void ResetMemberLead ();

        int m_memberLead = mlNone;
        Token m_memberLeadToken;

        // depth of '(' '(' pairs nested inside the member list (e.g. attribute
        // type args like varchar2(100)) that do not belong to a TypeMemberNode
        // child's own argument list - tracked so the matching ')' does not
        // prematurely close the whole "AS OBJECT ( ... )" member list.
        int m_memberListParenDepth = 0;
        Token m_prevToken;
    };

    class ProcedureNode : public SyntaxNodeImpl<FixedArray<Token, 12> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            eProcedure, eName1, eNameSeparator, eName2, eArguments, eAuthid1, eAuthid2, eIs, 
            eBegin, eException, eEnd, eComplete, eLanguage 
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitProcedure; }

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd. This is the node
        // folding case 45 measures: an unterminated PROCEDURE p1 inside a package
        // body used to swallow the body's own `END pkg;`, because eEnd skips any
        // identifier unmatched. eIs is NOT gated here - it accepts BEGIN and
        // LANGUAGE only, never END.
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning: IS/AS…BEGIN…[EXCEPTION]…END. An external (LANGUAGE)
        // subprogram records no BEGIN, so a caret there seeds nothing.
        virtual bool OwnsSections () const { return true; }

    protected:
        // A schema-qualified declaration (`sch.proc`) appends its second part with
        // state eName1 too - see the eNameSeparator case in the .cpp - so slot 1
        // ends up holding the LAST name part. That is the part `END proc;` carries,
        // which is the comparison that matters; a qualified `END sch.proc;` was
        // never accepted by this machine anyway, since eEnd has no DOT rule.
        virtual void NoteNameToken (int state)
        {
            if      (state == eName1) m_endLabel1 = LastTokenIndex();
            else if (state == eName2) m_endLabel2 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eIs, eBegin, eException, eEnd);
        }
    };

    class FunctionNode : public SyntaxNodeImpl<FixedArray<Token, 12> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            eFunction, eName1, eNameSeparator, eName2, eArguments, eReturn, eAuthid1, eAuthid2, eIs, 
            eBegin, eException, eEnd, eComplete, eLanguage 
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitFunction; }

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd. Mirrors
        // ProcedureNode exactly: eBegin and eException accept END, eIs does not.
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning, mirroring ProcedureNode; see GetSectionAt.
        virtual bool OwnsSections () const { return true; }

    protected:
        virtual void NoteNameToken (int state)
        {
            if      (state == eName1) m_endLabel1 = LastTokenIndex();
            else if (state == eName2) m_endLabel2 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eIs, eBegin, eException, eEnd);
        }
    };

    class JavaNode : public SyntaxNodeImpl<FixedArray<Token, 7> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, eAnd, eCompile, eJava, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitStatement; }
    };

    class TriggerNode : public SyntaxNodeImpl<FixedArray<Token, 10> >
    {
        enum State { eUndef = 0, eCreate, eOr, eReplace, 
            eTrigger, eName1, eNameSeparator, eName2, eHeader, eDeclare, 
            eBegin, eException, eEnd, eComplete
        };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitTrigger; }

        // Deferred-END support, see SyntaxNode::WantsDeferredEnd.
        //
        // COMPOUND TRIGGER is the reason RejectsEndLabel only ever rejects an
        // IDENTIFIER: a timing section ends with `END BEFORE EACH ROW;`, and those
        // section keywords are not etUNKNOWN, so they are never treated as a label
        // claim and the held END is delivered here exactly as before. A bare `END;`
        // is likewise delivered, since `;` is not an identifier either.
        virtual bool WantsDeferredEnd () const
        {
            return m_endLabel1 >= 0
                && (m_state == eBegin || m_state == eException);
        }
        virtual void AbandonTerminator () { m_state = eComplete; }

        // Section-owning, but see BlocksSeedDescent: a compound trigger reaches
        // eBegin from etTRIGGER and its payload is not a statement run.
        virtual bool OwnsSections () const { return true; }

        // A compound trigger's payload is declarations plus one timing-point
        // section per BEFORE/AFTER clause, so no position inside it is seedable.
        virtual bool BlocksSeedDescent () const
        {
            return m_bodyStart >= 0 && m_tokens.at(m_bodyStart) == etTRIGGER;
        }

    protected:
        virtual void NoteNameToken (int state)
        {
            if      (state == eName1) m_endLabel1 = LastTokenIndex();
            else if (state == eName2) m_endLabel2 = LastTokenIndex();
        }
        virtual void NoteSectionStart (int state)
        {
            NoteSectionBoundary(state, eDeclare, eBegin, eException, eEnd);
        }
    };

    class SelectionNode : public SyntaxNodeImpl<std::vector<Token> >
    {
        enum State { eUndef = 0, eIf, eElsIf, eElse, eEnd, eEndIf, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitIf; }
    };

    class CaseNode : public SyntaxNodeImpl<std::vector<Token> >
    {
        enum State { eUndef = 0, eCase, eWhen, eElse, eEnd, eEndCase, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual bool IsEndState () const { return m_state == eEnd; }
        void SetCompleted () { m_state = eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitCase; }
    };

    class SqlCaseNode : public SyntaxNodeImpl<std::vector<Token> >
    {
        enum State { eUndef = 0, eCase, eWhen, eElse, eEnd, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitCase; }
    };

    class IterationNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eFor, eWhile, eLoop, eEnd, eEndLoop, eComplete };
        wstring m_varName;

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitLoop; }
    };


    class ExpressionNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eOpen, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
    };

    class SelectNode : public SyntaxNodeImpl<std::vector<Token> >
    {
        enum State { eUndef = 0, eWith, eSelect, eFrom, eWhere, eUnion, eIntersect, eMinus, eNextPosibleSet, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual void OnChildCompletion (SyntaxNode* /*child*/, const Token&);
    };

    class InsertNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eInsert, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitInsert; }
    };

    class UpdateNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eUpdate, eSet, eWhere, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitUpdate; }
    };

    class DeleteNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eDelete, eWhere, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitDelete; }
    };

    // MERGE INTO tgt USING src ON (..) WHEN .. THEN .. ;
    // Stops are MERGE, each WHEN arm and the terminator. 
    class MergeNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eMerge, eWhen, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; };
        virtual EOutlineItemType GetOutlineItemType () const { return oitMerge; };
    };

    class OpenCursorNode : public SyntaxNodeImpl<FixedArray<Token, 5> >
    {
        enum State { eUndef = 0, eOpen, eFor, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitOpenCursor; }
    };

    class SqlPlusCmdNode : public SyntaxNodeImpl<FixedArray<Token, 2> >
    {
        enum State { eUndef = 0, ePending, eComplete };

    public:
        virtual bool IsScriptNode () const { return true; };
        virtual bool WantsEOL () const { return true; }
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
    };

    class GenericSqlNode : public SyntaxNodeImpl<FixedArray<Token, 2> >
    {
        enum State { eUndef = 0, ePending, eGrant, eRevoke, eComplete };

    public:
        virtual bool IsScriptNode () const { return false; };
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
        virtual EOutlineItemType GetOutlineItemType () const { return oitStatement; }
    };

    // there are a couple of sql statement and a bunch of sql*plus commands starting with "set"
    // but I'm afraid that oracle might inroduce a new sql statement
    // and prefer unknown syntax to be recognized rather as sql than command
    class GenericSetNode : public SyntaxNodeImpl<FixedArray<Token, 3> >
    {
        enum State { eUndef = 0, eSet, ePendingSql, ePendingSqlPlusCmd, eComplete };

    public:
        virtual bool IsScriptNode () const { return true; };
        virtual bool WantsEOL () const { return true; }
        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&);
        virtual bool IsCompleted () const { return m_state == eComplete; }
    };

    // Layout canaries. The six token_inx members plus m_state must cost no more than
    // the three ints they replaced, so that recording the section boundaries and the
    // end labels did not grow a single node.
    //
    // Expressed as header size rather than as hard-coded sizeof() numbers, because
    // absolute sizes are configuration-dependent: a Debug std::vector carries an
    // iterator-debug proxy pointer that a Release one does not, so the same node is
    // 88 bytes in Release x64 and 96 in Debug x64. sizeof(node) - sizeof(its token
    // array) is the header in any configuration, and the right-hand side is what that
    // header cost when it held three ints.
    //
    // The four types below cover both token storages - FixedArray<Token,5>,
    // <Token,12>, <Token,14> and std::vector<Token> - and none of them declares a
    // member of its own, so the subtraction is exact.
    //
    // What they guard is node size, not field count: the header has 6 bytes of spare
    // padding today, so a small future field that fits in them costs nothing and
    // passes, while anything that pushes the header past its alignment fails the
    // build. Checked with a temporary 12-byte probe - all four fired.

    // Rounds V up to a multiple of A.
    template <size_t V, size_t A> struct RoundUp { enum { value = (V + A - 1) / A * A }; };

    // What the SyntaxNodeImpl header cost when it held three ints: the base class
    // plus 12 bytes, rounded up to the alignment the token array imposes.
    template <class Node> struct OldHeaderSize
    { enum { value = RoundUp<sizeof(SyntaxNode) + 3 * sizeof(int), alignof(Node)>::value }; };

    static_assert(sizeof(BlockNode)     - sizeof(FixedArray<Token, 5>)  == OldHeaderSize<BlockNode>::value,     "SyntaxNodeImpl header grew");
    static_assert(sizeof(ProcedureNode) - sizeof(FixedArray<Token, 12>) == OldHeaderSize<ProcedureNode>::value, "SyntaxNodeImpl header grew");
    static_assert(sizeof(PackageNode)   - sizeof(FixedArray<Token, 14>) == OldHeaderSize<PackageNode>::value,   "SyntaxNodeImpl header grew");
    static_assert(sizeof(SelectionNode) - sizeof(std::vector<Token>)    == OldHeaderSize<SelectionNode>::value, "SyntaxNodeImpl header grew");
};
