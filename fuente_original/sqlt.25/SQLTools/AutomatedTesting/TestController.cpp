#include "stdafx.h"
#include "TestController.h"
#include <utility>
#include "COMMON/ExceptionHelper.h"

// forward declare built-in handlers registration
namespace TestingHandlers { void RegisterAll(TestController& ctl); }

TestController& TestController::Instance()
{
    static TestController s;
    s.EnsureRegistered();
    return s;
}

TestController::TestController() = default;

void TestController::EnsureRegistered()
{
    std::call_once(m_regOnce, [this]() {
        TestingHandlers::RegisterAll(*this);
    });
}

bool TestController::Handle(unsigned cmd, const std::wstring& payload, std::wstring& out)
{
    EnsureRegistered();
    auto it = m_handlers.find(cmd);
    if (it == m_handlers.end())
        return false;

    // A handler runs on the UI thread inside CMDIMainFrame::HandleProxyCopyData,
    // so an exception it throws would otherwise unwind through the frame's
    // top-level handler and the reply packet would never be built: the proxy
    // waits, the harness times out, and the only evidence of what broke dies with
    // the process it has to kill. Catch it here instead.
    //
    // Handlers emit into `out` as they go (Runner::Emit appends to the caller's
    // string, EditorScriptHandler.cpp:1045), so the transcript built up to the
    // throw point survives. That partial transcript plus the report below is what
    // a suite writes to its *.out file - which is where a failure is expected to
    // explain itself, so an exception becomes a visible transcript diff instead of
    // a hang to be diagnosed by hand.
    bool ok = false;

    try
    {
        ok = it->second->Execute(payload, out);
    }
#ifdef _AFX
    catch (CException* x)
    {
        // Common::DefaultHandler(CException*) rethrows by design, so this one is
        // reported inline. MFC requires whoever catches an exception to delete it.
        if (x)
        {
            TCHAR buf[512] = { 0 };
            x->GetErrorMessage(buf, _countof(buf));
            out += L"\n*** MFC CEXCEPTION DURING THIS COMMAND ***\n";
            out += buf;
            out += L'\n';
            x->Delete();
        }
        ok = false;
    }
#endif//_AFX
    catch (const std::exception& x)
    {
        // Captured rather than shown, because capture is on for the whole
        // /AutomatedTesting session - see Common::SetExceptionCapture.
        Common::DefaultHandler(x, __FILE__, __LINE__);
        ok = false;
    }
    catch (...)
    {
        Common::DefaultHandler(__FILE__, __LINE__);
        ok = false;
    }

    // Drain whatever the run captured. That includes a report from an exception
    // thrown asynchronously - idle, paint - which reached Common::DefaultHandler
    // through a frame handler rather than through Execute above, and which is
    // exactly the kind of failure a command cannot otherwise be blamed for.
    std::string report;

    if (Common::TakeCapturedExceptions(report))
    {
        out += L"\n*** UNHANDLED EXCEPTION DURING THIS COMMAND ***\n";
        out += Common::wstr(report);
        out += L'\n';
        ok = false;
    }

    // The PowerShell side gates on a RESULT line (SqlEditorScriptTestLib.ps1):
    // without one it reports "the handler did not run to completion" and writes
    // no *.out at all, which would discard precisely the evidence above.
    // Synthesise a failing one so the transcript is preserved. -Bless still
    // refuses it, because fail != 0 makes the run untrustworthy by definition -
    // so a crash can never be promoted into a golden baseline.
    if (!ok && out.find(L"RESULT ") == std::wstring::npos)
        out += L"RESULT checks=0 pass=0 fail=1\n";

    return ok;
}

void TestController::Register(unsigned cmd, std::unique_ptr<ITestCommandHandler> handler)
{
    m_handlers[cmd] = std::move(handler);
}

// ================= Handlers =================
#include "handlers/ColumnAutocompleteHandler.h"
#include "handlers/SqlFormatterHandler.h"
#include "handlers/EditorScriptHandler.h"
#include "handlers/ScriptSplitterHandler.h"

namespace TestingHandlers
{
    void RegisterAll(TestController& ctl)
    {
        ctl.Register(1 /*STP_CMD_TEST_COLUMN_AC*/, std::unique_ptr<ITestCommandHandler>(new ColumnAutocompleteHandler()));
        ctl.Register(2 /*STP_CMD_TEST_SQL_FORMATTER*/, std::unique_ptr<ITestCommandHandler>(new SqlFormatterHandler()));
        ctl.Register(3 /*STP_CMD_TEST_EDITOR_SCRIPT*/, std::unique_ptr<ITestCommandHandler>(new EditorScriptHandler()));
        ctl.Register(4 /*STP_CMD_TEST_SCRIPT_SPLITTER*/, std::unique_ptr<ITestCommandHandler>(new ScriptSplitterHandler()));
    }
}
