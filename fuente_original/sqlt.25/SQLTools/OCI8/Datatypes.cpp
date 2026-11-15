/* 
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2020 Aleksey Kochetov

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
#include <time.h>
#include <iomanip>

// Include Oracle object interface headers
extern "C" {
#include <orid.h>
}

#include "OCI8/Datatypes.h"
#include "OCI8/Statement.h"
#include "OCI8/Conversions.h"
#include "COMMON/ExceptionHelper.h"

#if !(OCI_DTYPE_LAST < SHRT_MAX)
#error("\tERROR: assumption OCI_DTYPE_LAST < SHORT_MAX is wrong!\n") 
#endif

// 04.09.2003 bug fix, wrong date for year less then 2000 (introduced in 141)
// 26.10.2003 workaround for oracle 8.1.6, removed trailing '0' for long columns and trigger text
// 08.07.2004 bug fix, Memory corruption on a query with blob columns
// 07.02.2005 (Ken Clubok) R1105003: Bind variables
// 13.03.2005 (ak) R1105003: Bind variables

    using namespace std;

namespace OCI8
{
///////////////////////////////////////////////////////////////////////////////
// OCI8::Variable
///////////////////////////////////////////////////////////////////////////////
const string Variable::m_null;

Variable::Variable (ub2 type, sb4 size)
: m_threadId(GetCurrentThreadId())
{
    m_type = type;
    m_indicator = OCI_IND_NULL;
    m_buffer_size = m_size = size;
    m_buffer = new char[m_size];
    memset(m_buffer, 0, m_size);
    m_owner = true;
    m_ret_size = 0;
    m_rcode = 0;
}

Variable::Variable (dvoid* buffer, ub2 type, sb4 size)
: m_threadId(GetCurrentThreadId())
{
    m_type = type;
    m_indicator = OCI_IND_NULL;
    m_buffer_size = m_size = size;
    m_buffer = buffer;
    m_owner = false;
	m_ret_size = 0;
    m_rcode = 0;
}

Variable::~Variable ()
{
    try { EXCEPTION_FRAME;

        if (m_owner)
            delete[] (char*)m_buffer;

        _ASSERTE(m_threadId == GetCurrentThreadId());
    }
    _DESTRUCTOR_HANDLER_;
}

void Variable::GetString (string&, const string& /*null*/) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::Variable::GetString(): method is not implemented."));
}

void Variable::Define (Statement& sttm, int pos)
{
    OCIDefine* defhp;
    // rcodep was 0 here, discarding the one signal OCI gives per column. Nothing read it, so
    // asking for it changes no behaviour; it is what makes a cut VARCHAR2 distinguishable from a
    // complete one that happens to fill its buffer.
    sttm.GetConnect().CHECK(
        OCIDefineByPos(sttm.GetOCIStmt(), &defhp, sttm.GetOCIError(), pos,
                        m_buffer, m_buffer_size, m_type, &m_indicator, &m_ret_size, &m_rcode, OCI_DEFAULT));
}

/** @brief Bind variable by position.
 *
 *  @arg sttm: Statement to bind - must be prepared.
 *  @arg pos: Position of bind variable within statement.
 */
void Variable::Bind (Statement& sttm, int pos)
{
    OCIBind* bndhp;

    switch (m_type)
    {
    case SQLT_CLOB:
    case SQLT_BLOB:
    case SQLT_BFILEE:
    case SQLT_CFILEE:
        sttm.GetConnect().CHECK(
            OCIBindByPos(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), pos,
                        &m_buffer, m_size, m_type, &m_indicator, 0, 0, 0, 0, OCI_DEFAULT));
        break;
    default:
        sttm.GetConnect().CHECK(
            OCIBindByPos(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), pos,
                        m_buffer, m_size, m_type, &m_indicator, 0, 0, 0, 0, OCI_DEFAULT));
        break;
    }
}

/** @brief Bind variable by name.
 *
 *  @arg sttm: Statement to bind - must be prepared.
 *  @arg name: Name of bind variable.
 */
void Variable::Bind (Statement& sttm, const char* name)
{
    OCIBind* bndhp;

    wstring wname = Common::wstr(name);

    switch (m_type)
    {
    case SQLT_CLOB:
    case SQLT_BLOB:
    case SQLT_BFILEE:
    case SQLT_CFILEE:
        sttm.GetConnect().CHECK(
            OCIBindByName(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), (OraText*)wname.c_str(), wname.length()*sizeof(wchar_t),
                        &m_buffer, m_size, m_type, &m_indicator, 0, 0, 0, 0, OCI_DEFAULT));
        break;
    default:
        sttm.GetConnect().CHECK(
            OCIBindByName(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), (OraText*)wname.c_str(), wname.length()*sizeof(wchar_t),
                        m_buffer, m_size, m_type, &m_indicator, 0, 0, 0, 0, OCI_DEFAULT));
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::RefCursorVariable
//      limited functionality, can be bound
///////////////////////////////////////////////////////////////////////////////

RefCursorVariable::RefCursorVariable (Connect& connect, int prefetch)
    : Variable(0, static_cast<ub2>(SQLT_RSET), 0),
    m_connect(connect),
    m_sttmp(0),
    m_prefetch(prefetch)
{
    CHECK_ALLOC(OCIHandleAlloc(GetOCIEnv(), (dvoid**)&m_sttmp, OCI_HTYPE_STMT, 0, 0));
}

RefCursorVariable::~RefCursorVariable ()
{
    try { EXCEPTION_FRAME;

        if (m_sttmp)
            OCIHandleFree(m_sttmp, OCI_HTYPE_STMT);

        m_sttmp = 0;
    }
    _DESTRUCTOR_HANDLER_;
}

void RefCursorVariable::GetString (string& str, const string& /*null = m_null*/) const
{
    str = "Use PRINT varible to see the result set";
}

void RefCursorVariable::Bind (Statement& sttm, int pos)
{
    //8171  1315603 when fetching NULL data.
    //              ie: indp / rcodep may be incorrect.
    //              Workaround: Set OCI_ATTR_PREFETCH_ROWS to 0
    //              This problem was introduced in 8.1.6
    if (Connect::GetClientVersion() >= ecvClient9X)
        CHECK(OCIAttrSet(m_sttmp, OCI_HTYPE_STMT, &m_prefetch, 0, OCI_ATTR_PREFETCH_ROWS, GetOCIError()));

    OCIBind* bndhp;

    sttm.GetConnect().CHECK(
        OCIBindByPos(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), pos,
                    &m_sttmp, 0, SQLT_RSET, 0, 0, 0, 0, 0, OCI_DEFAULT));
}

void RefCursorVariable::Bind (Statement& sttm, const char* name)
{
    OCIBind* bndhp;

    wstring wname = Common::wstr(name);

    sttm.GetConnect().CHECK(
        OCIBindByName(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), (OraText*)wname.c_str(), wname.length()*sizeof(wchar_t),
                    &m_sttmp, 0, SQLT_RSET, 0, 0, 0, 0, 0, OCI_DEFAULT));
}

bool RefCursorVariable::IsStateInitialized ()
{
    ub4 state;

    CHECK(OCIAttrGet(m_sttmp, OCI_HTYPE_STMT, &state, (ub4 *)0, OCI_ATTR_STMT_STATE, GetOCIError()));

    return state == OCI_STMT_STATE_INITIALIZED;
}

bool RefCursorVariable::IsStateExecuted ()
{
    ub4 state;

    CHECK(OCIAttrGet(m_sttmp, OCI_HTYPE_STMT, &state, (ub4 *)0, OCI_ATTR_STMT_STATE, GetOCIError()));

    return state == OCI_STMT_STATE_EXECUTED;
}

bool RefCursorVariable::IsStateEndOfFetch ()
{
    ub4 state;

    CHECK(OCIAttrGet(m_sttmp, OCI_HTYPE_STMT, &state, (ub4 *)0, OCI_ATTR_STMT_STATE, GetOCIError()));

    return state == OCI_STMT_STATE_END_OF_FETCH;
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::NativeOciVariable
///////////////////////////////////////////////////////////////////////////////

NativeOciVariable::NativeOciVariable (Connect& connect, ub2 sqlt_type, ub2 oci_handle_type)
    : Variable(0, static_cast<ub2>(sqlt_type), 0),
    m_connect(connect),
    m_oci_handle_type(oci_handle_type)
{
    m_connect.CHECK(OCIDescriptorAlloc(m_connect.GetOCIEnv(), &m_buffer, m_oci_handle_type, 0, 0));
}

NativeOciVariable::~NativeOciVariable ()
{
    try { EXCEPTION_FRAME;
	    m_connect.CHECK(OCIDescriptorFree(m_buffer, m_oci_handle_type));
        m_buffer = 0;
    }
    _DESTRUCTOR_HANDLER_;
}

void NativeOciVariable::Define (Statement& sttm, int pos)
{
    OCIDefine* defhp;
    sttm.GetConnect().CHECK(
        OCIDefineByPos(sttm.GetOCIStmt(), &defhp, sttm.GetOCIError(), pos,
                        &m_buffer, 0, m_type, &m_indicator, 0, 0, OCI_DEFAULT));
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::StringVar
///////////////////////////////////////////////////////////////////////////////

// SQLT_LVC carries a 4-byte length prefix ahead of the character payload. Floored at one character:
// OCIBindByPos takes value_sz at bind time, so zero capacity draws ORA-01459 even when the value is NULL.
sb4 LvcBufferSize (int chars)
{
    if (chars < 1)
        chars = 1;
    return static_cast<sb4>(chars) * static_cast<sb4>(sizeof(wchar_t)) + static_cast<sb4>(sizeof(int));
}

StringVar::StringVar (int size)
: Variable(SQLT_LVC, LvcBufferSize(size))
{
}

StringVar::StringVar (const string& str)
: Variable(SQLT_LVC, LvcBufferSize(static_cast<int>(str.size())))
{
    Assign(str.c_str(), str.size());
}

// `size` is assigned inside the initializer list on purpose: it resolves -1 before Assign sees it, so a
// null pointer with -1 yields a length of zero rather than reaching strlen(0).
StringVar::StringVar (const char* str, int size)
: Variable(SQLT_LVC, LvcBufferSize(size = (size != -1 ? size : (str ? static_cast<int>(strlen(str)) : 0))))
{
    Assign(str, size);
}

void StringVar::Assign (const char* _str, int _size)
{
    ASSERT_EXCEPTION_FRAME;

    if (_size == -1)
        _size = strlen(_str);

    wstring str = Common::wstr(_str, _size);

    int size = str.length() * sizeof(wchar_t);

    if (size)
    {
        if ((size + static_cast<int>(sizeof(int))) > m_buffer_size)
            _RAISE(Exception(0, "OCI8::StringVar::Assign(): String buffer overflow!"));

        m_indicator = OCI_IND_NOTNULL;

        *(int*)m_buffer = str.length();

        m_size = size + sizeof(int);

        memcpy((char*)m_buffer + sizeof(int), str.c_str(), size);
    }
    else
    {
        m_indicator = OCI_IND_NULL;

        // The prefix goes with the value, and Assign can run twice on one variable. m_size stays put: it
        // is the capacity the bind was created with, and narrowing it recreates the zero-capacity shape.
        *(int*)m_buffer = 0;
    }
}

void StringVar::Assign (const string& str)
{
    Assign(str.data(), str.size());
}

void StringVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        // 26.10.2003 workaround for oracle 8.1.6, removed trailing '0' for long columns and trigger text
        int length = *(int*)m_buffer;
        //int length2 = length;

        for (; length > 0; length--)
            if ( ((const wchar_t*)((char*)m_buffer + sizeof(int)))[length-1] != 0 ) 
                break;

        //strbuff = Common::str((wchar_t*)((char*)m_buffer + sizeof(int)), length2);
        //_ASSERT(length == length2);
        strbuff = Common::str((wchar_t*)((char*)m_buffer + sizeof(int)), length);
    }
}

// Whether OCI cut this value at the define buffer.
//
// Settled by measurement, not by the headers: oci.h documents no truncation indicator at all, and
// the two mechanisms that could plausibly report a cut disagree with each other in the cases that
// matter. GridValueLive's `trunc` mode (Tests/grid/live/main.cpp) defined the same 40-character
// SQLT_LVC buffer across every shape and printed all six candidate signals. Server 21.0.0.0.0:
//
//   value          fetch  errno   ind   rcode  ret/buf  prefix  len
//   fits (3)         0    sticky    0       0     3/84       3    3
//   exact (40)       0    sticky    0       0    40/84      40   40
//   over by one      1    24345    41    1406    40/84      40   40
//   way over (200)   1    24345   200    1406    40/84      40   40
//   NULL             0    sticky   -1    1405     0/84       0    0
//   empty            0    sticky   -1    1405     0/84       0    0
//   CHAR(2)          0    sticky    0       0     2/84       2    2
//
// Three columns of that table decided the predicate:
//
//   ret  is dead. m_ret_size reports what landed IN the buffer, never the source length, so it is 40
//        for the exact fit and 40 for the cut one. The length heuristic cannot separate them, which
//        is precisely the case a wrong answer here would have mis-painted.
//   ind  carries the true source length on a cut - 41 and 200 - and is 0 for every complete value
//        including the exact fit. This is what HostArray::IsTruncated has tested all along.
//   rcode is 1406 (ORA-01406, fetched column value was truncated) on a cut, 1405 (ORA-01405, fetched
//        column value is NULL) on NULL, and 0 on a complete value.
//
// Both signals are accepted, ORed. They agreed on every measured row, so either alone would do today;
// asking for both costs one comparison and keeps the answer right on a client that populates only
// one. NULL is safe under both arms - ind is -1, not positive, and rcode is 1405, not 1406 - which
// matters because a NULL cell must never be tinted.
//
// Not sticky either: the mode fetches a cut row followed by a whole one on the same variable and the
// return code goes 1406 -> 0, so a cut in row N cannot flag row N+1. That was the real hazard in
// accepting rcode at all, since Variable::Define passed 0 for rcodep until this change and nobody had
// observed what it does between rows.
//
// Deliberately on StringVar and not on Variable: the base returns false and must keep doing so. A
// positive indicator carries a legitimate length on piecewise and dynamic fetches, where it does not
// mean truncation, and LongStringVar - the piecewise path - derives from Variable directly and so is
// untouched by this override.
bool StringVar::IsTruncated () const
{
    // 1406 is ORA-01406, "fetched column value was truncated" - the same code Connect.cpp:117
    // already treats as an expected outcome rather than an error, so the product has always tolerated
    // a cut, it just never recorded one. 1405, ORA-01405 "fetched column value is NULL", is measured
    // on every NULL row and is deliberately not matched: a NULL is not a cut, and tinting it would be
    // wrong in a way no one would notice until they went looking for it.
    return (m_rcode == 1406) || (m_indicator > 0);
}

/////////////////////////////////////////////////////////////////////////////
// OCI8::CharVar implementation
/////////////////////////////////////////////////////////////////////////////

// SQLT_AFC carries no length prefix. Same floor as LvcBufferSize; precautionary, nothing binds a CharVar.
sb4 AfcBufferSize (int chars)
{
    if (chars < 1)
        chars = 1;
    return static_cast<sb4>(chars) * static_cast<sb4>(sizeof(wchar_t));
}

CharVar::CharVar (int size)
: Variable(SQLT_AFC, AfcBufferSize(size))
{
}

CharVar::CharVar (const string& str)
: Variable(SQLT_AFC, AfcBufferSize(static_cast<int>(str.size())))
{
    Assign(str.c_str(), static_cast<int>(str.size()));
}

CharVar::CharVar (const char* str, int size)
: Variable(SQLT_AFC, AfcBufferSize(size = (size != -1 ? size : (str ? static_cast<int>(strlen(str)) : 0))))
{
    Assign(str, size);
}

void CharVar::Assign (const char* _str, int _size)
{
    ASSERT_EXCEPTION_FRAME;

    if (_size == -1)
        _size = _str ? static_cast<int>(strlen(_str)) : 0;

    if (_size > 0)
    {
        wstring str = Common::wstr(_str, _size);

        int capacity = m_buffer_size / static_cast<int>(sizeof(wchar_t));

        if (static_cast<int>(str.length()) > capacity)
            _RAISE(Exception(0, "OCI8::CharVar::Assign(): String buffer overflow!"));

        m_indicator = OCI_IND_NOTNULL;

        wchar_t* buf = reinterpret_cast<wchar_t*>(m_buffer);
        int len = static_cast<int>(str.length());
        memcpy(buf, str.c_str(), len * sizeof(wchar_t));

        // Pad remaining positions with spaces to emulate Oracle CHAR behaviour
        for (int i = len; i < capacity; i++)
            buf[i] = L' ';
    }
    else
    {
        m_indicator = OCI_IND_NULL;
    }
}

void CharVar::Assign (const string& str)
{
    Assign(str.data(), static_cast<int>(str.size()));
}

void CharVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        int length = m_buffer_size / static_cast<int>(sizeof(wchar_t));
        strbuff = Common::str(reinterpret_cast<const wchar_t*>(m_buffer), length);
    }
}

/////////////////////////////////////////////////////////////////////////////
// OCI8::StringZVar implementation
/////////////////////////////////////////////////////////////////////////////

StringZVar::StringZVar (int size)
: Variable(SQLT_STR, (size + 1) * static_cast<int>(sizeof(wchar_t)))
{
}

StringZVar::StringZVar (const string& str)
: Variable(SQLT_STR, (static_cast<int>(str.size()) + 1) * static_cast<int>(sizeof(wchar_t)))
{
    Assign(str.c_str(), static_cast<int>(str.size()));
}

StringZVar::StringZVar (const char* str, int size)
: Variable(SQLT_STR, ((size = (size != -1 ? size : (str ? (int)strlen(str) : 0))) + 1) * static_cast<int>(sizeof(wchar_t)))
{
    Assign(str, size);
}

void StringZVar::Assign (const char* _str, int _size)
{
    ASSERT_EXCEPTION_FRAME;

    if (_size == -1)
        _size = _str ? (int)strlen(_str) : 0;

    if (_size > 0)
    {
        std::wstring w = Common::wstr(_str, _size);
        if ((static_cast<int>(w.size()) + 1) * static_cast<int>(sizeof(wchar_t)) > GetBufferSize())
            _RAISE(Exception(0, "OCI8::StringZVar::Assign(): String buffer overflow!"));

        memcpy(m_buffer, w.c_str(), static_cast<size_t>(w.size()) * sizeof(wchar_t));
        // terminating NUL wchar
        wchar_t* term = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(m_buffer) + static_cast<size_t>(w.size()) * sizeof(wchar_t));
        *term = 0;
        m_indicator = OCI_IND_NOTNULL;
    }
    else
    {
        // empty string treated as NULL
        m_indicator = OCI_IND_NULL;
        if (GetBufferSize() >= static_cast<int>(sizeof(wchar_t)))
            *((wchar_t*)m_buffer) = 0;
    }
}

void StringZVar::Assign (const string& str)
{
    Assign(str.c_str(), static_cast<int>(str.size()));
}

void StringZVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        // buffer contains NUL-terminated wide string
        const wchar_t* w = reinterpret_cast<const wchar_t*>(m_buffer);
        strbuff = Common::str(w);
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::LongStringVar
//    Piecewise LONG fetch; no static OCI buffer.
//    OCI calls cbDefine() once per piece.  The very last piece is written by
//    OCI *after* the final callback returns, so AfterFetch() flushes it.
///////////////////////////////////////////////////////////////////////////////

LongStringVar::LongStringVar (int limit)
: Variable(SQLT_LNG, 0),   // size=0 -> base allocates 0-byte dummy buffer
  m_limit(limit),
  m_is_pending(false),
  m_piece_len(0),
  m_rcode(0),
  m_dyn_indicator(OCI_IND_NULL)
{
    memset(m_piece_buf, 0, sizeof(m_piece_buf));
}

bool LongStringVar::IsNull () const
{
    return m_data.empty() && m_dyn_indicator == OCI_IND_NULL;
}

void LongStringVar::Define (Statement& sttm, int pos)
{
    OCIDefine* defhp = 0;
    sttm.GetConnect().CHECK(
        OCIDefineByPos(sttm.GetOCIStmt(), &defhp, sttm.GetOCIError(), pos,
                       NULL, m_limit, SQLT_LNG,
                       NULL, NULL, NULL, OCI_DYNAMIC_FETCH));
    sttm.GetConnect().CHECK(
        OCIDefineDynamic(defhp, sttm.GetOCIError(),
                         this, LongStringVar::cbDefine));
}

void LongStringVar::BeforeFetch ()
{
    m_data.clear();
    m_is_pending    = false;
    m_piece_len     = 0;
    m_dyn_indicator = OCI_IND_NULL;
    m_rcode         = 0;
}

void LongStringVar::AfterFetch ()
{
    // flush the last piece that OCI filled after the final callback returned
    if (m_is_pending)
    {
        int remaining = m_limit - static_cast<int>(m_data.size());
        if (remaining > 0 && m_piece_len > 0)
        {
            int to_append = static_cast<int>(m_piece_len);
            if (to_append > remaining)
                to_append = remaining;
            m_data.append(m_piece_buf, static_cast<size_t>(to_append));
        }
        m_is_pending = false;
    }
}

// static
sb4 CDECL LongStringVar::cbDefine (dvoid* octxp, OCIDefine* /*defnp*/, ub4 /*iter*/,
                                   dvoid** bufpp,  ub4**  alenpp, ub1*  piecep,
                                   dvoid** indpp,  ub2**  rcodep)
{
    LongStringVar* self = reinterpret_cast<LongStringVar*>(octxp);

    // IMPORTANT: define callback provides the next target buffer.
    // At callback entry, m_piece_buf/m_piece_len still describe the previous
    // piece that OCI filled after the previous callback returned.
    // So flush previous piece whenever we have a pending one.
    if (self->m_is_pending)
    {
        int remaining = self->m_limit - static_cast<int>(self->m_data.size());
        if (remaining > 0 && self->m_piece_len > 0)
        {
            int to_append = static_cast<int>(self->m_piece_len);
            if (to_append > remaining)
                to_append = remaining;
            self->m_data.append(self->m_piece_buf,
                                static_cast<size_t>(to_append));
        }
    }

    // provide buffer for the piece OCI is about to write
    self->m_piece_len  = PIECE_BUF_SIZE;
    self->m_is_pending = true;   // AfterFetch will flush this piece
    self->m_rcode      = 0;

    *bufpp  = self->m_piece_buf;
    *alenpp = &self->m_piece_len;
    *indpp  = &self->m_dyn_indicator;
    *rcodep = &self->m_rcode;

    return OCI_CONTINUE;
}

void LongStringVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
    {
        strbuff = null;
        return;
    }

    // m_data contains raw UTF-16 wchar bytes as delivered by OCI
    size_t wchar_count = m_data.size() / sizeof(wchar_t);
    if (wchar_count == 0)
    {
        strbuff.clear();
        return;
    }

    const wchar_t* wp = reinterpret_cast<const wchar_t*>(m_data.data());

    // strip trailing NUL wchars OCI may append
    while (wchar_count > 0 && wp[wchar_count - 1] == 0)
        --wchar_count;

    strbuff = Common::str(wp, wchar_count);
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::NumberVar
///////////////////////////////////////////////////////////////////////////////
const char overflow[] = "###"; 

NumberVar::NumberVar (Connect& connect)
: Variable(&m_value, SQLT_VNU, sizeof(m_value)),
m_connect(connect)
{
    OCINumberSetZero(m_connect.GetOCIError(), &m_value);
}

NumberVar::NumberVar (Connect& connect, const string& numberFormat)
: Variable(&m_value, SQLT_VNU, sizeof(m_value)),
m_connect(connect),
m_numberFormat(Common::wstr(numberFormat))
{
    OCINumberSetZero(m_connect.GetOCIError(), &m_value);
}

NumberVar::NumberVar (Connect& connect, int value)
: Variable(&m_value, SQLT_VNU, sizeof(m_value)),
m_connect(connect)
{
    Assign(value);
}

NumberVar::NumberVar (Connect& connect, double value)
: Variable(&m_value, SQLT_VNU, sizeof(m_value)),
m_connect(connect)
{
    Assign(value);
}

void NumberVar::Assign (int val)
{
    m_connect.CHECK(OCINumberFromInt(m_connect.GetOCIError(), &val, sizeof(val), OCI_NUMBER_SIGNED, &m_value));
    m_indicator = OCI_IND_NOTNULL;
}

void NumberVar::Assign (double val)
{
    m_connect.CHECK(OCINumberFromReal(m_connect.GetOCIError(), &val, sizeof(val), &m_value));
    m_indicator = OCI_IND_NOTNULL;
}

void NumberVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        OraText buffer[256];
        ub4 buffer_size = sizeof(buffer);

        // OCINumberToText dereferences its format pointer, so an empty GridNlsNumberFormat
        // (the shipped default, SQLToolsSettings.cpp:56) must never become a NULL fmt - it
        // faults inside OCI. TM9 is the canonical minimum-width rendering and is already the
        // convention in this file (the OCI_TYPECODE_NUMBER branch) and in BindSource.cpp:95.
        static const wchar_t DefaultFormat[] = L"TM9";
        const bool haveFormat = 0 != m_numberFormat.length();
        const size_t fmtChars = haveFormat ? m_numberFormat.length()
                                           : (sizeof(DefaultFormat) / sizeof(wchar_t)) - 1;
        const OraText* fmt = haveFormat ? (const OraText*)m_numberFormat.c_str()
                                        : (const OraText*)DefaultFormat;
        const ub1 fmt_length = (ub1)(fmtChars * sizeof(wchar_t));

        // D1 made this the only conversion point for a NUMBER cell. NumStrVar used to ask the
        // server to render the text, so the cell inherited the session's NLS_NUMERIC_CHARACTERS
        // for free; OCINumberToText called with a NULL nls_params does not - it falls back to the
        // process locale implied by NLS_LANG/the registry. Handing the session value back is what
        // makes the two paths equivalent. Without it, a client whose locale differs from its
        // database session (a German SQLTools against an NLS_NUMERIC_CHARACTERS='.,' session, say)
        // renders 123.45 as 123,45 - disagreeing both with the session and with every other string
        // that same session formats. Measured in Tests/grid: 5 of the 50 stable cases diverged
        // under NLS_LANG=GERMAN_GERMANY.WE8MSWIN1252, and -.001 came back as the malformed -,001.
        //
        // GetNlsNumericCharacters() is Connect's cached copy of the session parameter, loaded by
        // LoadSessionNlsParameters() during Open(). Rebuilding the string per call is what the old
        // TODO objected to; it is one short concatenation on a path that already crosses into OCI,
        // so it does not yet deserve a cache of its own.
        //
        // The parameter string must be UTF-16 with a BYTE length, exactly like fmt above: this call
        // runs in wide mode throughout (fmt is a wchar_t*, and buffer is read back through
        // reinterpret_cast<wchar_t*>). Passing a narrow c_str() with a character count is what the
        // original commented-out TODO did, and OCI rejects it with ORA-22064 "invalid NLS parameter
        // string" - which is very likely why it was commented out rather than finished. Measured:
        // the narrow form failed all 16 numeric cases in Tests/grid.
        const std::string& numericChars = m_connect.GetNlsNumericCharacters();
        std::wstring nls_param;
        if (!numericChars.empty())
        {
            nls_param = L"NLS_NUMERIC_CHARACTERS='";
            nls_param += Common::wstr(numericChars);
            nls_param += L'\'';
        }

        try
        {
            m_connect.CHECK(OCINumberToText(m_connect.GetOCIError(), &m_value, 
                fmt, fmt_length, 
                // An absent parameter must stay a genuine NULL: OCI reads nls_p_length bytes from
                // this pointer, and c_str() of an empty string would advertise a valid buffer of
                // length 0 - which happens to work but says the wrong thing.
                nls_param.empty() ? static_cast<const OraText*>(0)
                                  : reinterpret_cast<const OraText*>(nls_param.c_str()),
                static_cast<ub4>(nls_param.length() * sizeof(wchar_t)),
                &buffer_size, buffer));

            strbuff = Common::str(reinterpret_cast<wchar_t*>(buffer), buffer_size / sizeof(wchar_t));
        }
        catch (const Exception& x)
        {
            if (x != 22065) throw;
            strbuff = overflow;
        }
    }
}

int NumberVar::ToInt (int null) const
{
    if (IsNull()) return null;

    int val;
    m_connect.CHECK(OCINumberToInt(m_connect.GetOCIError(), &m_value, sizeof(val), OCI_NUMBER_SIGNED, &val));
    return val;
}

double NumberVar::ToDouble (double null) const
{
    if (IsNull()) return null;

    double val;
    m_connect.CHECK(OCINumberToReal(m_connect.GetOCIError(), &m_value, sizeof(val), &val));
    return val;
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::BinaryFloatVar
///////////////////////////////////////////////////////////////////////////////

BinaryFloatVar::BinaryFloatVar ()
: Variable(&m_value, SQLT_BFLOAT, sizeof(m_value)),
m_value(0.0f)
{
}

BinaryFloatVar::BinaryFloatVar (float value)
: Variable(&m_value, SQLT_BFLOAT, sizeof(m_value)),
m_value(0.0f)
{
    Assign(value);
}

BinaryFloatVar::BinaryFloatVar (double value)
: Variable(&m_value, SQLT_BFLOAT, sizeof(m_value)),
m_value(0.0f)
{
    Assign(static_cast<float>(value));
}

void BinaryFloatVar::Assign (float val)
{
    m_value = val;
    m_indicator = OCI_IND_NOTNULL;
}

void BinaryFloatVar::Assign (double val)
{
    m_value = static_cast<float>(val);
    m_indicator = OCI_IND_NOTNULL;
}

void BinaryFloatVar::Assign (const string& str)
{
    if (str.empty())
        m_indicator = OCI_IND_NULL;
    else
    {
        m_value = static_cast<float>(strtod(str.c_str(), nullptr));
        m_indicator = OCI_IND_NOTNULL;
    }
}

void BinaryFloatVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        if (_isnan(m_value))        { strbuff = "NaN";  return; }
        if (!_finite(m_value))      { strbuff = m_value > 0.0f ? "Inf" : "-Inf"; return; }
        char buf[32];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.7g", m_value);
        strbuff = buf;
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::BinaryDoubleVar
///////////////////////////////////////////////////////////////////////////////

BinaryDoubleVar::BinaryDoubleVar ()
: Variable(&m_value, SQLT_BDOUBLE, sizeof(m_value)),
m_value(0.0)
{
}

BinaryDoubleVar::BinaryDoubleVar (double value)
: Variable(&m_value, SQLT_BDOUBLE, sizeof(m_value)),
m_value(0.0)
{
    Assign(value);
}

void BinaryDoubleVar::Assign (double val)
{
    m_value = val;
    m_indicator = OCI_IND_NOTNULL;
}

void BinaryDoubleVar::Assign (const string& str)
{
    if (str.empty())
        m_indicator = OCI_IND_NULL;
    else
    {
        m_value = strtod(str.c_str(), nullptr);
        m_indicator = OCI_IND_NOTNULL;
    }
}

void BinaryDoubleVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        if (_isnan(m_value))        { strbuff = "NaN";  return; }
        if (!_finite(m_value))      { strbuff = m_value > 0.0 ? "Inf" : "-Inf"; return; }
        char buf[32];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.15g", m_value);
        strbuff = buf;
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::DateVar
///////////////////////////////////////////////////////////////////////////////
DateVar::DateVar (Connect& connect)
: Variable(&m_value, SQLT_ODT, sizeof(m_value)),
m_connect(connect)
{
}

DateVar::DateVar (Connect& connect, const string& dateFormat)
: Variable(&m_value, SQLT_ODT, sizeof(m_value)),
m_connect(connect),
m_dateFormat(Common::wstr(dateFormat))
{
    if (m_dateFormat.empty())
        m_dateFormat = Common::wstr(m_connect.GetNlsDateFormat());
}

void DateVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        wchar_t buffer[256];
        ub4 buffer_size = sizeof(buffer);

        //wstring nlsLanguage = Common::wstr(m_connect.GetNlsLanguage());

        m_connect.CHECK(OCIDateToText(m_connect.GetOCIError(), &m_value, 
            (const OraText*)m_dateFormat.c_str(), static_cast<ub1>(m_dateFormat.length() * sizeof(wchar_t)), 
            //0/*const oratext *fmt*/, 0/*ub1 fmt_length*/, 
            //(const OraText*)nlsLanguage.c_str(), nlsLanguage.length() * sizeof(wchar_t),
            0/*nls_params*/, 0/*nls_p_length*/,
            &buffer_size, (OraText*)buffer));

        strbuff = Common::str(reinterpret_cast<wchar_t*>(buffer), buffer_size / sizeof(wchar_t));
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::TimestampVar
///////////////////////////////////////////////////////////////////////////////
    ub2 TimestampVar::sqlt_dtype_map (ub2 dtype)
    {
        switch (dtype)
        {
        case SQLT_DATE:          return OCI_DTYPE_DATE;
        case SQLT_TIME:          return OCI_DTYPE_TIME;
        case SQLT_TIMESTAMP:     return OCI_DTYPE_TIMESTAMP;
        case SQLT_TIMESTAMP_TZ:  return OCI_DTYPE_TIMESTAMP_TZ;
        case SQLT_TIMESTAMP_LTZ: return OCI_DTYPE_TIMESTAMP_LTZ;
        }
        _RAISE(Exception(0, "TimestampVar::TimestampVar(): unsupported SQLT_."));
    }

TimestampVar::TimestampVar (Connect& connect, ESubtype subtype)
: NativeOciVariable(connect, static_cast<ub2>(subtype), sqlt_dtype_map(static_cast<ub2>(subtype)))
{
}

TimestampVar::TimestampVar (Connect& connect, ESubtype subtype, const string& dateFormat)
: NativeOciVariable(connect, static_cast<ub2>(subtype), sqlt_dtype_map(static_cast<ub2>(subtype))),
m_dateFormat(Common::wstr(dateFormat))
{
    if (m_dateFormat.empty())
    {
        switch (subtype)
        {
        case SQLT_DATE:          m_dateFormat = Common::wstr(m_connect.GetNlsDateFormat()); break;
        case SQLT_TIME:          m_dateFormat = Common::wstr(m_connect.GetNlsTimeFormat()); break;
        case SQLT_TIMESTAMP:     m_dateFormat = Common::wstr(m_connect.GetNlsTimestampFormat()); break;
        case SQLT_TIMESTAMP_TZ:  m_dateFormat = Common::wstr(m_connect.GetNlsTimestampTzFormat()); break;
        case SQLT_TIMESTAMP_LTZ: m_dateFormat = Common::wstr(m_connect.GetNlsTimestampTzFormat()); break;
        }
    }
}

void TimestampVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        wchar_t buffer[256];
        ub4 buffer_size = sizeof(buffer);
        size_t fmt_length = m_dateFormat.length();
        const wchar_t* fmt = fmt_length ? m_dateFormat.c_str() : 0;

        m_connect.DateTimeToText((const OCIDateTime*)m_buffer, 
            fmt, fmt_length * sizeof(wchar_t), 0/*fsprec*/, 
            //m_connect.GetNlsLanguage().c_str(), m_connect.GetNlsLanguage().length(),
            0/*lang_name*/, 0/*lang_length*/,
            buffer, &buffer_size);

        strbuff = Common::str(buffer, buffer_size /  sizeof(wchar_t));
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::IntervalVar
///////////////////////////////////////////////////////////////////////////////
    ub2 IntervalVar::sqlt_dtype_map (ub2 dtype)
    {
        switch (dtype)
        {
        case SQLT_INTERVAL_YM:  return OCI_DTYPE_INTERVAL_YM;
        case SQLT_INTERVAL_DS:  return OCI_DTYPE_INTERVAL_DS;
        }
        _RAISE(Exception(0, "TimestampVar::TimestampVar(): unsupported SQLT_."));
    }

IntervalVar::IntervalVar (Connect& connect, ESubtype subtype)
: NativeOciVariable(connect, static_cast<ub2>(subtype), sqlt_dtype_map(static_cast<ub2>(subtype)))
{
}

void IntervalVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        wchar_t buffer[256];
        size_t result_byte_size;

        m_connect.IntervalToText((const OCIInterval*)m_buffer, 0/*lfprec*/, 2/*fsprec*/, buffer, sizeof(buffer), &result_byte_size);

        strbuff = Common::str(buffer, result_byte_size / sizeof(wchar_t));
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::NamedTypeVariable
///////////////////////////////////////////////////////////////////////////////

// Oracle describes a named collection with the GENERIC typecode SQLT_NCO (122) =
// OCI_TYPECODE_NAMEDCOLLECTION, both on a select-list column and on an object attribute's
// parameter descriptor - OCI_TYPECODE_VARRAY (247) and OCI_TYPECODE_TABLE (248) are what the
// type manager's own accessors report instead. All three mean "collection", so every place that
// decides between walking a collection and printing a scalar has to accept all three.
// MDSYS.SDO_GEOMETRY describes its SDO_ELEM_INFO and SDO_ORDINATES attributes as 122; testing
// only for 247/248 is what let them fall through to serialize_scalar and print "?".
static bool IsCollectionTypeCode (OCITypeCode tc)
{
    return tc == OCI_TYPECODE_VARRAY
        || tc == OCI_TYPECODE_TABLE
        || tc == OCI_TYPECODE_NAMEDCOLLECTION;
}

NamedTypeVariable::NamedTypeVariable (Connect& connect, const char* schema, const char* type_name, int str_limit)
: Variable(0, static_cast<ub2>(SQLT_NTY), 0),
  m_connect(connect),
  m_tdo(nullptr),
  m_instance(nullptr),
  m_ind_struct(nullptr),
  m_str_limit(str_limit)
{
    // Get the type descriptor object (TDO)
    wstring wschema = Common::wstr(schema);
    wstring wtype = Common::wstr(type_name);

    m_connect.CHECK(
        OCITypeByName(
            m_connect.GetOCIEnv(),
            m_connect.GetOCIError(),
            m_connect.GetOCISvcCtx(),
            (const OraText*)wschema.c_str(), wschema.length() * sizeof(wchar_t),
            (const OraText*)wtype.c_str(),   wtype.length() * sizeof(wchar_t),
            nullptr, 0,
            OCI_DURATION_SESSION,
            OCI_TYPEGET_HEADER,
            &m_tdo
        )
    );
}

NamedTypeVariable::~NamedTypeVariable ()
{
    try { EXCEPTION_FRAME;
        if (m_instance)
            m_connect.CHECK(OCIObjectFree(m_connect.GetOCIEnv(), m_connect.GetOCIError(), m_instance, OCI_OBJECTFREE_FORCE));
    }
    _DESTRUCTOR_HANDLER_;
}

void NamedTypeVariable::Define (Statement& sttm, int pos)
{
    OCIDefine* defhp;
    sttm.GetConnect().CHECK(
        OCIDefineByPos(sttm.GetOCIStmt(), &defhp, sttm.GetOCIError(), pos,
                        nullptr, 0, m_type, &m_indicator, nullptr, nullptr, OCI_DEFAULT));

    ub4 obj_size = sizeof(void*);
    ub4 ind_size = 0;
    sttm.GetConnect().CHECK(
        OCIDefineObject(defhp, sttm.GetOCIError(), m_tdo, 
                        (dvoid**)&m_instance, &obj_size, (dvoid**)&m_ind_struct, &ind_size));
}

void NamedTypeVariable::BeforeFetch ()
{
    if (m_instance)
    {
        m_connect.CHECK(OCIObjectFree(m_connect.GetOCIEnv(), m_connect.GetOCIError(), m_instance, OCI_OBJECTFREE_FORCE));
        const_cast<NamedTypeVariable*>(this)->m_instance = nullptr;
    }
}

void NamedTypeVariable::GetString (string& strbuff, const string& null) const
{
    if (/*IsNull() ||*/ !m_instance)
    {
        strbuff = null;
        return;
    }

    try
    {
        strbuff.clear();

        // Determine if this is an object or collection using OCIDescribeAny
        OCIDescribe* dschp = nullptr;
        m_connect.CHECK_ALLOC(OCIHandleAlloc(m_connect.GetOCIEnv(), (dvoid**)&dschp, OCI_HTYPE_DESCRIBE, 0, nullptr));

        try
        {
            // Describe the type
            m_connect.CHECK(OCIDescribeAny(m_connect.GetOCISvcCtx(), m_connect.GetOCIError(), 
                                           m_tdo, 0, OCI_OTYPE_PTR, OCI_DEFAULT, OCI_PTYPE_TYPE, dschp));

            // Get parameter handle
            OCIParam* paramp = nullptr;
            m_connect.CHECK(OCIAttrGet(dschp, OCI_HTYPE_DESCRIBE, &paramp, nullptr, OCI_ATTR_PARAM, m_connect.GetOCIError()));

            // Get type code from the describe result
            OCITypeCode tc = OCI_TYPECODE_OBJECT;
            m_connect.CHECK(OCIAttrGet(paramp, OCI_DTYPE_PARAM, &tc, nullptr, OCI_ATTR_TYPECODE, m_connect.GetOCIError()));

            if (tc == OCI_TYPECODE_OBJECT)
            {
                // m_ind_struct is written per-fetch by OCI via OCIDefineObject's pindpp.
                // Standard convention: OCI_IND_NULL (-1) = null, OCI_IND_NOTNULL (0) = not null.
                if (!m_ind_struct || m_ind_struct[0] == OCI_IND_NULL)
                    strbuff = null;
                else
                    serialize_object(m_connect, m_instance, m_ind_struct, m_tdo, m_str_limit, strbuff, 0);
            }
            else if (IsCollectionTypeCode(tc))
            {
                // OCIObjectGetInd must NOT be called on collection types.
                // m_instance is an OCIColl* handle, not a named-type cache entry.
                // OCIObjectGetInd reads a field at a fixed offset inside OCIColl and
                // returns it as the indicator pointer — producing garbage (0x62, etc.)
                // that crashes on dereference.
                // Use m_ind_struct from OCIDefineObject instead (standard convention).
                if (!m_ind_struct || m_ind_struct[0] == OCI_IND_NULL)
                    strbuff = null;
                else
                    serialize_collection(m_connect, m_instance, OCI_IND_NOTNULL, m_tdo, m_str_limit, strbuff, 0);
            }
            else
            {
                char buf[64];
                sprintf_s(buf, sizeof(buf), "<Unknown Type Code: %d>", (int)tc);
                strbuff = buf;
            }

            OCIHandleFree(dschp, OCI_HTYPE_DESCRIBE);
        }
        catch (...)
        {
            if (dschp) OCIHandleFree(dschp, OCI_HTYPE_DESCRIBE);
            throw;
        }
    }
    catch (const Exception& x)
    {
        strbuff = string("<Error: ") + x.what() + ">";
    }
}

void NamedTypeVariable::serialize_object(Connect& conn, void* inst, OCIInd* inds, OCIType* tdo, int limit, string& out, int depth)
{
    const int MAX_DEPTH = 5;
    if (depth > MAX_DEPTH)
    {
        out += "(...)";
        return;
    }

    OCIDescribe* dschp = nullptr;
    OCIParam* paramp = nullptr;
    OCIParam* attr_list = nullptr;

    try
    {
        // If inds is NULL or invalid, try to get it using OCIObjectGetInd
        void* actual_inds = inds;
        if (!actual_inds)
        {
            void* null_struct = nullptr;
            sword status = OCIObjectGetInd(conn.GetOCIEnv(), conn.GetOCIError(), inst, &null_struct);
            if (status == OCI_SUCCESS && null_struct)
            {
                actual_inds = null_struct;
            }
        }

        // Allocate describe handle
        conn.CHECK_ALLOC(OCIHandleAlloc(conn.GetOCIEnv(), (dvoid**)&dschp, OCI_HTYPE_DESCRIBE, 0, nullptr));

        // Describe the type
        conn.CHECK(OCIDescribeAny(conn.GetOCISvcCtx(), conn.GetOCIError(), 
                                  tdo, 0, OCI_OTYPE_PTR, OCI_DEFAULT, OCI_PTYPE_TYPE, dschp));

        // Get parameter handle
        conn.CHECK(OCIAttrGet(dschp, OCI_HTYPE_DESCRIBE, &paramp, nullptr, OCI_ATTR_PARAM, conn.GetOCIError()));

        // Get type name for display
        wchar_t* type_name = nullptr;
        ub4 type_name_len = 0;
        conn.CHECK(OCIAttrGet(paramp, OCI_DTYPE_PARAM, &type_name, &type_name_len, OCI_ATTR_NAME, conn.GetOCIError()));

        // Get attribute list and count
        conn.CHECK(OCIAttrGet(paramp, OCI_DTYPE_PARAM, &attr_list, nullptr, OCI_ATTR_LIST_TYPE_ATTRS, conn.GetOCIError()));

        ub2 num_attrs = 0;
        conn.CHECK(OCIAttrGet(paramp, OCI_DTYPE_PARAM, &num_attrs, nullptr, OCI_ATTR_NUM_TYPE_ATTRS, conn.GetOCIError()));

        string type_name_str = Common::str(type_name, type_name_len / sizeof(wchar_t));
        out += type_name_str + "(";

        for (ub2 i = 0; i < num_attrs; ++i)
        {
            if (i > 0) out += ", ";

            OCIParam* attr_param = nullptr;
            conn.CHECK(OCIParamGet(attr_list, OCI_DTYPE_PARAM, conn.GetOCIError(), (dvoid**)&attr_param, i + 1));

            // Get attribute name
            wchar_t* attr_name = nullptr;
            ub4 name_len = 0;
            conn.CHECK(OCIAttrGet(attr_param, OCI_DTYPE_PARAM, &attr_name, &name_len, OCI_ATTR_NAME, conn.GetOCIError()));

            string attr_name_str = Common::str(attr_name, name_len / sizeof(wchar_t));

            // Get attribute type code
            OCITypeCode attr_tc = OCI_TYPECODE_OBJECT;
            conn.CHECK(OCIAttrGet(attr_param, OCI_DTYPE_PARAM, &attr_tc, nullptr, OCI_ATTR_TYPECODE, conn.GetOCIError()));

            // Get attribute TDO (for nested objects/collections)
            OCIRef* attr_type_ref = nullptr;
            OCIAttrGet(attr_param, OCI_DTYPE_PARAM, &attr_type_ref, nullptr, OCI_ATTR_REF_TDO, conn.GetOCIError());

            // Display attribute name
            out += attr_name_str + "=";

            // Extract attribute value using OCIObjectGetAttr
            // NOTE: Index-based access is NOT SUPPORTED by OCI!
            // We must use name-based access instead
            void* attr_value = nullptr;
            void* attr_null_struct = nullptr;
            OCIInd attr_null_status = OCI_IND_NULL;
            OCIType* attr_tdo = nullptr;

            // Convert attribute name to oratext array for OCI
            wstring wattr_name = Common::wstr(attr_name_str);
            const oratext* name_ptr = (const oratext*)wattr_name.c_str();
            ub4 name_byte_len = wattr_name.length() * sizeof(wchar_t);

            sword status = OCIObjectGetAttr(conn.GetOCIEnv(), conn.GetOCIError(),
                                            inst, actual_inds, tdo,
                                            &name_ptr, &name_byte_len, 1,  // names, lengths, name_count
                                            nullptr, 0,                     // indexes (NOT SUPPORTED)
                                            &attr_null_status,
                                            &attr_null_struct,
                                            &attr_value,
                                            &attr_tdo);

            // Check status but don't throw - some attribute types might not be supported
            if (status != OCI_SUCCESS)
            {
                // Get the actual error for debugging
                sb4 errcode = 0;
                text errbuf[512];
                OCIErrorGet((dvoid*)conn.GetOCIError(), 1, nullptr, &errcode, errbuf, sizeof(errbuf), OCI_HTYPE_ERROR);

                char debug[1024];
                sprintf_s(debug, sizeof(debug), "?[Err:%d]", (int)errcode);
                out += debug;
            }
            else if (attr_null_status == OCI_IND_NULL)
            {
                out += "null";
            }
            else if (attr_value)
            {
                // OCIObjectGetAttr returns the attribute's TDO for object and collection attributes.
                // Should a client leave it null, the parameter descriptor's OCI_ATTR_REF_TDO names
                // the same type - the fallback serialize_collection already uses for elements.
                if (!attr_tdo && attr_type_ref)
                    OCITypeByRef(conn.GetOCIEnv(), conn.GetOCIError(), attr_type_ref,
                                 OCI_DURATION_SESSION, OCI_TYPEGET_ALL, &attr_tdo);

                // Check if this is a nested object or collection
                if (attr_tc == OCI_TYPECODE_OBJECT && attr_tdo)
                {
                    serialize_object(conn, attr_value, (OCIInd*)attr_null_struct, attr_tdo, limit, out, depth + 1);
                }
                else if (IsCollectionTypeCode(attr_tc) && attr_tdo)
                {
                    // A collection attribute lives in the instance as an OCIColl* FIELD, and
                    // OCIObjectGetAttr returns the address of that field, not the collection: for
                    // SDO_GEOMETRY the attribute pointer lands at instance+0x70 while the collection
                    // itself sits elsewhere in the object cache. Passing the field address on would
                    // have OCICollSize read the object's own bytes as a collection header. Hence the
                    // one dereference - the same rule orl.h documents for OCICollGetElem (see below).
                    void* coll = *(void**)attr_value;
                    if (coll)
                        serialize_collection(conn, coll, attr_null_status, attr_tdo, limit, out, depth + 1);
                    else
                        out += "null";
                }
                else
                {
                    serialize_scalar(conn, attr_value, attr_null_status, attr_tc, out);
                }
            }
            else
            {
                out += "null";
            }

            if (out.size() > (size_t)limit)
            {
                out += "...";
                break;
            }
        }

        out += ")";

        // Free describe handle
        OCIHandleFree(dschp, OCI_HTYPE_DESCRIBE);
    }
    catch (...)
    {
        if (dschp) OCIHandleFree(dschp, OCI_HTYPE_DESCRIBE);
        throw;
    }
}

void NamedTypeVariable::serialize_collection(Connect& conn, void* coll, OCIInd ind, OCIType* tdo, int limit, string& out, int depth)
{
    const int MAX_DEPTH = 5;
    if (depth > MAX_DEPTH || ind == OCI_IND_NULL)
    {
        out += "null";
        return;
    }

    try
    {
        // Get collection size
        sb4 coll_size = 0;
        conn.CHECK(OCICollSize(conn.GetOCIEnv(), conn.GetOCIError(), (OCIColl*)coll, &coll_size));

        // Get element type using OCIDescribeAny
        OCIDescribe* dschp = nullptr;
        conn.CHECK_ALLOC(OCIHandleAlloc(conn.GetOCIEnv(), (dvoid**)&dschp, OCI_HTYPE_DESCRIBE, 0, nullptr));

        conn.CHECK(OCIDescribeAny(conn.GetOCISvcCtx(), conn.GetOCIError(),
                                  tdo, 0, OCI_OTYPE_PTR, OCI_DEFAULT, OCI_PTYPE_TYPE, dschp));

        OCIParam* paramp = nullptr;
        conn.CHECK(OCIAttrGet(dschp, OCI_HTYPE_DESCRIBE, &paramp, nullptr, OCI_ATTR_PARAM, conn.GetOCIError()));

        OCIParam* elem_param = nullptr;
        conn.CHECK(OCIAttrGet(paramp, OCI_DTYPE_PARAM, &elem_param, nullptr, OCI_ATTR_COLLECTION_ELEMENT, conn.GetOCIError()));

        OCITypeCode elem_tc = OCI_TYPECODE_OBJECT;
        conn.CHECK(OCIAttrGet(elem_param, OCI_DTYPE_PARAM, &elem_tc, nullptr, OCI_ATTR_TYPECODE, conn.GetOCIError()));

        // Get element TDO (type descriptor) for nested objects/collections
        OCIRef* elem_type_ref = nullptr;
        OCIType* elem_tdo = nullptr;
        if (elem_tc == OCI_TYPECODE_OBJECT || IsCollectionTypeCode(elem_tc))
        {
            OCIAttrGet(elem_param, OCI_DTYPE_PARAM, &elem_type_ref, nullptr, OCI_ATTR_REF_TDO, conn.GetOCIError());
            if (elem_type_ref)
            {
                conn.CHECK(OCITypeByRef(conn.GetOCIEnv(), conn.GetOCIError(), elem_type_ref, 
                                        OCI_DURATION_SESSION, OCI_TYPEGET_ALL, &elem_tdo));
            }
        }

        out += "[";

        for (sb4 i = 0; i < coll_size; ++i)
        {
            if (i > 0) out += ", ";

            boolean exists = FALSE;
            dvoid* elem_ptr = nullptr;
            dvoid* elem_ind_ptr = nullptr;

            conn.CHECK(OCICollGetElem(conn.GetOCIEnv(), conn.GetOCIError(), (OCIColl*)coll, i, 
                                      &exists, &elem_ptr, &elem_ind_ptr));

            if (!exists || !elem_ptr)
            {
                out += "null";
                continue;
            }

            OCIInd* elem_ind = (OCIInd*)elem_ind_ptr;
            OCIInd elem_ind_val = elem_ind ? *elem_ind : OCI_IND_NOTNULL;

            if (elem_ind_val == OCI_IND_NULL)
            {
                out += "null";
            }
            else
            {
                // Check element type and call appropriate serializer
                if (elem_tc == OCI_TYPECODE_OBJECT && elem_tdo)
                {
                    // Element is a nested object - get its null structure and serialize
                    void* elem_null_struct = nullptr;
                    OCIObjectGetInd(conn.GetOCIEnv(), conn.GetOCIError(), elem_ptr, &elem_null_struct);
                    serialize_object(conn, elem_ptr, (OCIInd*)elem_null_struct, elem_tdo, limit, out, depth + 1);
                }
                else if (IsCollectionTypeCode(elem_tc) && elem_tdo)
                {
                    // Element is a nested collection. orl.h's OCICollGetElem table documents the
                    // element pointer as one indirection deeper than the value for every handle type
                    // (OCIString**, OCIRaw**, OCIRef**, OCILobLocator**) while the inline types
                    // (OCINumber, OCIDate, object) come back directly; a collection is a handle, so
                    // elem_ptr addresses the OCIColl* slot and the collection is behind it.
                    void* nested = *(void**)elem_ptr;
                    if (nested)
                        serialize_collection(conn, nested, elem_ind_val, elem_tdo, limit, out, depth + 1);
                    else
                        out += "null";
                }
                else
                {
                    // Element is a scalar (number, string, date, etc.)
                    serialize_scalar(conn, elem_ptr, elem_ind_val, elem_tc, out);
                }
            }

            if (out.size() > (size_t)limit)
            {
                out += "...";
                break;
            }
        }

        out += "]";

        OCIHandleFree(dschp, OCI_HTYPE_DESCRIBE);
    }
    catch (const Exception& x)
    {
        out += "[Error: " + string(x.what()) + "]";
    }
}

void NamedTypeVariable::serialize_scalar(Connect& conn, void* val, OCIInd ind, OCITypeCode tc, string& out)
{
    if (ind == OCI_IND_NULL || !val)
    {
        out += "null";
        return;
    }

    try
    {
        switch (tc)
        {
        case OCI_TYPECODE_NUMBER:
        case OCI_TYPECODE_INTEGER:
        case OCI_TYPECODE_SMALLINT:
        case OCI_TYPECODE_REAL:
        case OCI_TYPECODE_DOUBLE:
        case OCI_TYPECODE_FLOAT:
        {
            OCINumber* num = (OCINumber*)val;
            wchar_t buf[128];
            ub4 buf_size = sizeof(buf);
            conn.CHECK(OCINumberToText(conn.GetOCIError(), num, 
                                       (const oratext*)L"TM9", 3 * sizeof(wchar_t),
                                       nullptr, 0,
                                       &buf_size, (oratext*)buf));
            out += Common::str(buf, buf_size / sizeof(wchar_t));
            break;
        }

        case OCI_TYPECODE_CHAR:
        case OCI_TYPECODE_VARCHAR:
        case OCI_TYPECODE_VARCHAR2:
        {
            OCIString* str = *(OCIString**)val;
            wchar_t* text = (wchar_t*)OCIStringPtr(conn.GetOCIEnv(), str);
            if (text)
            {
                out += "'";
                out += Common::str(text);
                out += "'";
            }
            break;
        }

        case OCI_TYPECODE_DATE:
        {
            OCIDate* date = (OCIDate*)val;
            wchar_t buf[64];
            ub4 buf_size = sizeof(buf);
            conn.CHECK(OCIDateToText(conn.GetOCIError(), date,
                                     (const oratext*)L"YYYY-MM-DD HH24:MI:SS", 21 * sizeof(wchar_t),
                                     nullptr, 0,
                                     &buf_size, (oratext*)buf));
            out += Common::str(buf, buf_size / sizeof(wchar_t));
            break;
        }

        case OCI_TYPECODE_TIMESTAMP:
        case OCI_TYPECODE_TIMESTAMP_TZ:
        case OCI_TYPECODE_TIMESTAMP_LTZ:
        {
            OCIDateTime* dt = *(OCIDateTime**)val;
            wchar_t buf[64];
            ub4 buf_size = sizeof(buf);
            conn.DateTimeToText(dt, L"YYYY-MM-DD HH24:MI:SS.FF", 24 * sizeof(wchar_t), 6,
                               nullptr, 0, buf, &buf_size);
            out += Common::str(buf, buf_size / sizeof(wchar_t));
            break;
        }

        case OCI_TYPECODE_INTERVAL_YM:
        {
            OCIInterval* inter = *(OCIInterval**)val;
            wchar_t buf[64];
            size_t result_byte_size = 0;
            conn.IntervalToText(inter, 9/*lfprec*/, 0/*fsprec*/, buf, sizeof(buf), &result_byte_size);
            out += Common::str(buf, result_byte_size / sizeof(wchar_t));
            break;
        }

        case OCI_TYPECODE_INTERVAL_DS:
        {
            OCIInterval* inter = *(OCIInterval**)val;
            wchar_t buf[64];
            size_t result_byte_size = 0;
            conn.IntervalToText(inter, 9/*lfprec*/, 9/*fsprec*/, buf, sizeof(buf), &result_byte_size);
            out += Common::str(buf, result_byte_size / sizeof(wchar_t));
            break;
        }

        case OCI_TYPECODE_RAW:
        {
            out += "<RAW>";
            break;
        }

        case OCI_TYPECODE_CLOB:
        case OCI_TYPECODE_BLOB:
        {
            out += "<LOB>";
            break;
        }

        default:
        {
            // Name the typecode instead of printing a bare "?": the question mark is what let the
            // named-collection attributes look like data rather than like an unhandled type.
            char buf[64];
            sprintf_s(buf, sizeof(buf), "<Unsupported typecode %d>", (int)tc);
            out += buf;
            break;
        }
        }
    }
    catch (const Exception& x)
    {
        out += "<Error: " + string(x.what()) + ">";
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::DummyVar
///////////////////////////////////////////////////////////////////////////////
const std::string DummyVar::m_unsupportedStr = "<Unsupported>";
const std::string DummyVar::m_skippedStr = "<Skipped>";

DummyVar::DummyVar (bool skipped)
: Variable(0, 0),
m_skipped(skipped)
{
}

void DummyVar::GetString (std::string& strbuff, const std::string& /*null*/) const
{
    strbuff = m_skipped ? m_skippedStr : m_unsupportedStr;
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::LobVar
///////////////////////////////////////////////////////////////////////////////

LobVar::LobVar (Connect& connect, ELobSubtype subtype, int limit, ECharForm charForm /*ecfImplicit*/)
: NativeOciVariable(connect, static_cast<ub2>(subtype), OCI_DTYPE_LOB),
  m_limit(limit),
  m_charForm(charForm),
  m_truncated(false)
  //m_temporary(false)
{
}

int LobVar::getLobLength () const
{
    if (IsNull())
    {
        m_truncated = false;        // nothing was read for this row, so nothing was cut
        return 0;
    }

    ub4 length = 0;
	m_connect.CHECK(OCILobGetLength(m_connect.GetOCISvcCtx(), m_connect.GetOCIError(), (OCILobLocator*)m_buffer, &length));

    // This clamp is the last place a LOB's real size is known: after it, every caller - getString(),
    // GetString(), BLobVar::GetString() and so GetValue() - sees only the capped value and cannot tell
    // a LOB that happens to be m_limit bytes from one that was cut down to that. Recording the
    // comparison here needs no extra OCILobGetLength, since the true length is already in hand.
    m_truncated = length > static_cast<ub4>(m_limit);
    if (m_truncated)
        length = m_limit;

    return length;
}

void LobVar::getString (char* strbuff, int buffsize, ub2 csid) const
{
    ASSERT_EXCEPTION_FRAME;

    int len = getLobLength();
    int char_size = (csid == OCI_UTF16ID) ? sizeof(wchar_t) : sizeof(char);

    if (char_size * len >= buffsize)
        _RAISE(Exception(0, "OCI8::LobVar::GetString(): String buffer overflow!"));

    if (!IsNull())
    {
	    ub4 offset = 1;
	    ub4 readsize = len;

        m_connect.CHECK(
            OCILobRead(m_connect.GetOCISvcCtx(), m_connect.GetOCIError(), (OCILobLocator*)m_buffer,
                      &readsize, offset, strbuff, (ub4)buffsize, 0, 0,  csid, (ub1)m_charForm)
            );
    }

    //strbuff[char_size * len] = 0;
}

//void LobVar::makeTemporary (ub1 lobtype)
//{
//    m_connect.CHECK(
//        OCILobCreateTemporary(m_connect.GetOCISvcCtx(), m_connect.GetOCIError(), (OCILobLocator*)m_buffer,
//            OCI_DEFAULT, OCI_DEFAULT, lobtype, TRUE, OCI_DURATION_SESSION)
//        );
//    m_temporary = true;
//}

void LobVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        std::wstring wbuff;
        wbuff.resize(getLobLength()+1);
        getString((char*)(wbuff.c_str()), sizeof(wchar_t) * wbuff.size(), OCI_UTF16ID);
        strbuff = Common::str(wbuff);
        if (strbuff.size() > 0 && *strbuff.rbegin() == 0)
            strbuff.erase(strbuff.size()-1);
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::CLobVar
///////////////////////////////////////////////////////////////////////////////
//void CLobVar::MakeTemporary ()
//{
//    makeTemporary(OCI_TEMP_CLOB);
//}

///////////////////////////////////////////////////////////////////////////////
// OCI8::BLobVar
///////////////////////////////////////////////////////////////////////////////

BLobVar::BLobVar (Connect& connect, ELobSubtype subtype, int limit, int hexFormatLength)
: LobVar(connect, subtype, limit),
m_hexFormatLength(hexFormatLength)
{
    _CHECK_AND_THROW_(m_hexFormatLength % 2 == 0,  "OCI8::BLobVar::BLobVar: Hex line length must be even.");
}

BLobVar::BLobVar (Connect& connect, int limit, int hexFormatLength)
: LobVar(connect, elsBLob, limit),
m_hexFormatLength(hexFormatLength)
{
    _CHECK_AND_THROW_(m_hexFormatLength % 2 == 0,  "OCI8::BLobVar::BLobVar: Hex line length must be even.");
}

void BLobVar::GetString (std::string& strbuff, const std::string& null) const
{
    if (IsNull())
        strbuff = null;
    else
    {
        std::string bin_data;
        bin_data.resize(getLobLength()+1);
        getString((char*)(bin_data.c_str()), bin_data.size(), 0);
        
        ostringstream out;
        out << std::hex << std::setfill('0');

        string::const_iterator it = bin_data.begin();
        for (int i = 0; it != bin_data.end(); ++it)
        {
            out  << std::setw(2) << (int)(unsigned char)*it;
            if (!(++i % m_hexFormatLength)) out << endl;
        }

        strbuff = out.str();
    }
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::BFileVar
///////////////////////////////////////////////////////////////////////////////

BFileVar::BFileVar (Connect& connect, int limit, int hexFormatLength)
: BLobVar(connect, elsBFile, limit, hexFormatLength)
{
}

void BFileVar::GetString (string& buff, const string& null) const
{
    m_connect.CHECK(
        OCILobFileOpen(m_connect.GetOCISvcCtx(),m_connect.GetOCIError(), (OCILobLocator*)m_buffer, OCI_FILE_READONLY)
        );
    try
    {
        BLobVar::GetString(buff, null);
    }
    catch (const Exception&)
    {
        m_connect.CHECK(
            OCILobFileClose(m_connect.GetOCISvcCtx(),m_connect.GetOCIError(), (OCILobLocator*)m_buffer)
            );
    }
    m_connect.CHECK(
        OCILobFileClose(m_connect.GetOCISvcCtx(),m_connect.GetOCIError(), (OCILobLocator*)m_buffer)
        );
}

///////////////////////////////////////////////////////////////////////////////
// Stage 2 (plan §4.1): Variable::GetValue and its overrides.
//
// GetValue writes the value OCI just fetched into the grid arena, in its own kind, and returns
// the payload offset. SqlArena::NullOffset means a NULL cell, which costs no bytes at all.
//
// These live in one block rather than beside each class so the whole Stage 2 surface can be
// read, tested and later moved as a unit.
///////////////////////////////////////////////////////////////////////////////

namespace
{
    // Stores already-rendered display text under an explicit kind. Used by the fallbacks below,
    // where the goal is to keep the cell readable rather than to keep it typed.
    Common::SqlArena::CellOffset StoreRendered (Common::SqlArena& arena, Common::SqlValueKind kind,
                                                const string& text)
    {
        return Common::SqlValue::Store(arena, kind, text.data(), text.size());
    }
}

// Base: render exactly what GetString() renders today and store it as text. Any type that has
// not been overridden therefore behaves as it does now, which is what makes the per-column
// swap in Stage 3/4 safe to do incrementally.
Common::SqlArena::CellOffset Variable::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    string str;
    GetString(str, m_null);

    // IsTruncated() is virtual, so this one call also picks up LobVar's clamp-based answer. Read
    // after GetString() on purpose: that is the call which resolves the LOB length and therefore
    // the only point at which a cut can have been recorded.
    const unsigned flags = IsTruncated() ? Common::evfTruncated : Common::evfNone;
    return Common::SqlValue::StoreText(arena, str.data(), str.size(), flags);
}

Common::SqlArena::CellOffset RefCursorVariable::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    string str;
    GetString(str, m_null);
    return StoreRendered(arena, Common::SqlValueKind::Unsupported, str);
}

// D1: NUMBER keeps its exact decimal value. OCINumber is decoded by our own reader, so this
// path makes no OCINumberToText call and its result does not depend on the NLS settings.
Common::SqlArena::CellOffset NumberVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    // An OCINumber is Oracle's VARNUM image, not a bare exponent+mantissa: byte 0 holds the
    // length of the number data that follows it, and OCI writes only that many bytes, so the
    // tail of the 22-byte buffer is untouched (0xCD under the debug CRT). SqlNumber::Decode
    // wants the number data itself - that is why its MaxBytes is 21 where sizeof(OCINumber)
    // is 22. Handing it the whole buffer read the length byte as the exponent and tripped the
    // size guard, so *every* live NUMBER was rejected and fell back to untyped text.
    const unsigned char* const raw = reinterpret_cast<const unsigned char*>(&m_value);
    const int len = static_cast<int>(raw[0]);

    Common::SqlNumber num;
    if (0 < len && len <= static_cast<int>(sizeof(m_value)) - 1
        && Common::SqlNumber::Decode(raw + 1, len, num))
        return Common::SqlValue::StoreNumber(arena, num);

    // Malformed bytes. Showing the cell untyped beats silently losing it.
    string str;
    GetString(str, m_null);
    return StoreRendered(arena, Common::SqlValueKind::Unsupported, str);
}

// Every value is stored bitwise, NaN and +-Inf included, so a float/double cell costs 4/8 bytes
// and compares and sorts without a conversion. Storing the non-finite ones under Unsupported
// instead - as this did - cost two things that keeping the kind recovers:
//
//   * ToSqlLiteral() could not name them, and its default: branch exported "NULL". Under their own
//     kind the already-tested BINARY_FLOAT_NAN / BINARY_FLOAT_INFINITY branches fire, which is real
//     Oracle SQL and round-trips.
//   * Sorting put all three of -Inf, +Inf and NaN in the last band - evbUnsupported, above every
//     number, date and text value - ordered against each other by byte-comparing their display
//     text, so -Inf sorted above every finite value. Under their own kind Compare's CompareReal()
//     gives -Inf < finite < +Inf < NaN - Oracle's own BINARY_FLOAT order, and the one its comment
//     already documents.
//
// The display is untouched: the fetch worker parks GetString()'s "NaN"/"Inf"/"-Inf" beside the
// payload (OciSource.cpp:466 and :653) and GetCellStr() reads only that, so the kind was never
// where the text came from. A negative NaN has no Oracle spelling and renders as BINARY_FLOAT_NAN.
Common::SqlArena::CellOffset BinaryFloatVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    return Common::SqlValue::StorePod(arena, Common::SqlValueKind::BinaryFloat, m_value);
}

Common::SqlArena::CellOffset BinaryDoubleVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    return Common::SqlValue::StorePod(arena, Common::SqlValueKind::BinaryDouble, m_value);
}

// DATE needs no OCI call at all: OCIDate is a plain struct and OCIDateGetDate/OCIDateGetTime
// are macros over its fields. That is also why OCIDateGetDate must never be added to the
// GetProcAddress table - it is not an exported function, so the lookup would always fail.
Common::SqlArena::CellOffset DateVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    sb2 year = 0;
    ub1 month = 0, day = 0, hour = 0, minute = 0, sec = 0;
    OCIDateGetDate(&m_value, &year, &month, &day);
    OCIDateGetTime(&m_value, &hour, &minute, &sec);

    Common::SqlDate d;
    memset(&d, 0, sizeof(d));    // padding is part of the payload, so it must be deterministic
    d.year   = year;
    d.month  = month;
    d.day    = day;
    d.hour   = hour;
    d.minute = minute;
    d.second = sec;

    return Common::SqlValue::StorePod(arena, Common::SqlValueKind::Date, d);
}

Common::SqlValueKind TimestampVar::GetKind () const
{
    switch (m_oci_handle_type)
    {
    case OCI_DTYPE_TIMESTAMP_TZ:  return Common::SqlValueKind::TimestampTz;
    case OCI_DTYPE_TIMESTAMP_LTZ: return Common::SqlValueKind::TimestampLtz;
    default:                      return Common::SqlValueKind::Timestamp;
    }
}

Common::SqlArena::CellOffset TimestampVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    // All-or-nothing capability gate. A client that can render text but cannot report the
    // parts falls back to that text, so the grid still shows exactly what it shows today.
    if (!Connect::IsTimestampPartsSupported())
    {
        string str;
        GetString(str, m_null);
        return StoreRendered(arena, Common::SqlValueKind::Unsupported, str);
    }

    const OCIDateTime* dt = static_cast<const OCIDateTime*>(m_buffer);

    sb2 year = 0;
    ub1 month = 0, day = 0, hour = 0, minute = 0, sec = 0;
    ub4 fsec = 0;

    m_connect.DateTimeGetDate(dt, &year, &month, &day);
    m_connect.DateTimeGetTime(dt, &hour, &minute, &sec, &fsec);

    Common::SqlTimestamp ts;
    memset(&ts, 0, sizeof(ts));
    ts.year   = year;
    ts.month  = month;
    ts.day    = day;
    ts.hour   = hour;
    ts.minute = minute;
    ts.second = sec;
    ts.fsec   = static_cast<int>(fsec);

    // Only the TZ and LTZ descriptors carry an offset. Asking a plain TIMESTAMP for one - or
    // asking the DATE and TIME descriptors this class is also instantiated for - is an OCI error.
    if (OCI_DTYPE_TIMESTAMP_TZ == m_oci_handle_type || OCI_DTYPE_TIMESTAMP_LTZ == m_oci_handle_type)
    {
        sb1 tzHour = 0, tzMinute = 0;
        m_connect.DateTimeGetTimeZoneOffset(dt, &tzHour, &tzMinute);
        ts.tzHour   = tzHour;
        ts.tzMinute = tzMinute;
    }

    // The region id and the DST flag are deliberately left unset: capturing them needs
    // OCIDateTimeGetTimeZoneName, which is outside the five procs Stage 2 adds. A TZ value is
    // therefore stored as its numeric offset, which is correct but not the region spelling.
    return Common::SqlValue::StorePod(arena, GetKind(), ts);
}

Common::SqlValueKind IntervalVar::GetKind () const
{
    return (OCI_DTYPE_INTERVAL_DS == m_oci_handle_type)
         ? Common::SqlValueKind::IntervalDs
         : Common::SqlValueKind::IntervalYm;
}

Common::SqlArena::CellOffset IntervalVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    if (!Connect::IsIntervalPartsSupported())
    {
        string str;
        GetString(str, m_null);
        return StoreRendered(arena, Common::SqlValueKind::Unsupported, str);
    }

    const OCIInterval* iv = static_cast<const OCIInterval*>(m_buffer);

    if (OCI_DTYPE_INTERVAL_DS == m_oci_handle_type)
    {
        sb4 dy = 0, hr = 0, mm = 0, ss = 0, fsec = 0;
        m_connect.IntervalGetDaySecond(iv, &dy, &hr, &mm, &ss, &fsec);

        Common::SqlIntervalDs ds;
        memset(&ds, 0, sizeof(ds));
        ds.days = dy; ds.hours = hr; ds.minutes = mm; ds.seconds = ss; ds.fsec = fsec;
        return Common::SqlValue::StorePod(arena, Common::SqlValueKind::IntervalDs, ds);
    }

    sb4 yr = 0, mnth = 0;
    m_connect.IntervalGetYearMonth(iv, &yr, &mnth);

    Common::SqlIntervalYm ym;
    memset(&ym, 0, sizeof(ym));
    ym.years = yr; ym.months = mnth;
    return Common::SqlValue::StorePod(arena, Common::SqlValueKind::IntervalYm, ym);
}

bool NamedTypeVariable::IsNull () const
{
    // The null indicator for an object/VARRAY/nested table is not Variable::m_indicator (that
    // byte-define is bypassed by OCIDefineObject). OCI writes the attribute indicator structure
    // m_ind_struct instead, and a fetch that produced no instance at all is null too. This is the
    // same test GetString() already applies, so IsNull() and GetString() agree on nullness.
    return !m_instance || !m_ind_struct || m_ind_struct[0] == OCI_IND_NULL;
}

Common::SqlArena::CellOffset NamedTypeVariable::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    // The serialised form is kept so the cell is not blank, but the kind stays Unsupported:
    // an object or collection has no ordering the grid could meaningfully sort by.
    string str;
    GetString(str, m_null);
    return StoreRendered(arena, Common::SqlValueKind::Unsupported, str);
}

// DummyVar's indicator is never written, so IsNull() is always true and the grid shows a NULL
// cell today rather than the placeholder GetString() would return. GetValue stays faithful to
// that; GetKind() still reports Unsupported so Stage 3/4 will not sort such a column as a value.
Common::SqlArena::CellOffset DummyVar::GetValue (Common::SqlArena&) const
{
    return Common::SqlArena::NullOffset;
}

// D6: a LOB is still read as text. The read can throw - a locator that went stale, a BFILE that
// cannot be opened - and one bad cell must not cost the whole row, so the failure is contained.
Common::SqlArena::CellOffset LobVar::GetValue (Common::SqlArena& arena) const
{
    if (IsNull())
        return Common::SqlArena::NullOffset;

    string str;
    try
    {
        GetString(str, m_null);
    }
    catch (const Exception&)
    {
        // Not flagged truncated: this placeholder stands in for a value that could not be read at
        // all, which is a different claim from "here are the first m_limit bytes of it".
        static const char failed[] = "<LOB read failed>";
        return Common::SqlValue::StoreText(arena, failed, sizeof(failed) - 1);
    }

    // GetString() reached getLobLength() at least once, and that is what set m_truncated, so this is
    // only meaningful after the call above rather than before it.
    const unsigned flags = IsTruncated() ? Common::evfTruncated : Common::evfNone;
    return Common::SqlValue::StoreText(arena, str.data(), str.size(), flags);
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::HostArray
///////////////////////////////////////////////////////////////////////////////
const string HostArray::m_null;

HostArray::HostArray (ub2 type, sb4 size, sb4 count)
: m_threadId(GetCurrentThreadId())
{
    m_type = type;
    m_elm_buff_size = size;
    m_count = count;
    m_cur_count = 0;
    m_buffer = new char[m_elm_buff_size * m_count];
    memset(m_buffer, 0, m_elm_buff_size * m_count);
    m_indicators = new sb2[m_count];
    memset(m_indicators, 0, sizeof (sb2) * m_count);
    m_sizes = new ub2[m_count];
    memset(m_sizes, 0, sizeof (ub2) * m_count);
    //m_ret_codes = new ub2[m_count];
}

HostArray::~HostArray ()
{
    try { EXCEPTION_FRAME;

        //delete[] m_ret_codes;
        delete[] m_sizes;
        delete[] m_indicators;
        delete[] (char*)m_buffer;

        _ASSERTE(m_threadId == GetCurrentThreadId());
    }
    _DESTRUCTOR_HANDLER_;
}

void HostArray::Assign (int inx, const dvoid* buffer, ub2 size)
{
    ASSERT_EXCEPTION_FRAME;

    if (inx < m_count)
    {
        if (size <= m_elm_buff_size)
	    {
		    memcpy(((char*)m_buffer) + m_elm_buff_size * inx, buffer, size);
            m_sizes[inx] = size;
	    }
	    else
            _RAISE(Exception(0, "OCI8::HostArray::Assign(): Buffer overflow!"));
    }
    else
        _RAISE(Exception(0, "OCI8::HostArray::Assign(): Out of range!"));
}

void HostArray::Assign (int inx, int val)
{
    Assign(inx, &val, sizeof(val));
}

void HostArray::Assign (int inx, long val)
{
    Assign(inx, &val, sizeof(val));
}

const void* HostArray::At (int inx) const
{
    ASSERT_EXCEPTION_FRAME;

    if (inx >= static_cast<int>(m_cur_count))
        _RAISE(Exception(0, "OCI8::HostArray::At(): Out of range!"));

    return ((const char*)m_buffer) + m_elm_buff_size * inx;
}


void HostArray::Define (Statement& sttm, int pos)
{
    OCIDefine* defhp;
    sttm.GetConnect().CHECK(
        OCIDefineByPos(sttm.GetOCIStmt(), &defhp, sttm.GetOCIError(), pos,
                         m_buffer, m_elm_buff_size, m_type, m_indicators,
                         m_sizes, 0, OCI_DEFAULT));
}

void HostArray::Bind (Statement& sttm, int pos)
{
    OCIBind* bndhp;
    sttm.GetConnect().CHECK(
        OCIBindByPos(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), pos,
                       m_buffer, m_elm_buff_size, m_type, m_indicators,
                       m_sizes, 0, m_count, 0, OCI_DEFAULT));
}

void HostArray::Bind (Statement& sttm, const char* name)
{
    OCIBind* bndhp;
    wstring wname = Common::wstr(name);
    sttm.GetConnect().CHECK(
        OCIBindByName(sttm.GetOCIStmt(), &bndhp, sttm.GetOCIError(), (OraText*)wname.c_str(), wname.length()*sizeof(wchar_t),
                       m_buffer, m_elm_buff_size, m_type, m_indicators,
                       m_sizes, 0, m_count, &m_cur_count, OCI_DEFAULT));
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::StringArray
///////////////////////////////////////////////////////////////////////////////

StringArray::StringArray (int size, int count)
: HostArray(SQLT_LVC, sizeof(wchar_t) * size + sizeof(int), count)
{
}

void StringArray::GetString (int inx, std::string& strbuff, const std::string& null) const
{
    if (IsNull(inx))
        strbuff = null;
    else
    {
        // 26.10.2003 workaround for oracle 8.1.6, removed trailing '0' for long columns and trigger text
        int length = *(int*)At(inx);
        //int length2 = length;

        for (; length > 0; length--)
            if ( ((const wchar_t*)((const char*)at(inx) + sizeof(int)))[length-1] != 0 ) 
                break;

        //strbuff = Common::str((wchar_t*)(at(inx) + sizeof(int)), length2);
        //_ASSERT(length == length2);
        strbuff = Common::str((wchar_t*)((const char*)at(inx) + sizeof(int)), length);
    }
}

void StringArray::GetTime (int, struct tm&, struct tm*) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::StringArray::GetTime(): Cannot cast String to \"tm\"!"));
}

int StringArray::ToInt (int, int) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::StringArray::ToInt(): Cannot cast String to Int!"));
}

__int64 StringArray::ToInt64 (int, __int64) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::StringArray::ToInt64(): Cannot cast Date to Int!"));
}

double StringArray::ToDouble (int, double) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::StringArray::ToDouble(): Cannot cast String to Double!"));
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::NumberArray
///////////////////////////////////////////////////////////////////////////////

NumberArray::NumberArray (int count)
: HostArray(SQLT_STR, sizeof(wchar_t) * 41, count)
{
}

void NumberArray::GetString (int inx, std::string& strbuff, const std::string& null) const
{
    if (IsNull(inx))
        strbuff = null;
    else
        strbuff = Common::str((const wchar_t*)At(inx));
}

void NumberArray::GetTime (int, struct tm&, struct tm*) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::NumberArray::GetTime(): Cannot cast Number to \"tm\"!"));
}

int NumberArray::ToInt (int inx, int null) const
{
    if (IsNull(inx)) return null;

    return _wtoi((const wchar_t*)At(inx));
}

__int64 NumberArray::ToInt64 (int inx, __int64 null) const
{
    if (IsNull(inx)) return null;

    return _wtoi64((const wchar_t*)At(inx));
}

double NumberArray::ToDouble (int inx, double null) const
{
    if (IsNull(inx)) return null;

    return _wtof((const wchar_t*)At(inx));
}

///////////////////////////////////////////////////////////////////////////////
// OCI8::DateArray
///////////////////////////////////////////////////////////////////////////////
const std::string DateArray::m_defaultDateFormat = "%Y.%m.%d %H:%M:%S";

DateArray::DateArray (int count, const std::string& defaultDateFormat)
: HostArray(SQLT_DAT, 7, count),
  m_dateFormat(defaultDateFormat),
  m_dataStrLength(0)
{
    time_t t1;
    time(&t1);
    tm *t2 = gmtime(&t1);
    char buff[80];
    strftime(buff, sizeof(buff), m_dateFormat.c_str(), t2);
    m_dataStrLength = strlen(buff);
}

void DateArray::GetString (int inx, std::string& strbuff, const std::string& null) const
{
    if (IsNull(inx))
        strbuff = null;
    else
    {
        const char* data = (const char*)At(inx);
        tm time;
        time.tm_isdst = time.tm_wday = time.tm_yday = -1;
        time.tm_year = (data[0] - 100) * 100 + (data[1] - 100) - 1900;
        time.tm_mon  = data[2] - 1;
        time.tm_mday = data[3];
        time.tm_hour = data[4] - 1;
        time.tm_min  = data[5] - 1;
        time.tm_sec  = data[6] - 1;
        char buff[80];
        strftime(buff, sizeof buff, m_dateFormat.c_str(), &time);
        strbuff = buff;
    }
}

void DateArray::GetTime (int inx, struct tm& time, struct tm* null /*= 0*/) const
{
    if (!IsNull(inx))
    {
        const char* data = (const char*)At(inx);
        time.tm_isdst = time.tm_wday = time.tm_yday = -1;
        time.tm_year = (data[0] - 100) * 100 + (data[1] - 100) - 1900;
        time.tm_mon  = data[2] - 1;
        time.tm_mday = data[3];
        time.tm_hour = data[4] - 1;
        time.tm_min  = data[5] - 1;
        time.tm_sec  = data[6] - 1;
    }
    else
    {
        if (null)
            time = *null;
        else
            memset(&time, 0, sizeof(time));
    }
}

int DateArray::ToInt (int, int) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::DateArray::ToInt(): Cannot cast Date to Int!"));
}

__int64 DateArray::ToInt64 (int, __int64) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::DateArray::ToInt64(): Cannot cast Date to Int!"));
}

double DateArray::ToDouble (int, double) const
{
    ASSERT_EXCEPTION_FRAME;
    _RAISE(Exception(0, "OCI8::DateArray::ToDouble(): Cannot cast Date to Double!"));
}

///////////////////////////////////////////////////////////////////////////////
};