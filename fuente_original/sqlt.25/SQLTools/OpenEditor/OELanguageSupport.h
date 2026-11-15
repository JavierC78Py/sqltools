/* 
    Copyright (C) 2002 Aleksey Kochetov

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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OELanguageSupport_h__
#define __OELanguageSupport_h__

#include <set>
#include "OpenEditor/OEHelpers.h"


namespace OpenEditor
{
    using std::set;
    class Storage;

    // Which flavour of language support a document is supposed to have.
    //
    // This exists so Storage::OnSettingsChanged can answer "did the KIND of support
    // change?" WITHOUT constructing one to compare against. That matters because
    // constructing a throwaway PlSqlSupport is not free even when it is discarded on
    // the next line: its constructor allocates a ParseContext and runs
    // beginGeneration(), and its destructor takes the process-wide parse queue's lock
    // in RemoveByOwner() - all on the UI thread, on every settings change, for an
    // object that was never installed. An enum comparison allocates nothing.
    //
    // It is an enum rather than typeid for that reason and not because RTTI is
    // unavailable (it is enabled in all four configurations, and OEPlsSqlSyntaxImpl.cpp
    // dispatches on typeid). typeid would work, but only by building the instance
    // first, which is the cost this avoids.
    enum LanguageSupportKind { lskCommon, lskPlSql };

    // interface for language support
    class LanguageSupport
    {
    public:
        LanguageSupport (Storage* pstorage) : m_pStorage(pstorage) {}
        virtual ~LanguageSupport () {}

    // Services:
        // line & offset are in/out parameters, length is out only
        struct Match 
        { 
            bool found, backward, partial, broken; 
            int line[2], offset[2], length[2]; 
            void reset ()                               { memset(this, 0, sizeof(*this)); }
            Match ()                                    { reset(); }
            bool operator == (const Match& other) const { return !memcmp(this, &other, sizeof(*this)); }
        };

        virtual bool FindMatch (int line, int offset, Match&, bool quickExactMatch = false) = 0;

        // builds a list of lines which are not parsed properly 
        virtual void GetLineStatus (LineStatusMap&, int, int) const = 0;

        virtual bool HasExtendedSupport () const { return false; }

        // Which flavour this object is. Storage::OnSettingsChanged compares it against
        // LanguageManager::GetLanguageSupportKind() and REPLACES the support only when
        // the two differ; a same-kind change goes to OnSettingsChanged below instead.
        virtual LanguageSupportKind GetKind () const { return lskCommon; }

        // A settings change that did NOT change the kind of support arrives here, on the
        // installed object, instead of rebuilding it.
        //
        // The base is a no-op because CommonLanguageSupport caches nothing out of
        // Settings - every value it uses is read live, so there is nothing to refresh.
        // PlSqlSupport overrides it, because it caches the two parse limits and the
        // outline flag in its constructor and a rebuild was the only way those could
        // ever be updated.
        //
        // This virtual is the whole point of the split. Storage::OnSettingsChanged used
        // to rebuild unconditionally, and because a new PlSqlSupport starts a parse in
        // its constructor, changing a FONT reparsed every open PL/SQL document: the
        // notify chain is GlobalSettings -> Settings -> every Storage subscriber, with
        // no filtering at any level. Asking the one object that knows what it cached to
        // decide is exact by construction, and needs no change-mask plumbing through
        // BaseSettings and every settings page - which would also have to ACCUMULATE
        // across a whole dialog, since the pages deliberately write their properties
        // with notify = false and raise one NotifySettingsChanged() at the end.
        virtual void OnSettingsChanged () {}

        // for background processing
        virtual bool OnIdle () { return false; };

        // The two halves of a FORCED-parse wait, consumed by
        // COEDocument::WaitForParseQuiescent. Phase 1 moved the PL/SQL parse onto
        // ParseQueue's worker, so "I need the tree NOW" stopped meaning "parse it
        // here" and became "block on the worker, then publish on this thread".
        //
        // Both default to the QUIESCENT answer, and that default is load-bearing
        // rather than a formality. COEDocument::OnIdle returns FALSE immediately when
        // GetPlSqlKeywordNavigation() is off, so with no PlSqlSupport in play - or
        // with one whose pump is gated off upstream - there is nothing that could ever
        // publish. A base default of `false` would make the wait spin to its deadline
        // and always report failure, on every document that is not PL/SQL. Reporting
        // done means "nothing is pending", which for a language with no background
        // parse is simply true.
        //
        // GetParseCompletionEvent() returns the handle of the CURRENT generation, or
        // NULL when there is none - which after a completed parse is the steady state,
        // not an error. The waiter tests IsParseDone() first and only reaches for the
        // handle when a parse is genuinely pending.
        virtual bool   IsParseDone () const            { return true;  }
        virtual HANDLE GetParseCompletionEvent () const { return NULL; }

        // Progress of the background parse, for the status bar: how many BUFFER lines have
        // been PUBLISHED, and how many this parse will actually reach.
        //
        // Returns false when there is nothing worth reporting - no background parse, one
        // already finished, or one too short to span a batch - and a caller must read that as
        // "show nothing", never as "0%".
        // Hence a false default rather than a zero-filled pair, which is the quiescent-
        // default argument above taken one step further. A PlSqlSupport runs
        // beginGeneration() in its constructor, so IsParseDone() is false from birth on
        // every PL/SQL document, including one whose pump is gated off upstream and whose
        // cursor will therefore never move. A percentage derived from that alone would sit
        // at 0% for the life of the document.
        virtual bool GetParseProgress (int& linesDone, int& linesTotal) const
            { linesDone = 0; linesTotal = 0; return false; }

    protected:
        Storage* m_pStorage;
    };

    typedef std::shared_ptr<LanguageSupport> LanguageSupportPtr;


};//namespace OpenEditor


#endif//__OELanguageSupport_h__
