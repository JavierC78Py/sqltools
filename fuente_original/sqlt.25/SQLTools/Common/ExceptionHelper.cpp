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

// 25.12.2004 (AK) Added AppException - an untraceable exception

#include "stdafx.h"
#include <string>
#include <sstream>
#include <signal.h>
#include "ExceptionHelper.h"
#include "StackTracer.h"
#include "ErrorDlg.h"
#include "OsInfo.h"
#include "MyUtf.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define VERBOSE_EXCEPTION_HANDLER

static const char* REPORT_TITLE = 
#if !defined(_WIN64)
    "Please make a post about this problem in the forum:"
    "\n\thttp://www.sqltools.net/cgi-bin/yabb25/YaBB.pl,"
    "\ninclude the stack trace (see below) and optionally a short description"
    "\nwhat were you doing when this problem occurred.\n\n";
#else
    "";
#endif

namespace Common
{
    using namespace std;

    AppInfoFn g_AppInfoFn = 0;

    void SetAppInfoFn (AppInfoFn fn)
    {
        g_AppInfoFn = fn;
    }

    string g_ProgramId;
    string g_PlatformInfo;

    static __declspec(thread) char g_lastStackOnThrow[4096] = { 0 };

    // ---- exception capture (see SetExceptionCapture in the header) -----------
    static bool   g_exceptionCapture   = false;
    static string g_capturedExceptions;
    static int    g_capturedCount      = 0;
    static int    g_capturedSuppressed = 0;

    // A runaway throw site - one that fires on every idle tick or every paint -
    // would otherwise grow this without bound, and the reports are useful as
    // evidence, not as a log of every repetition. Keep the first few in full and
    // count the rest; the count is itself the interesting part, because it is
    // what distinguishes a one-off throw from a livelock.
    static const int MAX_CAPTURED_REPORTS = 4;

    void SetExceptionCapture (bool bCapture)
    {
        g_exceptionCapture = bCapture;

        if (bCapture)
        {
            g_capturedExceptions.clear();
            g_capturedCount      = 0;
            g_capturedSuppressed = 0;
        }
    }

    bool TakeCapturedExceptions (string& out)
    {
        if (g_capturedCount == 0)
            return false;

        out.swap(g_capturedExceptions);

        if (g_capturedSuppressed > 0)
        {
            ostringstream o;
            o << "\n... and " << g_capturedSuppressed
              << " further report(s) suppressed (same cause, repeating).\n";
            out += o.str();
        }

        g_capturedExceptions.clear();
        g_capturedCount      = 0;
        g_capturedSuppressed = 0;

        return true;
    }

    // Returns true when the report was captured rather than shown, i.e. when the
    // caller must NOT put a modal dialog on the UI thread.
    static bool capture_exception (const string& str)
    {
        if (!g_exceptionCapture)
            return false;

        g_capturedCount++;

        if (g_capturedCount > MAX_CAPTURED_REPORTS)
        {
            g_capturedSuppressed++;
            return true;
        }

        if (!g_capturedExceptions.empty())
            g_capturedExceptions += "\n---------------- next report ----------------\n";

        g_capturedExceptions += str;

        return true;
    }

    #ifdef _AFX
        inline void print_exception (const string& str, const string& reportPrefix)
        {
            if (capture_exception(str))
                return;

            CErrorDlg(str.c_str(), reportPrefix.c_str()).DoModal();
        }
    #else
        inline void print_exception (const string& str, const string&)
        {
            if (capture_exception(str))
                return;

            cerr << str << endl;
        }
    #endif//_AFX

#ifdef VERBOSE_EXCEPTION_HANDLER ///////////////////////////////////////////////

void DefaultHandler (const char* file, int line) // for ...
{
    string programName, infoText, reportPrefix;
    if (g_AppInfoFn)
        g_AppInfoFn(programName, infoText, reportPrefix);

    ostringstream o;

    o << REPORT_TITLE;
    o << "Unknown exception.\n\n";

    if (!programName.empty())
        o << "Program: " << programName << endl;

    o << "Location: " << file << '(' << line << ").\n"
      << "Platform: " << getOsInfo() << endl
      << "COMCTL32.DLL: " << getDllVersion("COMCTL32.DLL") << endl;

    if (!infoText.empty())
        o << infoText << endl;

    string result;
    StackTracer::Trace(result);
    o << "\nStack at the catch point:\n" << result;

    print_exception(o.str(), reportPrefix);
    g_lastStackOnThrow[0] = 0;
}

static void defaultHandler (const AppException& x)
{
    // Captured as well: an AppException is untraceable, so it gets no bug report,
    // but it still raises a modal box here - which blocks an unattended run
    // exactly like the report dialog does.
    if (capture_exception(string("Runtime error: ") + x.what()))
        return;

#ifdef _AFX
    AfxMessageBox(wstr(x.what()).c_str(), MB_OK|MB_ICONSTOP);
#else
    cerr << "Runtime error (" << x.what() << ")!" << endl;
#endif//_AFX
}

void DefaultHandler (const std::exception& x, const char* file, int line)
{
    if (const AppException* px = dynamic_cast<const AppException*>(&x))
    {
        // this is an untraceable exception - no bug report
        defaultHandler(*px);
        return;
    }

    string programName, infoText, reportPrefix;
    if (g_AppInfoFn)
        g_AppInfoFn(programName, infoText, reportPrefix);

    ostringstream o;

    o << REPORT_TITLE;
    o << x.what() << endl << endl;

    if (!programName.empty())
        o << "Program: " << programName << endl;

    o << "Location: " << file << '(' << line << ").\n"
      << "Platform: " << getOsInfo() << endl
      << "COMCTL32.DLL: " << getDllVersion("COMCTL32.DLL") << endl;

    if (!infoText.empty())
        o << infoText << endl;

    const char* lastStack
        = (typeid(x) == typeid(SEException)) ? SEException::GetLastStackDump() : g_lastStackOnThrow;

    if (lastStack && lastStack[0] != 0)
        o << "\nStack at the throw point:\n" << lastStack;

    string result;
    StackTracer::Trace(result);
    o << "\nStack at the catch point:\n" << result;

    print_exception(o.str(), reportPrefix);
    g_lastStackOnThrow[0] = 0;
}

void TraceStackOnThrow ()
{
    // x64 is excluded from the release build because every throw then pays for a
    // symbolic stack walk. Debug keeps it: _RAISE calls this, so the report a
    // suite captures says where the exception was *thrown* and not merely which
    // frame happened to catch it - without it an "invalid subscript" from a
    // container tells you nothing about which caller passed the bad index.
#if !defined(_WIN64) || defined(_DEBUG)
    string result;
    StackTracer::Trace(result);
    size_t length = min(sizeof(g_lastStackOnThrow)-1, result.length());
    memcpy(g_lastStackOnThrow, result.data(), length);
    g_lastStackOnThrow[length] = 0;
#endif
}

#ifdef _AFX
void DefaultHandler (CException* /*x*/, const char* /*file*/, int /*line*/)
{
    throw;
}
#endif//_AFX

#else// VERBOSE_EXCEPTION_HANDLER is not defined ///////////////////////////////

void DefaultHandler (const char* file, int line) // for ...
{
#ifdef _AFX
    AfxMessageBox("Unknown exception!", MB_OK|MB_ICONSTOP);
#else
    cerr << "Unknown exception!" << endl;
#endif//_AFX
}

void DefaultHandler (const std::exception& x, const char* file, int line)
{
#ifdef _AFX
    AfxMessageBox(x.what(), MB_OK|MB_ICONSTOP);
#else
    cerr << std::string("Unexpected error (") + x.what() + ")!") << endl;
#endif//_AFX
}

#ifdef _AFX
void DefaultHandler (CException*, const char* file, int line)
{
    throw;
}
#endif//_AFX

void TraceStackOnThrow ()
{
}
#endif//VERBOSE_EXCEPTION_HANDLER///////////////////////////////////////////////

static __declspec(thread) int g_exceptionFrameCounter = 0;

ExceptionFrame::ExceptionFrame ()
{
    g_exceptionFrameCounter++;
}

ExceptionFrame::~ExceptionFrame ()
{
    g_exceptionFrameCounter--;
}

void CheckExceptionFrame (const char* file, int line)
{
    if (g_exceptionFrameCounter <= 0)
        DefaultHandler(std::exception("ASSERTION FAILED: exception is possible outside try/catch block"), file, line);

//#ifndef NDEBUG
//    if (g_exceptionFrameCounter > 1)
//        TRACE("\tWARNING: g_exceptionFrameCounter > 1\n");
//#endif
}

__declspec(noreturn) void __cdecl terminate ()
{
    string programName, infoText, reportPrefix;
    if (g_AppInfoFn)
        g_AppInfoFn(programName, infoText, reportPrefix);

    ostringstream o;

    o << REPORT_TITLE;
    o << "*** UNHANDLED FATAL ERROR ***" << endl << endl;

    if (!programName.empty())
        o << "Program: "   << programName << endl;

    o << "Platform: "  << getOsInfo() << endl
      << "COMCTL32.DLL: " << getDllVersion("COMCTL32.DLL") << endl;

    if (!infoText.empty())
        o << infoText << endl;

#if !defined(_WIN64)
    string result;
    StackTracer::Trace(result);
    o << "\nStack at termination:\n" << result;
#endif

    print_exception(o.str(), reportPrefix);

    raise(SIGABRT);     /* raise abort signal */
    /* We usually won't get here, but it's possible that
        SIGABRT was ignored.  So hose the program anyway. */
    _exit(3);
}

void StackOverflowGuard::Throw (const char* file, int line)
{
    TraceStackOnThrow();

    ostringstream o;

    o << "StackOverflowGuard::Throw at: " << file << '(' << line << ").";

    throw exception(o.str().c_str());
}

}

