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

#include "SqlNumber.h"
#include <cmath>
#include <cstring>

namespace Common
{

// expByte = exp + 192 when positive, 63 - exp when negative: the high bit is the sign.
static const int ExpBiasPositive = 192;
static const int ExpBiasNegative = 63;

SqlNumber::SqlNumber ()
{
    SetZero();
}

void SqlNumber::SetZero ()
{
    // The whole struct, not just the fields: interning hashes payload bytes, so padding
    // and the unused bits of the `neg` bitfield byte must be deterministic too.
    memset(this, 0, sizeof(*this));
}

bool SqlNumber::Decode (const unsigned char* num, int size, SqlNumber& out)
{
    out.SetZero();
    if (0 == num || size <= 0 || MaxBytes < size)
        return false;

    const unsigned char expByte = num[0];
    if (1 == size)
        return ZeroByte == expByte;         // the single legal one-byte encoding is zero

    const bool isNeg = expByte < ZeroByte;
    const int e = isNeg ? (ExpBiasNegative - static_cast<int>(expByte))
                        : (static_cast<int>(expByte) - ExpBiasPositive);

    int n = 0;
    for (int i = 1; i < size; ++i)
    {
        const int b = num[i];
        if (isNeg && Terminator == b)
            break;                          // end of a negative; anything after it is not ours

        const int d = isNeg ? (101 - b) : (b - 1);
        if (d < 0 || 99 < d || MaxDigits <= n)
            return false;                   // malformed, or more precision than NUMBER can hold

        out.digits[n++] = static_cast<unsigned char>(d);
    }

    while (0 < n && 0 == out.digits[n - 1])
        --n;                                // trailing zero pairs are not significant

    if (0 == n)
        return true;                        // a negative zero normalises to zero

    out.len = static_cast<unsigned char>(n);
    out.exp = static_cast<signed char>(e);
    out.neg = isNeg ? 1 : 0;
    return true;
}

SqlNumber SqlNumber::FromOciBytes (const unsigned char* num, int size)
{
    SqlNumber out;
    Decode(num, size, out);
    return out;
}

int SqlNumber::ToOciBytes (unsigned char* num, int size) const
{
    if (0 == num || size < 1)
        return 0;

    if (0 == len)
    {
        num[0] = ZeroByte;
        return 1;
    }

    const bool isNeg = 0 != neg;
    const bool terminator = isNeg && MaxDigits > len;
    const int need = 1 + len + (terminator ? 1 : 0);
    if (size < need)
        return 0;

    num[0] = static_cast<unsigned char>(isNeg ? (ExpBiasNegative - exp) : (exp + ExpBiasPositive));
    for (int i = 0; i < len; ++i)
        num[1 + i] = static_cast<unsigned char>(isNeg ? (101 - digits[i]) : (digits[i] + 1));

    if (terminator)
        num[1 + len] = Terminator;

    return need;
}

int SqlNumber::Compare (const SqlNumber& other) const
{
    const bool aZero = (0 == len), bZero = (0 == other.len);
    if (aZero && bZero)
        return 0;
    if (aZero)
        return other.neg ? 1 : -1;          // zero sits above every negative, below every positive
    if (bZero)
        return neg ? -1 : 1;
    if ((0 != neg) != (0 != other.neg))
        return neg ? -1 : 1;

    int c = 0;
    if (exp != other.exp)
    {
        c = (exp < other.exp) ? -1 : 1;
    }
    else
    {
        const int n = (len < other.len) ? len : other.len;
        for (int i = 0; 0 == c && i < n; ++i)
            if (digits[i] != other.digits[i])
                c = (digits[i] < other.digits[i]) ? -1 : 1;

        if (0 == c && len != other.len)
            c = (len < other.len) ? -1 : 1; // no trailing zeros are stored, so longer means bigger
    }

    return neg ? -c : c;
}

bool SqlNumber::ToDouble (double& out) const
{
    if (0 == len)
    {
        out = 0.0;
        return true;
    }

    double t = 0.0;                         // Horner over base-100 digits: t == 0.d1d2...
    for (int i = len - 1; i >= 0; --i)
        t = (t + digits[i]) / 100.0;

    const double v = t * std::pow(100.0, static_cast<double>(exp));
    if (!std::isfinite(v))
        return false;

    out = neg ? -v : v;
    return true;
}

std::string SqlNumber::ToCanonical () const
{
    if (0 == len)
        return "0";

    std::string d;                          // the decimal digits, two per base-100 digit
    d.reserve(2 * static_cast<size_t>(len));
    for (int i = 0; i < len; ++i)
    {
        d += static_cast<char>('0' + digits[i] / 10);
        d += static_cast<char>('0' + digits[i] % 10);
    }

    // value == 0.d * 10^(2*exp), so 2*exp is the decimal point position inside d.
    const int shift = 2 * static_cast<int>(exp);

    std::string ip, fp;
    if (shift <= 0)
    {
        ip = "0";
        fp.append(static_cast<size_t>(-shift), '0');
        fp += d;
    }
    else if (shift >= static_cast<int>(d.size()))
    {
        ip = d;
        ip.append(static_cast<size_t>(shift) - d.size(), '0');
    }
    else
    {
        ip = d.substr(0, static_cast<size_t>(shift));
        fp = d.substr(static_cast<size_t>(shift));
    }

    size_t first = ip.find_first_not_of('0');
    if (std::string::npos == first)
        ip = "0";
    else if (0 < first)
        ip.erase(0, first);

    if (!fp.empty())
    {
        size_t last = fp.find_last_not_of('0');
        if (std::string::npos == last)
            fp.clear();
        else
            fp.erase(last + 1);
    }

    std::string out;
    out.reserve(ip.size() + fp.size() + 3);
    if (0 != neg)
        out += '-';
    out += ip;
    if (!fp.empty())
    {
        out += '.';
        out += fp;
    }
    return out;
}

}

