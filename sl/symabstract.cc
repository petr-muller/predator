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

#include "config.h"
#include "symabstract.hh"
#include "symabstract_util.hh"

#include <cl/cl_msg.hh>
#include <cl/clutil.hh>
#include <cl/storage.hh>

#include "prototype.hh"
#include "symabstract.hh"
#include "symcmp.hh"
#include "symdebug.hh"
#include "symjoin.hh"
#include "symdiscover.hh"
#include "symgc.hh"
#include "symseg.hh"
#include "symutil.hh"
#include "symtrace.hh"
#include "util.hh"

#include <iomanip>
#include <set>
#include <sstream>

#include <boost/foreach.hpp>

LOCAL_DEBUG_PLOTTER(symabstract, DEBUG_SYMABSTRACT)

void debugSymAbstract(const bool enable)
{
    if (enable == __ldp_enabled_symabstract)
        return;

    CL_DEBUG("symabstract: debugSymAbstract(" << enable << ") takes effect");
    __ldp_enabled_symabstract = enable;
}

// visitor
struct UnknownValuesDuplicator {
    TObjSet ignoreList;

    bool operator()(const ObjHandle &obj) const {
        if (hasKey(ignoreList, obj))
            return /* continue */ true;

        const TValId valOld = obj.value();
        if (valOld <= 0)
            return /* continue */ true;

        SymHeapCore *sh = obj.sh();
        const EValueTarget code = sh->valTarget(valOld);
        if (isPossibleToDeref(code) || (VT_CUSTOM == code))
            return /* continue */ true;

        // duplicate unknown value
        const TValId valNew = sh->valClone(valOld);
        obj.setValue(valNew);

        return /* continue */ true;
    }
};

// when concretizing an object, we need to duplicate all _unknown_ values
void duplicateUnknownValues(SymHeap &sh, TValId at)
{
    UnknownValuesDuplicator visitor;
    buildIgnoreList(visitor.ignoreList, sh, at);

    // traverse all sub-objects
    traverseLiveObjs(sh, at, visitor);
}

void detachClonedPrototype(
        SymHeap                 &sh,
        const TValId            proto,
        const TValId            clone,
        const TValId            rootDst,
        const TValId            rootSrc,
        const bool              uplink)
{
    const bool isRootDls = (OK_DLS == sh.valTargetKind(rootDst));
    CL_BREAK_IF(isRootDls && (OK_DLS != sh.valTargetKind(rootSrc)));

    TValId rootDstPeer = VAL_INVALID;
    TValId rootSrcPeer = VAL_INVALID;
    if (isRootDls) {
        rootDstPeer = dlSegPeer(sh, rootDst);
        rootSrcPeer = dlSegPeer(sh, rootSrc);
        CL_BREAK_IF(uplink && (rootDstPeer != rootSrcPeer));
    }

    redirectRefs(sh, rootDst, proto, clone);
    redirectRefs(sh, proto, rootDst, rootSrc);

    if (isRootDls) {
        if (uplink)
            redirectRefs(sh, clone, rootSrcPeer, rootDst);
        else
            redirectRefs(sh, rootDstPeer, proto, clone);
    }

    if (OK_DLS == sh.valTargetKind(proto)) {
        const TValId protoPeer = dlSegPeer(sh, proto);
        const TValId clonePeer = dlSegPeer(sh, clone);
        redirectRefs(sh, rootDst, protoPeer, clonePeer);
        redirectRefs(sh, protoPeer, rootDst, rootSrc);
        if (isRootDls && uplink)
            redirectRefs(sh, clonePeer, rootSrcPeer, rootDst);
    }
}

TValId protoClone(SymHeap &sh, const TValId proto)
{
    const TValId clone = segClone(sh, proto);
    objDecrementProtoLevel(sh, clone);

    const EValueTarget code = sh.valTarget(proto);
    if (!isAbstract(code))
        // clone all unknown values in order to keep prover working
        duplicateUnknownValues(sh, clone);

    return clone;
}

void clonePrototypes(
        SymHeap                &sh,
        const TValId            rootDst,
        const TValId            rootSrc,
        const TValList         &protoList)
{
    // allocate some space for clone IDs
    const unsigned cnt = protoList.size();
    TValList cloneList(cnt);

    // clone the prototypes and reconnect them to the new root
    for (unsigned i = 0; i < cnt; ++i) {
        const TValId proto = protoList[i];
        const TValId clone = protoClone(sh, protoList[i]);
        detachClonedPrototype(sh, proto, clone, rootDst, rootSrc,
                /* uplink */ true);

        cloneList[i] = clone;
    }

    // FIXME: works, but likely to kill the CPU
    for (unsigned i = 0; i < cnt; ++i) {
        const TValId proto = protoList[i];
        const TValId clone = cloneList[i];

        for (unsigned j = 0; j < cnt; ++j) {
            if (i == j)
                continue;

            const TValId otherProto = protoList[j];
            const TValId otherClone = cloneList[j];
            detachClonedPrototype(sh, proto, clone, otherClone, otherProto,
                    /* uplink */ false);
        }
    }
}

// FIXME: the semantics of this function is quite contra-intuitive
TValId segDeepCopy(SymHeap &sh, TValId seg)
{
    // collect the list of prototypes
    TValList protoList;
    collectPrototypesOf(protoList, sh, seg, /* skipDlsPeers */ true);

    // clone the root itself
    const TValId dup = objClone(sh, seg);

    // clone all unknown values in order to keep prover working
    duplicateUnknownValues(sh, dup);

    // clone all prototypes originally owned by seg
    clonePrototypes(sh, seg, dup, protoList);

    return dup;
}

bool dispatchAbstractionAttempt(SymHeap &sh, AbstractionDiscovery *discovery){
  switch (discovery->getType()) {
    case ABSTRACTION_LIST: return ListAbstractionAbstractNamespace::attemptAbstraction(sh, discovery);
    default: CL_TRAP; return false;
  }
}

void dlSegReplaceByConcrete(SymHeap &sh, TValId seg, TValId peer)
{
    LDP_INIT(symabstract, "dlSegReplaceByConcrete");
    LDP_PLOT(symabstract, sh);
    CL_BREAK_IF(!dlSegCheckConsistency(sh));
    CL_BREAK_IF(!protoCheckConsistency(sh));

    // take the value of 'next' pointer from peer
    const PtrHandle peerPtr = prevPtrFromSeg(sh, seg);
    const TValId valNext = nextValFromSeg(sh, peer);
    peerPtr.setValue(valNext);

    // redirect all references originally pointing to peer to the current object
    redirectRefs(sh,
            /* pointingFrom */ VAL_INVALID,
            /* pointingTo   */ peer,
            /* redirectTo   */ seg);

    // destroy peer, including all prototypes
    REQUIRE_GC_ACTIVITY(sh, peer, dlSegReplaceByConcrete);

    // concretize self
    sh.valTargetSetConcrete(seg);
    LDP_PLOT(symabstract, sh);
    CL_BREAK_IF(!dlSegCheckConsistency(sh));
    CL_BREAK_IF(!protoCheckConsistency(sh));
}

void spliceOutListSegment(
        SymHeap                &sh,
        const TValId            seg,
        const TValId            peer,
        const TValId            valNext,
        TValList               *leakList)
{
    LDP_INIT(symabstract, "spliceOutListSegment");
    LDP_PLOT(symabstract, sh);

    CL_BREAK_IF(objMinLength(sh, seg));

    TOffset offHead = 0;
    if (OK_OBJ_OR_NULL != sh.valTargetKind(seg))
        offHead = sh.segBinding(seg).head;

    if (OK_DLS == sh.valTargetKind(seg)) {
        // OK_DLS --> unlink peer
        CL_BREAK_IF(seg == peer);
        CL_BREAK_IF(offHead != sh.segBinding(peer).head);
        const TValId valPrev = nextValFromSeg(sh, seg);
        redirectRefs(sh,
                /* pointingFrom */ VAL_INVALID,
                /* pointingTo   */ peer,
                /* redirectTo   */ valPrev,
                /* offHead      */ offHead);
    }

    // unlink self
    redirectRefs(sh,
            /* pointingFrom */ VAL_INVALID,
            /* pointingTo   */ seg,
            /* redirectTo   */ valNext,
            /* offHead      */ offHead);

    collectSharedJunk(sh, seg, leakList);

    // destroy peer in case of DLS
    if (peer != seg && collectJunk(sh, peer))
        CL_DEBUG("spliceOutListSegment() drops a sub-heap (peer)");

    // destroy self, including all nested prototypes
    if (collectJunk(sh, seg))
        CL_DEBUG("spliceOutListSegment() drops a sub-heap (seg)");

    LDP_PLOT(symabstract, sh);
}

void spliceOutSegmentIfNeeded(
        TMinLen                *pLen,
        SymHeap                &sh,
        const TValId            seg,
        const TValId            peer,
        TSymHeapList           &todo,
        TValList               *leakList)
{
    if (!*pLen) {
        // possibly empty LS
        SymHeap sh0(sh);
        const TValId valNext = nextValFromSeg(sh0, peer);
        spliceOutListSegment(sh0, seg, peer, valNext, leakList);

        // append a trace node for this operation
        Trace::Node *tr = new Trace::SpliceOutNode(sh.traceNode());

        todo.push_back(sh0);
        todo.back().traceUpdate(tr);
    }
    else
        // we are going to detach one node
        --(*pLen);

    LDP_INIT(symabstract, "concretizeObj");
    LDP_PLOT(symabstract, sh);
}

void abstractIfNeeded(SymHeap &sh)
{
#if SE_DISABLE_SLS && SE_DISABLE_DLS
    return;
#endif
    AbstractionDiscovery *hint = NULL;

    while ((hint = discoverBestAbstraction(sh))) {
        if (!dispatchAbstractionAttempt(sh, hint))
            // the best abstraction given is unfortunately not good enough
            break;

        // some part of the symbolic heap has just been successfully abstracted,
        // let's look if there remains anything else suitable for abstraction
    }
}

void concretizeObj(
        SymHeap                     &sh,
        const TValId                 addr,
        TSymHeapList                &todo,
        TValList                    *leakList)
{
    CL_BREAK_IF(!protoCheckConsistency(sh));

    const TValId seg = sh.valRoot(addr);
    const TValId peer = segPeer(sh, seg);

    // handle the possibly empty variant (if exists)
    TMinLen len = sh.segMinLength(seg);
    spliceOutSegmentIfNeeded(&len, sh, seg, peer, todo, leakList);

    const EObjKind kind = sh.valTargetKind(seg);
    sh.traceUpdate(new Trace::ConcretizationNode(sh.traceNode(), kind));

    if (isMayExistObj(kind)) {
        // these kinds are much easier than regular list segments
        sh.valTargetSetConcrete(seg);
        decrementProtoLevel(sh, seg);
        LDP_PLOT(symabstract, sh);
        CL_BREAK_IF(!protoCheckConsistency(sh));
        return;
    }

    // duplicate self as abstract object (including all prototypes)
    const TValId dup = segDeepCopy(sh, seg);

    // resolve the relative placement of the 'next' pointer
    const BindingOff off = sh.segBinding(seg);
    const TOffset offNext = (OK_SLS == kind)
        ? off.next
        : off.prev;

    // concretize self
    sh.valTargetSetConcrete(seg);

    // update 'next' pointer
    const PtrHandle nextPtr(sh, sh.valByOffset(seg, offNext));
    const TValId dupHead = segHeadAt(sh, dup);
    nextPtr.setValue(dupHead);

    if (OK_DLS == kind) {
        // update 'prev' pointer going from peer to the cloned object
        const PtrHandle prev1 = prevPtrFromSeg(sh, peer);
        prev1.setValue(dupHead);

        // update 'prev' pointer going from the cloned object to the concrete
        const PtrHandle prev2(sh, sh.valByOffset(dup, off.next));
        const TValId headAddr = sh.valByOffset(seg, off.head);
        prev2.setValue(headAddr);

        CL_BREAK_IF(!dlSegCheckConsistency(sh));
    }

    // if there was a self loop from 'next' to the segment itself, recover it
    const PtrHandle nextNextPtr = nextPtrFromSeg(sh, dup);
    const TValId nextNextVal = nextNextPtr.value();
    const TValId nextNextRoot = sh.valRoot(nextNextVal);
    if (nextNextRoot == dup)
        // FIXME: we should do this also the other way around for OK_DLS
        nextNextPtr.setValue(sh.valByOffset(seg, sh.valOffset(nextNextVal)));

    sh.segSetMinLength(dup, len);

    LDP_PLOT(symabstract, sh);

    CL_BREAK_IF(!protoCheckConsistency(sh));
}
