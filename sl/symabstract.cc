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

#include <cl/cl_msg.hh>
#include <cl/storage.hh>

#include "symgc.hh"
#include "symseg.hh"
#include "symutil.hh"
#include "util.hh"

#include <set>
#include <stack>

#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

#ifndef GC_ADMIT_LINUX_LISTS
#   define GC_ADMIT_LINUX_LISTS 0
#endif

#ifndef SE_DISABLE_DLS
#   define SE_DISABLE_DLS 0
#endif

#ifndef SE_DISABLE_SLS
#   define SE_DISABLE_SLS 0
#endif

/// common configuration template for abstraction triggering
struct AbstractionThreshold {
    unsigned sparePrefix;
    unsigned innerSegLen;
    unsigned spareSuffix;
};

/// abstraction trigger threshold for SLS
static struct AbstractionThreshold slsThreshold = {
    /* sparePrefix */ 1,
    /* innerSegLen */ 1,
    /* spareSuffix */ 0
};

/// abstraction trigger threshold for DLS
static struct AbstractionThreshold dlsThreshold = {
    /* sparePrefix */ 1,
    /* innerSegLen */ 1,
    /* spareSuffix */ 1
};

namespace {

// we use a controlled recursion of depth 1
void flatScan(SymHeap &sh, EObjKind kind, TObjId obj);

bool doesAnyonePointToInsideVisitor(const SymHeap &sh, TObjId sub) {
    const TValueId subAddr = sh.placedAt(sub);
    return /* continue */ !sh.usedByCount(subAddr);
}

bool doesAnyonePointToInside(const SymHeap &sh, TObjId obj) {
    return !traverseSubObjs(sh, obj, doesAnyonePointToInsideVisitor);
}

void dlSegHandleCrossNeq(SymHeap &sh, TObjId dls, SymHeap::ENeqOp op) {
    const TObjId peer = dlSegPeer(sh, dls);
    const TObjId next = nextPtrFromSeg(sh, peer);
    const TValueId valNext = sh.valueOf(next);
    const TValueId valSelf = sh.placedAt(dls);

    sh.neqOp(op, valSelf, valNext);
}

TValueId /* addr */ segClone(SymHeap &sh, TValueId atAddr) {
    const TObjId seg = sh.pointsTo(atAddr);
    const TObjId dupSeg = sh.objDup(seg);

    if (OK_DLS == sh.objKind(seg)) {
        // we need to clone the peer as well
        const TObjId peer = dlSegPeer(sh, seg);
        const TObjId dupPeer = sh.objDup(peer);

        // dig the 'peer' selectors of the cloned objects
        const TFieldIdxChain icpSeg  = sh.objBindingField(BF_PEER, dupSeg);
        const TFieldIdxChain icpPeer = sh.objBindingField(BF_PEER, dupPeer);

        // resolve selectors -> sub-objects
        const TObjId ppSeg  = subObjByChain(sh, dupSeg , icpSeg);
        const TObjId ppPeer = subObjByChain(sh, dupPeer, icpPeer);

        // now cross the 'peer' pointers
        sh.objSetValue(ppSeg, sh.placedAt(dupPeer));
        sh.objSetValue(ppPeer, sh.placedAt(dupSeg));
    }

    return sh.placedAt(dupSeg);
}

void considerFlatScan(SymHeap &sh, TObjId obj) {
    const TValueId addr = sh.placedAt(obj);
    const unsigned cnt = sh.usedByCount(addr);
    switch (cnt) {
        case 0:
#if GC_ADMIT_LINUX_LISTS
            if (!doesAnyonePointToInside(sh, obj))
#endif
                CL_WARN("considerFlatScan() encountered an unused root "
                        "object #" << obj);
            break;

        case 1:
            // we use a controlled recursion of depth 1
            flatScan(sh, OK_SLS, obj);
            break;

        case 2:
            // we use a controlled recursion of depth 1
            flatScan(sh, OK_DLS, obj);
            break;
    }
}

void considerFlatScan(SymHeap &sh, TValueId val1, TValueId val2) {
    const EUnknownValue code1 = sh.valGetUnknown(val1);
    if (UV_KNOWN != code1 && UV_ABSTRACT != code1)
        // too fuzzy for us
        return;

    const EUnknownValue code2 = sh.valGetUnknown(val2);
    if (UV_KNOWN != code2 && UV_ABSTRACT != code2)
        // too fuzzy for us
        return;

    const TObjId o1 = sh.pointsTo(val1);
    const TObjId o2 = sh.pointsTo(val2);
    if (o1 <= 0 || o2 <= 0)
        // there is no valid target --> nothing to abstract actually
        return;

    considerFlatScan(sh, o1);
    considerFlatScan(sh, o2);
}

template <class TIgnoreList>
void buildIgnoreList(const SymHeap &sh, TObjId obj, TIgnoreList &ignoreList) {
    TObjId tmp;

    const EObjKind kind = sh.objKind(obj);
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of buildIgnoreList()
            TRAP;

        case OK_DLS:
            // preserve 'peer' field
            tmp = subObjByChain(sh, obj, sh.objBindingField(BF_PEER, obj));
            ignoreList.insert(tmp);
            // fall through!

        case OK_SLS:
            // preserve 'next' field
            tmp = subObjByChain(sh, obj, sh.objBindingField(BF_NEXT, obj));
            ignoreList.insert(tmp);
    }
}

struct DataMatchVisitor {
    std::set<TObjId> ignoreList;

    bool operator()(const SymHeap &sh, TObjPair item) const {
        const TObjId o1 = item.first;
        if (hasKey(ignoreList, o1))
            return /* continue */ true;

        // first compare value IDs
        const TValueId v1 = sh.valueOf(o1);
        const TValueId v2 = sh.valueOf(item.second);
        if (v1 == v2)
            return /* continue */ true;

        // special values have to match
        if (v1 <= 0 || v2 <= 0)
            return /* mismatch */ false;

        // compare _unknown_ value codes
        const EUnknownValue code = sh.valGetUnknown(v1);
        if (code != sh.valGetUnknown(v2))
            return /* mismatch */ false;

        switch (code) {
            case UV_UNINITIALIZED:
                // basically the asme as UV_KNOWN
            case UV_KNOWN:
                // known values have to match
                return false;

            case UV_UNKNOWN:
                // safe to keep UV_UNKNOWN values as they are
                return true;

            case UV_ABSTRACT:
                TRAP;
        }
        return /* mismatch */ false;
    }
};

bool segEqual(const SymHeap &sh, TValueId v1, TValueId v2) {
    const TObjId o1 = sh.pointsTo(v1);
    const TObjId o2 = sh.pointsTo(v2);
    if (o1 <= 0 || o2 <= 0)
        TRAP;

    const EObjKind kind = sh.objKind(o1);
    if (sh.objKind(o2) != kind)
        return false;

    TObjId peer1 = o1;
    TObjId peer2 = o2;
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of segEqual()
            TRAP;

        case OK_DLS:
            if (sh.objBindingField(BF_PEER, o1)
                    != sh.objBindingField(BF_PEER, o2))
                // 'peer' selector mismatch
                return false;

            peer1 = dlSegPeer(sh, o1);
            peer2 = dlSegPeer(sh, o2);
            // fall through!

        case OK_SLS:
            if (sh.objBindingField(BF_NEXT, o1)
                    != sh.objBindingField(BF_NEXT, o2))
                // 'next' selector mismatch
                return false;
    }

    // so far equal, now compare the 'next' values
    const TObjId next1 = nextPtrFromSeg(sh, peer1);
    const TObjId next2 = nextPtrFromSeg(sh, peer2);
    if (sh.valueOf(next1) != sh.valueOf(next2))
        return false;

    // compare the data
    DataMatchVisitor visitor;
    buildIgnoreList(sh, o1, visitor.ignoreList);
    const TObjPair item(o1, o2);
    return traverseSubObjs(sh, item, visitor);
}

bool segMayBePrototype(const SymHeap &sh, const TValueId segAt, bool refByDls) {
    const TObjId seg = sh.pointsTo(segAt);
    TObjId peer = seg;
    TObjId nextPtr;
    TValueId addr;

    const EObjKind kind = sh.objKind(seg);
    switch (kind) {
        case OK_CONCRETE:
            // concrete objects are not supported as prototypes now
            TRAP;
            return false;

        case OK_SLS:
            if ((1U + refByDls) != sh.usedByCount(segAt)) {
                CL_WARN("head is referenced, refusing SLS as prototype");
                return false;
            }
            break;

        case OK_DLS:
            if ((2U + refByDls) != sh.usedByCount(segAt)) {
                CL_WARN("head is referenced, refusing DLS as prototype");
                return false;
            }
            peer = dlSegPeer(sh, seg);
            addr = sh.placedAt(peer);
            if (1 != sh.usedByCount(addr)) {
                CL_WARN("tail is referenced, refusing DLS as prototype");
                return false;
            }
            nextPtr = nextPtrFromSeg(sh, peer);
            if (VAL_NULL != sh.valueOf(nextPtr)) {
                CL_WARN("next-link is not NULL, refusing DLS as prototype");
                return false;
            }
            break;
    }

    nextPtr = nextPtrFromSeg(sh, seg);
    if (VAL_NULL != sh.valueOf(nextPtr)) {
        CL_WARN("out-link is not NULL, refusing segment as prototype");
        return false;
    }

    return true;
}

TValueId mergeValues(SymHeap &sh, TValueId v1, TValueId v2,
                     bool srcRefByDls, bool dstRefByDls)
{
    if (v1 == v2)
        return v1;

    // attempt to dig some type-info for the new unknown value
    const struct cl_type *clt1 = sh.valType(v1);
    const struct cl_type *clt2 = sh.valType(v2);
    if (clt1 && clt2 && clt1 != clt2)
        // should be safe to ignore
        TRAP;

    // if we know type of at least one of the values, use it
    const struct cl_type *clt = (clt1)
        ? clt1
        : clt2;

    // if the types of _unknown_ values are compatible, it should be safe to
    // pass it through;  UV_UNKNOWN otherwise
    const EUnknownValue code1 = sh.valGetUnknown(v1);
    const EUnknownValue code2 = sh.valGetUnknown(v2);
    EUnknownValue code = (code1 != UV_KNOWN && code1 == code2)
        ? code1
        : UV_UNKNOWN;

    if (UV_ABSTRACT == code) {
        CL_WARN("support for nested segments is not well tested yet");
        if (segEqual(sh, v1, v2)
                && segMayBePrototype(sh, v1, srcRefByDls)
                && segMayBePrototype(sh, v2, dstRefByDls))
        {
            // by merging the values, we drop the last reference;  destroy the seg
            const TObjId seg1 = sh.pointsTo(v1);
            segDestroy(sh, seg1);

            // duplicate the nested abstract object on call of concretizeObj()
            const TObjId seg2 = sh.pointsTo(v2);
            sh.objSetShared(seg2, false);

            return v2;
        }
        else
            // segments are not equal, no chance to merge them
            code = UV_UNKNOWN;
    }

    return sh.valCreateUnknown(code, clt);
}

// visitor
struct ValueAbstractor {
    std::set<TObjId>    ignoreList;
    bool                srcIsDlSeg;
    bool                dstIsDlSeg;
    bool                flatScan;
    bool                bidir;

    bool operator()(SymHeap &sh, TObjPair item) const {
        const TObjId src = item.first;
        const TObjId dst = item.second;
        if (hasKey(ignoreList, dst))
            return /* continue */ true;

        TValueId valSrc = sh.valueOf(item.first);
        TValueId valDst = sh.valueOf(dst);
        bool eq;
        if (sh.proveEq(&eq, valSrc, valDst) && eq)
            // values are equal
            return /* continue */ true;

        if (!this->flatScan) {
            // prepare any nested abstractions for collapsing eventually
            considerFlatScan(sh, valSrc, valDst);
            valSrc = sh.valueOf(src);
            valDst = sh.valueOf(dst);
        }

        // create a new unknown value as a placeholder
        const TValueId valNew = mergeValues(sh, valSrc, valDst,
                                            this->srcIsDlSeg, this->dstIsDlSeg);
        sh.objSetValue(dst, valNew);
        if (this->bidir)
            sh.objSetValue(src, valNew);

        // if the last reference is gone, we have a problem
        if (collectJunk(sh, valDst))
            CL_ERROR("junk detected during abstraction"
                    ", the analysis is no more sound!");

        return /* continue */ true;
    }
};

// visitor
struct UnknownValuesDuplicator {
    std::set<TObjId> ignoreList;

    bool operator()(SymHeap &sh, TObjId obj) const {
        if (hasKey(ignoreList, obj))
            return /* continue */ true;

        const TValueId valOld = sh.valueOf(obj);
        if (valOld <= 0)
            return /* continue */ true;

        // branch by _unknown_ value type
        TValueId valNew = VAL_INVALID;
        const EUnknownValue code = sh.valGetUnknown(valOld);
        switch (code) {
            case UV_KNOWN:
                // we can keep known values as they are (shared data)
                break;

            case UV_ABSTRACT:
                if (!sh.objShared(sh.pointsTo(valOld)))
                    // we need to clone nested list segments
                    valNew = segClone(sh, valOld);
                break;

            default:
                valNew = sh.valDuplicateUnknown(valOld);
        }

        // duplicate any unknown value
        if (VAL_INVALID != valNew)
            sh.objSetValue(obj, valNew);

        return /* continue */ true;
    }
};

// when abstracting an object, we need to abstract all non-matching values in
void abstractNonMatchingValues(SymHeap &sh, TObjId src, TObjId dst,
                               bool flatScan, bool bidir = false,
                               bool fresh = false)
{
    ValueAbstractor visitor;
    visitor.srcIsDlSeg  = (!fresh && OK_DLS == sh.objKind(src));
    visitor.dstIsDlSeg  = (!fresh && OK_DLS == sh.objKind(dst));
    visitor.flatScan    = flatScan;
    visitor.bidir       = bidir;
    buildIgnoreList(sh, dst, visitor.ignoreList);

    // traverse all sub-objects
    const TObjPair item(src, dst);
    traverseSubObjs(sh, item, visitor);
}

// when concretizing an object, we need to duplicate all _unknown_ values
void duplicateUnknownValues(SymHeap &sh, TObjId obj) {
    UnknownValuesDuplicator visitor;
    buildIgnoreList(sh, obj, visitor.ignoreList);

    // traverse all sub-objects
    traverseSubObjs(sh, obj, visitor);
}

class ProbeVisitor {
    private:
        TValueId                addr_;
        const struct cl_type    *clt_;
        unsigned                arrity_;
        TFieldIdxChain          icNext_;

        bool dlSegEndCandidate(const SymHeap &sh, TObjId obj) const {
            if (OK_DLS != static_cast<EObjKind>(arrity_) || icNext_.empty())
                // we are not looking for a DLS either
                return false;

            const TObjId root = sh.pointsTo(addr_);
            TObjId nextPtr = subObjByChain(sh, root, icNext_);
            obj = sh.pointsTo(sh.valueOf(nextPtr));
            if (obj <= 0)
                // no valid next object
                return false;

            // if the next object has zero as 'next', give it a chance to go
            nextPtr = subObjByChain(sh, obj, icNext_);
            return (VAL_NULL == sh.valueOf(nextPtr));
        }

    public:
        ProbeVisitor(const SymHeap &sh, TObjId root, EObjKind kind,
                     TFieldIdxChain icNext = TFieldIdxChain()):
            icNext_(icNext)
        {
            addr_ = sh.placedAt(root);
            clt_  = sh.objType(root);
            if (!addr_ || !clt_ || CL_TYPE_STRUCT != clt_->code)
                TRAP;

            arrity_ = static_cast<unsigned>(kind);
            if (!arrity_)
                TRAP;
        }

    bool operator()(const SymHeap &sh, TObjId obj) const {
        const TValueId valNext = sh.valueOf(obj);
        if (valNext <= 0 || valNext == addr_ || sh.valType(valNext) != clt_)
            return /* continue */ true;

        const EUnknownValue code = sh.valGetUnknown(valNext);
        switch (code) {
            case UV_KNOWN:
            case UV_ABSTRACT:
                // only known objects can be chained
                break;

            default:
                return /* continue */ true;
        }

        const TObjId target = sh.pointsTo(valNext);
        const TValueId targetAddr = sh.placedAt(target);
        if (targetAddr <= 0)
            // someone points to an already deleted object
            return /* continue */ true;

        if (sh.cVar(0, obj))
            // a list segment through non-heap objects basically makes no sense
            return /* continue */ true;

        // compare arrity vs. count of references
        const unsigned refs = sh.usedByCount(targetAddr);
        if (refs != arrity_
                // special quirk for DLS "end"
                && (1 != refs || !this->dlSegEndCandidate(sh, obj)))
            return /* continue */ true;

        return doesAnyonePointToInside(sh, target);
    }
};

template <class TDst>
class ProbeVisitorTopLevel {
    private:
        TDst        &heads;
        EObjKind    kind;

    public:
        ProbeVisitorTopLevel(TDst &heads_, EObjKind kind_):
            heads(heads_),
            kind(kind_)
        {
        }

        bool operator()(const SymHeap &sh, TObjId sub, TFieldIdxChain ic) {
            const struct cl_type *clt = sh.objType(sub);
            if (!clt || clt->code != CL_TYPE_STRUCT)
                return /* continue */ true;

            if (doesAnyonePointToInside(sh, sub))
                return /* continue */ true;

            const ProbeVisitor visitor(sh, sub, kind);
            if (traverseSubObjs(sh, sub, visitor))
                return /* continue */ true;

            heads.push_back(ic);
            return /* continue */ true;
        }
};

template <class TDst>
void probe(TDst &heads, const SymHeap &sh, TObjId obj, EObjKind kind) {
    // create low level visitor
    ProbeVisitorTopLevel<TDst> visitor(heads, kind);

    // try to treat the root as list head (regular lists)
    visitor(sh, obj, /* use the root */ TFieldIdxChain());

    // TODO
#if 0
    // try to look for alternative list heads (Linux lists)
    traverseSubObjsIc(sh, obj, visitor);
#endif
}

template <class TDst>
class SelectorFinder {
    private:
        TDst                    &dst_;
        const struct cl_type    *clt_;
        const ProbeVisitor      visitor_;
        const EObjKind          kind_;

    public:
        SelectorFinder(TDst &dst, const SymHeap &sh, TObjId root, EObjKind kind)
            :
            dst_(dst),
            clt_(sh.objType(root)),
            visitor_(sh, root, kind),
            kind_(kind)
        {
        }

        bool operator()(const SymHeap &sh, TObjId sub, TFieldIdxChain ic) {
            const struct cl_type *subClt = sh.objType(sub);
            const bool backLinkCandidate = (VAL_NULL == sh.valueOf(sub)
                    && subClt && subClt->code == CL_TYPE_PTR
                    && subClt->items[0].type == clt_);

            if (backLinkCandidate || !visitor_(sh, sub))
                // great, we have a candidate
                dst_.push_back(ic);

            return /* continue */ true;
        }
};

template <class TDst>
void digAnyListSelectors(TDst &dst, const SymHeap &sh, TObjId obj,
                         EObjKind kind)
{
    SelectorFinder<TDst> visitor(dst, sh, obj, kind);
    traverseSubObjsIc(sh, obj, visitor);
}

// FIXME: this function tends to be crowded
// TODO: split it somehow to some more dedicated functions
unsigned /* len */ segDiscover(const SymHeap &sh, TObjId entry, EObjKind kind,
                               TFieldIdxChain icNext, TFieldIdxChain icPrev)
{
    int dlSegsOnPath = 0;

    // we use std::set to avoid an infinite loop
    TObjId obj = entry;
    std::set<TObjId> path;
    while (!hasKey(path, obj)) {
        path.insert(obj);
        const TObjId objCurrent = obj;

        const EObjKind kindEncountered = sh.objKind(obj);
        if (OK_DLS == kindEncountered) {
            // we've hit an already existing DLS on path, let's handle it such
            if (OK_DLS != kind)
                // arrity vs. kind mismatch
                break;

            // check selectors
            const TFieldIdxChain icPeerEncountered =
                sh.objBindingField(BF_PEER, obj);
            if (icPeerEncountered != icNext && icPeerEncountered != icPrev)
                // completely incompatible DLS, it gives us no go
                break;

            // jump to peer
            skipObj(sh, &obj, sh.objBindingField(BF_PEER, obj));
            if (hasKey(path, obj))
                // we came from the wrong side this time
                break;

            path.insert(obj);
            dlSegsOnPath++;
        }

        const TObjId objPtrNext = subObjByChain(sh, obj, icNext);
        const ProbeVisitor visitor(sh, obj, kind, icNext);
        if (visitor(sh, objPtrNext))
            // we can't go further
            break;

        const TValueId valNext = sh.valueOf(objPtrNext);
        const TObjId objNext = sh.pointsTo(valNext);
        if (objNext <= 0)
            // there is no valid next object
            break;

        if (OK_DLS == kind) {
            // check the back-link
            const TValueId addrSelf = sh.placedAt(obj);
            const TObjId objBackLink = subObjByChain(sh, objNext, icPrev);
            const TValueId valBackLink = sh.valueOf(objBackLink);

            // we allow VAL_NULL as backLink in the first item of DLL
            const bool dlSegHeadCandidate =
                (objCurrent == entry && VAL_NULL == valBackLink);

            if (!dlSegHeadCandidate && valBackLink != addrSelf)
                // inappropriate back-link
                break;
        }

        obj = objNext;
    }

    // path consisting of N nodes has N-1 edges
    const unsigned rawPathLen = path.size() - 1;

    // each DLS consists of two nodes
    return rawPathLen - dlSegsOnPath;
}

template <class TSelectorList>
unsigned segDiscoverAll(const SymHeap &sh, const TObjId entry, EObjKind kind,
                        const TSelectorList &selectors, TFieldIdxChain *icNext,
                        TFieldIdxChain *icPrev)
{
    // check count of the selectors
    const unsigned cnt = selectors.size();
    CL_DEBUG("    --> found " << cnt << " list selector candidate(s)");
    if (!cnt)
        // why are we called actually?
        TRAP;

    unsigned prevMax = cnt;
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of segDiscoverAll()
            TRAP;

        case OK_SLS:
            // just choose one selector
            prevMax = /* dummy value */ 1;
            break;

        case OK_DLS:
            // we need at least 2 selectors for DLS abstraction
            if (cnt < 2)
                return /* found nothing */ 0;
    }

    // if (2 < cnt), try all possible combinations in case of DLS
    // NOTE: This may take some time...
    unsigned bestLen = 0, bestNext, bestPrev;
    for (unsigned next = 0; next < cnt; ++next) {
        for (unsigned prev = 0; prev < prevMax; ++prev) {
            if (OK_DLS == kind && next == prev)
                // we need two _distinct_ selectors for a DLS
                continue;

            const unsigned len = segDiscover(sh, entry, kind,
                                             selectors[next],
                                             selectors[prev]);
            if (!len)
                continue;

            CL_DEBUG("        --> found segment of length " << len);
            if (bestLen < len) {
                bestLen = len;
                bestNext = next;
                bestPrev = prev;
            }
        }
    }

    if (!bestLen) {
        CL_DEBUG("        <-- no segment found");
        return /* not found */ 0;
    }

    // something found
    *icNext = selectors[bestNext];
    *icPrev = selectors[bestPrev];
    return bestLen;
}

void slSegCreateIfNeeded(SymHeap &sh, TObjId obj, TFieldIdxChain icNext) {
    const EObjKind kind = sh.objKind(obj);
    switch (kind) {
        case OK_SLS:
            // already abstract, check the next pointer
            if (sh.objBindingField(BF_NEXT, obj) == icNext)
                // all OK
                return;
            // fall through!

        case OK_DLS:
            TRAP;
            // fall through!

        case OK_CONCRETE:
            break;
    }

    // abstract a concrete object
    sh.objSetAbstract(obj, OK_SLS, /* TODO */ TFieldIdxChain(), icNext);

    // we're constructing the abstract object from a concrete one --> it
    // implies non-empty LS at this point
    const TValueId addr = sh.placedAt(obj);
    const TObjId objNextPtr = subObjByChain(sh, obj, icNext);
    const TValueId valNext = sh.valueOf(objNextPtr);
    sh.neqOp(SymHeap::NEQ_ADD, addr, valNext);
}

void slSegAbstractionStep(SymHeap &sh, TObjId *pObj, TFieldIdxChain icNext,
                          bool flatScan)
{
    const TObjId objPtrNext = subObjByChain(sh, *pObj, icNext);
    const TValueId valNext = sh.valueOf(objPtrNext);
    if (valNext <= 0 || 1 != sh.usedByCount(valNext))
        // this looks like a failure of segDiscover()
        TRAP;

    // make sure the next object is abstract
    const TObjId objNext = sh.pointsTo(valNext);
    slSegCreateIfNeeded(sh, objNext, icNext);
    if (OK_SLS != sh.objKind(objNext))
        TRAP;

    // replace self by the next object
    abstractNonMatchingValues(sh, *pObj, objNext, flatScan);
    objReplace(sh, *pObj, objNext);

    // move to the next object
    *pObj = objNext;
}

void dlSegCreate(SymHeap &sh, TObjId o1, TObjId o2,
                 TFieldIdxChain icNext, TFieldIdxChain icPrev, bool flatScan)
{
    if (OK_CONCRETE != sh.objKind(o1) || OK_CONCRETE != sh.objKind(o2))
        // invalid call of dlSegCreate()
        TRAP;

    sh.objSetAbstract(o1, OK_DLS, /* TODO */ TFieldIdxChain(), icPrev, icNext);
    sh.objSetAbstract(o2, OK_DLS, /* TODO */ TFieldIdxChain(), icNext, icPrev);

    // introduce some UV_UNKNOWN values if necessary
    abstractNonMatchingValues(sh, o1, o2, flatScan, /* bidir */ true,
                              /* fresh */ true);

    // a just created DLS is said to be 2+
    const TValueId a1 = sh.placedAt(o1);
    const TValueId a2 = sh.placedAt(o2);
    sh.neqOp(SymHeap::NEQ_ADD, a1, a2);
}

void dlSegGobble(SymHeap &sh, TObjId dls, TObjId var, bool backward,
                 bool flatScan)
{
    if (OK_DLS != sh.objKind(dls) || OK_CONCRETE != sh.objKind(var))
        // invalid call of dlSegGobble()
        TRAP;

    // kill Neq if any
    // TODO: we may distinguish among 1+/2+ at this point
    dlSegHandleCrossNeq(sh, dls, SymHeap::NEQ_DEL);

    if (!backward)
        // jump to peer
        skipObj(sh, &dls, sh.objBindingField(BF_PEER, dls));

    // introduce some UV_UNKNOWN values if necessary
    abstractNonMatchingValues(sh, var, dls, flatScan);

    // store the pointer DLS -> VAR
    const TFieldIdxChain icNext = sh.objBindingField(BF_NEXT, dls);
    const TObjId dlsNextPtr = subObjByChain(sh, dls, icNext);
    const TObjId varNextPtr = subObjByChain(sh, var, icNext);
    sh.objSetValue(dlsNextPtr, sh.valueOf(varNextPtr));

    // replace VAR by DLS
    objReplace(sh, var, dls);

    // we've just added an object, the DLS can't be empty
    dlSegHandleCrossNeq(sh, dls, SymHeap::NEQ_ADD);
}

void dlSegMerge(SymHeap &sh, TObjId seg1, TObjId seg2, bool flatScan) {
    // the resulting DLS will be non-empty as long as at least one of the given
    // DLS is non-empty
    const bool ne = dlSegNotEmpty(sh, seg1) || dlSegNotEmpty(sh, seg2);
    if (ne) {
        // TODO: we may distinguish among 1+/2+ at this point
        dlSegHandleCrossNeq(sh, seg1, SymHeap::NEQ_DEL);
        dlSegHandleCrossNeq(sh, seg2, SymHeap::NEQ_DEL);
    }

    if (sh.objBindingField(BF_NEXT, seg1) != sh.objBindingField(BF_NEXT, seg2)
    || (sh.objBindingField(BF_PEER, seg1) != sh.objBindingField(BF_PEER, seg2)))
        // failure of segDiscover()?
        TRAP;

    const TObjId peer1 = dlSegPeer(sh, seg1);
    const TObjId nextPtr = nextPtrFromSeg(sh, peer1);
    const TValueId valNext = sh.valueOf(nextPtr);
    if (valNext != sh.placedAt(seg2))
        TRAP;

    const TObjId peer2 = dlSegPeer(sh, seg2);

    // introduce some UV_UNKNOWN values if necessary
    abstractNonMatchingValues(sh,  seg1,  seg2, flatScan, /* bidir */ true);
    abstractNonMatchingValues(sh, peer1, peer2, flatScan, /* bidir */ true);

    // preserve backLink
    const TValueId valNext2 = sh.valueOf(nextPtrFromSeg(sh, seg1));
    sh.objSetValue(nextPtrFromSeg(sh, seg2), valNext2);

    // replace both parts point-wise
    objReplace(sh,  seg1,  seg2);
    objReplace(sh, peer1, peer2);

    if (ne)
        // non-empty DLS
        dlSegHandleCrossNeq(sh, seg2, SymHeap::NEQ_ADD);
}

void dlSegAbstractionStep(SymHeap &sh, TObjId *pObj, TFieldIdxChain icNext,
                          TFieldIdxChain icPrev, bool flatScan)
{
    // the first object is clear
    const TObjId o1 = *pObj;

    // we'll find the next one later on
    TObjId o2 = o1;

    EObjKind kind = sh.objKind(o1);
    switch (kind) {
        case OK_SLS:
            // *** segDiscover() failure detected ***
            TRAP;

        case OK_DLS:
            // jump to peer
            o2 = dlSegPeer(sh, o2);

            // jump to the next object (as we know such an object exists)
            skipObj(sh, &o2, sh.objBindingField(BF_NEXT, o2));
            if (OK_CONCRETE == sh.objKind(o2)) {
                // DLS + VAR
                dlSegGobble(sh, o1, o2, /* backward */ false, flatScan);
                return;
            }

            // DLS + DLS
            dlSegMerge(sh, o1, o2, flatScan);
            break;

        case OK_CONCRETE:
            // jump to the next object (as we know such an object exists)
            skipObj(sh, &o2, icNext);
            if (OK_CONCRETE == sh.objKind(o2)) {
                // VAR + VAR
                dlSegCreate(sh, o1, o2, icNext, icPrev, flatScan);
                return;
            }

            // VAR + DLS
            dlSegGobble(sh, o2, o1, /* backward */ true, flatScan);
            break;
    }

    // the current object has been just consumed, move to the next one
    *pObj = o2;

    // just check if the Neq predicates work well so far (safe to remove when
    // not debugging)
    (void) dlSegNotEmpty(sh, o2);
}

bool considerSegAbstraction(SymHeap &sh, TObjId obj, EObjKind kind,
                            TFieldIdxChain icNext, TFieldIdxChain icPrev,
                            unsigned lenTotal, bool flatScan)
{
    // select the appropriate threshold for the given type of abstraction
    AbstractionThreshold at;
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of considerSegAbstraction()
            TRAP;

        case OK_SLS:
            at = slsThreshold;
            break;

        case OK_DLS:
            at = dlsThreshold;
            break;
    }

    // check whether the threshold is satisfied or not
    unsigned threshold = at.sparePrefix + at.innerSegLen + at.spareSuffix;
    if (flatScan) {
        CL_DEBUG("    FFF ignoring threshold cfg during flat scan!");
        threshold = 1;
    }

    if (lenTotal < threshold) {
        CL_DEBUG("<-- length (" << lenTotal
                << ") of the longest segment is under the threshold ("
                << threshold << ")");
        return false;
    }

    int len = lenTotal;
    if (!flatScan) {
        CL_DEBUG("    --- length of the longest segment is " << lenTotal
                << ", prefix=" << at.sparePrefix
                << ", suffix=" << at.spareSuffix);

        // handle sparePrefix/spareSuffix
        len = lenTotal - at.sparePrefix - at.spareSuffix;
        for (unsigned i = 0; i < at.sparePrefix; ++i)
            skipObj(sh, &obj, icNext);
    }

    CL_DEBUG("    AAA initiating abstraction of length " << len);

    if (OK_SLS == kind) {
        // perform SLS abstraction!
        for (int i = 0; i < len; ++i)
            slSegAbstractionStep(sh, &obj, icNext, flatScan);

        CL_DEBUG("<-- successfully abstracted SLS");
        return true;
    }
    else {
        // perform DLS abstraction!
        for (int i = 0; i < len; ++i)
            dlSegAbstractionStep(sh, &obj, icNext, icPrev, flatScan);

        CL_DEBUG("<-- successfully abstracted DLS");
        return true;
    }
}

template <class TCont, class TContHeads>
bool considerAbstraction(SymHeap &sh, EObjKind kind, TCont entries,
                         TContHeads heads, bool flatScan = false)
{
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of considerAbstraction()
            TRAP;

        case OK_SLS:
            CL_DEBUG("--> considering SLS abstraction...");
            break;

        case OK_DLS:
            CL_DEBUG("--> considering DLS abstraction...");
            break;
    }

    // go through all candidates and find the best possible abstraction
    std::vector<TFieldIdxChain> bestSelectors;
    TFieldIdxChain bestNext, bestPrev;
    TObjId bestEntry;
    unsigned bestLen = 0;

    // check how many candidates did we get
    const unsigned cnt = entries.size();
    if (heads.size() != cnt)
        TRAP;

    for (unsigned i = 0; i < cnt; ++i) {
        const TObjId obj = entries[i];
        if (!heads[i].empty())
            // TODO
            TRAP;

        // gather suitable selectors
        std::vector<TFieldIdxChain> selectors;
        digAnyListSelectors(selectors, sh, obj, kind);

        // run the LS discovering process
        TFieldIdxChain icNext, icPrev;
        const unsigned len = segDiscoverAll(sh, obj, kind, selectors,
                                            &icNext, &icPrev);

        if (len <= bestLen)
            continue;

        bestLen         = len;
        bestEntry       = obj;
        bestSelectors   = selectors;
        bestNext        = icNext;
        bestPrev        = icPrev;
    }
    if (!bestLen) {
        CL_DEBUG("<-- nothing useful found");
        return false;
    }

    // consider abstraction threshold and trigger the abstraction eventually
    return considerSegAbstraction(sh, bestEntry, kind, bestNext, bestPrev,
                                  bestLen, flatScan);
}

void flatScan(SymHeap &sh, EObjKind kind, TObjId obj) {
    std::vector<TFieldIdxChain> heads;
    probe(heads, sh, obj, kind);

    SymHeapCore::TContObj cont;
    BOOST_FOREACH(const TFieldIdxChain icHead, heads) {
        cont.push_back(obj);
    }

    considerAbstraction(sh, kind, cont, heads, /* flatScan */ true);
}

template <class TDst>
void digAnyListHeads(TDst &heads, const SymHeap &sh, TObjId obj,
                     EObjKind kind)
{
    if (sh.cVar(0, obj))
        // skip static/automatic objects
        return;

    const TValueId addr = sh.placedAt(obj);
    if (VAL_INVALID == addr)
        return;

    const unsigned uses = sh.usedByCount(addr);
    if (static_cast<unsigned>(kind) != uses)
        return;

    probe(heads, sh, obj, kind);
}

bool abstractIfNeededCore(SymHeap &sh) {
    SymHeapCore::TContObj slSegEntries;
    SymHeapCore::TContObj dlSegEntries;

    std::vector<TFieldIdxChain> slSegHeads;
    std::vector<TFieldIdxChain> dlSegHeads;

    // collect all possible SLS/DLS entries
    SymHeapCore::TContObj roots;
    sh.gatherRootObjs(roots);
    BOOST_FOREACH(const TObjId obj, roots) {
        // look for SLS heads
        std::vector<TFieldIdxChain> heads;
        (void) heads;

#if !SE_DISABLE_SLS
        digAnyListHeads(heads, sh, obj, OK_SLS);
        BOOST_FOREACH(const TFieldIdxChain icHead, heads) {
            slSegEntries.push_back(obj);
            slSegHeads.push_back(icHead);
        }

        // look for DLS heads
        heads.clear();
#endif
#if !SE_DISABLE_DLS
        digAnyListHeads(heads, sh, obj, OK_DLS);
        BOOST_FOREACH(const TFieldIdxChain icHead, heads) {
            dlSegEntries.push_back(obj);
            dlSegHeads.push_back(icHead);
        }
#endif
    }

    // TODO: check if the order of following two steps is anyhow important
    if (!slSegEntries.empty()
            && considerAbstraction(sh, OK_SLS, slSegEntries, slSegHeads))
        return true;

    if (!dlSegEntries.empty()
            && considerAbstraction(sh, OK_DLS, dlSegEntries, dlSegHeads))
        return true;

    // no hit
    return false;
}

bool dlSegReplaceByConcrete(SymHeap &sh, TObjId obj, TObjId peer) {
    // first kill any related Neq predicates, we're going to concretize anyway
    dlSegHandleCrossNeq(sh, obj, SymHeap::NEQ_DEL);

    // take the value of 'next' pointer from peer
    const TFieldIdxChain icPeer = sh.objBindingField(BF_PEER, obj);
    const TObjId peerPtr = subObjByChain(sh, obj, icPeer);
    const TValueId valNext = sh.valueOf(nextPtrFromSeg(sh, peer));
    sh.objSetValue(peerPtr, valNext);

    // redirect all references originally pointing to peer to the current object
    const TValueId addrSelf = sh.placedAt(obj);
    const TValueId addrPeer = sh.placedAt(peer);
    sh.valReplace(addrPeer, addrSelf);

    // destroy the peer object and concretize self
    sh.objDestroy(peer);
    sh.objSetConcrete(obj);

    // this can't fail (at least I hope so...)
    return true;
}

void spliceOutListSegmentCore(SymHeap &sh, TObjId obj, TObjId peer) {
    const TObjId next = nextPtrFromSeg(sh, peer);
    const TValueId valNext = sh.valueOf(next);

    if (obj != peer) {
        // OK_DLS --> destroy peer
        const TFieldIdxChain icPrev = sh.objBindingField(BF_NEXT, obj);
        const TValueId valPrev = sh.valueOf(subObjByChain(sh, obj, icPrev));
        sh.valReplace(sh.placedAt(peer), valPrev);
        sh.objDestroy(peer);
    }

    // destroy self
    const TValueId addr = sh.placedAt(obj);
    sh.valReplace(addr, valNext);
    sh.objDestroy(obj);
}

void spliceOutSegmentIfNeeded(SymHeap &sh, TObjId ao, TObjId peer,
                              TSymHeapList &todo)
{
    // check if the LS may be empty
    const TValueId addrSelf = sh.placedAt(ao);
    if (segNotEmpty(sh, ao)) {
        // the segment was _guaranteed_ to be non-empty now, but the
        // concretization makes it _possibly_ empty --> remove the Neq predicate 
        const TObjId next = nextPtrFromSeg(sh, peer);
        const TValueId nextVal = sh.valueOf(next);
        sh.neqOp(SymHeap::NEQ_DEL, addrSelf, nextVal); 
        return;
    }

    // possibly empty LS
    SymHeap sh0(sh);
    spliceOutListSegmentCore(sh0, ao, peer);
    todo.push_back(sh0);
}

} // namespace

void abstractIfNeeded(SymHeap &sh) {
#if SE_DISABLE_SLS && SE_DISABLE_DLS
    (void) sh;
#else
    while (abstractIfNeededCore(sh))
        ;
#endif
}

void concretizeObj(SymHeap &sh, TValueId addr, TSymHeapList &todo) {
    const TObjId obj = sh.pointsTo(addr);
    TObjId peer = obj;

    // branch by SLS/DLS
    const EObjKind kind = sh.objKind(obj);
    switch (kind) {
        case OK_CONCRETE:
            // invalid call of concretizeObj()
            TRAP;

        case OK_SLS:
            break;

        case OK_DLS:
            // jump to peer
            skipObj(sh, &peer, sh.objBindingField(BF_PEER, obj));
            break;
    }

    // handle the possibly empty variant (if exists)
    spliceOutSegmentIfNeeded(sh, obj, peer, todo);

    // duplicate self as abstract object
    const TObjId aoDup = sh.objDup(obj);
    const TValueId aoDupAddr = sh.placedAt(aoDup);
    if (OK_DLS == kind) {
        // DLS relink
        const TFieldIdxChain icPeer = sh.objBindingField(BF_PEER, peer);
        const TObjId peerField = subObjByChain(sh, peer, icPeer);
        sh.objSetValue(peerField, aoDupAddr);
    }

    // duplicate all unknown values, to keep the prover working
    duplicateUnknownValues(sh, obj);

    // concretize self and recover the list
    const TObjId ptrNext = subObjByChain(sh, obj, (OK_SLS == kind)
            ? sh.objBindingField(BF_NEXT, obj)
            : sh.objBindingField(BF_PEER, obj));
    sh.objSetConcrete(obj);
    sh.objSetValue(ptrNext, aoDupAddr);

    if (OK_DLS == kind) {
        // update DLS back-link
        const TFieldIdxChain icPrev = sh.objBindingField(BF_NEXT, aoDup);
        const TObjId backLink = subObjByChain(sh, aoDup, icPrev);
        sh.objSetValue(backLink, sh.placedAt(obj));
    }
}

bool spliceOutListSegment(SymHeap &sh, TValueId atAddr, TValueId pointingTo)
{
    const TObjId obj = sh.pointsTo(atAddr);
    const EObjKind kind = sh.objKind(obj);
    const TObjId peer = (OK_DLS == kind)
        ? dlSegPeer(sh, obj)
        : obj;

    if (OK_DLS == sh.objKind(obj)) {
        const TObjId peer = dlSegPeer(sh, obj);
        if (sh.placedAt(peer) == pointingTo)
            // assume identity over the two parts of DLS
            return dlSegReplaceByConcrete(sh, obj, peer);
    }

    const TObjId next = nextPtrFromSeg(sh, peer);
    const TValueId valNext = sh.valueOf(next);
    if (valNext != pointingTo)
        return false;

    spliceOutListSegmentCore(sh, obj, peer);
    return true;
}
