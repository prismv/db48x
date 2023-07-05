// ****************************************************************************
//  algebraic.cc                                                DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Shared code for all algebraic commands
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

#include "algebraic.h"

#include "arithmetic.h"
#include "bignum.h"
#include "input.h"
#include "integer.h"
#include "parser.h"
#include "renderer.h"
#include "runtime.h"
#include "settings.h"

#include <ctype.h>
#include <stdio.h>


RECORDER(algebraic,       16, "RPL Algebraics");
RECORDER(algebraic_error, 16, "Errors processing a algebraic");


INSERT_BODY(algebraic)
// ----------------------------------------------------------------------------
//   Enter data in algebraic mode
// ----------------------------------------------------------------------------
{
    return i.edit(o->fancy(), i.ALGEBRAIC);
}


bool algebraic::real_promotion(algebraic_g &x, object::id type)
// ----------------------------------------------------------------------------
//   Promote the value x to the given type
// ----------------------------------------------------------------------------
{
    id xt = x->type();
    if (xt == type)
        return true;

    record(algebraic, "Real promotion of %p from %+s to %+s",
           (object_p) x, object::name(xt), object::name(type));
    switch(xt)
    {
    case ID_integer:
    {
        integer_p i    = x->as<integer>();
        ularge    ival = i->value<ularge>();
        switch (type)
        {
        case ID_decimal32:
            x = rt.make<decimal32>(ID_decimal32, ival);
            return true;
        case ID_decimal64:
            x = rt.make<decimal64>(ID_decimal64, ival);
            return true;
        case ID_decimal128:
            x = rt.make<decimal128>(ID_decimal128, ival);
            return true;
        default:
            break;
        }
        record(algebraic_error,
               "Cannot promote integer %p (%llu) from %+s to %+s",
               i, ival, object::name(xt), object::name(type));
        break;
    }
    case ID_neg_integer:
    {
        integer_p i    = x->as<neg_integer>();
        ularge    ival = i->value<ularge>();
        switch (type)
        {
        case ID_decimal32:
            x = rt.make<decimal32>(ID_decimal32, ival, true);
            return true;
        case ID_decimal64:
            x = rt.make<decimal64>(ID_decimal64, ival, true);
            return true;
        case ID_decimal128:
            x = rt.make<decimal128>(ID_decimal128, ival, true);
            return true;
        default:
            break;
        }
        record(algebraic_error,
               "Cannot promote neg_integer %p (%lld) from %+s to %+s",
               i, ival, object::name(xt), object::name(type));
        break;
    }

    case ID_bignum:
    case ID_neg_bignum:
    {
        bignum_p i = bignum_p(object_p(x));
        switch (type)
        {
        case ID_decimal32:
            x = rt.make<decimal32>(ID_decimal32, i);
            return true;
        case ID_decimal64:
            x = rt.make<decimal64>(ID_decimal64, i);
            return true;
        case ID_decimal128:
            x = rt.make<decimal128>(ID_decimal128, i);
            return true;
        default:
            break;
        }
        record(algebraic_error,
               "Cannot promote bignum %p from %+s to %+s",
               i, object::name(xt), object::name(type));
        break;
    }

    case ID_decimal32:
    {
        decimal32_p d = x->as<decimal32>();
        bid32       dval = d->value();
        switch (type)
        {
        case ID_decimal64:
            x = rt.make<decimal64>(ID_decimal64, dval);
            return true;
        case ID_decimal128:
            x = rt.make<decimal128>(ID_decimal128, dval);
            return true;
        default:
            break;
        }
        record(algebraic_error,
               "Cannot promote decimal32 %p from %+s to %+s",
               d, object::name(xt), object::name(type));
        break;
    }

    case ID_decimal64:
    {
        decimal64_p d = x->as<decimal64>();
        bid64       dval = d->value();
        switch (type)
        {
        case ID_decimal64:
            x = rt.make<decimal64>(ID_decimal64, dval);
            return true;
        case ID_decimal128:
            x = rt.make<decimal128>(ID_decimal128, dval);
            return true;
        default:
            break;
        }
        record(algebraic_error,
               "Cannot promote decimal64 %p from %+s to %+s",
               d, object::name(xt), object::name(type));
        break;
    }
    default:
        break;
    }

    return false;
}


object::id algebraic::real_promotion(algebraic_g &x)
// ----------------------------------------------------------------------------
//   Promote the value x to a type selected based on preferences
// ----------------------------------------------------------------------------
{
    // Auto-selection of type
    uint16_t prec = Settings.precision;
    id       type = prec > BID64_MAXDIGITS ? ID_decimal128
                  : prec > BID32_MAXDIGITS ? ID_decimal64
                                           : ID_decimal32;
    return real_promotion(x, type) ? type : ID_object;
}


object::id algebraic::bignum_promotion(algebraic_g &x)
// ----------------------------------------------------------------------------
//   Promote the value x to the corresponding bignum
// ----------------------------------------------------------------------------
{
    id xt = x->type();
    id ty = xt;

    switch(xt)
    {
    case ID_hex_integer:        ty = ID_hex_bignum;     break;
    case ID_dec_integer:        ty = ID_dec_bignum;     break;
    case ID_oct_integer:        ty = ID_oct_bignum;     break;
    case ID_bin_integer:        ty = ID_bin_bignum;     break;
    case ID_based_integer:      ty = ID_based_bignum;   break;
    case ID_neg_integer:        ty = ID_neg_bignum;     break;
    case ID_integer:            ty = ID_bignum;         break;
    default:
        break;
    }
    if (ty != xt)
    {
        integer_g i = (integer *) object_p(x);
        x = rt.make<bignum>(ty, i);
    }
    return ty;
}
