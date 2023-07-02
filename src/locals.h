#ifndef LOCALS_H
#define LOCALS_H
// ****************************************************************************
//  locals.h                                                      DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Block with local variables, e.g. `→ X Y « X Y - X Y + * »`
//
//     Local values reside above the stack. They are referenced by an index,
//     which makes them very efficient (there is no name lookup). Reading
//     or storing in a local variable is as efficient as accessing the stack.
//     This is much faster than global variables, which require a rather slow
//     linear name lookup and, when storing, moving the directory object.
//
//
// ****************************************************************************
//   (C) 2023 Christophe de Dinechin <christophe@dinechin.org>
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
/*
  A local block has the following structure (everything encoded as LEB128)

  0. ID_locals
  1. Total length for fast skipping
  2. Number of locals
  3. Sequence of local names, each one being
  3.1 Local 1 name length
  3.2 Local 1 name
  4. Length of code block
  5. Sequence of code block objects

  A local variable name has the following structure:

  0. ID_local
  1. Index of local (can be beyond current locals block)

  Since locals accumulate beyond the stack, it is possible to refer to a
  local outside of the current one, by using an index above what is in the
  current locals scope. For example, consider

      → X Y « X Y - X Y + * 2 → A B « A B + X Y - * » »

  In the inner block, A and B will be index 0 and 1 respectively, X and Y will
  be index 2 and 3 respectively, referring to the outer block.

  When exiting a local scope, a local name like 'X' on the stack or in an
  algebraic object or elsewhere becomes invalid. It is a program error to
  do so. A local object referring beyond the last object will show up as
  'InvalidLocal'.

  Local names cannot be stored in global variables. No attempt is made to
  detect that condition recursively, e.g. an algebraic or program containing
  a local name.
 */

#include "list.h"


struct locals : program
// ----------------------------------------------------------------------------
//   A locals block
// ----------------------------------------------------------------------------
{
    locals(gcbytes bytes, size_t len, id type = ID_locals)
        : program(bytes, len, type) {}

    result execute(runtime &rt = RT) const;

    OBJECT_HANDLER(locals);
    OBJECT_PARSER(locals);
    OBJECT_RENDERER(locals);
};
typedef const locals *locals_p;
typedef gcp<const locals> locals_g;


struct local : object
// ----------------------------------------------------------------------------
//   A local variable name (represented by its index in enclosing local block)
// ----------------------------------------------------------------------------
{
    local(uint index, id type = ID_local): object(type)
    {
        byte *p = payload();
        leb128(p, index);
    }

    static size_t required_memory(id i, uint index)
    {
        return leb128size(i) + leb128size(index);
    }

    local(gcbytes ptr, size_t size, id type = ID_local): object(type)
    {
        byte *p = payload();
        memmove(p, byte_p(ptr), size);
    }

    static size_t required_memory(id i, gcbytes UNUSED ptr, size_t size)
    {
        return leb128size(i) + size;
    }

    size_t index() const
    {
        byte_p p = payload();
        return leb128<size_t>(p);
    }

    object_p recall(runtime &rt = RT) const
    {
        return rt.local(index());
    }

    bool store(gcobj obj, runtime &rt = RT) const
    {
        return rt.local(index(), obj);
    }

    result execute(runtime &rt = RT) const;
    result evaluate(runtime &rt = RT) const;

    OBJECT_HANDLER(local);
    OBJECT_RENDERER(local);
    OBJECT_PARSER(local);
};
typedef const local *local_p;
typedef gcp<const local> local_g;


struct locals_stack
// ----------------------------------------------------------------------------
//   A structure used in parser and renderer to identify locals
// ----------------------------------------------------------------------------
{
    locals_stack(gcbytes names) : names_list(names), next(stack)
    {
        stack = this;
    }
    ~locals_stack()
    {
        stack = next;
    }

    byte_p               names()        { return names_list; }
    static locals_stack *current()      { return stack; }
    locals_stack *       enclosing()    { return next; }

private:
    static locals_stack *stack;
    gcbytes             names_list;
    locals_stack        *next;
};

#endif // LOCALS_H
