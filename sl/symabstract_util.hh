/*
 * Copyright (C) 2009-2010 Kamil Dudka <kdudka@redhat.com>
 * Copyright (C) 2010 Petr Peringer, FIT
 *
 * This file is part of predator.
 *
 * predator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * predator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with predator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef H_GUARD_SYMDISCOVER_UTIL_H
#define H_GUARD_SYMDISCOVER_UTIL_H
#include "symdebug.hh"

#define REQUIRE_GC_ACTIVITY(sh, val, fnc) do {                                 \
    if (collectJunk(sh, val))                                                  \
        break;                                                                 \
    CL_ERROR(#fnc "() failed to collect garbage, " #val " still referenced");  \
    CL_BREAK_IF("REQUIRE_GC_ACTIVITY has not been successful");                \
} while (0)

#endif
