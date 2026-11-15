// Capa de compatibilidad macOS/Linux para el código de SQLTools (reemplaza el stdafx.h de MFC).
// No contiene nada de MFC ni de la API de Windows.
#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "win_compat.h"

using std::string;
using std::istringstream;
using std::ostringstream;
using std::ends;
using std::endl;

namespace OCI8
{
    class Connect;
    class Statement;
    class AutoCursor;
    class BuffCursor;
    class StringVar;
    class NumberVar;
    class DateVar;
    class HostArray;
    class StringArray;
    class NumberArray;
    class DateArray;
    class Exception;
    class UserCancel;
};

typedef OCI8::Connect       OciConnect;
typedef OCI8::Statement     OciStatement;
typedef OCI8::AutoCursor    OciAutoCursor;
typedef OCI8::BuffCursor    OciCursor;
typedef OCI8::StringVar     OciStringVar;
typedef OCI8::NumberVar     OciNumberVar;
typedef OCI8::DateVar       OciDateVar;
typedef OCI8::Exception     OciException;
typedef OCI8::UserCancel    OciUserCancel;
typedef OCI8::HostArray     OciHostArray;
typedef OCI8::StringArray   OciStringArray;
typedef OCI8::NumberArray   OciNumberArray;
typedef OCI8::DateArray     OciDateArray;

void default_oci_handler (const OciException&, const char*, int);

#define DEFAULT_OCI_HANDLER(x) default_oci_handler(x, __FILE__, __LINE__);

#define _DESTRUCTOR_HANDLER_ catch (...) {}
#define _COMMON_DEFAULT_HANDLER_ catch (const std::exception& x) { Common::DefaultHandler(x, __FILE__, __LINE__); }
#define _DEFAULT_HANDLER_ \
    catch (const OciException& x) { DEFAULT_OCI_HANDLER(x); } \
    _COMMON_DEFAULT_HANDLER_
#define _OE_DEFAULT_HANDLER_ _COMMON_DEFAULT_HANDLER_
