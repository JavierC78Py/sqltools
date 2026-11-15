/*
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2004 Aleksey Kochetov

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

#pragma once
#ifndef __SqlNumber_h__
#define __SqlNumber_h__

#include <string>

namespace Common
{

// Exact decimal, our copy of Oracle NUMBER: value = 0.d1d2...dk (base 100) * 100^exp.
// On the wire the exponent byte's high bit is the sign and negatives carry a 102 terminator.
struct SqlNumber
{
    enum
    {
        MaxDigits  = 20,    // 20 base-100 digits; Oracle caps precision at 38 decimal digits
        MaxBytes   = 21,    // exponent byte + 20 mantissa bytes (terminator dropped when full)
        ZeroByte   = 0x80,  // the whole encoding of zero
        Terminator = 102    // appended to a negative unless the mantissa is already full
    };

    unsigned char len;                  // significant base-100 digits, 0 == zero
    signed char   exp;                  // base-100 exponent, -64..63
    unsigned char neg : 1;
    unsigned char digits[MaxDigits];    // 0..99, most significant first, no trailing zeros

    SqlNumber ();
    void SetZero ();
    bool IsZero () const { return 0 == len; }

    // Decode reports malformed bytes instead of quietly producing a wrong number;
    // FromOciBytes is the convenience form and decodes them as zero.
    static bool Decode (const unsigned char* num, int size, SqlNumber& out);
    static SqlNumber FromOciBytes (const unsigned char* num, int size);

    int ToOciBytes (unsigned char* num, int size) const;    // bytes written, 0 when it does not fit

    int  Compare (const SqlNumber& other) const;            // <0, 0, >0
    bool ToDouble (double& out) const;                      // autofit and quick math only
    std::string ToCanonical () const;                       // plain decimal, '.', no grouping
};

}

#endif // __SqlNumber_h__
