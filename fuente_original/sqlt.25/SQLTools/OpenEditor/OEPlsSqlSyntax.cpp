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
    02.05.2008 bug fix, some syntax constructins are not recognized as valid because of comments 
                        solution is to skip COMMENT token
    2011.09.21 bug fix, PL/SQLAnalyzer fails on packages with startup/shuldtown/run procedures 
                        (those are script keywords)
*/

#include "stdafx.h"
// OEStorage.h is deliberately NOT included here. This translation unit holds the
// whole outline pass, and since it started taking OutlineTextSource and
// OutlineSettings it no longer names Storage at all - so the compiler now enforces
// that the pass cannot reach the document buffer except through the two methods it
// is handed. That is the property the background parse needs, and adding the
// include back would quietly give it up.
#include "OpenEditor/OEPlsSqlSyntax.h"
#include "OpenEditor/OEPlsSqlSyntaxImpl.h"
#include "OpenEditor/OEPlsSqlSyntaxImpl.inl"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{

bool SyntaxNode::StopSign (EToken token) const
{
    switch (token)
    {
    case etDECLARE:				return true;
    case etFUNCTION:			return true;
    case etRETURN:			    return false;
    case etPROCEDURE:			return true;
    case etPACKAGE:				return true;
    case etBODY:				return false;
    case etBEGIN:				return true;
    case etEXCEPTION:			return true;
    case etEND:					return false;
    case etIF:					return false;
    case etTHEN:				return false;
    case etELSE:				return true;
    case etELSIF:				return true;
    case etWHEN:				return true;
    case etFOR:					return true;
    case etWHILE:				return true;
    case etLOOP:				return false;
    case etEXIT:				return true;
    case etIS:					return false;
    case etAS:					return false;
    case etSEMICOLON:			return true;
    case etQUOTE:				return true;
    case etDOUBLE_QUOTE:		return false;
    case etLEFT_ROUND_BRACKET:	return true;
    case etRIGHT_ROUND_BRACKET:	return true;
    case etMINUS:   			return false;
    case etSLASH:				return true;
    case etSTAR:				return false;
    case etSELECT:				return true;
    case etINSERT:				return true;
    case etUPDATE:				return true;
    case etDELETE:				return true;
    case etALTER:				return true;
    case etANALYZE:				return true;
    case etCREATE:				return true;
    case etDROP:				return true;
    case etFROM:				return true;
    case etWHERE:				return true;
    case etSET:					return true;
    case etOPEN:                return true;
    case etUNION:               return true;
    case etINTERSECT:           return true;
    case etMINUS_SQL:           return true;   
    case etTRIGGER:             return true;   
    }
    return false;
}

SyntaxNode::SyntaxNode ()                               
{ 
    m_failure = false;
    m_failureLine = -1;
    m_sibling = m_parent = m_child = 0; 
    m_lastChild = 0;
}

void SyntaxNode::clear ()
{
    m_failure = false;
    m_failureLine = -1;
    m_sibling = m_parent = m_child = 0; 
    m_lastChild = 0;
}

void SyntaxNode::AttachSibling (SyntaxNode* sibling)    
{ 
    if (!m_sibling)
    {
        m_sibling = sibling; 
        sibling->m_parent = m_parent; 
    }
    else
        m_sibling->AttachSibling(sibling);
}

void SyntaxNode::AttachChild (SyntaxNode* child)       
{ 
    if (!m_child)
    {
        m_lastChild =
        m_child = child; 
        child->m_parent = this; 
    }
    else
    {
        m_lastChild->AttachSibling(child);
        m_lastChild = child;
    }
}

int SyntaxNode::GetLevel () const
{
    const SyntaxNode* node = this;
    int level = 0;
    for (; node && node->m_parent; node = node->m_parent, level++);
    return level;
}

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PlSqlParseProduct - the finished tree, and everything that keeps it alive

PlSqlParseProduct::PlSqlParseProduct ()
: m_root(new ScriptNode)
{
}

PlSqlParseProduct::~PlSqlParseProduct ()
{
    try { EXCEPTION_FRAME;

        Reset();
    }
    _DESTRUCTOR_HANDLER_;
}

// Not noexcept, deliberately: the invariant is that a product always owns a root,
// so the source of a move has to be handed a new one. See the class comment.
PlSqlParseProduct::PlSqlParseProduct (PlSqlParseProduct&& other)
: m_root(std::move(other.m_root)),
  m_pool(std::move(other.m_pool)),
  m_comments(std::move(other.m_comments)),
  m_tokenCounter(other.m_tokenCounter)
{
    other.m_root.reset(new ScriptNode);
    other.Reset();
}

PlSqlParseProduct& PlSqlParseProduct::operator= (PlSqlParseProduct&& other)
{
    if (this != &other)
    {
        Reset();

        m_root         = std::move(other.m_root);
        m_pool         = std::move(other.m_pool);
        m_comments     = std::move(other.m_comments);
        m_tokenCounter = other.m_tokenCounter;

        other.m_root.reset(new ScriptNode);
        other.Reset();
    }
    return *this;
}

void PlSqlParseProduct::ReleaseNodes ()
{
    std::vector<SyntaxNode*>::const_iterator it = m_pool.begin();
    for (; it != m_pool.end(); ++it)
        delete *it;

    m_pool.clear();
}

void PlSqlParseProduct::Reset ()
{
    m_tokenCounter = 0;

    m_comments.clear();

    ReleaseNodes();

    m_root->Clear();
}

///////////////////////////////////////////////////////////////////////////////
// PlSqlAnalyzer - the builder

PlSqlAnalyzer::PlSqlAnalyzer ()
{
    m_top   = m_product.Root();
    m_error = false;
}

PlSqlAnalyzer::~PlSqlAnalyzer ()
{
}

void PlSqlAnalyzer::Clear ()
{
    m_product.Reset();

    m_error = false;
    m_hasPendingEnd = false;
    m_pendingEnd    = Token();

    m_top = m_product.Root();
}

// Hand the finished tree over. The move leaves m_product holding a fresh empty
// tree, so the cursor has to follow it - otherwise the next parse would attach
// its nodes into the tree that was just published.
PlSqlParseProduct PlSqlAnalyzer::TakeProduct ()
{
    PlSqlParseProduct taken (std::move(m_product));
    m_top = m_product.Root();
    return taken;
}

void PlSqlAnalyzer::Attach (SyntaxNode* node)
{
    if (m_top)
    {
        m_top->AttachChild(node);
        m_top = node;
        m_product.m_pool.push_back(node);
    }
}

void PlSqlAnalyzer::CloseTop (const Token& token)
{
    ASSERT(m_top);
    
    if (m_top)
    {
        ASSERT(m_top->IsCompleted() || m_top->IsFailed());

        SyntaxNode* parent = m_top->GetParent();
        
        if (parent) 
            parent->OnChildCompletion(m_top, token);

        m_top = parent;
    }
}

void PlSqlAnalyzer::CloseAll (const Token& token) 
{
    ASSERT(m_top);
    ASSERT(token == etEOS || token == etFAILURE);

    if (m_top)
    {
        if (!m_error
        && token == etEOS
        && m_top->GetLevel() == 1
        && m_top->IsCompleted())
        {
            CloseTop(token);
        }
        else
        {
            while (m_top->GetParent())
            {
                if (token == etFAILURE
                && m_top->GetLevel() == 1)
                {
                    m_top->Failure(token.line); // set failed but don't colapse it
                    break;
                }
                m_top->Colapse(token);
                CloseTop(token);
            }
        }
    }
}

// See SyntaxAnalyser::AtStatementStart.
//
// "No statement open" is read straight off the tree instead of being tracked in a
// flag, so it cannot drift out of step with it: the root ScriptNode is the top
// exactly while nothing is attached below it, and it holds statement tokens only
// until that statement is recognised - ScriptNode::PutToken clears m_tokens the
// moment it creates a child, and a completed child is popped back off by
// CloseAll/CloseTop.
//
// Both halves are load-bearing. A partial recognition match (a leading CREATE
// still waiting for OR REPLACE ...) is the one case where the root is the top WITH
// tokens pending, and that is not a statement start: reporting it as one would let
// the lexer turn a following line into a comment halfway through a DDL header.
bool PlSqlAnalyzer::AtStatementStart () const
{
    return m_top == m_product.Root() && m_product.Root()->GetTokens().empty();
}

void PlSqlAnalyzer::PutToken (const Token& token)
{
    m_product.m_tokenCounter++;

    if (token == etEOF 
    || token == etEOS
    || (token == etSLASH && token.offset == 0))
    {
        // An END held for a label decision that never came: the statement is over,
        // so it goes to whatever is still open before everything is closed out.
        FlushPendingEnd();

        Token eos;
        eos.token = etEOS;
        eos.line = token.line;
        std::unique_ptr<SyntaxNode> dummy;
        m_top->PutToken(eos, dummy);
        CloseAll(eos);
        m_error = false;
        return;
    }

    // Record every comment's span before anything else can drop it.
    //
    // Control flow is deliberately NOT changed here: the existing
    // `if (token == etCOMMENT ...) return;` inside the `!m_error` branch below
    // still decides what reaches the tree, so error recovery behaves exactly as
    // before. This only takes a copy of the position.
    //
    // A multi-line /* ... */ arrives as ONE token whose `line` is where it
    // STARTED and whose value carries the interior line breaks - the lexer
    // appends '\n' for every line it stays open (PlSqlParser.cpp:256). `length`
    // is a character count summed across all of those lines and says nothing
    // about rows, so the end line has to be counted out of the value.
    if (token == etCOMMENT)
    {
        PlSqlParseProduct::CommentSpan span;
        span.startLine   = token.line;
        span.startOffset = token.offset;
        span.endLine     = token.line;
        for (size_t i = 0; i < token.value.size(); ++i)
            if (token.value[i] == L'\n')
                ++span.endLine;
        m_product.m_comments.push_back(span);
    }

    if (!m_error)
    {
        if (token == etCOMMENT
        || (token == etEOL && !m_top->WantsEOL()))
            return;

        // A held END is resolved by THIS token rather than delivered beside it.
        if (m_hasPendingEnd)
        {
            ResolvePendingEnd(token);
            return;
        }

        OfferToken(token);
    }
}

// Hand `token` to the tree, unless it is an END that a named construct should
// get a chance to look past first - see SyntaxNode::WantsDeferredEnd.
//
// Split from PutToken because the deferred-END path has to re-offer a reclaimed
// END to the enclosing construct through this very gate, and skipping the gate
// there would let the parent swallow a terminator that is not its own either.
void PlSqlAnalyzer::OfferToken (const Token& token)
{
    if (token == etEND && m_top->WantsDeferredEnd())
    {
        m_pendingEnd    = token;
        m_hasPendingEnd = true;
        return;
    }

    DeliverToTree(token);
}

// The token-delivery half of PutToken, unchanged in behaviour: create a child if
// the node wants one, bail out on a failure, and pop completed nodes.
void PlSqlAnalyzer::DeliverToTree (const Token& token)
{
    if (m_error)
        return;

    std::unique_ptr<SyntaxNode> child;

    auto* caseNode = dynamic_cast<CaseNode*>(m_top);
    if (caseNode && caseNode->IsEndState() && token.token != etCASE)
    {
        caseNode->SetCompleted();
        CloseTop(caseNode->GetTokens().empty() ? Token() : *(caseNode->GetTokens().rbegin()));
    }

    m_top->PutToken(token, child);

    if (child.get())
    {
        Attach(child.release());
    }
    else if (m_top->IsFailed())
    {
        TRACE("FALURE: token = %s, line = %d, col = %d\n", PlSqlParserEx::GetStringToken(token), token.line+1, token.offset+1);

        m_error = true;
        Token failure  = token;
        failure.token  = etFAILURE;
        failure.length = 0;
        CloseAll(failure);
        return;
    }
    else
    {
        while (m_top->IsCompleted())
            CloseTop(token);
    }
}

// The token after a held END has arrived, so its fate can now be decided.
//
// Two outcomes. Either the label is this node's own (or there is no label at all
// - a bare `END;`, `END LOOP`, ...), in which case both tokens are delivered and
// the node closes exactly as it did before this existed. Or the label belongs to
// an ENCLOSING construct, and the node must give the terminator back.
//
// Giving it back deliberately does NOT call Failure(). The caller asked for the
// folding range to stop swallowing the parent's `END`, not for a new syntax
// error, and Failure() here would not stay local anyway: DeliverToTree sees
// m_top->IsFailed() with no child created and answers m_error = true plus
// CloseAll(etFAILURE), abandoning the rest of the statement. So the node is
// marked complete without a terminator - AbandonTerminator, which is what keeps
// Colapse() off it - and popped.
void PlSqlAnalyzer::ResolvePendingEnd (const Token& token)
{
    Token end = m_pendingEnd;
    m_hasPendingEnd = false;

    // Reclaim only when there IS an enclosing construct to reclaim for. With the
    // root as parent there is nowhere to hand the terminator: ScriptNode::PutToken
    // has no etEND case, so it would fall to default: and accumulate `END pkg;` as
    // stray statement text, then wrap it in whatever ScriptFactory makes of that -
    // a spurious node where today there is none. A mismatched label at TOP LEVEL
    // therefore keeps today's behaviour and the node swallows the END, which is the
    // tidier of two wrong answers for input Oracle rejects anyway.
    SyntaxNode* parent = m_top->GetParent();

    if (!m_top->RejectsEndLabel(token)
    || !parent
    || parent->IsScriptNode())
    {
        DeliverToTree(end);
        DeliverToTree(token);
        return;
    }

    m_top->AbandonTerminator();
    CloseTop(end);

    // Re-offer rather than deliver: the enclosing construct is very often a named
    // one too, and it must get the same chance to look past this END.
    OfferToken(end);

    if (m_hasPendingEnd)
    {
        // It held the END, so `token` is THAT node's label decision. Recursing is
        // bounded: every branch that gets here has just popped a node, so m_top
        // ascends strictly toward the root, whose WantsDeferredEnd is false.
        ResolvePendingEnd(token);
        return;
    }

    DeliverToTree(token);
}

// Statement end with an END still held. No following token will ever decide it,
// so it goes straight to the tree, bypassing the gate on purpose: re-offering it
// would just hold it again (m_top is unchanged and still WantsDeferredEnd) and
// silently drop it. What follows is the CloseAll in PutToken's statement-end
// branch, which force-closes whatever is still open - the same fate an
// unterminated construct always had.
void PlSqlAnalyzer::FlushPendingEnd ()
{
    if (!m_hasPendingEnd)
        return;

    Token end = m_pendingEnd;
    m_hasPendingEnd = false;
    DeliverToTree(end);
}

bool PlSqlParseProduct::FindToken (int line, int offset, const SyntaxNode*& node, int& index) const
{ 
    return m_root->FindToken(line, offset, node, index); 
}

// True when (line, offset) lies inside the node's own first..last token span, which
// brackets its whole subtree. Offset-granular, unlike GetHitCode's line-only test.
static bool CoversPosition (const SyntaxNode* node, int line, int offset)
{
    Square span;

    if (!node->GetOwnExtent(span))
        return false;

    const bool atOrAfterStart = span.start.line <  line
                             || (span.start.line == line && span.start.column <= offset);
    const bool atOrBeforeEnd  = span.end.line   >  line
                             || (span.end.line   == line && span.end.column   >= offset);

    return atOrAfterStart && atOrBeforeEnd;
}

// True when the position sits BELOW a block's opening line and ABOVE its closing line -
// the part of an IF / CASE / LOOP that the whole-file emitters indent one level deeper
// than the construct itself. The brackets are not that part: IF…THEN, WHEN…THEN, LOOP and
// END IF; are emitted at the construct's OWN level, so a fragment whose end lands on
// END IF; has to measure the same depth as one whose end lands on the statement before it.
// Otherwise the two ends disagree, the seed is declined, and the commonest selections of
// all - a block plus its closing line - stay unseeded.
//
// Line-granular on purpose. Token-granular would need the body-opening keyword, and a CASE
// has one per WHEN arm rather than one; a line test gets every multi-line shape right with
// no per-construct knowledge, including CASE, whose WHEN lines are genuinely one level in.
// The known cost is a body statement written on the SAME line as its bracket (IF x THEN
// y := 1;), which measures at the bracket's level instead of one below. Such a fragment is
// emitted and dedented by that same amount, so its own relative shape still comes out
// right; only its absolute level differs, and that level is what the dedent removes.
static bool CoversBlockBody (const SyntaxNode* node, int line)
{
    Square span;

    if (!node->GetOwnExtent(span))
        return false;

    return span.start.line < line && line < span.end.line;
}

// One step of FindSection's walk. The DEEPEST node reporting a section wins, and
// `depth` counts the indent levels the ancestors above `node` contribute.
static void FindSectionImpl (const SyntaxNode* node, int line, int offset, int depth,
                             int& recursion, SectionHit& hit)
{
    // The bound SyntaxNode::RECURSION_LIMIT puts on GetLineStatus, restated because
    // that constant is protected and this walk is a free function.
    static const int kRecursionLimit = 1000;

    if (!node)
        return;

    if (++recursion > kRecursionLimit)
    {
        --recursion;
        return;
    }

    // A barrier contributes no hit and is not entered, so every position inside a
    // compound trigger keeps the unseeded behaviour it has today.
    if (node->BlocksSeedDescent())
    {
        --recursion;
        return;
    }

    if (CoversPosition(node, line, offset))
        hit.innermost = node;

    const EOutlineItemType itemType = node->GetOutlineItemType();
    const SyntaxNode::ESectionHit section = node->GetSectionAt(line, offset);

    // A construct's own sections are emitted one level below its header, which is
    // the "+ 1" every emitter in the whole-file path applies.
    if (section != SyntaxNode::shNone)
    {
        hit.section = section;
        hit.owner   = itemType;
        hit.depth   = depth + 1;
    }

    // So are the bodies of IF / CASE / LOOP, and so is a nested section owner: an
    // indent-adding node BELOW the one that reported the hit deepens it in step.
    const bool addsIndent = node->OwnsSections()
                         || itemType == oitIf || itemType == oitCase || itemType == oitLoop;

    if (addsIndent && hit.section != SyntaxNode::shNone && CoversBlockBody(node, line))
        hit.depth = depth + 1;

    const int childDepth = addsIndent ? depth + 1 : depth;

    for (const SyntaxNode* child = node->GetChild(); child; child = child->GetSibling())
    {
        const SyntaxNode::EHitCode hc = child->GetHitCode(line, 1);

        if (hc == SyntaxNode::ABOVE)
            break;                       // siblings are in buffer order
        if (hc == SyntaxNode::BELOW)
            continue;

        FindSectionImpl(child, line, offset, childDepth, recursion, hit);
    }

    --recursion;
}

bool PlSqlParseProduct::FindSection (int line, int offset, SectionHit& hit) const
{
    hit = SectionHit();

    if (!m_root.get())
        return false;

    int recursion = 0;
    FindSectionImpl(m_root.get(), line, offset, 0, recursion, hit);

    return hit.section != SyntaxNode::shNone;
}

void PlSqlParseProduct::GetLineStatus (LineStatusMap& statusMap) const
{ 
    int recursion = 0;
    m_root->GetLineStatus(statusMap, recursion); 
}

// ---------------------------------------------------------------------------
// Outline extraction
// ---------------------------------------------------------------------------

// Extract a whitespace-collapsed text snippet from the document in the range
// [startLine:startCol, endLine:endCol).  Appends L"\u2026" if the result
// exceeds maxLen characters.  Leading/trailing whitespace is trimmed.
static std::wstring ExtractSnippet (const OutlineTextSource& text,
                                    int startLine, int startCol,
                                    int endLine,   int endCol,
                                    int maxLen = 40)
{
    std::wstring result;
    result.reserve(maxLen + 4);
    Common::OEStringW buf;

    // These bounds come from the token stream, where an unterminated statement
    // at EOF ends on a synthetic {column 0, line == GetLineCount()} sentinel —
    // one line past the last real one.  GetLineW indexes a bounds-checked
    // container, so honouring that line would throw out of the outline pass and
    // take folding down with it.  A snippet is cosmetic: clamp it to the real
    // text and let a label come up short rather than fatal.
    const int lastLine = text.GetLineCount() - 1;
    if (lastLine < 0)
        return result;

    if (endCol <= 0)
        endCol = INT_MAX;   // "to end of line", as the clause-slot caller does
    endLine   = min(endLine, lastLine);
    startLine = max(0, min(startLine, lastLine));
    if (startCol < 0)
        startCol = 0;

    for (int ln = startLine; ln <= endLine; ++ln)
    {
        text.GetLineW(ln, buf);
        int lineLen = static_cast<int>(buf.length());
        int col0 = (ln == startLine) ? startCol : 0;
        int col1 = (ln == endLine)   ? min(endCol, lineLen) : lineLen;
        if (col0 >= col1) continue;

        const wchar_t* lineData = buf.data();
        for (int c = col0; c < col1; ++c)
        {
            wchar_t ch = lineData[c];
            // Collapse runs of whitespace to a single space
            if (ch == L'\t' || ch == L' ')
            {
                if (!result.empty() && result.back() != L' ')
                    result += L' ';
            }
            else
            {
                result += ch;
            }
            if (static_cast<int>(result.size()) >= maxLen)
            {
                // trim trailing space before ellipsis
                while (!result.empty() && result.back() == L' ')
                    result.pop_back();
                result += L'\u2026'; // …
                return result;
            }
        }
        // Separate lines with a space
        if (ln < endLine && !result.empty() && result.back() != L' ')
            result += L' ';
    }
    // Trim trailing space
    while (!result.empty() && result.back() == L' ')
        result.pop_back();
    return result;
}

// Walk a node's token array and build label "TYPE name1[.name2]"
// The name tokens are the last non-keyword identifier(s) stored before IS/AS.
// For PackageNode / ProcedureNode / FunctionNode / TriggerNode / TypeBodyNode
// the layout is: keyword(s) … eName1-token [DOT eName2-token] IS …
// We use the following heuristic:
//   - last token's line == endLine of the node (the closing ; / EOS)
//   - eName1 and optional eName2 are non-keyword, non-punctuation UNKNOWN/IDENTIFIER tokens
//     found before the IS/AS/BEGIN position and after the type keyword.

// node class name and then grab the last 1-or-2 identifiers seen before IS/AS.

// Generic label builder: scan the token array
// tokens that appear before the first IS/AS/BEGIN/BODY keyword.
template<class NodeT>
static std::wstring BuildNodeLabel(const std::wstring& typePrefix,
                                   const NodeT* n)
{
    // Collect text for tokens that look like identifiers (not reserved keywords,
    // or are DOT separators).  Stop when we see IS/AS/BEGIN.
    std::wstring name1, name2;
    bool hasDot = false;
    auto& tokens = n->GetTokens();
    for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
    {
        const Token& tok = tokens[i];
        switch (tok.token)
        {
        case etIS: case etAS: case etBEGIN:
            goto done;
        case etDOT:
            hasDot = true;
            break;
        case etUNKNOWN: case etDOUBLE_QUOTED_STRING:
            if (hasDot) { name2 = tok.value; hasDot = false; }
            else        { name1 = tok.value; name2.clear(); }
            break;
        default:
            break;
        }
    }
done:
    std::wstring label = typePrefix;
    if (!name1.empty())
    {
        label += L' ';
        label += name1;
        if (!name2.empty()) { label += L'.'; label += name2; }
    }
    return label;
}

// Label builder for TypeMemberNode: tokens may start with an optional lead
// keyword (NOT/OVERRIDING/MAP/ORDER/STATIC/CONSTRUCTOR), then PROCEDURE/
// FUNCTION, followed by the member name (possibly schema-qualified), then
// arguments. Stops name gathering at the first
// LEFT_ROUND_BRACKET/RETURN/COMMA/RIGHT_ROUND_BRACKET.
//
// Label rules: CONSTRUCTOR name / PROCEDURE name / FUNCTION name /
// MAP FUNCTION name / ORDER FUNCTION name / STATIC PROCEDURE name, etc.
// (CONSTRUCTOR drops the PROCEDURE/FUNCTION keyword, everything else keeps it.)
template<class NodeT>
static std::wstring BuildTypeMemberLabel(const NodeT* n)
{
    std::wstring lead, prefix, name1, name2;
    bool hasDot = false;
    bool sawProcOrFunc = false;
    auto& tokens = n->GetTokens();
    for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
    {
        const Token& tok = tokens[i];
        switch (tok.token)
        {
        case etPROCEDURE:
            prefix = L"PROCEDURE";
            sawProcOrFunc = true;
            break;
        case etFUNCTION:
            prefix = L"FUNCTION";
            sawProcOrFunc = true;
            break;
        case etLEFT_ROUND_BRACKET: case etRETURN:
        case etCOMMA: case etRIGHT_ROUND_BRACKET:
            goto done;
        case etDOT:
            hasDot = true;
            break;
        case etUNKNOWN: case etDOUBLE_QUOTED_STRING:
            if (!sawProcOrFunc) { lead = tok.value; break; } // lead keyword token
            if (hasDot) { name2 = tok.value; hasDot = false; }
            else        { name1 = tok.value; name2.clear(); }
            break;
        default:
            if (!sawProcOrFunc && tok.token == etORDER)
                lead = tok.value;
            break;
        }
    }
done:
    std::wstring label;
    if (!lead.empty())
    {
        std::wstring upperLead = lead;
        for (auto& ch : upperLead) ch = towupper(ch);
        if (upperLead == L"CONSTRUCTOR")
            label = L"CONSTRUCTOR";
        else if (upperLead == L"MAP" || upperLead == L"ORDER" || upperLead == L"STATIC")
            label = upperLead + L' ' + prefix;
        else
            label = prefix; // MEMBER / NOT / OVERRIDING - not shown, plain PROCEDURE/FUNCTION
    }
    else
        label = prefix;

    if (!name1.empty())
    {
        label += L' ';
        label += name1;
        if (!name2.empty()) { label += L'.'; label += name2; }
    }
    return label;
}

// Context passed through outline extraction to carry the text source, the
// settings, and the TWO product sinks.
struct OutlineCtx
{
    const OutlineTextSource& text;
    bool selectEnabled;   // if false, skip all SelectNodes
    bool selectOnly;      // if true, emit SELECT labels only (no FROM/WHERE children)
    // PANE budget: how many depth-0 units the Code Outline pane may list. It no
    // longer stops the tree walk - see GetOutlineItemsWithCtx - and it is a PANE
    // rule only: the fold sink keeps every candidate whether or not the budget is
    // spent, which is why the walk must still run to the end of the tree.
    int  maxTopItems;
    int  topItemCount;    // running count of depth-0 items the pane may still show

    // The two products (OEHelpers.h, FoldCandidate). `fold` is always non-null and
    // takes a geometry-only candidate for every construct the walk reaches.
    // `pane` is NULL when the caller wants folds alone, and that is the point of
    // the split: with no pane sink there is nothing to label, so every
    // BuildNodeLabel, BuildTypeMemberLabel and ExtractSnippet below is SKIPPED
    // rather than run and thrown away. Test WantPane() rather than the pointer
    // directly, so the two questions cannot drift apart.
    FoldCandidates* fold;
    OutlineItems*   pane;

    bool WantPane () const { return pane != nullptr; }
};

// ---------------------------------------------------------------------------
// Failure queries over a subtree
// ---------------------------------------------------------------------------

// Does this subtree hold a failed node? The outline walk answers this WITHOUT
// calling this function wherever it can: GetOutlineItemsWithCtx already recurses
// over exactly these nodes, so it propagates the answer on the way out and pays
// nothing extra (ToDoNotes 107 step 3 asks for precisely that). This helper is for
// the two places the walk cannot propagate through - a child it skips because the
// child holds no tokens, and a SelectNode, whose items come from a side recursion
// that only ever visits SelectNode/ExpressionNode and so never sees the rest of
// the subtree. Both are bounded and rare; the walk's own path stays linear.
static bool SubtreeHasFailedNode (const SyntaxNode* node)
{
    if (!node) return false;

    if (node->IsFailed())
        return true;

    for (const SyntaxNode* child = node->GetChild(); child; child = child->GetSibling())
        if (SubtreeHasFailedNode(child))
            return true;

    return false;
}

// Lowest failure line held anywhere in this subtree, or -1 when it holds none.
// Separate from the test above because the two questions differ: whether the parse
// gave up decides OutlineQuality, WHERE it gave up decides which ranges are
// invented. SyntaxNode::GeFailureLine is only meaningful on a node that IsFailed.
static int SubtreeFailureLine (const SyntaxNode* node)
{
    if (!node) return -1;

    int best = node->IsFailed() ? node->GeFailureLine() : -1;

    for (const SyntaxNode* child = node->GetChild(); child; child = child->GetSibling())
    {
        const int sub = SubtreeFailureLine(child);
        if (sub >= 0 && (best < 0 || sub < best))
            best = sub;
    }

    return best;
}

// Forward declarations.
//
// GetOutlineItemsWithCtx RETURNS "this subtree contains a failed node", which is
// how FoldCandidate::rangeTrusted gets stamped without a second walk: the
// candidate for a child is pushed BEFORE the recursion into that child, so the
// call remembers the index it landed at and writes the flag once the recursion
// reports back.
//
// Neither takes an `out` vector any more. The two products live on the ctx, so a
// recursion cannot accidentally emit into the wrong sink.
static void GetSelectOutlineItems  (OutlineCtx& ctx, const SyntaxNode* selectNode, int depth, bool paneVisible);
static bool GetOutlineItemsWithCtx (OutlineCtx& ctx, const SyntaxNode* node,       int depth, bool paneVisible);

// ---------------------------------------------------------------------------
// Folding-only statement items
// ---------------------------------------------------------------------------

// True for the folding-only item types the STATEMENT arm of the walk handles.
// Blocks and comment runs are folding-only as well, but they are emitted by
// their own arms, so they are excluded here. Deliberately a SUBSET of the shared
// IsFoldingOnlyOutlineType list in OEHelpers.h rather than a second copy of it:
// the shared list is the one the pane projection and the automated suite consult,
// so a type can never be added there without this arm seeing it too.
static bool IsFoldStatementType (EOutlineItemType type)
{
    return IsFoldingOnlyOutlineType(type)
        && type != oitBlock
        && type != oitComment;
}

// Full source extent of a subtree: the node's own tokens widened by every
// descendant's own tokens.
//
// Widening is a requirement, not a refinement. A statement node keeps only its
// structural keywords - UpdateNode holds UPDATE, SET, WHERE and ';', with the
// column list and the predicate in children - so "own last token" is not always
// the last character of the statement. It also guarantees the invariant
// FoldModel::BuildAvailableFromFiltered relies on: a child region can never end below its
// parent's, so nothing is dropped as an overlap.
static bool GetSubtreeExtent (const SyntaxNode* node, Square& pos)
{
    if (!node) return false;

    Square own;
    own.clear();

    bool have = node->GetOwnExtent(own);
    if (have) pos = own;

    for (const SyntaxNode* c = node->GetChild(); c; c = c->GetSibling())
    {
        Square sub;
        sub.clear();
        if (!GetSubtreeExtent(c, sub)) continue;

        if (!have) { pos = sub; have = true; continue; }

        if (sub.start < pos.start) pos.start = sub.start;
        if (pos.end   < sub.end)   pos.end   = sub.end;
    }

    return have;
}

// Emit one folding-only statement region. Returns false when the statement is not
// worth folding, i.e. when it does not span at least two lines - FoldModel D10
// would drop it anyway, and not emitting it keeps the snapshot small. The CALLER
// still recurses into the children either way, because a pane-visible unit nested
// in a one-line statement must not be lost.
//
// Fold sink only. Every type reaching this helper is in
// IsFoldingOnlyOutlineType, so there is no pane entry to build and no label: the
// constant-per-construct FoldStatementLabel this used to assign had no reader at
// all once folding stopped matching on labels at D16, and the pane never listed a
// statement, so it was pure waste on the hottest emit path in the walk.
static bool EmitStatementFoldItem (const SyntaxNode* node, EOutlineItemType type,
                                   int depth, FoldCandidates& out)
{
    Square pos;
    pos.clear();

    if (!GetSubtreeExtent(node, pos)) return false;
    if (pos.start.line >= pos.end.line) return false;   // one line: nothing to hide

    FoldCandidate item;
    item.pos.start     = pos.start;
    item.pos.end       = pos.end;
    item.depth         = depth;
    item.type          = type;
    out.push_back(item);
    return true;
}

// `paneVisible` is the inherited answer to "may the Code Outline pane show what
// this subtree produces?". It starts true at the root and is forced false by an
// anonymous block, so every node reached through one is handed to the fold sink
// alone. It is now a PANE-ONLY question: the fold sink takes the region either
// way, which is why nothing below tests it before emitting a candidate.
//
// Both products go to the sinks carried on `ctx` rather than to a parameter, so a
// recursion cannot cross-wire them.
static bool GetOutlineItemsWithCtx (
    OutlineCtx& ctx, const SyntaxNode* node, int depth, bool paneVisible)
{
    if (!node) return false;

    // Whether anything at or below `node` failed. Seeded with `node` itself and OR'd
    // with every child subtree's own answer, so what the bottom of this function
    // returns is the whole-subtree test the caller needs in order to stamp
    // FoldCandidate::rangeTrusted - obtained on the way out of a walk that was
    // happening anyway, which is what keeps step 3 from costing a second pass.
    bool failed = node->IsFailed();

    // `maxTopItems` is a CODE OUTLINE PANE budget, and this walk used to STOP here
    // once it was spent. That truncated folding as well: a script holding more
    // top-level units than the budget lost every fold below the cut, and
    // OutlineQuality never reported it (D6 / C2) because it was handed a snapshot
    // that had already been clipped. The walk now always runs to the end of the
    // tree; once the budget is spent the remaining depth-0 units simply stop
    // reaching the pane sink, so the pane shows exactly the same first
    // `maxTopItems` units it always did while folding sees the whole file.
    const SyntaxNode* child = node->GetChild();
    while (child)
    {
        const SyntaxNode* next = child->GetSibling();

        // Per CHILD, not once per call. The budget is spent inside this loop - see
        // the two `++ctx.topItemCount` below - so a paneHere hoisted above the loop
        // is evaluated while the count is still 0 and then stays stale for every
        // later unit. Worse, it could never be true at all: `depth == 0` holds only
        // on this call, because both recursive calls below pass depth + 1 or an
        // unchanged depth for a node that is not itself a pane unit, and a
        // maxTopItems of 0 arrives already mapped to INT_MAX. Hoisted, the test was
        // `0 >= 500` for every child of the root, so PlSqlOutlineMaxTopItems was
        // inert and the pane received every top-level unit in the file regardless of
        // the budget the user set - the exact flooding the budget exists to prevent,
        // and the reason the pane entry emitted below could only ever be
        // suppressed by an enclosing anonymous block.
        const bool paneCapped = (depth == 0 && ctx.topItemCount >= ctx.maxTopItems);
        const bool paneHere   = paneVisible && !paneCapped;

        EOutlineItemType childType = child->GetOutlineItemType();

        switch (childType)
        {
        case oitBlock:
            {
                // An anonymous DECLARE / bare BEGIN..END block.
                //
                // FOLD SINK ONLY. A block has never been listed by the Code Outline
                // pane, and nothing reached through one may be either, so the walk
                // passes paneVisible = false down and emits no pane item here at
                // all. That keeps the pane exactly as deep and as wide as it was
                // when this case was `break; // completely skip it for now` - but
                // the old break also PRUNED the subtree, which is why a subprogram
                // nested inside an anonymous block used to be invisible to folding
                // too. Recursing is what makes it foldable.
                //
                // No label is built: the pane is the only reader a label ever had,
                // and there is no pane entry here to carry one.
                //
                // Index the BLOCK candidate will occupy. Taken BEFORE the push, and
                // used after the recursion, which appends every candidate nested
                // inside the block and so cannot have moved it.
                const size_t selfFold = ctx.fold->size();

                if (auto* p = dynamic_cast<const BlockNode*>(child))
                {
                    auto& t = p->GetTokens();
                    if (!t.empty())
                    {
                        FoldCandidate item;
                        item.depth     = depth;
                        item.type      = oitBlock;
                        item.pos.start = { t.front().offset, t.front().line };
                        item.pos.end   = { t.back().endCol(), t.back().line };
                        ctx.fold->push_back(item);
                    }
                }

                const bool subtreeFailed =
                    GetOutlineItemsWithCtx(ctx, child, depth + 1, false);

                if (ctx.fold->size() > selfFold)
                    (*ctx.fold)[selfFold].rangeTrusted = !subtreeFailed;

                failed |= subtreeFailed;
            }
            break;
        case oitPackage:
        case oitPackageBody:
        case oitProcedure:
        case oitFunction:
        case oitTypeBody:
        case oitType:
        case oitTypeMemberFunction:
        case oitTypeMemberProcedure:
            {
                // The pane entry, built only if there is a pane to build it for.
                // `wantPaneItem` is the whole point of the split: when the caller
                // passed no pane sink - folding with the Code Outline pane closed,
                // which is the common case - every BuildNodeLabel and
                // BuildTypeMemberLabel below is SKIPPED instead of run and thrown
                // away, while the fold candidate still gets its geometry from the
                // very same token pair.
                const bool wantPaneItem = paneHere && ctx.WantPane();

                std::wstring label;

                bool haveTokens = false;
                const Token* front = nullptr;
                const Token* back  = nullptr;

                switch (childType)
                {
                case oitPackage:
                    { auto* p = static_cast<const PackageNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"PACKAGE", p); } }
                    break;
                case oitPackageBody:
                    { auto* p = static_cast<const PackageNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"PACKAGE BODY", p); } }
                    break;
                case oitProcedure:
                    { auto* p = static_cast<const ProcedureNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"PROCEDURE", p); } }
                    break;
                case oitFunction:
                    { auto* p = static_cast<const FunctionNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"FUNCTION", p); } }
                    break;
                case oitTypeBody:
                    { auto* p = static_cast<const TypeBodyNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"TYPE BODY", p); } }
                    break;
                case oitType:
                    { auto* p = static_cast<const TypeNode*>(child);
                      auto& t = p->GetTokens();
                      if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                          if (wantPaneItem) label = BuildNodeLabel(L"TYPE", p); } }
                    break;
                case oitTypeMemberFunction:
                case oitTypeMemberProcedure:
                    {
                        if (auto* p = dynamic_cast<const TypeMemberNode*>(child))
                        {
                            auto& t = p->GetTokens();
                            if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                                if (wantPaneItem) label = BuildTypeMemberLabel(p); }
                        }
                        else if (auto* p2 = dynamic_cast<const TypeBodyMemberNode*>(child))
                        {
                            auto& t = p2->GetTokens();
                            if (!t.empty()) { front = &t.front(); back = &t.back(); haveTokens = true;
                                if (wantPaneItem) label = BuildTypeMemberLabel(p2); }
                        }
                    }
                    break;
                default:
                    break;
                }

                if (haveTokens)
                {
                    // One span, two sinks. Both products describe the same unit, so
                    // measuring it once and handing the result to each is what keeps
                    // the split from costing a second token lookup.
                    Square span;
                    span.start = { front->offset, front->line };
                    span.end   = { back->endCol(), back->line };

                    // FOLD SINK: geometry only, and ALWAYS. This is the candidate
                    // folding lost whenever the walk used to stop at the pane's
                    // top-item budget, and the one it never needed a label for.
                    FoldCandidate fc;
                    fc.depth = depth;
                    fc.type  = childType;
                    fc.pos   = span;
                    const size_t selfFold = ctx.fold->size();
                    ctx.fold->push_back(fc);

                    // PANE SINK: the enriched entry, and only when the pane may
                    // really list this unit. `paneHere` is false inside an anonymous
                    // block and once the top-item budget is spent - both pane rules,
                    // which is why the candidate above is emitted either way.
                    if (wantPaneItem)
                    {
                        OutlineItem item;
                        item.depth = depth;
                        item.type  = childType;
                        item.pos   = span;
                        item.label = std::move(label);
                        ctx.pane->push_back(item);

                        // PlSqlOutlineMaxTopItems is a PANE budget: it exists to stop
                        // the tree view being flooded. An item the pane will never
                        // show must not spend it, or turning on block folding would
                        // silently push real units out of the outline.
                        if (depth == 0) ++ctx.topItemCount;
                    }

                    const bool subtreeFailed =
                        GetOutlineItemsWithCtx(ctx, child, depth + 1, paneHere);

                    // A unit's range is only as good as the parse inside it. When a
                    // descendant failed, CloseAll ran Colapse() over everything below
                    // level 1 (OEPlsSqlSyntax.cpp:233-243), so this node's last token
                    // is the failure token rather than its real terminator and the end
                    // column above describes where the parser gave up, not where the
                    // construct does. Only the fold candidate carries the answer: the
                    // pane navigates to a start, and never trusted an end.
                    (*ctx.fold)[selfFold].rangeTrusted = !subtreeFailed;
                    failed |= subtreeFailed;
                }
                else
                {
                    // No tokens: no item, and the subtree is not walked at all -
                    // long-standing behaviour this pass does not change. It still has
                    // to answer for its own failures, or an enclosing unit would
                    // report a clean subtree nobody ever looked at.
                    failed |= SubtreeHasFailedNode(child);
                }
            }
            break;

        default:
            if (const auto* p = dynamic_cast<const SelectNode*>(child))
            {
                // A SELECT folds wherever it sits, not only at depth 0. The old
                // `depth == 0` filter was a PANE rule - the pane lists top-level
                // SELECTs only - but it was applied to the walk itself, so every
                // SELECT inside a package or subprogram body was skipped outright
                // and could not fold. Same for selectEnabled, which is the
                // PlSqlOutlineSelectEnabled PANE preference. Both now decide only
                // paneSelect; the legs are always emitted as fold candidates.
                const bool paneSelect = paneHere && depth == 0 && ctx.selectEnabled;
                const size_t firstFold = ctx.fold->size();
                GetSelectOutlineItems(ctx, p, depth, paneSelect);

                // Every leg and clause candidate this SELECT produced shares one
                // answer, because their ends all come from this SelectNode's own
                // token list. This is the one emit path that pays for
                // SubtreeHasFailedNode: the side recursion only descends into
                // SelectNode/ExpressionNode, so it has no way to propagate an answer
                // about the rest of the subtree.
                const bool subtreeFailed = SubtreeHasFailedNode(p);

                for (size_t k = firstFold; k < ctx.fold->size(); k++)
                    (*ctx.fold)[k].rangeTrusted = !subtreeFailed;

                // Charged only when the pane can really show it, same reasoning as
                // for the named units above.
                if (paneSelect) ++ctx.topItemCount;
                failed |= subtreeFailed;
            }
            else if (IsFoldStatementType(childType))
            {
                // IF / CASE / loop / INSERT / UPDATE / DELETE / OPEN cursor /
                // TRIGGER / generic SQL. FOLD SINK ONLY: EmitStatementFoldItem
                // writes a candidate and nothing else, so the Code Outline pane is
                // untouched by every one of them.
                //
                // Recursion happens whether or not a region was emitted, so a
                // single-line statement stays transparent and a unit nested inside
                // one still gets its own fold candidate. Depth advances only when the
                // statement really produced a region, which keeps the depth of
                // every nested candidate exactly what it was before.
                const size_t selfFold = ctx.fold->size();
                const bool emitted = EmitStatementFoldItem(child, childType, depth, *ctx.fold);

                const bool subtreeFailed =
                    GetOutlineItemsWithCtx(ctx, child, depth + (emitted ? 1 : 0), false);

                // EmitStatementFoldItem pushes exactly one candidate when it returns
                // true and none when it returns false, so selfFold is that
                // candidate's index.
                if (emitted)
                    (*ctx.fold)[selfFold].rangeTrusted = !subtreeFailed;

                failed |= subtreeFailed;
            }
            else
            {
                failed |= GetOutlineItemsWithCtx(ctx, child, depth, paneHere);
            }
            break;
        }

        child = next;
    }

    return failed;
}

/*static*/ bool PlSqlParseProduct::HasFailedNodeImpl (const SyntaxNode* node)
{
    // Same walk, one definition. The outline extraction uses SubtreeHasFailedNode
    // directly only where it cannot propagate the answer out of a recursion it is
    // already making; see that helper's comment.
    return SubtreeHasFailedNode(node);
}

int PlSqlParseProduct::GetFailureLine () const
{
    return SubtreeFailureLine(m_root.get());
}

// ---------------------------------------------------------------------------
// SELECT outline extraction
// ---------------------------------------------------------------------------

// Token types used as clause markers inside SelectNode::m_tokens
// (same values the parser stores via SET_AND_RET)
static bool IsSetOperator (int tok)
{
    return tok == etUNION || tok == etINTERSECT || tok == etMINUS_SQL;
}

static bool IsSetOperatorOrAll (int tok)
{
    return IsSetOperator(tok) || tok == etALL;
}

static const wchar_t* SetOperatorLabel (int tok)
{
    if (tok == etUNION)     return L"UNION SELECT\t";
    if (tok == etINTERSECT) return L"INTERSECT SELECT\t";
    return                         L"MINUS SELECT\t";
}

// Recursively emit clause items for one SELECT leg.
// clauseTokens  — the clause keyword tokens for this leg (SELECT, FROM, WHERE …)
// legChildren   — child nodes whose start line falls within this leg
// depth         — outline depth for the clause items
static void EmitSelectLeg (
    OutlineCtx&                      ctx,
    const std::wstring&              legLabel,
    EOutlineItemType                 legType,
    const Token&                     legStartToken,
    const Token&                     selectToken,    // actual SELECT keyword of this leg
    OpenEditor::Position             legEndPos,
    const std::vector<Token>&        clauseTokens,  // FROM, WHERE … (not SELECT itself)
    const std::vector<const SyntaxNode*>& legChildren,
    int                              depth,
    bool                             paneVisible)
{
    // Emit the SELECT / UNION-SELECT leg itself.
    {
        Square span;
        span.start = { legStartToken.offset, legStartToken.line };
        span.end   = legEndPos;

        // FOLD SINK: always. A leg folds at whatever depth it sits, including one
        // nested inside a subprogram body, which is why nothing here tests
        // paneVisible before emitting.
        FoldCandidate fc;
        fc.depth = depth;
        fc.type  = legType;
        fc.pos   = span;
        ctx.fold->push_back(fc);

        // PANE SINK: the labelled entry, and the only place this leg pays for
        // ExtractSnippet. Skipped outright when the pane is not listening, or is not
        // allowed to list this leg - which is the common case, since folding keeps
        // this walk warm with the pane closed.
        //
        // Snippet: text between end of SELECT keyword and start of first clause
        // (FROM). If no FROM exists use legEndPos as the limit.
        if (paneVisible && ctx.WantPane())
        {
            const Token* fromTok = nullptr;
            for (const Token& ct : clauseTokens)
                if (ct.token == etFROM) { fromTok = &ct; break; }

            int snipEndLine = fromTok ? fromTok->line   : legEndPos.line;
            int snipEndCol  = fromTok ? fromTok->offset : legEndPos.column;

            std::wstring fullLabel = legLabel;

            std::wstring snippet = ExtractSnippet(ctx.text,
                selectToken.line, selectToken.endCol(),
                snipEndLine, snipEndCol);
            if (!snippet.empty()) { fullLabel += L"  "; fullLabel += snippet; }

            OutlineItem legItem;
            legItem.label = std::move(fullLabel);
            legItem.type  = legType;
            legItem.pos   = span;
            legItem.depth = depth;
            ctx.pane->push_back(legItem);
        }
    }

    int clauseDepth = depth + 1;

    // Build clause slots in token order, prefixed with a sentinel (SELECT list).
    struct ClauseSlot
    {
        Token                startToken;  // the clause keyword token (FROM, WHERE …)
        OpenEditor::Position endPos;      // end position of this clause
        std::wstring         label;       // base keyword label (no snippet yet)
        EOutlineItemType     type;
    };

    std::vector<ClauseSlot> slots;

    // Sentinel slot: covers the SELECT column list (before FROM)
    slots.push_back({ selectToken, legEndPos, L"", oitSelect });

    for (const Token& tok : clauseTokens)
    {
        ClauseSlot s;
        s.startToken = tok;
        s.endPos     = legEndPos; // will be trimmed below
        switch (tok.token)
        {
        case etFROM:    
            s.label = L"FROM\t";     
            s.type = oitFromClause;
            break;
        case etWHERE:   
            s.label = L"WHERE\t";    
            s.type = oitWhereClause;
            break;
        default:        s.label = L"";         break; // GROUP BY etc. if ever added
        }
        if (!s.label.empty())
            slots.push_back(s);
    }

    // Trim each slot's end to one before the next slot's start line
    for (int i = 0; i + 1 < static_cast<int>(slots.size()); ++i)
        slots[i].endPos = { 0, slots[i + 1].startToken.line - 1 };

    // slot 0 is the sentinel for the column list — no label, kept for child placement only
    slots[0].label = L"";

    // For each clause slot that has a label, emit a clause outline item,
    // then emit any child subselects whose start line falls in that slot.
    // Children in the column-list slot (slots[0]) are emitted at clauseDepth
    // directly under the leg, before the FROM item.

    // Collect children per slot index
    std::vector<std::vector<const SyntaxNode*>> slotChildren(slots.size());
    for (const SyntaxNode* child : legChildren)
    {
        const auto* sc = dynamic_cast<const SelectNode*>(child);
        if (!sc) continue;
        auto& ct = sc->GetTokens();
        if (ct.empty()) continue;
        int childLine = ct.front().line;
        // Find the last slot whose line <= childLine
        int slotIdx = 0;
        for (int i = 1; i < static_cast<int>(slots.size()); ++i)
            if (slots[i].startToken.line <= childLine) slotIdx = i;
        slotChildren[slotIdx].push_back(child);
    }

    // Emit column-list subqueries first (slot 0), then clause slots
    for (const SyntaxNode* sub : slotChildren[0])
        GetSelectOutlineItems(ctx, sub, clauseDepth, paneVisible);

    for (int i = 1; i < static_cast<int>(slots.size()); ++i)
    {
        const ClauseSlot& slot = slots[i];

        // FOLD SINK: always. `selectOnly` is the PlSqlOutlineSelectOnly PANE
        // preference - "show SELECT legs only, suppress FROM/WHERE nodes" - and it
        // used to gate this emit outright, so ticking a pane checkbox also took
        // every clause region out of the gutter. That is the same leak the walk
        // already fixed for maxTopItems, `depth == 0` and selectEnabled: a pane rule
        // applied to the traversal itself. It now gates the pane entry below only.
        {
            FoldCandidate fc;
            fc.depth     = clauseDepth;
            fc.type      = slot.type;
            fc.pos.start = { slot.startToken.offset, slot.startToken.line };
            fc.pos.end   = slot.endPos;
            ctx.fold->push_back(fc);
        }

        if (!ctx.selectOnly && paneVisible && ctx.WantPane())
        {
            // Snippet: text from end of clause keyword to start of next slot (or legEndPos)
            int snipEndLine = (i + 1 < static_cast<int>(slots.size()))
                              ? slots[i + 1].startToken.line
                              : slot.endPos.line;
            int snipEndCol  = (i + 1 < static_cast<int>(slots.size()))
                              ? slots[i + 1].startToken.offset
                              : slot.endPos.column;

            std::wstring fullLabel = slot.label;

            std::wstring snippet = ExtractSnippet(ctx.text,
                slot.startToken.line, slot.startToken.endCol(),
                snipEndLine, snipEndCol == 0 ? INT_MAX : snipEndCol);
            if (!snippet.empty()) { fullLabel += L"  "; fullLabel += snippet; }

            OutlineItem ci;
            ci.label     = std::move(fullLabel);
            ci.type      = slot.type;
            ci.pos.start = { slot.startToken.offset, slot.startToken.line };
            ci.pos.end   = slot.endPos;
            ci.depth     = clauseDepth;
            ctx.pane->push_back(ci);
        }

        // Always recurse into subqueries regardless of selectOnly
        for (const SyntaxNode* sub : slotChildren[i])
            GetSelectOutlineItems(ctx, sub, ctx.selectOnly ? clauseDepth : clauseDepth + 1,
                                  paneVisible);
    }
}

static void GetSelectOutlineItems (
    OutlineCtx& ctx, const SyntaxNode* selectNode, int depth, bool paneVisible)
{
    const auto* sn = dynamic_cast<const SelectNode*>(selectNode);
    if (!sn) return;

    const auto& tokens = sn->GetTokens();
    if (tokens.empty()) return;

    // Collect all child SelectNodes in source order (sibling chain).
    // Subqueries are wrapped in ExpressionNode by the parser, so look one
    // level deeper when the direct child is not itself a SelectNode.
    std::vector<const SyntaxNode*> allChildren;
    for (const SyntaxNode* c = sn->GetChild(); c; c = c->GetSibling())
    {
        if (dynamic_cast<const SelectNode*>(c))
        {
            allChildren.push_back(c);
        }
        else if (dynamic_cast<const ExpressionNode*>(c))
        {
            // ExpressionNode wraps a parenthesised subquery — its first child
            // is the actual SelectNode
            for (const SyntaxNode* inner = c->GetChild(); inner; inner = inner->GetSibling())
                if (dynamic_cast<const SelectNode*>(inner))
                    allChildren.push_back(inner);
        }
    }

    // Partition tokens into legs separated by UNION/INTERSECT/MINUS.
    // Each leg starts with a SELECT token and contains its clause tokens.
    // We also need the end line of each leg = start of next leg - 1 (or last token line).
    struct Leg
    {
        EOutlineItemType     type;
        std::wstring         label;
        Token                startToken;     // first token of this leg (SELECT or set-op)
        Token                selectToken;    // the actual SELECT keyword of this leg
        OpenEditor::Position endPos;
        std::vector<Token>   clauseTokens;   // FROM, WHERE (not SELECT itself)
        int                  childStartLine;
        int                  childEndLine;
    };

    std::vector<Leg> legs;
    // Determine the end position of the whole select node.
    // If the last token is etUNKNOWN it is a synthetic marker placed at the
    // exact offset of the closing ')' — use that position directly.
    // For top-level SELECTs the last token is a real keyword.
    const Token& lastTok = tokens.back();
    OpenEditor::Position selectEndPos = lastTok.token == etUNKNOWN
        ? OpenEditor::Position{ lastTok.offset, lastTok.line }
        : OpenEditor::Position{ lastTok.endCol(), lastTok.line };

    for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
    {
        const Token& tok = tokens[i];
        if (tok.token == etSELECT)
        {
            Leg leg;
            if (legs.empty())
            {
                leg.label       = L"SELECT\t";
                leg.type        = oitSelect;
                leg.startToken  = tok;
            }
            else
            {
                // Set-op leg starts at the UNION/INTERSECT/MINUS token.
                // Scan back past optional ALL to find the actual operator token.
                int j = i - 1;
                while (j > 0 && tokens[j].token == etALL) --j;
                const Token& opTok = tokens[j];
                leg.label       = SetOperatorLabel(opTok.token);
                leg.type        = oitSelect;
                leg.startToken  = opTok;
            }
            leg.selectToken    = tok;   // always the SELECT keyword
            leg.endPos         = selectEndPos;
            leg.childStartLine = tok.line;
            leg.childEndLine   = selectEndPos.line;
            legs.push_back(leg);
        }
        else if (!legs.empty() && !IsSetOperatorOrAll(tok.token) && tok.token != etWITH)
        {
            // Clause token belonging to the current leg
            legs.back().clauseTokens.push_back(tok);
        }
    }

    if (legs.empty()) return;

    // Trim each leg's end to one before the next leg's start line
    for (int i = 0; i + 1 < static_cast<int>(legs.size()); ++i)
        legs[i].endPos = { 0, legs[i + 1].startToken.line - 1 };

    // Assign children to legs by start line
    for (int i = 0; i < static_cast<int>(legs.size()); ++i)
    {
        legs[i].childEndLine   = legs[i].endPos.line;
        legs[i].childStartLine = legs[i].startToken.line;
    }

    // Emit each leg
    for (int i = 0; i < static_cast<int>(legs.size()); ++i)
    {
        const Leg& leg = legs[i];

        // Collect children whose start line falls within this leg's range
        std::vector<const SyntaxNode*> legChildren;
        for (const SyntaxNode* c : allChildren)
        {
            const auto* sc = dynamic_cast<const SelectNode*>(c);
            if (!sc) continue;
            auto& ct = sc->GetTokens();
            if (ct.empty()) continue;
            int cl = ct.front().line;
            if (cl >= leg.startToken.line && cl <= leg.endPos.line)
                legChildren.push_back(c);
        }

        EmitSelectLeg(ctx, leg.label, leg.type, leg.startToken, leg.selectToken,
                      leg.endPos, leg.clauseTokens, legChildren,
                      (i == 0) ? depth : depth + 1, paneVisible);
    }
}



// ---------------------------------------------------------------------------
// Comment runs - folding only, never shown in the Code Outline pane
// ---------------------------------------------------------------------------

// Depth for a folding-only candidate the tree walk did not place itself: the run
// is nested as deeply as the innermost candidate spanning it, so that
// FoldModel::CollapseToLevel (`depth <= maxDepth`, OEFolding.cpp) treats a
// comment block inside a procedure as one level below that procedure instead of
// as a top-level peer of it.
//
// Only the first `treeItems` entries are searched. The comment candidates appended
// by this same pass are maximal and disjoint, so none could ever contain a run,
// and excluding them keeps the answer independent of append order.
//
// It searches the FOLD vector, which is the complete set: the pane vector holds
// only what the pane may list, so a run inside an anonymous block would find no
// enclosing unit there and be promoted to depth 0.
static int EnclosingOutlineDepth (const FoldCandidates& items, size_t treeItems,
                                  int startLine, int endLine)
{
    int depth = -1;

    for (size_t i = 0; i < treeItems; i++)
    {
        const FoldCandidate& it = items.at(i);

        if (it.pos.start.line <= startLine && endLine <= it.pos.end.line
            && it.depth > depth)
            depth = it.depth;
    }

    return depth + 1;
}

// True when nothing but whitespace precedes the comment on its start line, i.e.
// the comment OWNS that row and folding it cannot hide code.
//
// This is decided from the text rather than from token order on purpose. The
// lexer records a multi-line quoted string against the line where the string
// STARTED and never gives an end line, so "the previous significant token was on
// an earlier line" does not prove this comment is alone on its row: a trailing
// comment written after the closing quote of a multi-line literal would pass
// that test, and folding it would swallow the literal's last line.
static bool IsOwnLineComment (const OutlineTextSource& text, const PlSqlParseProduct::CommentSpan& c)
{
    // The span's line can sit outside the buffer for the same reason its offset
    // can: the text moved under us between the analysis and this walk. GetLineW
    // indexes a bounds-checked container and raises out_of_range, and this runs
    // inside the view's idle handler, where a throw repeats on every pass and
    // leaves the application unable to answer at all. Same answer as the offset
    // test below - refuse to fold.
    if (c.startLine < 0 || c.startLine >= text.GetLineCount())
        return false;

    Common::OEStringW buf;
    text.GetLineW(c.startLine, buf);

    const int lineLen = static_cast<int>(buf.length());

    // A span reaching past the end of its own line means the text moved under us.
    // Refuse to fold: under-folding a comment is a small loss, over-folding
    // hides code.
    if (c.startOffset > lineLen)
        return false;

    // OEStringW has no operator[]; ExtractSnippet above uses the same .data()
    // accessor.
    const wchar_t* lineData = buf.data();

    for (int col = 0; col < c.startOffset; col++)
        if (!iswspace(lineData[col]))
            return false;

    return true;
}

// Appends one oitComment item per maximal run of consecutive comment-only lines.
//
// "Consecutive" means no blank line and no line of code in between. Both comment
// forms count and may be mixed inside a single run: `--` end-of-line comments
// and `/* ... */` block comments, including one multi-line /* */ standing on its
// own. A trailing comment (code, then `-- note`) never starts a run, and only
// joins one when its row is already established as comment-only, so a run always
// spans whole rows and collapsing it can only ever hide comment text.
//
// Single-line runs are not emitted at all: FoldModel::FilterItems would drop
// them anyway under D10 (the anchor stays visible, so there is nothing to hide),
// and leaving them out keeps the snapshot smaller.
//
// FOLD SINK ONLY. A comment run has never been listed by the Code Outline pane,
// so it takes a candidate and no label - the constant L"COMMENT" this used to
// assign had no reader once folding stopped matching on labels at D16.
static void AppendCommentRuns (const OutlineTextSource& text,
                               const std::vector<PlSqlParseProduct::CommentSpan>& comments,
                               FoldCandidates& out)
{
    if (comments.empty())
        return;

    const size_t treeItems = out.size();

    int runStart = -1;
    int runEnd   = -1;

    auto flushRun = [&]()
    {
        if (runStart >= 0 && runEnd > runStart)
        {
            FoldCandidate item;
            item.pos.clear();
            item.pos.start.column = 0;
            item.pos.start.line   = runStart;
            item.pos.end.column   = 0;
            item.pos.end.line     = runEnd;
            item.depth            = EnclosingOutlineDepth(out, treeItems, runStart, runEnd);
            item.type             = oitComment;
            out.push_back(item);
        }

        runStart = -1;
        runEnd   = -1;
    };

    for (size_t i = 0; i < comments.size(); i++)
    {
        const PlSqlParseProduct::CommentSpan& c = comments.at(i);

        // A second comment on a row the run already owns (`/* a */ /* b */`) is
        // accepted without re-testing the text: the row was proven comment-only
        // when the run reached it, and the only thing that can precede this
        // comment is that earlier comment plus whitespace.
        const bool rowAlreadyInRun = (runStart >= 0 && c.startLine == runEnd);

        if (!rowAlreadyInRun && !IsOwnLineComment(text, c))
        {
            // Code shares this row, so the run ends here and this comment
            // cannot begin a new one.
            flushRun();
            continue;
        }

        if (runStart < 0)
        {
            runStart = c.startLine;
            runEnd   = c.endLine;
        }
        else if (c.startLine == runEnd || c.startLine == runEnd + 1)
        {
            // Same row, or the row immediately below. Anything further away is
            // a blank line or a line of code, and breaks the run.
            if (c.endLine > runEnd)
                runEnd = c.endLine;
        }
        else
        {
            flushRun();
            runStart = c.startLine;
            runEnd   = c.endLine;
        }
    }

    flushRun();
}


void PlSqlParseProduct::GetOutlineProducts (const OutlineTextSource& text,
                                            const OutlineSettings& settings,
                                            FoldCandidates& fold,
                                            OutlineItems*   pane) const
{
    fold.clear();

    // `fold` is this call's whole output and is always filled from scratch.
    //
    // `pane` is a POINTER, and null means "no pane entries wanted" - which is the
    // signal the walk uses to skip every BuildNodeLabel, BuildTypeMemberLabel and
    // ExtractSnippet rather than run them and throw the results away. When it is
    // non-null it is likewise filled from scratch, so a caller cannot accidentally
    // accumulate entries across extractions.
    if (pane)
        pane->clear();

    OutlineCtx ctx {
        text,
        settings.selectEnabled,
        settings.selectOnly,
        settings.maxTopItems,
        0,
        &fold,
        pane
    };
    GetOutlineItemsWithCtx(ctx, m_root.get(), 0, true);

    // Comment runs come from the lexer's side table rather than from the tree,
    // because etCOMMENT is dropped before it can reach the tree at all. They are
    // appended after the walk so that EnclosingOutlineDepth can nest each run
    // inside whatever the walk found spanning it. FoldModel::SortItems re-sorts
    // everything by position anyway, so appending out of document order is safe.
    //
    // They also keep FoldCandidate::rangeTrusted at its default of TRUE, and that is
    // a deliberate answer rather than an oversight: a run's extent is measured from
    // the comment tokens themselves, which the lexer recorded on its way past and
    // which no failed state machine and no Colapse() ever moved. A run is therefore
    // accurate even in a buffer the parse gave up on - with one exception this does
    // not try to detect, namely a run sitting past the cut of a TRUNCATED parse,
    // which the lexer never reached and so never recorded either.
    //
    // Fold sink only: a run is never a pane entry, so `pane` is not touched here
    // and this costs nothing when the pane is closed.
    AppendCommentRuns(text, m_comments, fold);
//#ifdef _DEBUG
//    if (m_root.get())
//        m_root->DbgPrintNode();
//#endif
}

bool PlSqlParseProduct::HasAnyFailedNode () const
{
    return HasFailedNodeImpl(m_root.get());
}
};