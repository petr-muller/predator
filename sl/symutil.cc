/*
 * Copyright (C) 2009-2011 Kamil Dudka <kdudka@redhat.com>
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
#include "symutil.hh"

#include <cl/cl_msg.hh>
#include <cl/storage.hh>

#include "symbt.hh"
#include "symheap.hh"
#include "symproc.hh"
#include "util.hh"

#include <stack>

#include <boost/foreach.hpp>

void moveKnownValueToLeft(
        const SymHeapCore           &sh,
        TValId                      &valA,
        TValId                      &valB)
{
    sortValues(valA, valB);

    if ((0 < valA) && UV_KNOWN != sh.valGetUnknown(valA)) {
        const TValId tmp = valA;
        valA = valB;
        valB = tmp;
    }
}

TObjId /* root */ objRoot(const SymHeap &sh, TObjId obj) {
    if (obj <= 0)
        return obj;

    const TValId addr = sh.placedAt(obj);
    const TValId rootAt = sh.valRoot(addr);
    const TObjId root = const_cast<SymHeap &>(sh).objAt(rootAt);
    if (OBJ_UNKNOWN == root)
        // FIXME: a dangling object??? (try test-0093 with a debugger)
        return obj;

    return root;
}

void getPtrValues(TValList &dst, const SymHeap &sh, TValId at) {
    TObjList ptrs;
    sh.gatherLivePointers(ptrs, at);
    BOOST_FOREACH(const TObjId obj, ptrs) {
        const TValId val = sh.valueOf(obj);
        if (0 < val)
            dst.push_back(sh.valueOf(obj));
    }
}

typedef std::pair<TObjId, const cl_initializer *> TInitialItem;

// TODO: remove this
template <class TItem> struct TraverseSubObjsHelper { };

// TODO: remove this
template <class THeap, class TVisitor, class TItem = TObjId>
bool /* complete */ traverseSubObjs(THeap &sh, TItem item, TVisitor &visitor,
                                    bool leavesOnly)
{
    std::stack<TItem> todo;
    todo.push(item);
    while (!todo.empty()) {
        item = todo.top();
        todo.pop();

        typedef TraverseSubObjsHelper<TItem> THelper;
        const struct cl_type *clt = THelper::getItemClt(sh, item);
        CL_BREAK_IF(!clt || !isComposite(clt));

        for (int i = 0; i < clt->item_cnt; ++i) {
            const TItem next = THelper::getNextItem(sh, item, i);

            const struct cl_type *subClt = THelper::getItemClt(sh, next);
            if (subClt && isComposite(subClt)) {
                todo.push(next);

                if (leavesOnly)
                    // do not call the visitor for internal nodes, if requested
                    continue;
            }

            if (!/* continue */visitor(sh, next))
                return false;
        }
    }

    // the traversal is done, without any interruption by visitor
    return true;
}


// TODO: remove this
template <> struct TraverseSubObjsHelper<TInitialItem> {
    static const struct cl_type* getItemClt(const SymHeap           &sh,
                                            const TInitialItem      &item)
    {
        const struct cl_type *clt = sh.objType(item.first);
        CL_BREAK_IF(item.second && (!clt || *clt != *item.second->type));
        return clt;
    }

    static TInitialItem getNextItem(const SymHeap                   &sh,
                                    TInitialItem                    item,
                                    int                             nth)
    {
        item.first = sh.subObj(item.first, nth);

        const struct cl_initializer *&initial = item.second;
        if (initial)
            initial = initial->data.nested_initials[nth];

        return item;
    }
};

bool initSingleVariable(SymHeap &sh, const TInitialItem &item) {
    const TObjId obj = item.first;
    const struct cl_type *clt = sh.objType(obj);
    CL_BREAK_IF(!clt);

    const enum cl_type_e code = clt->code;
    switch (code) {
        case CL_TYPE_ARRAY:
            CL_DEBUG("CL_TYPE_ARRAY is not supported by VarInitializer");
            return /* continue */ true;

        case CL_TYPE_UNION:
        case CL_TYPE_STRUCT:
            CL_TRAP;

        default:
            break;
    }

    const struct cl_initializer *initial = item.second;
    if (!initial) {
        // no initializer given, nullify the variable
        sh.objSetValue(obj, /* also equal to VAL_FALSE */ VAL_NULL);
        return /* continue */ true;
    }

    // FIXME: we're asking for troubles this way
    SymBackTrace dummyBt(sh.stor());
    SymProc proc(sh, &dummyBt);

    // resolve initial value
    const struct cl_operand *op = initial->data.value;
    const TValId val = proc.valFromOperand(*op);
    CL_DEBUG("using explicit initializer: obj #"
            << static_cast<int>(obj) << " <-- val #"
            << static_cast<int>(val));

    // set the initial value
    CL_BREAK_IF(VAL_INVALID == val);
    sh.objSetValue(obj, val);

    return /* continue */ true;
}

void initVariable(SymHeap                       &sh,
                  TObjId                        obj,
                  const CodeStorage::Var        &var)
{
    const TInitialItem item(obj, var.initial);

    if (isComposite(var.type))
        traverseSubObjs(sh, item, initSingleVariable, /* leavesOnly */ true);
    else
        initSingleVariable(sh, item);
}

class PointingObjectsFinder {
    public:
        // we have to use std::set, a vector is not sufficient in all cases
        typedef std::set<TObjId> TResults;

    private:
        TResults results_;

    public:
        const TResults& results() const { return results_; }

        bool operator()(const SymHeap &sh, TObjId obj) {
            const TValId addr = sh.placedAt(obj);
            CL_BREAK_IF(addr <= 0);

            TObjList refs;
            sh.usedBy(refs, addr);
            std::copy(refs.begin(), refs.end(),
                      std::inserter(results_, results_.begin()));

            return /* continue */ true;
        }
};

void redirectRefs(
        SymHeap                 &sh,
        const TValId            pointingFrom,
        const TValId            pointingTo,
        const TValId            redirectTo)
{
    // go through all objects pointing at/inside pointingTo
    TObjList refs;
    sh.pointedBy(refs, pointingTo);
    BOOST_FOREACH(const TObjId obj, refs) {
        const TValId referrerAt = sh.valRoot(sh.placedAt(obj));
        if (VAL_INVALID != pointingFrom && pointingFrom != referrerAt)
            // pointed from elsewhere, keep going
            continue;

        // check the current link
        const TValId nowAt = sh.valueOf(obj);
        const TOffset offToRoot = sh.valOffset(nowAt);
        CL_BREAK_IF(sh.valOffset(redirectTo));

        // redirect accordingly
        const TValId result = sh.valByOffset(redirectTo, offToRoot);
        sh.objSetValue(obj, result);
    }
}
