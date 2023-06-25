// ****************************************************************************
//  decimal128.cc                                                 DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Implementation of decimal floating point using Intel's library
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2022 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of DB48X.
//
//   DB48X is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   DB48X is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "decimal128.h"

#include "bignum.h"
#include "parser.h"
#include "renderer.h"
#include "runtime.h"
#include "settings.h"
#include "utf8.h"

#include <algorithm>
#include <bid_conf.h>
#include <bid_functions.h>
#include <cstdio>
#include <cstdlib>

using std::min;
using std::max;

RECORDER(decimal128, 32, "Decimal128 data type");


decimal128::decimal128(bignum_p num, id type)
// ----------------------------------------------------------------------------
//   Create a decimal128 from a bignum value
// ----------------------------------------------------------------------------
    : object(type)
{
    bid128 result;
    bid128 mul;
    unsigned z = 0;
    bid128_from_uint32(&result.value, &z);
    z = 256;
    bid128_from_uint32(&mul.value, &z);

    size_t size = 0;
    byte_p n = num->value(&size);
    for (uint i = 0; i < size; i++)
    {
        unsigned digits = n[size - i - 1];
        bid128 step;
        bid128_mul(&step.value, &result.value, &mul.value);
        bid128 add;
        bid128_from_uint32(&add.value, &digits);
        bid128_add(&result.value, &step.value, &add.value);
    }
    if (num->type() == ID_neg_bignum)
        bid128_negate(&result.value, &result.value);
    byte *p = payload();
    memcpy(p, &result, sizeof(result));
}


OBJECT_HANDLER_BODY(decimal128)
// ----------------------------------------------------------------------------
//    Handle commands for decimal128s
// ----------------------------------------------------------------------------
{
    record(decimal128, "Command %+s on %p", name(op), obj);
    switch(op)
    {
    case EXEC:
    case EVAL:
        // Decimal128 values evaluate as self
        return rt.push(obj) ? OK : ERROR;
    case SIZE:
        return ptrdiff(payload, obj) + sizeof(bid128);
    case PARSE:
        return object_parser(OBJECT_PARSER_ARG(), rt);
    case RENDER:
        return obj->object_renderer(OBJECT_RENDERER_ARG(), rt);
    case HELP:
        return (intptr_t) "decimal";

    default:
        // Check if anyone else knows how to deal with it
        return DELEGATE(object);
    }

}


OBJECT_PARSER_BODY(decimal128)
// ----------------------------------------------------------------------------
//    Try to parse this as an decimal128
// ----------------------------------------------------------------------------
{
    record(decimal128, "Parsing [%s]", (utf8) p.source);

    utf8 source = p.source;
    utf8 s = source;
    utf8 last = source + p.length;

    // Skip leading sign
    if (*s == '+' || *s == '-')
    {
        // In an equation, `1 + 3` should interpret `+` as an infix
        if (p.precedence < 0)
            return SKIP;
        s++;
    }

    // Skip digits
    utf8 digits = s;
    while (s < last && (*s >= '0' && *s <= '9'))
        s++;

    // Check decimal dot
    bool hadDecimalDot = *s == '.' || *s == ',';
    if (hadDecimalDot)
    {
        s++;
        while (s < last && (*s >= '0' && *s <= '9'))
            s++;
    }

    // If we had no digits, check for special names or exit
    if (s == digits)
    {
        if (strncasecmp(cstring(s), "inf", sizeof("inf") - 1) != 0 &&
            strncasecmp(cstring(s), "∞",   sizeof("∞")   - 1) != 0 &&
            strncasecmp(cstring(s), "NaN", sizeof("NaN") - 1) != 0)
            return SKIP;
        record(decimal128, "Recognized NaN or Inf", s);
    }

    // Check how many digits were given
    uint mantissa = s - digits - hadDecimalDot;
    record(decimal128, "Had %u digits, max %u", mantissa, BID128_MAXDIGITS);
    if (mantissa >= BID128_MAXDIGITS)
    {
        rt.mantissa_error().source(digits + BID128_MAXDIGITS);
        return WARN;                    // Try again with higher-precision
    }

    // Check exponent
    utf8 exponent = nullptr;
    if (*s == 'e' || *s == 'E' || utf8_codepoint(s) == Settings.exponent_char)
    {
        s = utf8_next(s);
        exponent = s;
        if (*s == '+' || *s == '-')
            s++;
        utf8 expval = s;
        while (s < last && (*s >= '0' && *s <= '9'))
            s++;
        if (s == expval)
        {
            rt.exponent_error().source(s);
            return ERROR;
        }
    }

    // Check if exponent is withing range, if not skip to wider format
    if (exponent)
    {
        int expval = atoi(cstring(exponent));
        int maxexp = 128 == 127+1 ? 6144 : 128 == 63+1 ? 384 : 96;
        record(decimal128, "Exponent is %d, max is %d", expval, maxexp);
        if (expval < -(maxexp-1) || expval > maxexp)
        {
            rt.exponent_range_error().source(s);
            return WARN;
        }
    }

    // Patch the input to the BID library
    char buf[50];
    char *b = buf;
    for (utf8 u = source; u < s && b < buf+sizeof(buf) - 1; u++)
    {
        if (*u == Settings.decimal_dot)
        {
            *b++ = '.';
        }
        else if (utf8_codepoint(u) == Settings.exponent_char)
        {
            *b++ = 'E';
            u = utf8_next(u) - 1;
        }
        else
        {
            *b++ = *u;
        }
    }
    *b++ = 0;

    // Create the number
    p.end = s - source;
    p.out = rt.make<decimal128>(ID_decimal128, buf);

    return OK;
}

// Max number of characters written by BID128
// 1 sign
// 34 digits
// 1 exponent delimiater
// 1 exponent sign
// 4 exponent
// 1 decimal separator
// Total 42
// However, even if 42 is the correct answer, this project is about the 48.
// Also, the exponent can be UTF8 in the output, so that could be 3 more.
#define MAXBIDCHAR 48

// Trick to only put the decimal_format function inside decimal128.cc
#if 128 == 64 + 64                      // Check if we are in decimal128.cc

size_t decimal_format(char *buf, size_t len, bool editing)
// ----------------------------------------------------------------------------
//   Format the number according to our preferences
// ----------------------------------------------------------------------------
//   The decimal128 library has a very peculiar way to emit text:
//   it always uses scientific notation, and the mantissa is integral.
//   For example, 123.45 is emitted as 12345E-2.
//   However, it seems to carefully avoid exponent 0 for some reason,
//   so 123 is emitted as 1230E-1, whereas 12,3 is emitted as 123E-1.
//
//   I used to have code lifted from the DM42 SDK here, but it was hard
//   for me to comprehend and maintain, and I could not get it to do what
//   I wanted for SCI and FIX modes.
{
    // First make a copy of zany original input
    char copy[MAXBIDCHAR];
    strncpy(copy, buf, sizeof(copy));

    // Read settings
    const settings &display = Settings;
    auto mode       = editing ? display.NORMAL : display.display_mode;
    int  digits     = editing ? BID128_MAXDIGITS : display.displayed;
    int  max_nonsci = editing ? BID128_MAXDIGITS : display.max_nonsci;
    bool showdec    = display.show_decimal;
    char decimal    = display.decimal_dot; // Can be '.' or ','

    bool overflow = false;
    do
    {
        char *in = copy;
        char *out = buf;
        char *expptr = strchr(in, 'E');
        if (!expptr)
        {
            // If there is no exponent, it's most likely a special number
            // like an infinity or a NaN
            if (strncasecmp(in, "+inf", sizeof("+inf") - 1) == 0)
                strncpy(out, "∞", len);
            else if (strncasecmp(in, "-inf", sizeof("-inf") - 1) == 0)
                strncpy(out, "-∞", len);

            // Otherwise, nothing to do, the buffer already is what we need
            return strlen(out);
        }

        // The first character is always + or -. Skip the '+'
        char sign = *in++;
        bool negative = sign == '-';
        if (negative)
            out++;                  // Keep sign in copy
        else if (sign != '+')       // Defensive coding in case + is not present
            in--;

        // The exponent as given to us by the BID library
        int bidexp = atoi(expptr + 1);

        // Mantissa exponent, i.e. number of digits in mantissa (+1234E-1 -> 4)
        int mexp = expptr - in - 1;

        // Actual exponent is the sum of the two, e.g. 1234E-1 is 1.234E3
        int realexp = bidexp + mexp;

        // BID curiously emits 123.0 as 1230E-1, not even in a consistent way
        // (apparently, parsing "1." gives +1E+0, parsing "1.0" gives +10E-1...,
        // all the way to "1.000" giving "1000E-4"...).
        // This leads us to emit a useless trailing 0. Keep the 0 only for 0.0
        char *last = expptr;
        while (last > copy + 2 && last[-1] == '0')
        {
            last--;
            mexp--;
            bidexp++;
        }

        // Position where we will emit the decimal dot when there is an exponent
        int decpos = 1;

        // Check if we need to switch to scientific notation in normal mode
        // On the negative exponents, we switch when digits would be lost on
        // display compared to actual digits. This is consistent with how HP
        // calculators do it. e.g 1.234556789 when divided by 10 repeatedly
        // switches to scientific notation at 1.23456789E-5, but 1.23 at
        // 1.23E-11 and 1.2 at 1.2E-12 (on an HP50G with 12 digits).
        // This is not symmetrical. Positive exponents switch at 1E12.
        // Note that the behaviour here is purposely different than HP's
        // when in FIX mode. In FIX 5, for example, 1.2345678E-5 is shown
        // on HP50s as 0.00001, and is shown here as 1.23457E-5, which I believe
        // is more useful.
        // Also, since DB48X can compute on 34 digits, and counting zeroes
        // can be annoying, there is a separate setting for when to switch
        // to scientific notation.
        bool hasexp = mode >= settings::display::SCI;
        if (!hasexp)
        {
            if (realexp < 0)
            {
                int minexp = digits < max_nonsci ? digits : max_nonsci;
                hasexp = mexp - realexp - 1 >= minexp;
            }
            else
            {
                hasexp = realexp >= max_nonsci;
                if (!hasexp)
                    decpos = realexp + 1;
            }
        }

        // Number of decimals to show is given number of digits for most modes
        // (This counts *all* digits for standard / SIG mode)
        int decimals = digits;

        // Write leading zeroes if necessary
        if (!hasexp && realexp < 0)
        {
            // HP RPL calculators don't show leading 0, i.e. 0.5 shows as .5,
            // but this is only in STD mode, not in other modes.
            // This is pure evil and inconsistent with all older HP calculators
            // (which, granted, did not have STD mode) and later ones (Prime)
            // So let's decide that 0.3 will show as 0.3 in STD mode and not .3
            *out++ = '0';
            decpos--;               // Don't emit the decimal separator twice

            // Emit decimal dot and leading zeros on fractional part
            *out++ = decimal;
            for (int zeroes = realexp + 1; zeroes < 0; zeroes++)
            {
                *out++ = '0';
                decimals--;
            }
        }

        // Adjust exponent being displayed for engineering mode
        int dispexp = realexp;
        bool engmode = mode == display.ENG;
        if (engmode)
        {
            int offset = dispexp >= 0 ? dispexp % 3 : (dispexp - 2) % 3 + 2;
            decpos += offset;
            dispexp -= offset;
            decimals += 1;
        }

        // Copy significant digits, inserting decimal separator when needed
        bool sigmode = mode == display.NORMAL;
        while (in < last && decimals > 0)
        {
            *out++ = *in++;
            decpos--;
            if (decpos == 0 && (in < last || showdec))
                *out++ = decimal;

            // Count decimals after decimal separator, except in SIG mode
            // where we count all significant digits being displayed
            if (decpos < 0 || sigmode || engmode)
                decimals--;
        }

        // Check if we need some rounding on what is being displayed
        if (in < last && *in >= '5')
        {
            char *rptr = out;
            bool rounding = true;
            while (rounding && --rptr > buf)
            {
                if (*rptr >= '0')   // Do not convert '.' or '-'
                {
                    *rptr += 1;
                    rounding = *rptr > '9';
                    if (rounding)
                        *rptr -= 10;
                }
            }

            // If we ran past the first digit, we overflowed during rounding
            // Need to re-run with the next larger exponent
            // This can only occur with a conversion of 9.9999 to 1
            if (rounding)
            {
                overflow = true;
                snprintf(copy, sizeof(copy),
                         "%c1E%d",
                         negative ? '-' : '+',
                         realexp + 1);
                continue;
            }
        }

        // Do not add trailing zeroes in standard mode
        if (sigmode)
        {
            if (decpos > 0)
                decimals = decpos;
            else
                decimals = 0;
        }
        else if (mode == display.FIX && decpos > 0)
        {
            decimals = digits + decpos;
        }

        // Add trailing zeroes if necessary
        while (decimals > 0)
        {
            *out++ = '0';
            decpos--;
            if (decpos == 0)
                *out++ = decimal;
            decimals--;
        }

        // Add exponent if necessary
        if (hasexp)
        {
            size_t sz = utf8_encode(display.exponent_char, (byte *) out);
            out += sz;
            size_t remaining = buf + MAXBIDCHAR - out;
            size_t written = snprintf(out, remaining, "%d", dispexp);
            out += written;
        }
        *out = 0;
        return out - buf;
    } while (overflow);
    return 0;
}
#endif // In original decimal128.cc


OBJECT_RENDERER_BODY(decimal128)
// ----------------------------------------------------------------------------
//   Render the decimal128 into the given string buffer
// ----------------------------------------------------------------------------
{
    // Align the value
    bid128 num = value();

    // Render in a separate buffer to avoid overflows
    char buf[MAXBIDCHAR];
    bid128_to_string(buf, &num.value);
    record(decimal128, "Render raw output [%s]", buf);

    size_t sz = decimal_format(buf, sizeof(buf), r.editing());
    record(decimal128, "Render formatted output [%s]", buf);

    // And return it to the caller
    return r.put(buf, sz) ? sz : 0;
}



// ============================================================================
//
//   Arithmetic wrappers
//
// ============================================================================
//   Define mod and rem in a way that matches mathematical definition

void bid128_mod(BID_UINT128 *pres, BID_UINT128 *px, BID_UINT128 *py)
// ----------------------------------------------------------------------------
//   The fmod function is really a remainder, adjust it for negative input
// ----------------------------------------------------------------------------
{
    int zero = 0;
    bid128_fmod(pres, px, py);
    bid128_isZero(&zero, pres);
    if (!zero)
    {
        bool xneg = decimal128::is_negative(px);
        bool yneg = decimal128::is_negative(py);
        if (xneg != yneg)
        {
            BID_UINT128 tmp = *pres;
            bid128_add(pres, &tmp, py);
        }
    }
}


void bid128_rem(BID_UINT128 *pres, BID_UINT128 *px, BID_UINT128 *py)
// ----------------------------------------------------------------------------
//   The fmod function is really a remainder, use it as is
// ----------------------------------------------------------------------------
{
    bid128_fmod(pres, px, py);
}
