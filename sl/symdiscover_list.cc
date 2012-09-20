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
#include "symdiscover_list.hh"
#include "symdiscover_util.hh"

#include "prototype.hh"
#include "symseg.hh"
#include "symjoin.hh"
#include <cl/cl_msg.hh>

#include <boost/foreach.hpp>

// costs are now hard-wired in the paper, so they were removed from config.h
#define SE_PROTO_COST_SYM           0
#define SE_PROTO_COST_ASYM          1
#define SE_PROTO_COST_THREEWAY      2


unsigned int ListAbstractionDiscoveryNamespace::minLengthByCost(int cost)
{
    // abstraction length thresholds are now configurable in config.h
    static const int thrTable[] = {
        (SE_COST0_LEN_THR),
        (SE_COST1_LEN_THR),
        (SE_COST2_LEN_THR)
    };

    static const int maxCost = sizeof(thrTable)/sizeof(thrTable[0]) - 1;
    if (maxCost < cost)
        cost = maxCost;

    // Predator counts elementar merges whereas the paper counts objects on path
    const int minLength = thrTable[cost] - 1;
    CL_BREAK_IF(minLength < 1);
    return minLength;
}

ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery::ListAbstractionDiscovery(const TValId entry,
                                         const BindingOff &off) :
    AbstractionDiscovery(entry)
{
    this->off = off;
    if (isDlsBinding(off)) {
        this->kind = OK_DLS;
    }
    else {
        this->kind = OK_SLS;
    }
}

bool matchSegBinding(
        const SymHeap               &sh,
        const TValId                seg,
        const BindingOff            &offPath)
{
    const EObjKind kind = sh.valTargetKind(seg);
    switch (kind) {
        case OK_CONCRETE:
            // nothing to match actually
            return true;

        case OK_OBJ_OR_NULL:
            // OK_OBJ_OR_NULL can be the last node of a NULL-terminated list
            return true;

        default:
            break;
    }

    const BindingOff offObj = sh.segBinding(seg);
    if (offObj.head != offPath.head)
        // head mismatch
        return false;

    if (!isDlsBinding(offPath)) {
        // OK_SLS
        switch (kind) {
            case OK_SEE_THROUGH:
            case OK_SLS:
                return (offObj.next == offPath.next);

            default:
                return false;
        }
    }

    // OK_DLS
    switch (kind) {
        case OK_SEE_THROUGH_2N:
            if ((offObj.next == offPath.next) && (offObj.prev == offPath.prev))
                // both fields are equal
                return true;

            // fall through!

        case OK_DLS:
            return (offObj.next == offPath.prev)
                && (offObj.prev == offPath.next);

        default:
            return false;
    }
}

/// (VAL_INVALID == prev && VAL_INVALID == next) denotes prototype validation
bool validatePointingObjects(
        SymHeap                    &sh,
        const BindingOff           &off,
        const TValId                root,
        TValId                      prev,
        const TValId                next,
        TValSet                     allowedReferers,
        const bool                  allowHeadPtr = false)
{
    // we allow pointers to self at this point, but we require them to be
    // absolutely uniform along the abstraction path -- joinDataReadOnly()
    // is responsible for that
    allowedReferers.insert(root);
    if (OK_DLS == sh.valTargetKind(root))
        allowedReferers.insert(dlSegPeer(sh, root));

    if (OK_DLS == sh.valTargetKind(prev))
        // jump to peer in case of DLS
        prev = dlSegPeer(sh, prev);

    // we allow pointers to self at this point, but we require them to be
    // absolutely uniform along the abstraction path -- matchData() should
    // later take care of that
    allowedReferers.insert(root);

    // collect all objects pointing at/inside the object
    ObjList refs;
    sh.pointedBy(refs, root);

    // unless this is a prototype, disallow self loops from _binding_ pointers
    TObjSet blackList;
    if (VAL_INVALID != prev || VAL_INVALID != next)
        buildIgnoreList(blackList, sh, root, off);

    const TValId headAddr = sh.valByOffset(root, off.head);

    TValSet whiteList;
    whiteList.insert(sh.valByOffset(prev, off.next));
    if (isDlsBinding(off))
        whiteList.insert(sh.valByOffset(next, off.prev));

    BOOST_FOREACH(const ObjHandle &obj, refs) {
        if (hasKey(blackList, obj))
            return false;

        const TValId at = obj.placedAt();
        if (hasKey(whiteList, at))
            continue;

        if (allowHeadPtr && (obj.value() == headAddr))
            continue;

        if (hasKey(allowedReferers, sh.valRoot(at)))
            continue;

        // someone points at/inside who should not
        return false;
    }

    // no problems encountered
    return true;
}



TValId jumpToNextObj(
        SymHeap                     &sh,
        const BindingOff            &off,
        std::set<TValId>            &haveSeen,
        const TValSet               &protoRoots,
        TValId                      at)
{
    if (!matchSegBinding(sh, at, off))
        // binding mismatch
        return VAL_INVALID;

    const bool dlSegOnPath = (OK_DLS == sh.valTargetKind(at));
    if (dlSegOnPath) {
        // jump to peer in case of DLS
        at = dlSegPeer(sh, at);
        haveSeen.insert(at);
    }

    const TValId nextHead = valOfPtrAt(sh, at, off.next);
    if (nextHead <= 0 || off.head != sh.valOffset(nextHead))
        // no valid head pointed by nextPtr
        return VAL_INVALID;

    const TValId next = sh.valRoot(nextHead);
    if (!isOnHeap(sh.valTarget(next)))
        // only objects on heap can be abstracted out
        return VAL_INVALID;

    if (!matchSegBinding(sh, next, off))
        // binding mismatch
        return VAL_INVALID;

    if (sh.valSizeOfTarget(at) != sh.valSizeOfTarget(next))
        // mismatch in size of targets
        return VAL_INVALID;

    const TObjType clt = sh.valLastKnownTypeOfTarget(at);
    if (clt) {
        const TObjType cltNext = sh.valLastKnownTypeOfTarget(next);
        if (cltNext && *cltNext != *clt)
            // both roots are (kind of) type-aware, but the types differ
            return VAL_INVALID;
    }

    const bool isDls = isDlsBinding(off);
    if (isDls) {
        // check DLS back-link
        const TValId headAt = sh.valByOffset(at, off.head);
        const TValId valPrev = valOfPtrAt(sh, next, off.prev);
        if (headAt != valPrev)
            // DLS back-link mismatch
            return VAL_INVALID;
    }

    if (dlSegOnPath && !validatePointingObjects(sh, off,
                /* root */ at,
                /* prev */ at,
                /* next */ next,
                protoRoots))
        // never step over a peer object that is pointed from outside!
        return VAL_INVALID;

    // check if valNext inside the 'next' object is at least VAL_NULL
    // (otherwise we are not able to construct Neq edges to express its length)
    if (valOfPtrAt(sh, next, off.next) < 0)
        return VAL_INVALID;

    return next;
}

typedef TValSet TProtoRoots[2];

bool matchData(
        SymHeap                     &sh,
        const BindingOff            &off,
        const TValId                at1,
        const TValId                at2,
        TProtoRoots                 *protoRoots,
        int                         *pCost)
{
    if (!isDlsBinding(off) && isPointedByVar(sh, at2))
        // only first node of an SLS can be pointed by a program var, giving up
        return false;

    EJoinStatus status;
    if (!joinDataReadOnly(&status, sh, off, at1, at2, protoRoots)) {
        CL_DEBUG("    joinDataReadOnly() refuses to create a segment!");
        return false;
    }

    int cost = 0;
    switch (status) {
        case JS_USE_ANY:
            cost = (SE_PROTO_COST_SYM);
            break;

        case JS_USE_SH1:
        case JS_USE_SH2:
            cost = (SE_PROTO_COST_ASYM);
            break;

        case JS_THREE_WAY:
            cost = (SE_PROTO_COST_THREEWAY);
            break;
    }

    *pCost = cost;
    return true;
}

// FIXME: suboptimal implementation
bool validatePrototypes(
        SymHeap                     &sh,
        const BindingOff            &off,
        const TValId                rootAt,
        TValSet                     protoRoots)
{
    TValId peerAt = VAL_INVALID;
    protoRoots.insert(rootAt);
    if (OK_DLS == sh.valTargetKind(rootAt)) {
        peerAt = dlSegPeer(sh, rootAt);
        protoRoots.insert(peerAt);
    }

    BOOST_FOREACH(const TValId protoAt, protoRoots) {
        if (protoAt == rootAt || protoAt == peerAt)
            // we have inserted root/peer into protoRoots, in order to get them
            // on the list of allowed referrers, but it does not mean that they
            // are prototypes
            continue;

        if (!validatePointingObjects(sh, off, protoAt, VAL_INVALID, VAL_INVALID,
                                     protoRoots))
            return false;
    }

    // all OK!
    return true;
}

bool validateSegEntry(
        SymHeap                     &sh,
        const BindingOff            &off,
        const TValId                entry,
        const TValId                prev,
        const TValId                next,
        const TValSet               &protoRoots)
{
    // first validate 'root' itself
    if (!validatePointingObjects(sh, off, entry, prev, next, protoRoots,
                                 /* allowHeadPtr */ true))
        return false;

    return validatePrototypes(sh, off, entry, protoRoots);
}



typedef std::map<int /* cost */, int /* length */> TRankMap;

ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery* ListAbstractionDiscoveryNamespace::listDiscover(
        SymHeap                     &sh,
        const BindingOff            &off,
        const TValId                entry)
{
    ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery* discovered = NULL;
    // we use std::set to detect loops
    std::set<TValId> haveSeen;
    haveSeen.insert(entry);
    TValId prev = entry;

    // the entry can already have some prototypes we should take into account
    TValSet initialProtos;
    if (OK_DLS == sh.valTargetKind(entry)) {
        TValList protoList;
        collectPrototypesOf(protoList, sh, entry, /* skipDlsPeers */ false);
        BOOST_FOREACH(const TValId proto, protoList)
            initialProtos.insert(proto);
    }

    // jump to the immediate successor
    TValId at = jumpToNextObj(sh, off, haveSeen, initialProtos, entry);
    if (!insertOnce(haveSeen, at))
        // loop detected
        return NULL;

    // we need a way to prefer lossless prototypes
    int maxCostOnPath = 0;

    // main loop of segDiscover()
    std::vector<TValId> path;
    while (VAL_INVALID != at) {
        // compare the data
        TProtoRoots protoRoots;
        int cost = 0;

        // join data of the current pair of objects
        if (!matchData(sh, off, prev, at, &protoRoots, &cost))
            break;

        if (prev == entry && !validateSegEntry(sh, off, entry, VAL_INVALID, at,
                                               protoRoots[0]))
            // invalid entry
            break;

        if (!insertOnce(haveSeen, segNextRootObj(sh, at, off.next)))
            // loop detected
            break;

        if (!validatePrototypes(sh, off, at, protoRoots[1]))
            // someone points to a prototype
            break;

        bool leaving = false;

        // look ahead
        TValId next = jumpToNextObj(sh, off, haveSeen, protoRoots[1], at);
        if (!validatePointingObjects(sh, off, at, prev, next, protoRoots[1])) {
            // someone points at/inside who should not

            leaving = /* looking for a DLS */ isDlsBinding(off)
                && /* got a DLS */ OK_DLS != sh.valTargetKind(at)
                && validateSegEntry(sh, off, at, prev, VAL_INVALID,
                                    protoRoots[1]);

            if (!leaving)
                break;
        }

        // enlarge the path by one
        path.push_back(at);
        if (maxCostOnPath < cost)
            maxCostOnPath = cost;

        // remember the longest path at this cost level
        if (!discovered){
          discovered = new ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery(entry, off);
        }
#if SE_ALLOW_SUBPATH_RANKING
        discovered->enlargeIfBetter(maxCostOnPath, path.size());
#else
        discovered->setCollapsed(path.size());
        discovered->setCost(maxCostOnPath);
#endif

        if (leaving)
            // we allow others to point at DLS end-point's _head_
            break;

        // jump to the next object on the path
        prev = at;
        at = next;
    }

    return discovered;
}
