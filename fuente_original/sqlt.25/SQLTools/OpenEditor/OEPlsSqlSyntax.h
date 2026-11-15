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

#pragma once

#include <set>
#include "OpenEditor/OEHelpers.h"
#include "OpenEditor/OEPlsSqlParser.h"

namespace OpenEditor
{ 
    using std::string;
    using std::set;
    using namespace Common::PlsSql;

    class EditContext;
    class ScriptNode; 

    class OutlineTextSource
    {
    public:
        virtual ~OutlineTextSource () {}
        virtual int  GetLineCount () const = 0;
        virtual void GetLineW (int line, Common::OEStringW&) const = 0;
    };

    struct OutlineSettings
    {
        bool selectEnabled;   // if false, skip all SelectNodes
        bool selectOnly;      // if true, emit SELECT labels only (no FROM/WHERE children)
        int  maxTopItems;
    };

    //
    // Attention: a tree builder is responsinle for destroing all nodes!
    //
    class SyntaxNode : private noncopyable
    {
    public:
        SyntaxNode ();
        virtual ~SyntaxNode () {};

        virtual bool IsScriptNode () const = 0;

        virtual void PutToken (const Token&, std::unique_ptr<SyntaxNode>&) = 0;
        virtual void OnChildCompletion (SyntaxNode* /*child*/, const Token&) {}

        virtual EOutlineItemType GetOutlineItemType () const { return oitUnknown; }

        // Where THIS node's own tokens begin and end, in buffer coordinates, or
        // false when it holds none. Implemented once on SyntaxNodeImpl so that
        // every concrete node type answers it without a cast.
        //
        // Folding needs the FULL extent of a statement, and a statement node
        // keeps only its structural keywords - an UpdateNode holds UPDATE, SET,
        // WHERE and ';', with the column list and predicate in children - so
        // this is only half of the answer. GetSubtreeExtent() in
        // OEPlsSqlSyntax.cpp widens it with every descendant's own extent.
        virtual bool GetOwnExtent (Square& /*pos*/) const { return false; }

        // Which structural section of THIS construct brackets (line, offset)?
        // shNone when the node has no sections or the position is outside it.
        //
        // shTypeMembers is an object type's "AS OBJECT ( … )" member list. It is a
        // declaration run in every structural sense - one opener, one closer, items
        // between them - but the items are comma-separated and laid out by
        // emitTypeMemberList rather than by emitDeclareSection, so it gets its own
        // answer instead of sharing shDeclarations.
        enum ESectionHit { shNone = 0, shHeader, shDeclarations, shBody, shException,
                           shTypeMembers };
        virtual ESectionHit GetSectionAt (int /*line*/, int /*offset*/) const { return shNone; }

        // True on the constructs whose DECLARE-IS, BEGIN, EXCEPTION or member-list runs
        // are seedable: block, procedure, function, package, trigger, object type spec,
        // type body, and a type body's member subprogram.
        virtual bool OwnsSections () const { return false; }

        // What the run between the declaration opener and the next boundary is called.
        // shDeclarations for every block-shaped construct; TypeNode answers
        // shTypeMembers, which is the only reason this is a virtual.
        virtual ESectionHit DeclarationSectionKind () const { return shDeclarations; }

        // True when nothing inside this node has a trustworthy section, so a section
        // walk stops here instead of descending: a compound trigger's payload.
        virtual bool BlocksSeedDescent () const { return false; }

        virtual bool WantsEOL    () const = 0;
        virtual bool IsCompleted () const = 0;

        // Deferred-END support, see PlSqlAnalyzer::PutToken.
        //
        // A named construct's `END` is ambiguous until the token AFTER it is seen:
        // `END p1;` closes the construct declared as p1, while `END pkg;` arriving
        // inside that same p1 means the terminator belongs to the ENCLOSING pkg.
        // Recognising the difference needs one token of lookahead a node does not
        // have - PutToken is single-token with no peek - so the analyzer holds the
        // `END` back and asks three questions instead:
        //
        //   WantsDeferredEnd   would an etEND arriving now move this node to its
        //                      own eEnd, AND has it captured a declared name to
        //                      match a label against? Both halves matter: without
        //                      a name there is nothing to compare and today's
        //                      accept-anything behaviour is the only option.
        //   RejectsEndLabel    is `token` an identifier that is NOT this node's
        //                      declared name, i.e. the held END was never ours?
        //   AbandonTerminator  give that END back: mark the node complete WITHOUT
        //                      a terminator, so Colapse() does not Failure() it.
        //
        // The defaults are all "no", which reproduces exactly the behaviour before
        // this existed: the END is appended at once and any following identifier
        // is skipped unmatched (`// skipping the package name`). Every default is
        // therefore load-bearing for the ~twenty node types that do not override.
        virtual bool WantsDeferredEnd () const { return false; }
        virtual bool RejectsEndLabel (const Token&) const { return false; }
        virtual void AbandonTerminator () {}

        int GetLevel () const;
        virtual bool StopSign (EToken token) const;

        virtual bool FindToken (int line, int offset, const SyntaxNode*& node, int& index) const = 0;
        virtual void GetToken (int index, int& line, int& offset, int& length) const = 0;
        virtual void GetNextToken (int index, int& line, int& offset, int& length, bool& backward) const = 0;
        virtual void GetLineStatus (LineStatusMap&, int& recursion) const = 0;
        virtual void DbgPrintNode () const = 0;

        void AttachSibling (SyntaxNode*);   
        void AttachChild   (SyntaxNode*);  

        SyntaxNode* GetSibling ()                   { return m_sibling; }
        SyntaxNode* GetParent ()                    { return m_parent; }
        SyntaxNode* GetChild ()                     { return m_child; }

        const SyntaxNode* GetSibling () const       { return m_sibling; }
        const SyntaxNode* GetParent () const        { return m_parent; }
        const SyntaxNode* GetChild () const         { return m_child; }

        void Failure (int line)                     { if (!m_failure) m_failureLine = line, m_failure = true; }
        bool IsFailed () const                      { return m_failure; }
        int  GeFailureLine () const                 { return m_failureLine; }
        virtual void Colapse (const Token&) = 0;

        enum EHitCode { UNKNOWN, ABOVE, HIT, BELOW };
        virtual EHitCode GetHitCode (int line, int count) const = 0;

    protected:
        void clear ();
        static const int RECURSION_LIMIT = 1000;

    private:
        bool m_failure;
        int m_failureLine;
        SyntaxNode *m_sibling, *m_parent, *m_child;
        SyntaxNode *m_lastChild;
    };

    // Answer of PlSqlParseProduct::FindSection: which section a position sits in,
    // which construct owns it, and how many formatter indent levels that adds.
    struct SectionHit
    {
        SyntaxNode::ESectionHit section   = SyntaxNode::shNone;
        EOutlineItemType        owner     = oitUnknown;
        int                     depth     = 0;
        const SyntaxNode*       innermost = 0;   // deepest node covering the position
    };

    // Attention: the product is responsible for destroying all of its nodes!
    class PlSqlParseProduct : private noncopyable
    {
    public:
        PlSqlParseProduct ();
        ~PlSqlParseProduct ();

        PlSqlParseProduct (PlSqlParseProduct&&);
        PlSqlParseProduct& operator= (PlSqlParseProduct&&);

        struct CommentSpan
        {
            int startLine;    // buffer line holding the '--' or the '/*'
            int startOffset;  // column of that first character
            int endLine;      // last buffer line the comment occupies
        };

        const std::vector<CommentSpan>& GetCommentSpans () const { return m_comments; }

        int GetTokenCount () const { return m_tokenCounter; }

        bool FindToken (int line, int offset, const SyntaxNode*& node, int& index) const;

        // Locate the section enclosing (line, offset) rather than the token under it:
        // a caret in a body sits in the gap between its BEGIN and END. False on a miss.
        bool FindSection (int line, int offset, SectionHit& hit) const;
        void GetLineStatus (LineStatusMap& statusMap) const;

        void GetOutlineProducts (const OutlineTextSource& text,
                                 const OutlineSettings&   settings,
                                 FoldCandidates&          fold,
                                 OutlineItems*            pane) const;
        bool HasAnyFailedNode () const;
        int  GetFailureLine () const;

    private:
        friend class PlSqlAnalyzer;

        void Reset ();
        void ReleaseNodes ();
        ScriptNode* Root () const { return m_root.get(); }

        std::unique_ptr<ScriptNode> m_root;
        vector<SyntaxNode*>         m_pool;

        std::vector<CommentSpan>    m_comments;

        int m_tokenCounter = 0;

        static bool HasFailedNodeImpl (const SyntaxNode* node);
    };

    class PlSqlAnalyzer : public SyntaxAnalyser, private noncopyable
    {
    public:
        PlSqlAnalyzer ();
        ~PlSqlAnalyzer ();

        void Clear ();
        void PutToken (const Token&);

        PlSqlParseProduct TakeProduct ();

        int GetTokenCount () const { return m_product.GetTokenCount(); }

        virtual bool AtStatementStart () const;

    private:
        PlSqlParseProduct m_product;

        // --- transient state: meaningless once the parse is over ---
        SyntaxNode* m_top;
        bool m_error;

        // One token of lookahead, used only for the deferred-END decision.
        //
        // m_hasPendingEnd is the flag rather than a test on m_pendingEnd.token
        // because a default-constructed Token is etNONE and so is a legitimate
        // "empty" value - the pair is set and cleared together and never read
        // apart. Both are reset by Clear().
        Token m_pendingEnd;
        bool  m_hasPendingEnd = false;

        void Attach   (SyntaxNode*);
        void CloseTop (const Token&);
        void CloseAll (const Token&) ;

        // The whole of PutToken's "hand this token to the tree" half, split out so
        // the deferred-END logic can call it twice for one incoming token (the held
        // END, then the token that decided its fate) without duplicating the
        // child/failed/completed handling. Also where the CaseNode pre-delivery
        // escape lives.
        void DeliverToTree (const Token&);

        // The deferral gate itself, split out so a reclaimed END can be re-offered
        // to the enclosing construct instead of being forced on it.
        void OfferToken (const Token&);

        // Decide the fate of a held END once the following token is known, and
        // flush a held END that never got a following token because the statement
        // ended. See SyntaxNode::WantsDeferredEnd.
        void ResolvePendingEnd (const Token&);
        void FlushPendingEnd   ();

        static bool HasFailedNodeImpl   (const SyntaxNode* node);
    };

}; // namespace OpenEditor
