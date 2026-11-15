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
#include "OEParseQueue.h"

#include "Threading/Singleton.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{

CEvent ParseQueue::m_eventShutdown;

ParseTask::ParseTask (void* owner, HANDLE completionEvent)
: m_owner(owner),
  m_completionEvent(completionEvent),
  m_failed(false)
{
}

ParseTask::~ParseTask ()
{
}

void ParseTask::Execute ()
{
    m_failed = false;

    try
    {
        Run();
    }
    catch (...)
    {
        m_failed = true;
    }

    if (m_completionEvent)
        ::SetEvent(m_completionEvent);
}

ParseQueue::ParseQueue ()
: m_shutdown(false),
  m_resultReady(FALSE, TRUE),
  m_pThread(NULL)
{
    m_eventShutdown.ResetEvent();
}

void ParseQueue::Start ()
{
    if (m_pThread || m_shutdown)
        return;

    m_pThread = AfxBeginThread(ThreadProc, NULL, THREAD_PRIORITY_BELOW_NORMAL);

    if (!m_pThread)
        TRACE("ParseQueue: worker thread creation FAILED\n");
    else
        TRACE("ParseQueue: thread started\n");
}

ParseQueue::~ParseQueue ()
{
    CSingleLock lk(&m_criticalSection, TRUE);
    m_requests.clear();
    m_results.clear();
    m_resultReady.ResetEvent();
}

ParseQueue& ParseQueue::Get ()
{
    return ParseQueueSingleton::Get();
}

void ParseQueue::Destroy ()
{
    m_eventShutdown.SetEvent();

    ParseQueueSingleton::Destroy();
}

CWinThread* ParseQueue::Shutdown ()
{
    m_shutdown = true;

    m_eventShutdown.SetEvent();
    m_newTask.SetEvent();     // wake the worker so it observes the shutdown flag

    return m_pThread;
}

bool ParseQueue::Push (const ParseTaskPtr& ptr)
{
    if (m_shutdown || !ptr)
        return false;

    Start();      // safety net; InitInstance normally got here first

    CSingleLock lk(&m_criticalSection, TRUE);

    m_requests.push_back(ptr);

    m_newTask.SetEvent();

    return true;
}

void ParseQueue::RemoveByOwner (void* owner)
{
    if (!owner)
        return;

    CSingleLock lk(&m_criticalSection, TRUE);

    std::list<ParseTaskPtr> keep;

    std::list<ParseTaskPtr>::iterator it = m_requests.begin();
    for (; it != m_requests.end(); ++it)
        if ((*it)->GetOwner() != owner)
            keep.push_back(*it);

    m_requests.swap(keep);

    keep.clear();

    it = m_results.begin();
    for (; it != m_results.end(); ++it)
        if ((*it)->GetOwner() != owner)
            keep.push_back(*it);

    m_results.swap(keep);

    if (m_results.empty())
        m_resultReady.ResetEvent();
}

ParseTaskPtr ParseQueue::PopRequest ()
{
    CSingleLock lk(&m_criticalSection, TRUE);

    ParseTaskPtr ptr;

    if (!m_requests.empty())
    {
        ptr = m_requests.front();
        m_requests.pop_front();
    }

    if (m_requests.empty())
        m_newTask.ResetEvent();

    return ptr;
}

void ParseQueue::PushResult (const ParseTaskPtr& ptr)
{
    if (!ptr)
        return;

    CSingleLock lk(&m_criticalSection, TRUE);

    if (!ptr->IsOwnerAlive())
        return;      // nobody will ever drain this, so it dies here, on the worker

    m_results.push_back(ptr);

    m_resultReady.SetEvent();
}

ParseTaskPtr ParseQueue::PopResult (void* owner)
{
    if (!owner)
        return ParseTaskPtr();

    CSingleLock lk(&m_criticalSection, TRUE);

    std::list<ParseTaskPtr>::iterator it = m_results.begin();
    for (; it != m_results.end(); ++it)
    {
        if ((*it)->GetOwner() != owner)
            continue;

        ParseTaskPtr ptr = *it;

        m_results.erase(it);

        if (m_results.empty())
            m_resultReady.ResetEvent();

        return ptr;
    }

    return ParseTaskPtr();
}
UINT ParseQueue::ThreadProc (LPVOID)
{
    ParseQueue& queue = ParseQueue::Get();

    HANDLE events[2] = { m_eventShutdown, queue.m_newTask };

    for (;;)
    {
        DWORD retVal = WaitForMultipleObjects(2, events, FALSE, INFINITE);

        if (retVal != WAIT_OBJECT_0 + 1)
            break;

        for (;;)
        {
            ParseTaskPtr task = queue.PopRequest();

            if (!task)
                break;

            try
            {
                task->Execute();
            }
            catch (...)
            {
                TRACE("ParseQueue: task raised through Execute()\n");
            }

            queue.PushResult(task);
        }
    }

    TRACE("ParseQueue: thread stopped\n");

    return 0;
}

}; // namespace OpenEditor
