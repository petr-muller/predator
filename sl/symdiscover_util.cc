/*
 * Copyright (C) 2010 Kamil Dudka <kdudka@redhat.com>
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

#include "config.h"
#include "symdiscover_util.hh"

#include <boost/foreach.hpp>

bool isPointedByVar(SymHeap &sh, const TValId root)
{
    ObjList refs;
    sh.pointedBy(refs, root);
    BOOST_FOREACH(const ObjHandle obj, refs) {
        const TValId at = obj.placedAt();
        const EValueTarget code = sh.valTarget(at);
        if (isProgramVar(code))
            return true;
    }

    // no reference by a program variable
    return false;
}
