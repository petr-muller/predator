/*
 * Copyright (C) 2009 Kamil Dudka <kdudka@redhat.com>
 *
 * This file is part of sl.
 *
 * sl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * sl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef H_GUARD_CL_PRIVATE_H
#define H_GUARD_CL_PRIVATE_H

#include "code_listener.h"
#include "location.hh"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

#ifndef STREQ
#   define STREQ(s1, s2) (0 == strcmp(s1, s2))
#endif

// pull in __attribute__ ((__noreturn__))
#define CL_DIE(msg) do { \
    cl_die("fatal error: " msg); \
    abort(); \
} while (0)

#define CL_MSG_STREAM(fnc, to_stream) do { \
    std::ostringstream str; \
    str << to_stream; \
    fnc(str.str().c_str()); \
} while (0)

#define CL_MSG_STREAM_INTERNAL(fnc, to_stream) \
    CL_MSG_STREAM(fnc, __FILE__ << ":" << __LINE__ \
            << ": " << to_stream << " [internal location]")

#define CL_DEBUG(to_stream) \
    CL_MSG_STREAM_INTERNAL(cl_debug, "debug: " << to_stream)

#define CL_WARN(to_stream) \
    CL_MSG_STREAM_INTERNAL(cl_warn, "warning: " << to_stream)

#define CL_ERROR(to_stream) \
    CL_MSG_STREAM_INTERNAL(cl_error, "error: " << to_stream)

#define CL_NOTE(to_stream) \
    CL_MSG_STREAM_INTERNAL(cl_note, "note: " << to_stream)

#define CL_DEBUG_MSG(loc, what) \
    CL_MSG_STREAM(cl_debug, (loc) << "debug: " << what)

#define CL_WARN_MSG(loc, what) \
    CL_MSG_STREAM(cl_warn, (loc) << "warning: " << what)

#define CL_ERROR_MSG(loc, what) \
    CL_MSG_STREAM(cl_error, (loc) << "error: " << what)

#define CL_NOTE_MSG(loc, what) \
    CL_MSG_STREAM(cl_note, (loc) << "note: " << what)

void cl_debug(const char *msg);
void cl_warn(const char *msg);
void cl_error(const char *msg);
void cl_note(const char *msg);
void cl_die(const char *msg);

/**
 * C++ interface for listener objects. It can be wrapped to struct code_listener
 * object when exposing to pure C world. See code_listener for details about
 * methods and their parameters.
 */
class ICodeListener {
    public:
        virtual ~ICodeListener() { }

        virtual void file_open(
            const char              *file_name)
            = 0;

        virtual void file_close()
            = 0;

        virtual void fnc_open(
            const struct cl_operand *fnc)
            = 0;

        virtual void fnc_arg_decl(
            int                     arg_id,
            const struct cl_operand *arg_src)
            = 0;

        virtual void fnc_close()
            = 0;

        virtual void bb_open(
            const char              *bb_name)
            = 0;

        virtual void insn(
            const struct cl_insn    *cli)
            = 0;

        virtual void insn_call_open(
            const struct cl_location*loc,
            const struct cl_operand *dst,
            const struct cl_operand *fnc)
            = 0;

        virtual void insn_call_arg(
            int                     arg_id,
            const struct cl_operand *arg_src)
            = 0;

        virtual void insn_call_close()
            = 0;

        virtual void insn_switch_open(
            const struct cl_location*loc,
            const struct cl_operand *src)
            = 0;

        virtual void insn_switch_case(
            const struct cl_location*loc,
            const struct cl_operand *val_lo,
            const struct cl_operand *val_hi,
            const char              *label)
            = 0;

        virtual void insn_switch_close()
            = 0;

        virtual void finalize()
            = 0;
};

/**
 * wrap ICodeListener object so that it can be exposed to pure C world
 */
struct cl_code_listener* cl_create_listener_wrap(ICodeListener *);

/**
 * retrieve wrapped ICodeListener object
 */
ICodeListener* cl_obtain_from_wrap(struct cl_code_listener *);

template <typename TCont>
bool hasKey(const TCont &cont, const typename TCont::key_type &key) {
    return cont.end() != cont.find(key);
}

template <typename TCont>
bool hasKey(const TCont *cont, const typename TCont::key_type &key) {
    return hasKey(*cont, key);
}

template <class TStack, class TFirst, class TSecond>
void push(TStack &dst, const TFirst &first, const TSecond &second)
{
    dst.push(typename TStack::value_type(first, second));
}

template <class TStack, class TFirst, class TSecond>
void push(TStack *dst, const TFirst &first, const TSecond &second)
{
    push(*dst, first, second);
}

#endif /* H_GUARD_CL_PRIVATE_H */
