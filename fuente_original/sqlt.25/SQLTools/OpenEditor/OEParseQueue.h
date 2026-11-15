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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OEParseQueue_h__
#define __OEParseQueue_h__

#include <afxmt.h>
#include <boost/noncopyable.hpp>
#include <list>
#include <memory>

namespace Threading { template <class T, int instance> class Singleton; };

namespace OpenEditor
{
    // Liveness token for a task submitter. The submitter holds the shared_ptr and every
    // task it submits holds a weak_ptr, so the worker can tell a dead submitter from a
    // live one without dereferencing an address that may already have been reused.
    struct ParseOwnerToken {};

    // One unit of background parse work. It owns its input and its output: the submitter
    // pushes it into the request queue and keeps no pointer to it, the worker runs it and
    // pushes it back through the result queue, and the submitter drains that queue from
    // its own idle tick. Run() is worker thread, PublishInForeground() is UI thread.
    class ParseTask : private boost::noncopyable
    {
    public:
        ParseTask (void* owner, HANDLE completionEvent);
        virtual ~ParseTask ();

        virtual void Run () = 0;

        virtual void PublishInForeground () = 0;

        void Execute ();

        bool   IsCompleted () const          { return WaitForSingleObject(m_completionEvent, 0) == WAIT_OBJECT_0; }
        bool   IsFailed () const             { return m_failed; }
        bool   IsOwnerAlive () const         { return !m_ownerToken.expired(); }
        void*  GetOwner () const             { return m_owner; }
        HANDLE GetCompletionEvent () const   { return m_completionEvent; }

        void SetOwnerToken (const std::weak_ptr<ParseOwnerToken>& token) { m_ownerToken = token; }

    private:
        void*  m_owner;                      // identity for RemoveByOwner, never dereferenced by the worker
        HANDLE m_completionEvent;            // borrowed from the parse generation, NULL when nobody waits
        bool   m_failed;
        std::weak_ptr<ParseOwnerToken> m_ownerToken;
    };

    typedef std::shared_ptr<ParseTask> ParseTaskPtr;

    // The background parse worker: one below-normal thread, a request queue and a result
    // queue, both guarded by one section. Created eagerly during InitInstance because
    // Threading::Singleton::Get() has no real mutual exclusion of its own.
    class ParseQueue : private boost::noncopyable
    {
    public:
        static ParseQueue& Get ();
        static void        Destroy ();

        void Start ();

        CWinThread* Shutdown ();

        bool Push (const ParseTaskPtr&);

        ParseTaskPtr PopResult (void* owner);   // null when this owner has nothing completed

        HANDLE GetResultEvent () const          { return m_resultReady; }

        void RemoveByOwner (void* owner);       // purges both queues

    private:
        template <class T, int instance> friend class Threading::Singleton;

        ParseQueue ();
        ~ParseQueue ();

        static UINT ThreadProc (LPVOID);

        ParseTaskPtr PopRequest ();
        void         PushResult (const ParseTaskPtr&);   // worker thread only

        static CEvent m_eventShutdown;

        bool                     m_shutdown;
        CEvent                   m_newTask;
        CEvent                   m_resultReady;          // manual reset, set while m_results is non-empty
        mutable CCriticalSection m_criticalSection;
        std::list<ParseTaskPtr>  m_requests;
        std::list<ParseTaskPtr>  m_results;
        CWinThread*              m_pThread;
    };

    typedef Threading::Singleton<ParseQueue, 3> ParseQueueSingleton;

}; // namespace OpenEditor

#endif // __OEParseQueue_h__
