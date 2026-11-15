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
// 07.02.2005 (Ken Clubok) R1105003: Bind variables
// 13.03.2005 (ak) R1105003: Bind variables

#pragma once
#include <oci.h>
#include "OCI8/Connect.h"
#include "Common/SqlArena.h"
#include "Common/SqlValue.h"

namespace OCI8
{
    class Statement;
    class AutoCursor;

///////////////////////////////////////////////////////////////////////////////
// OCI8::Variable
///////////////////////////////////////////////////////////////////////////////

/** @brief Data storage for select results and bind variables.
 */
class Variable
{
    friend class AutoCursor;
protected:
    Variable (ub2 type, sb4 size);
    Variable (dvoid* buffer, ub2 type, sb4 size);

    sb4    GetSize () const         { return m_size; }
    sb4    GetBufferSize () const   { return m_buffer_size; }
    ub2    GetReturnedSize () const { return m_ret_size; }
    // The per-column return code OCI writes through rcodep on every fetch. Distinct from both the
    // indicator and the fetch status: the indicator answers NULL-ness, the status answers for the
    // whole row, and this one answers for this column alone - which is the granularity a grid cell
    // needs, since one cut value in a 40-column row must tint one cell and not forty.
    ub2    GetReturnCode () const   { return m_rcode; }

    virtual void BeforeFetch () {}
    virtual void AfterFetch  () {}

public:
    virtual ~Variable ();

    virtual void GetString (string&, const string& null = m_null) const /*= 0*/;

    // Stage 2 (plan §4.1). GetValue writes the fetched value into the grid arena in its own
    // kind and returns the payload offset; SqlArena::NullOffset means a NULL cell, which costs
    // no bytes at all. The base implementation renders display text into a Text payload, so a
    // type that has not been overridden yet still shows exactly what it shows today - that is
    // what makes the per-column swap in Stage 3/4 safe to do incrementally.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;

    // The kind GetValue will produce, known without fetching. Stage 3/4 uses it to decide
    // alignment and sort band per column before the first row arrives.
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Text; }

    //bool IsGood () const { return (m_indicator == -1 || m_indicator == 0) ? true : false; }
    virtual bool IsNull () const { return m_indicator == -1 ? true : false; }

    // False in the base, and that is the honest answer rather than a placeholder. The test that used
    // to sit here commented out - `m_indicator == -2 || m_indicator > 0` - does not mean truncated:
    // oro.h:308-311 enumerates the whole OCI indicator vocabulary and -2 is OCI_IND_BADNULL, an
    // object-runtime state, while a positive indicator carries a length for piecewise and dynamic
    // fetches. oci.h defines no truncation indicator at all, so there is no constant to compare
    // against here and nothing in the headers proves the claim. Subclasses that can prove a cut
    // override this - LobVar does, from its own clamp.
    //
    // A VARCHAR2 wider than AutoCursor::m_StringLimit really is cut by OCI at the define buffer,
    // since ACursor.cpp:179 sizes StringVar to min(column size, m_StringLimit). That case used to be
    // unflagged here; it is now settled by StringVar::IsTruncated against a live server, and the
    // measurement it was chosen from is recorded there. The fetch status and the OCI error code were
    // both ruled out: the status answers for the whole row, so one cut column in a forty-column row
    // would tint forty cells, and the error handle is sticky across calls.
    virtual bool IsTruncated () const { return false; }

    virtual void Define (Statement&, int pos);
    virtual void Bind   (Statement&, int pos);
    virtual void Bind   (Statement&, const char* name);

protected:
    ub2    m_type;
    sb2    m_indicator;
    sb4    m_size, m_buffer_size;
    ub2    m_ret_size;
    ub2    m_rcode;

    dvoid* m_buffer;
    bool   m_owner;
    DWORD m_threadId;

public:
    static const string m_null;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::RefCursorVariable
//      limited functionality, can be bound
///////////////////////////////////////////////////////////////////////////////

class RefCursorVariable : public Variable
{
public:
    RefCursorVariable (Connect&, int prefetch = 100);
    ~RefCursorVariable ();

    virtual void GetString (string&, const string& null = m_null) const;
    virtual void Bind (Statement&, int pos);
    virtual void Bind (Statement&, const char* name);

    // Stage 2: a nested cursor has no scalar value of its own.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Unsupported; }

    bool IsStateInitialized ();
    bool IsStateExecuted ();
    bool IsStateEndOfFetch ();

protected:
    friend AutoCursor;

    Connect& m_connect;
    OCIStmt* m_sttmp;
    int m_prefetch;

    Connect&    GetConnect () const     { return m_connect; }
    OCIStmt*    GetOCIStmt () const     { return m_sttmp; }
    OCIError*   GetOCIError () const    { return m_connect.GetOCIError();  }

    OCIEnv*    GetOCIEnv ()     { return m_connect.GetOCIEnv(); }
    OCISvcCtx* GetOCISvcCtx ()  { return m_connect.GetOCISvcCtx(); }

    void CHECK_ALLOC (sword status) { m_connect.CHECK_ALLOC(status); }
    void CHECK (sword status)       { m_connect.CHECK(status); }
    void CHECK_INTERRUPT ()         { m_connect.CHECK_INTERRUPT(); }
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::NativeOciVariable
///////////////////////////////////////////////////////////////////////////////

class NativeOciVariable : public Variable
{
protected:
    NativeOciVariable (Connect&, ub2 sqlt_type, ub2 oci_handle_type);
    ~NativeOciVariable ();

    virtual void Define (Statement&, int pos);

    Connect& m_connect;
    ub2 m_oci_handle_type;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::StringVar
///////////////////////////////////////////////////////////////////////////////

class StringVar : public Variable
{
public:
    StringVar (int size);
    StringVar (const string&);
    StringVar (const char*, int = -1);

    void Assign (const char*, int len = -1);
    void Assign (const string&);

    virtual void GetString (string&, const string& null = m_null) const;

    // The VARCHAR2 define-buffer cut, settled by measurement rather than by the headers: see the
    // implementation for the table that decided it. StringVar is the only fetch-side string class
    // (ACursor.cpp:174-184 builds one for SQLT_CHR, SQLT_VCS, SQLT_AFC and SQLT_LVC alike; CharVar
    // and StringZVar are bind-side scaffolding and are constructed nowhere), so this one override
    // covers every string column the grid fetches.
    virtual bool IsTruncated () const;

    sb4 GetStringBufSize () const { return GetSize() - static_cast<sb4>(sizeof(int)); }

protected:
    //using Variable::Assign;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::CharVar
//    Fixed-length character variant (uses SQLT_AFC). Stores UTF-16 wide string
//    in a plain wchar_t buffer (no length prefix). Assign always pads the
//    buffer to its full capacity with spaces, emulating Oracle CHAR behaviour.
///////////////////////////////////////////////////////////////////////////////

class CharVar : public Variable
{
public:
    CharVar (int size);
    CharVar (const string&);
    CharVar (const char*, int = -1);

    void Assign (const char*, int len = -1);
    void Assign (const string&);

    virtual void GetString (string&, const string& null = m_null) const;

    sb4 GetStringBufSize () const { return GetSize() / static_cast<sb4>(sizeof(wchar_t)); }
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::StringZVar
//    Null-terminated string variant (uses SQLT_STR). Stores UTF-16 wide string
//    buffer terminated by NUL wchar and used for OCI define/bind as SQLT_STR.
///////////////////////////////////////////////////////////////////////////////

class StringZVar : public Variable
{
public:
    StringZVar(int size);
    StringZVar(const string&);
    StringZVar(const char*, int = -1);

    void Assign(const char*, int len = -1);
    void Assign(const string&);

    virtual void GetString(string&, const string& null = m_null) const;

    sb4 GetStringBufSize() const { return GetSize() / static_cast<sb4>(sizeof(wchar_t)) - 1; }
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::LongStringVar
//    Piecewise LONG fetch for unbounded data.
//    Uses OCIDefineDynamic; accumulates pieces into an internal std::string
//    and converts UTF-16 -> UTF-8 in GetString().
///////////////////////////////////////////////////////////////////////////////

class LongStringVar : public Variable
{
public:
    explicit LongStringVar (int limit);

    virtual void GetString (string&, const string& null = m_null) const;
    virtual void Define (Statement&, int pos);

    virtual bool IsNull () const;

protected:
    virtual void BeforeFetch ();
    virtual void AfterFetch  ();

private:
    static sb4 CDECL cbDefine (dvoid* octxp, OCIDefine* defnp, ub4 iter,
                               dvoid** bufpp, ub4** alenpp, ub1* piecep,
                               dvoid** indpp, ub2** rcodep);

    int         m_limit;
    std::string m_data;                 // accumulated raw bytes (UTF-16)
    bool        m_is_pending;           // last piece waiting to be flushed
    ub4         m_piece_len;            // bytes OCI filled in m_piece_buf
    ub2         m_rcode;
    sb2         m_dyn_indicator;        // per-piece OCI indicator
    enum { PIECE_BUF_SIZE = 65536 };
    char        m_piece_buf[PIECE_BUF_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::NumStrVar
//    Legacy numeric fetch: defines SQLT_LVC into a 41-byte character buffer and
//    lets OCI render the NUMBER to text using the session NLS settings.
//
//    Since D1 nothing constructs this class any more - AutoCursor::Define() now
//    always uses NumberVar (SQLT_VNU) for numeric select-list columns, and the
//    bind path uses NumberVar/BinaryFloatVar/BinaryDoubleVar directly. It is kept
//    because it is part of the OCI8 surface and documents the pre-D1 behaviour.
//
//    It deliberately has no GetValue() override. Plan §4.2 suggested one that
//    parsed the rendered text back into a SqlNumber via a new SqlNumber::FromText,
//    but the buffer only ever holds NLS-formatted *characters* (see
//    StringVar::GetString), so such a parser would have to second-guess
//    NLS_NUMERIC_CHARACTERS, grouping and currency symbols to recover a value the
//    server already threw away - and with D1 landed there is no reachable caller
//    to justify it. Inheriting Variable::GetValue (Text) is both honest and safe.
///////////////////////////////////////////////////////////////////////////////

class NumStrVar : public StringVar
{
public:
    NumStrVar () : StringVar(41) {}
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::NumberVar
///////////////////////////////////////////////////////////////////////////////

class NumberVar : public Variable
{
public:
    NumberVar (Connect&);
    NumberVar (Connect&, const string& format);
    NumberVar (Connect&, int value);
    NumberVar (Connect&, double value);

	void Assign (int);
	void Assign (double);

    virtual void GetString (string&, const string& null = m_null) const;

    virtual int    ToInt (int = 0) const;
    virtual double ToDouble (double = 0) const;

    // Stage 2, D1. This class already binds SQLT_VNU into an OCINumber and already renders via
    // OCINumberToText, which is exactly what the plan asked a new "NumberValueVar" to provide,
    // so the NUMBER column gets its exact-decimal payload from an override here instead of from
    // a second class that would duplicate both the buffer and the format handling.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Number; }

protected:
    Connect& m_connect;
    std::wstring m_numberFormat;
    OCINumber m_value;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::BinaryFloatVar
//    Native IEEE-754 single-precision float (uses SQLT_BFLOAT, 4 bytes).
//    Maps directly to Oracle BINARY_FLOAT. No OCI number conversion needed.
///////////////////////////////////////////////////////////////////////////////

class BinaryFloatVar : public Variable
{
public:
    BinaryFloatVar ();
    explicit BinaryFloatVar (float value);
    explicit BinaryFloatVar (double value);

    void Assign (float);
    void Assign (double);
    void Assign (const string&);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: every value is stored bitwise as BinaryFloat, NaN and +-Inf included, so GetKind()
    // is the kind of every cell in the column rather than an upper bound on it.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::BinaryFloat; }

private:
    float m_value;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::BinaryDoubleVar
//    Native IEEE-754 double-precision float (uses SQLT_BDOUBLE, 8 bytes).
//    Maps directly to Oracle BINARY_DOUBLE. No OCI number conversion needed.
///////////////////////////////////////////////////////////////////////////////

class BinaryDoubleVar : public Variable
{
public:
    BinaryDoubleVar ();
    explicit BinaryDoubleVar (double value);

    void Assign (double);
    void Assign (const string&);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: every value is stored bitwise as BinaryDouble, NaN and +-Inf included, so GetKind()
    // is the kind of every cell in the column rather than an upper bound on it.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::BinaryDouble; }

private:
    double m_value;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::DateVar
// Oracle 7.3 Server and OCI 8.0.X supports only OCIDate 
///////////////////////////////////////////////////////////////////////////////

class DateVar : public Variable
{
public:
    DateVar (Connect&);
    DateVar (Connect&, const string& dateFormat);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: OCIDate is a plain struct and OCIDateGetDate/OCIDateGetTime are macros over its
    // fields, so GetValue makes no OCI call and needs no availability gate. It still needs a
    // Connect to be *constructed* (and m_value is protected), which is why the offline checks
    // reach it through a test subclass rather than directly.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Date; }

protected:
    Connect& m_connect;
    std::wstring m_dateFormat;
    OCIDate m_value;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::TimestampVar
///////////////////////////////////////////////////////////////////////////////

class TimestampVar : public NativeOciVariable
{
public:
    enum ESubtype {
        DATE = SQLT_DATE,
        TIME = SQLT_TIME,
        TIMESTAMP = SQLT_TIMESTAMP,
        TIMESTAMP_TZ = SQLT_TIMESTAMP_TZ,
        TIMESTAMP_LTZ = SQLT_TIMESTAMP_LTZ,
    };

    TimestampVar (Connect&, ESubtype);
    TimestampVar (Connect&, ESubtype, const string& dateFormat);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: the kind follows the descriptor already held by NativeOciVariable, so no new
    // state is needed. GetKind() is out of line because it switches on m_oci_handle_type.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const;

protected:
    std::wstring m_dateFormat;
    static ub2 sqlt_dtype_map (ub2);
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::IntervalVar
///////////////////////////////////////////////////////////////////////////////

class IntervalVar : public NativeOciVariable
{
public:
    enum ESubtype {
        INTERVAL_YM = SQLT_INTERVAL_YM,
        INTERVAL_DS = SQLT_INTERVAL_DS,
    };

    IntervalVar (Connect&, ESubtype);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: YEAR TO MONTH and DAY TO SECOND are different kinds with different payloads,
    // so the kind is derived from the descriptor rather than stored twice.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const;

protected:
    static ub2 sqlt_dtype_map (ub2);
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::NamedTypeVariable
// Oracle Named Types (objects, VARRAYs, nested tables)
///////////////////////////////////////////////////////////////////////////////

class NamedTypeVariable : public Variable
{
public:
    NamedTypeVariable (Connect&, const char* schema, const char* type_name, int str_limit);
    ~NamedTypeVariable ();

    virtual void GetString (string&, const string& null = m_null) const;
    virtual void Define (Statement&, int pos);

    // Stage 2: an object/VARRAY/nested table has no scalar payload. The serialised text is kept
    // under Unsupported so the grid still displays it, but it is not sortable as a value.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Unsupported; }
    // Base Variable::IsNull reads m_indicator, which OCIDefineObject never writes for a named
    // type - it writes the per-attribute indicator structure m_ind_struct instead. So the null
    // question must be answered from m_instance/m_ind_struct, the same rule GetString() applies.
    virtual bool IsNull () const;

protected:
    virtual void BeforeFetch ();

private:
    Connect&    m_connect;
    OCIType*    m_tdo;          // pinned type descriptor object
    void*       m_instance;     // current row's object/collection pointer
    OCIInd*     m_ind_struct;   // attribute indicator structure (for objects)
    int         m_str_limit;

    // recursive serializers
    static void serialize_object    (Connect&, void* inst, OCIInd* inds, OCIType* tdo, int limit, string& out, int depth);
    static void serialize_collection(Connect&, void* coll, OCIInd  ind,  OCIType* tdo, int limit, string& out, int depth);
    static void serialize_scalar    (Connect&, void* val,  OCIInd  ind,  OCITypeCode tc, string& out);
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::DummyVar
///////////////////////////////////////////////////////////////////////////////

class DummyVar : public Variable
{
public:
    DummyVar (bool skipped = false);

    virtual void GetString (string&, const string& null = m_null) const;

    // Stage 2: the placeholder text is preserved so the cell is not blank, but the kind stays
    // Unsupported so nothing tries to sort or re-literal it as a value.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Unsupported; }

public:
    bool m_skipped;
    static const string m_unsupportedStr;
    static const string m_skippedStr;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::LobVar
///////////////////////////////////////////////////////////////////////////////

enum ELobSubtype { elsCLob = SQLT_CLOB, elsBLob = SQLT_BLOB, elsBFile = SQLT_FILE };
enum ECharForm   { ecfImplicit = SQLCS_IMPLICIT, ecfNchar = SQLCS_NCHAR };

class LobVar : public NativeOciVariable
{
protected:
    int m_limit;
    ECharForm m_charForm;
    //bool m_temporary;

    // Revived from the commented-out member that used to sit here. getLobLength() sets it whenever
    // it clamps the length OCILobGetLength reported, which is precisely the case where the read that
    // follows comes up short. Mutable because the whole fetch surface is const - GetValue(),
    // GetString(), getLobLength() - while this records a fact about the row currently in the buffer
    // rather than anything about the variable's identity.
    mutable bool m_truncated;

    LobVar (Connect&, ELobSubtype, int limit, ECharForm = ecfImplicit);

    virtual void GetString (string&, const string& null = m_null) const;

public:
    // Stage 2, D6: a LOB is still read as text, and it lands in the arena under the Text kind
    // so the grid treats it like any other string. Overridden rather than left to the base
    // because reading a LOB can throw, and the fetch loop must not lose the whole row if it does.
    virtual Common::SqlArena::CellOffset GetValue (Common::SqlArena& arena) const;
    virtual Common::SqlValueKind GetKind () const { return Common::SqlValueKind::Text; }

    // True when the last read of this LOB stopped at m_limit instead of at the end of the value.
    // Only meaningful once GetString()/GetValue() has run for the current row, since those are what
    // drive getLobLength(). Overrides the base because a LOB arrives through an explicit
    // OCILobRead rather than a define buffer, so the clamp in getLobLength() is the only evidence
    // that anything was cut - and unlike the VARCHAR2 case, it is provable from this code alone.
    virtual bool IsTruncated () const { return m_truncated; }

protected:
    int  getLobLength () const;
    void getString (char* strbuff, int buffsize, ub2 csid) const;
    //void makeTemporary (ub1 type);
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::CLobVar
///////////////////////////////////////////////////////////////////////////////

class CLobVar : public LobVar
{
public:
    CLobVar (Connect& connect, int limit, ECharForm charForm = ecfImplicit) 
        : LobVar(connect, elsCLob, limit, charForm) {}
    //void MakeTemporary ();

    using LobVar::GetString;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::BLobVar
///////////////////////////////////////////////////////////////////////////////

class BLobVar : public LobVar
{
protected:
    int m_hexFormatLength;
    BLobVar (Connect&, ELobSubtype, int limit, int hexFormatLength);

public:
    BLobVar (Connect&, int limit, int hexFormatLength);

    virtual void GetString (string&, const string& null = m_null) const;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::BFileVar
///////////////////////////////////////////////////////////////////////////////

class BFileVar : public BLobVar
{
public:
    BFileVar (Connect&, int limit, int hexFormatLength);

    virtual void GetString (string&, const string& null = m_null) const;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::HostArray
///////////////////////////////////////////////////////////////////////////////

class HostArray
{
    friend class BuffCursor;
public:
    HostArray (ub2 type, sb4 size, sb4 count);
    virtual ~HostArray ();

    virtual void GetString (int, string&, const string& null = m_null) const = 0;
    virtual void GetTime   (int, struct tm&, struct tm* null = 0) const = 0;

    virtual int    ToInt (int, int = 0) const = 0;
    virtual __int64 ToInt64 (int, __int64 = 0) const = 0;
    virtual double ToDouble (int, double = 0) const = 0;

    bool IsGood (int inx) const;
    bool IsNull (int inx) const;
    bool IsTruncated (int inx) const;

	void Assign (int inx, const dvoid* buffer, ub2 size);
	void Assign (int inx, int);
	void Assign (int inx, long);

    void Define (Statement&, int pos);
    void Bind (Statement&, int pos);
    void Bind (Statement&, const char*);

protected:
    const void* At (int inx) const;
    const void* at (int inx) const      { return ((const char*)m_buffer) + m_elm_buff_size * inx; }

    friend Statement;
    ub2    m_type;
    sb2*   m_indicators;
    ub2*   m_sizes;
    sb4    m_elm_buff_size;
    sb4    m_count;
    dvoid* m_buffer;
    ub4    m_cur_count;
    DWORD  m_threadId;

public:
    static const string m_null;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::StringArray
///////////////////////////////////////////////////////////////////////////////

class StringArray : public HostArray
{
public:
    StringArray (int size, int count);

    virtual void GetString (int, string&, const string& null = m_null) const;
    virtual void GetTime   (int, struct tm&, struct tm* null = 0) const;

    virtual int    ToInt (int, int = 0) const;
    virtual __int64 ToInt64 (int, __int64 = 0) const;
    virtual double ToDouble (int, double = 0) const;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::NumberArray
///////////////////////////////////////////////////////////////////////////////

class NumberArray : public HostArray
{
public:
    NumberArray (int count);

    virtual void GetString (int, string&, const string& null = m_null) const;
    virtual void GetTime   (int, struct tm&, struct tm* null = 0) const;

    virtual int    ToInt (int, int = 0) const;
    virtual __int64 ToInt64 (int, __int64 = 0) const;
    virtual double ToDouble (int, double = 0) const;
};

///////////////////////////////////////////////////////////////////////////////
// OCI8::DateArray
///////////////////////////////////////////////////////////////////////////////

class DateArray : public HostArray
{
public:
    DateArray (int count, const string& = m_defaultDateFormat);

    virtual void GetString (int, string&, const string& null = m_null) const;
    virtual void GetTime   (int, struct tm&, struct tm* null = 0) const;

    virtual int    ToInt (int, int = 0) const;
    virtual __int64 ToInt64 (int, __int64 = 0) const;
    virtual double ToDouble (int, double = 0) const;

protected:
    int m_dataStrLength;
    string m_dateFormat;
public:
    static const string m_defaultDateFormat; // "%Y.%m.%d %H:%M:%S"
};

///////////////////////////////////////////////////////////////////////////////

inline
bool HostArray::IsGood (int inx) const
{
    return (m_indicators[inx] == -1 || m_indicators[inx] == 0) ? true : false;
}

inline
bool HostArray::IsNull (int inx) const
{
    return m_indicators[inx] == -1 ? true : false;
}

inline
bool HostArray::IsTruncated (int inx) const
{
    return m_indicators[inx] == -2 || m_indicators[inx] > 0 ? true : false;
}

///////////////////////////////////////////////////////////////////////////////
};