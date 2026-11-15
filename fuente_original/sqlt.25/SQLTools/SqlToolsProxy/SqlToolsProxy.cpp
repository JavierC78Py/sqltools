#include "StdAfx.h"
#include "SqlToolsProxy.h"
#include <objbase.h>
#include <initguid.h>
#include <atlbase.h>
#include <vector>

// Globals for class factory registration
static DWORD g_cfCookie = 0;
static IClassFactory* g_pFactory = nullptr;
static volatile LONG g_cObj = 0;
static volatile LONG g_cLock = 0;

static void MaybeShutdown()
{
    if (g_cObj == 0 && g_cLock == 0)
    {
        PostQuitMessage(0);
    }
}

// Hidden window per-thread storage
thread_local HWND ProxyAutomation::t_hiddenWnd = nullptr;
thread_local UINT32 ProxyAutomation::t_corrId = 0;
thread_local std::wstring* ProxyAutomation::t_pResult = nullptr;
thread_local bool ProxyAutomation::t_gotResponse = false;

static const wchar_t* kHiddenWndClass = L"SQLTools.Proxy.HiddenWnd";

// Window class of a SQLTools instance started with /AutomatedTesting
// (CMDIMainFrame::m_cszAutomationClassName - the two must stay in step).
//
// Looking for this class and nothing else is deliberate. The proxy used to
// search for the interactive class "Kochware.SQLTools.MainFrame", which meant
// it attached to whichever SQLTools happened to be open - including one a
// developer had started by hand before a rebuild. Every suite then reported
// green against that older binary: a settings key the running build did not
// recognise was echoed back as SQL instead of being consumed, and no test could
// tell. Not finding a window is now the correct answer, because the harness
// treats it as "start one" (SqlFormatterTestLib.ps1, start_sqltools.ps1).
static const wchar_t* kAutomationWndClass = L"Automated SQLTools";

// NULL when no automated-testing instance is running.
static HWND FindTargetWindow()
{
    return FindWindowW(kAutomationWndClass, nullptr);
}

// Registration helpers (minimal: CLSID + LocalServer32)
static std::wstring GetModulePath()
{
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

HRESULT ProxyRegisterServer()
{
    HKEY hKeyCLSID = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{A7E8F8B0-0FDD-4B37-9F61-6D4F3F8FCE21}",
                                 0, nullptr, 0, KEY_SET_VALUE|KEY_CREATE_SUB_KEY, nullptr, &hKeyCLSID, nullptr);
    if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);

    const wchar_t* desc = L"SQLTools Proxy COM Server";
    RegSetValueExW(hKeyCLSID, nullptr, 0, REG_SZ, (const BYTE*)desc, (DWORD)((wcslen(desc)+1)*sizeof(wchar_t)));

    HKEY hKeyLS = nullptr;
    st = RegCreateKeyExW(hKeyCLSID, L"LocalServer32", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKeyLS, nullptr);
    if (st == ERROR_SUCCESS)
    {
        std::wstring path = GetModulePath();
        RegSetValueExW(hKeyLS, nullptr, 0, REG_SZ, (const BYTE*)path.c_str(), (DWORD)((path.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKeyLS);
    }
    RegCloseKey(hKeyCLSID);

    // ProgID
    HKEY hKeyProg = nullptr;
    st = RegCreateKeyExW(HKEY_CLASSES_ROOT, L"SQLTools.Proxy\\CLSID", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKeyProg, nullptr);
    if (st == ERROR_SUCCESS)
    {
        const wchar_t* clsid = L"{A7E8F8B0-0FDD-4B37-9F61-6D4F3F8FCE21}";
        RegSetValueExW(hKeyProg, nullptr, 0, REG_SZ, (const BYTE*)clsid, (DWORD)((wcslen(clsid)+1)*sizeof(wchar_t)));
        RegCloseKey(hKeyProg);
    }
    return S_OK;
}

HRESULT ProxyUnregisterServer()
{
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"SQLTools.Proxy");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, L"CLSID\\{A7E8F8B0-0FDD-4B37-9F61-6D4F3F8FCE21}");
    return S_OK;
}

// ProxyAutomation implementation
ProxyAutomation::ProxyAutomation() : m_ref(1), m_typeInfo(nullptr)
{
    InterlockedIncrement(&g_cObj);
    CoAddRefServerProcess();
}
ProxyAutomation::~ProxyAutomation()
{
    if (m_typeInfo) m_typeInfo->Release();
    if (InterlockedDecrement(&g_cObj) == 0)
    {
        CoReleaseServerProcess();
        MaybeShutdown();
    }
}

STDMETHODIMP ProxyAutomation::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDispatch)
    {
        *ppv = static_cast<IDispatch*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) ProxyAutomation::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) ProxyAutomation::Release()
{
    ULONG ul = InterlockedDecrement(&m_ref);
    if (!ul) delete this;
    return ul;
}

HRESULT ProxyAutomation::EnsureTypeInfo(LCID lcid)
{
    if (m_typeInfo) return S_OK;
    PARAMDATA param = { const_cast<LPOLESTR>(L"sqlWithPlaceholder"), VT_BSTR };
    METHODDATA meths[4] = {};
    meths[0].szName = const_cast<LPOLESTR>(L"TestColumnAutocompletion");
    meths[0].ppdata = &param;
    meths[0].dispid = 1;
    meths[0].iMeth = 0;
    meths[0].cc = CC_STDCALL;
    meths[0].cArgs = 1;
    meths[0].wFlags = DISPATCH_METHOD;
    meths[0].vtReturn = VT_BSTR;
    PARAMDATA param2 = { const_cast<LPOLESTR>(L"sqlToFormat"), VT_BSTR };
    meths[1].szName = const_cast<LPOLESTR>(L"TestSqlFormatter");
    meths[1].ppdata = &param2;
    meths[1].dispid = 2;
    meths[1].iMeth = 1;
    meths[1].cc = CC_STDCALL;
    meths[1].cArgs = 1;
    meths[1].wFlags = DISPATCH_METHOD;
    meths[1].vtReturn = VT_BSTR;
    // TestEditorScript is DISPID 4, not 3: DISPID 3 is already GetSqlToolsProcessId.
    // Its WM_COPYDATA command is STP_CMD_TEST_EDITOR_SCRIPT == 3 - the two number
    // spaces are independent (see SqlToolsProxy.h).
    PARAMDATA param3 = { const_cast<LPOLESTR>(L"script"), VT_BSTR };
    meths[2].szName = const_cast<LPOLESTR>(L"TestEditorScript");
    meths[2].ppdata = &param3;
    meths[2].dispid = 4;
    meths[2].iMeth = 2;
    meths[2].cc = CC_STDCALL;
    meths[2].cArgs = 1;
    meths[2].wFlags = DISPATCH_METHOD;
    meths[2].vtReturn = VT_BSTR;
    // TestScriptSplitter is DISPID 5 with WM_COPYDATA command
    // STP_CMD_TEST_SCRIPT_SPLITTER == 4 - the two number spaces stay independent.
    PARAMDATA param4 = { const_cast<LPOLESTR>(L"script"), VT_BSTR };
    meths[3].szName = const_cast<LPOLESTR>(L"TestScriptSplitter");
    meths[3].ppdata = &param4;
    meths[3].dispid = 5;
    meths[3].iMeth = 3;
    meths[3].cc = CC_STDCALL;
    meths[3].cArgs = 1;
    meths[3].wFlags = DISPATCH_METHOD;
    meths[3].vtReturn = VT_BSTR;
    INTERFACEDATA idata = { meths, 4 };
    return CreateDispTypeInfo(&idata, lcid, &m_typeInfo);
}

STDMETHODIMP ProxyAutomation::GetTypeInfoCount(UINT* pctinfo)
{ 
    if (pctinfo) *pctinfo = 0/*1*/; 
    return S_OK; 
}

STDMETHODIMP ProxyAutomation::GetTypeInfo(UINT, LCID lcid, ITypeInfo** ppTInfo)
{
    return E_NOTIMPL;
    //if (!ppTInfo) return E_POINTER;
    //HRESULT hr = EnsureTypeInfo(lcid);
    //if (FAILED(hr)) return hr;
    //m_typeInfo->AddRef();
    //*ppTInfo = m_typeInfo;
    //return S_OK;
}
STDMETHODIMP ProxyAutomation::GetIDsOfNames(REFIID, LPOLESTR* rgszNames, UINT cNames, LCID, DISPID* rgDispId)
{
    for (UINT i = 0; i < cNames; ++i)
    {
        if (_wcsicmp(rgszNames[i], L"TestColumnAutocompletion") == 0) { rgDispId[i] = 1; continue; }
        if (_wcsicmp(rgszNames[i], L"TestSqlFormatter") == 0) { rgDispId[i] = 2; continue; }
        if (_wcsicmp(rgszNames[i], L"GetSqlToolsProcessId") == 0) { rgDispId[i] = 3; continue; }
        if (_wcsicmp(rgszNames[i], L"TestEditorScript") == 0) { rgDispId[i] = 4; continue; }
        if (_wcsicmp(rgszNames[i], L"TestScriptSplitter") == 0) { rgDispId[i] = 5; continue; }
        return DISP_E_UNKNOWNNAME;
    }
    return S_OK;
}

ATOM ProxyAutomation::RegisterHiddenClass(HINSTANCE hInst)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProxyAutomation::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kHiddenWndClass;
    ATOM a = RegisterClassW(&wc);
    if (a == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        a = 1;
    return a;
}

HWND ProxyAutomation::EnsureHiddenWindow()
{
    if (t_hiddenWnd && IsWindow(t_hiddenWnd)) return t_hiddenWnd;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    RegisterHiddenClass(hInst);
    t_hiddenWnd = CreateWindowW(kHiddenWndClass, L"", 0, 0,0,0,0, HWND_MESSAGE, nullptr, hInst, nullptr);
    return t_hiddenWnd;
}

LRESULT CALLBACK ProxyAutomation::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COPYDATA)
    {
        COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (cds && cds->dwData == STP_COPYDATA_RESP && cds->lpData && cds->cbData >= sizeof(UINT32))
        {
            const BYTE* p = reinterpret_cast<const BYTE*>(cds->lpData);
            UINT32 corr = *reinterpret_cast<const UINT32*>(p);
            if (corr == t_corrId && t_pResult)
            {
                const wchar_t* s = reinterpret_cast<const wchar_t*>(p + sizeof(UINT32));
                if (s)
                    *t_pResult = s;
                else
                    t_pResult->clear();
                t_gotResponse = true;
                // Wake waiting thread
                PostMessageW(hWnd, WM_APP+1, 0, 0);
                return TRUE;
            }
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HRESULT ProxyAutomation::SendRequestToSqlTools(UINT cmd, const std::wstring& payload, std::wstring& outResult, DWORD timeoutMs)
{
    HWND hRecv = EnsureHiddenWindow();
    if (!hRecv) return E_FAIL;

    // Find the automated-testing main frame (see kAutomationWndClass)
    HWND hTarget = FindTargetWindow();
    if (!hTarget) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    // build request
    UINT32 corr = GetTickCount();
    t_corrId = corr;
    t_pResult = &outResult;
    t_gotResponse = false;

    // The reply handle is a fixed 8 bytes, not sizeof(HWND). Proxy and SQLTools
    // are paired by a machine-wide COM registration and a FindWindow on a shared
    // class name, so a 64-bit proxy can legitimately drive a 32-bit SQLTools (and
    // vice versa); sizeof(HWND) is 4 on x86 but 8 on x64, which would shift every
    // offset after it and make the receiver read the payload 4 bytes early. On x64
    // this layout is byte-identical to the old one, so mixed-version x64 pairs
    // still interoperate; on x86 it converges onto the x64 layout.
    const size_t cb = sizeof(UINT32)*2 + sizeof(UINT64) + (payload.size()+1)*sizeof(wchar_t);
    std::vector<BYTE> buf(cb);
    BYTE* p = buf.data();
    *reinterpret_cast<UINT32*>(p) = cmd; p += sizeof(UINT32);
    *reinterpret_cast<UINT32*>(p) = corr; p += sizeof(UINT32);
    *reinterpret_cast<UINT64*>(p) = static_cast<UINT64>(reinterpret_cast<UINT_PTR>(hRecv)); p += sizeof(UINT64);
    memcpy(p, payload.c_str(), (payload.size()+1)*sizeof(wchar_t));

    COPYDATASTRUCT cds{};
    cds.dwData = STP_COPYDATA_REQ;
    cds.cbData = (DWORD)cb;
    cds.lpData = buf.data();

    DWORD_PTR dwRes = 0;
    if (!SendMessageTimeoutW(hTarget, WM_COPYDATA, (WPARAM)hRecv, (LPARAM)&cds,
                             SMTO_BLOCK, timeoutMs, &dwRes))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Wait for WndProc to receive response. Simple pump loop with timeout.
    DWORD start = GetTickCount();
    MSG m;
    while (GetTickCount() - start < timeoutMs)
    {
        while (PeekMessageW(&m, hRecv, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
            if (t_gotResponse)
                return S_OK;
        }
        Sleep(10);
    }
    return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
}

STDMETHODIMP ProxyAutomation::Invoke(DISPID dispIdMember, REFIID, LCID, WORD wFlags,
    DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO*, UINT*)
{
    if (!(wFlags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
    if (dispIdMember == 1)
    {
        if (!pDispParams || pDispParams->cArgs != 1) return DISP_E_BADPARAMCOUNT;
        CComVariant v(pDispParams->rgvarg[0]);
        HRESULT hr = v.ChangeType(VT_BSTR);
        if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
        std::wstring payload(v.bstrVal ? v.bstrVal : L"");
        std::wstring result = L"echo:" + payload;

        hr = SendRequestToSqlTools(STP_CMD_TEST_COLUMN_AC, payload, result);
        if (FAILED(hr)) return hr;

        if (pVarResult)
        {
            VariantInit(pVarResult);
            pVarResult->vt = VT_BSTR;
            pVarResult->bstrVal = SysAllocString(result.c_str());
        }
        return S_OK;
    }
    if (dispIdMember == 2)
    {
        if (!pDispParams || pDispParams->cArgs != 1) return DISP_E_BADPARAMCOUNT;
        CComVariant v(pDispParams->rgvarg[0]);
        HRESULT hr = v.ChangeType(VT_BSTR);
        if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
        std::wstring payload(v.bstrVal ? v.bstrVal : L"");
        std::wstring result;

        hr = SendRequestToSqlTools(STP_CMD_TEST_SQL_FORMATTER, payload, result);
        if (FAILED(hr)) return hr;

        if (pVarResult)
        {
            VariantInit(pVarResult);
            pVarResult->vt = VT_BSTR;
            pVarResult->bstrVal = SysAllocString(result.c_str());
        }
        return S_OK;
    }
    if (dispIdMember == 3)
    {
        // GetSqlToolsProcessId() -> VT_I4. PID of the automated-testing
        // SQLTools the proxy would talk to, 0 when there is none. Lets a caller
        // *verify* it is driving the process it started instead of assuming so.
        if (pDispParams && pDispParams->cArgs != 0) return DISP_E_BADPARAMCOUNT;

        DWORD pid = 0;
        HWND hTarget = FindTargetWindow();
        if (hTarget)
            ::GetWindowThreadProcessId(hTarget, &pid);

        if (pVarResult)
        {
            VariantInit(pVarResult);
            pVarResult->vt = VT_I4;
            pVarResult->lVal = (LONG)pid;
        }
        return S_OK;
    }
    if (dispIdMember == 4)
    {
        // TestEditorScript(script) -> BSTR. Runs an editor step script inside the
        // automated-testing SQLTools and returns the transcript. Same shape as
        // DISPID 2; a distinct STP_CMD so MainFrm can route it to
        // EditorScriptHandler, which is feature-agnostic (see its .h).
        if (!pDispParams || pDispParams->cArgs != 1) return DISP_E_BADPARAMCOUNT;
        CComVariant v(pDispParams->rgvarg[0]);
        HRESULT hr = v.ChangeType(VT_BSTR);
        if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
        std::wstring payload(v.bstrVal ? v.bstrVal : L"");
        std::wstring result;

        hr = SendRequestToSqlTools(STP_CMD_TEST_EDITOR_SCRIPT, payload, result);
        if (FAILED(hr)) return hr;

        if (pVarResult)
        {
            VariantInit(pVarResult);
            pVarResult->vt = VT_BSTR;
            pVarResult->bstrVal = SysAllocString(result.c_str());
        }
        return S_OK;
    }
    if (dispIdMember == 5)
    {
        // TestScriptSplitter(script) -> BSTR. Runs the worksheet script splitter
        // over the payload inside the automated-testing SQLTools and returns the
        // statement transcript. Same shape as DISPID 2 and 4; a distinct STP_CMD
        // so MainFrm can route it to ScriptSplitterHandler.
        if (!pDispParams || pDispParams->cArgs != 1) return DISP_E_BADPARAMCOUNT;
        CComVariant v(pDispParams->rgvarg[0]);
        HRESULT hr = v.ChangeType(VT_BSTR);
        if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
        std::wstring payload(v.bstrVal ? v.bstrVal : L"");
        std::wstring result;

        hr = SendRequestToSqlTools(STP_CMD_TEST_SCRIPT_SPLITTER, payload, result);
        if (FAILED(hr)) return hr;

        if (pVarResult)
        {
            VariantInit(pVarResult);
            pVarResult->vt = VT_BSTR;
            pVarResult->bstrVal = SysAllocString(result.c_str());
        }
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
}

// Factory
STDMETHODIMP ProxyClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IClassFactory)
    {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr; return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) ProxyClassFactory::AddRef() { return InterlockedIncrement(&m_ref); }
STDMETHODIMP_(ULONG) ProxyClassFactory::Release()
{
    ULONG ul = InterlockedDecrement(&m_ref);
    if (!ul) delete this;
    return ul;
}
STDMETHODIMP ProxyClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
{
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;
    ProxyAutomation* p = new (std::nothrow) ProxyAutomation();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}
STDMETHODIMP ProxyClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
    {
        InterlockedIncrement(&g_cLock);
        CoAddRefServerProcess();
    }
    else
    {
        if (InterlockedDecrement(&g_cLock) == 0)
        {
            CoReleaseServerProcess();
            MaybeShutdown();
        }
    }
    return S_OK;
}

// Entry point for EXE COM server
int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmd, int)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return (int)hr;

    int ret = 0;
    if (cmd && (wcsstr(cmd, L"/RegServer") || wcsstr(cmd, L"-RegServer")))
    {
        ret = FAILED(ProxyRegisterServer()) ? 1 : 0;
    }
    else if (cmd && (wcsstr(cmd, L"/UnregServer") || wcsstr(cmd, L"-UnregServer")))
    {
        ret = FAILED(ProxyUnregisterServer()) ? 1 : 0;
    }
    else
    {
        ProxyClassFactory* pCF = new (std::nothrow) ProxyClassFactory();
        if (!pCF) { CoUninitialize(); return E_OUTOFMEMORY; }
        g_pFactory = pCF;
        hr = CoRegisterClassObject(CLSID_SqlToolsProxy, g_pFactory, CLSCTX_LOCAL_SERVER,
                                   REGCLS_MULTIPLEUSE, &g_cfCookie);
        if (SUCCEEDED(hr))
        {
            // Message loop
            MSG m; BOOL b;
            while ((b = GetMessageW(&m, nullptr, 0, 0)) != 0)
            {
                if (b == -1) break;
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            CoRevokeClassObject(g_cfCookie); g_cfCookie = 0;
        }
        g_pFactory->Release(); g_pFactory = nullptr;
    }
    CoUninitialize();
    return ret;
}
