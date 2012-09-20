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

#include "symabstract_list.hh"
#include "symabstract_util.hh"

#include <cl/cl_msg.hh>
#include "symdebug.hh"
#include "symtrace.hh"
#include "symseg.hh"
#include "symjoin.hh"
#include "symgc.hh"
#include "prototype.hh"

LOCAL_DEBUG_PLOTTER(symabstract_list, DEBUG_SYMABSTRACT)

void enlargeMayExist(SymHeap &sh, const TValId at)
{
    const EObjKind kind = sh.valTargetKind(at);
    if (!isMayExistObj(kind))
        return;

    decrementProtoLevel(sh, at);
    sh.valTargetSetConcrete(at);
}

struct ValueSynchronizer {
    SymHeap            &sh;
    TObjSet             ignoreList;

    ValueSynchronizer(SymHeap &sh_): sh(sh_) { }

    bool operator()(ObjHandle item[2]) const {
        const ObjHandle &src = item[0];
        const ObjHandle &dst = item[1];
        if (hasKey(ignoreList, src))
            return /* continue */ true;

        // store value of 'src' into 'dst'
        const TValId valSrc = src.value();
        const TValId valDst = dst.value();
        dst.setValue(valSrc);

        // if the last reference is gone, we have a problem
        const TValId rootDst = sh.valRoot(valDst);
        if (collectJunk(sh, rootDst))
            CL_DEBUG("    ValueSynchronizer drops a sub-heap (dlSegPeerData)");

        return /* continue */ true;
    }
};

void dlSegSyncPeerData(SymHeap &sh, const TValId dls)
{
    const TValId peer = dlSegPeer(sh, dls);
    ValueSynchronizer visitor(sh);
    buildIgnoreList(visitor.ignoreList, sh, dls);

    // if there was "a pointer to self", it should remain "a pointer to self";
    ObjList refs;
    sh.pointedBy(refs, dls);
    BOOST_FOREACH(const ObjHandle &obj, refs) {
        visitor.ignoreList.insert(obj);
    }

    const TValId roots[] = { dls, peer };
    traverseLiveObjs<2>(sh, roots, visitor);
}

void slSegAbstractionStep(
        SymHeap                     &sh,
        const TValId                at,
        const TValId                nextAt,
        const BindingOff            &off)
{
    // compute the resulting minimal length
    const TMinLen len = objMinLength(sh, at) + objMinLength(sh, nextAt);

    enlargeMayExist(sh, at);
    enlargeMayExist(sh, nextAt);

    // merge data
    joinData(sh, off, nextAt, at, /* bidir */ false);

    if (OK_SLS != sh.valTargetKind(nextAt))
        // abstract the _next_ object
        sh.valTargetSetAbstract(nextAt, OK_SLS, off);

    // replace all references to 'head'
    const TOffset offHead = sh.segBinding(nextAt).head;
    const TValId headAt = sh.valByOffset(at, offHead);
    sh.valReplace(headAt, segHeadAt(sh, nextAt));

    // destroy self, including all prototypes
    REQUIRE_GC_ACTIVITY(sh, at, slSegAbstractionStep);

    if (len)
        // declare resulting segment's minimal length
        sh.segSetMinLength(nextAt, len);
}

void dlSegCreate(SymHeap &sh, TValId a1, TValId a2, BindingOff off)
{
    // compute resulting segment's length
    const TMinLen len = objMinLength(sh, a1) + objMinLength(sh, a2);

    // OK_SEE_THROUGH -> OK_CONCRETE if necessary
    enlargeMayExist(sh, a1);
    enlargeMayExist(sh, a2);

    // merge data
    joinData(sh, off, a2, a1, /* bidir */ true);

    swapValues(off.next, off.prev);
    sh.valTargetSetAbstract(a1, OK_DLS, off);

    swapValues(off.next, off.prev);
    sh.valTargetSetAbstract(a2, OK_DLS, off);

    // just created DLS is said to be 2+ as long as no OK_SEE_THROUGH are involved
    sh.segSetMinLength(a1, len);
}

void dlSegGobble(SymHeap &sh, TValId dls, TValId var, bool backward)
{
    CL_BREAK_IF(OK_DLS != sh.valTargetKind(dls));

    // compute the resulting minimal length
    const TMinLen len = sh.segMinLength(dls) + objMinLength(sh, var);

    // we allow to gobble OK_SEE_THROUGH objects (if compatible)
    enlargeMayExist(sh, var);

    if (!backward)
        // jump to peer
        dls = dlSegPeer(sh, dls);

    // merge data
    const BindingOff &off = sh.segBinding(dls);
    joinData(sh, off, dls, var, /* bidir */ false);
    dlSegSyncPeerData(sh, dls);

    // store the pointer DLS -> VAR
    const PtrHandle nextPtr(sh, sh.valByOffset(dls, off.next));
    const TValId valNext = valOfPtrAt(sh, var, off.next);
    nextPtr.setValue(valNext);

    // replace VAR by DLS
    const TValId headAt = sh.valByOffset(var, off.head);
    sh.valReplace(headAt, segHeadAt(sh, dls));
    REQUIRE_GC_ACTIVITY(sh, var, dlSegGobble);

    // handle DLS Neq predicates
    sh.segSetMinLength(dls, len);

    dlSegSyncPeerData(sh, dls);
}

void dlSegMerge(SymHeap &sh, TValId seg1, TValId seg2)
{
    // compute the resulting minimal length
    const TMinLen len = sh.segMinLength(seg1) + sh.segMinLength(seg2);

    // dig peers
    const TValId peer1 = dlSegPeer(sh, seg1);
    const TValId peer2 = dlSegPeer(sh, seg2);

    // check for a failure of segDiscover()
    CL_BREAK_IF(sh.segBinding(seg1) != sh.segBinding(seg2));
    CL_BREAK_IF(nextValFromSeg(sh, peer1) != segHeadAt(sh, seg2));

    // merge data
    const BindingOff &bf2 = sh.segBinding(seg2);
    joinData(sh, bf2, seg2, seg1, /* bidir */ true);

    // preserve backLink
    const TValId valNext1 = nextValFromSeg(sh, seg1);
    const PtrHandle ptrNext2 = nextPtrFromSeg(sh, seg2);
    ptrNext2.setValue(valNext1);

    // replace both parts point-wise
    const TValId headAt = segHeadAt(sh,  seg1);
    const TValId peerAt = segHeadAt(sh, peer1);

    sh.valReplace(headAt, segHeadAt(sh,  seg2));
    sh.valReplace(peerAt, segHeadAt(sh, peer2));

    // destroy headAt and peerAt, including all prototypes -- either at once, or
    // one by one (depending on the shape of subgraph)
    REQUIRE_GC_ACTIVITY(sh, seg1, dlSegMerge);
    if (!collectJunk(sh, peer1) && isPossibleToDeref(sh.valTarget(peer1))) {
        CL_ERROR("dlSegMerge() failed to collect garbage"
                 ", peer1 still referenced");
        CL_BREAK_IF("collectJunk() has not been successful");
    }

    if (len)
        // handle DLS Neq predicates
        sh.segSetMinLength(seg2, len);

    dlSegSyncPeerData(sh, seg2);
}

bool /* jump next */ dlSegAbstractionStep(
        SymHeap                     &sh,
        const TValId                at,
        const TValId                next,
        const BindingOff            &off)
{
    const EObjKind kind = sh.valTargetKind(at);
    const EObjKind kindNext = sh.valTargetKind(next);
    CL_BREAK_IF(OK_SLS == kind || OK_SLS == kindNext);

    if (OK_DLS == kindNext) {
        if (OK_DLS == kind)
            // DLS + DLS
            dlSegMerge(sh, at, next);
        else
            // CONCRETE + DLS
            dlSegGobble(sh, next, at, /* backward */ true);

        return /* jump next */ true;
    }
    else {
        if (OK_DLS == kind)
            // DLS + CONCRETE
            dlSegGobble(sh, at, next, /* backward */ false);
        else
            // CONCRETE + CONCRETE
            dlSegCreate(sh, at, next, off);

        return /* nobody moves */ false;
    }
}

bool segAbstractionStep(
        SymHeap                     &sh,
        const BindingOff            &off,
        TValId                      *pCursor)
{
    const TValId at = *pCursor;

    // jump to peer in case of DLS
    TValId peer = at;
    if (OK_DLS == sh.valTargetKind(at))
        peer = dlSegPeer(sh, at);

    // jump to the next object (as we know such an object exists)
    const TValId next = nextRootObj(sh, peer, off.next);

    // check wheter he upcoming abstraction step is still doable
    EJoinStatus status;
    if (!joinDataReadOnly(&status, sh, off, at, next, 0))
        return false;

    if (isDlsBinding(off)) {
        // DLS
        CL_BREAK_IF(!dlSegCheckConsistency(sh));
        const bool jumpNext = dlSegAbstractionStep(sh, at, next, off);
        CL_BREAK_IF(!dlSegCheckConsistency(sh));
        if (!jumpNext)
            // stay in place
            return true;
    }
    else {
        // SLS
        slSegAbstractionStep(sh, at, next, off);
    }

    // move the cursor one step forward
    *pCursor = next;
    return true;
}


bool ListAbstractionAbstractNamespace::attemptAbstraction(SymHeap &sh, AbstractionDiscovery *discovery){
  ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery *list_discovery = static_cast<ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery*>(discovery);

    CL_DEBUG("    AAA initiating " << list_discovery->getName() << " abstraction of length " << list_discovery->getCollapsed());
    // cursor
    TValId cursor = discovery->getEntry();

    LDP_INIT(symabstract_list, list_discovery->getName());
    LDP_PLOT(symabstract_list, sh);

    for (unsigned i = 0; i < list_discovery->getCollapsed(); ++i) {
        CL_BREAK_IF(!protoCheckConsistency(sh));

        if (!segAbstractionStep(sh, list_discovery->getBinding(), &cursor)) {
            CL_DEBUG("<-- validity of next " << (list_discovery->getCollapsed() - i - 1)
                    << " abstraction step(s) broken, forcing re-discovery...");

            if (i)
                return true;

            CL_BREAK_IF("segAbstractionStep() failed, nothing has been done");
            return false;
        }

        Trace::Node *trAbs = new Trace::AbstractionNode(sh.traceNode(), discovery->getKind());
        sh.traceUpdate(trAbs);

        LDP_PLOT(symabstract_list, sh);

        CL_BREAK_IF(!protoCheckConsistency(sh));
    }

    CL_DEBUG("<-- successfully abstracted " << discovery->getName());
    return true;
}
