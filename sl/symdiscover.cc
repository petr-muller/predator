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
#include "symdiscover.hh"

#include <cl/cl_msg.hh>

#include "symseg.hh"

void AbstractionDiscovery::enlargeIfBetter(int cost, unsigned int collapsed){
    if (!this->_betterThan(cost, collapsed))
    {
        this->setCollapsed(collapsed);
        this->setCost(cost);
    }
}

class PtrFinder {
    private:
        const TValId                lookFor_;
        TOffset                     offFound_;

    public:
        // cppcheck-suppress uninitMemberVar
        PtrFinder(TValId lookFor):
            lookFor_(lookFor)
        {
        }

        TOffset offFound() const {
            return offFound_;
        }

    bool operator()(const ObjHandle &sub) {
        const TValId val = sub.value();
        if (val <= 0)
            return /* continue */ true;

        if (val != lookFor_)
            return /* continue */ true;

        // target found!
        SymHeapCore *sh = sub.sh();
        offFound_ = sh->valOffset(sub.placedAt());
        return /* break */ false;
    }
};

bool digBackLink(
        BindingOff                 *pOff,
        SymHeap                     &sh,
        const TValId                root,
        const TValId                next)
{
    // set up a visitor
    const TValId lookFor = sh.valByOffset(root, pOff->head);
    PtrFinder visitor(lookFor);

    // guide it through the next root entity
    const TValId lookAt = sh.valRoot(next);
    if (/* found nothing */ traverseLivePtrs(sh, lookAt, visitor))
        return false;

    // got a back-link!
    pOff->prev = visitor.offFound();
    return true;
}

typedef std::vector<BindingOff> TBindingCandidateList;

class ProbeEntryVisitor {
    private:
        TBindingCandidateList   &dst_;
        const TValId            root_;

    public:
        ProbeEntryVisitor(
                TBindingCandidateList         &dst,
                const TValId                  root):
            dst_(dst),
            root_(root)
        {
        }

        bool operator()(const ObjHandle &sub) const
        {
            SymHeap &sh = *static_cast<SymHeap *>(sub.sh());
            const TValId next = sub.value();
            if (!canWriteDataPtrAt(sh, next))
                return /* continue */ true;

            // read head offset
            BindingOff off;
            off.head = sh.valOffset(next);

            // entry candidate found, check the back-link in case of DLL
            off.next = sh.valOffset(sub.placedAt());
            off.prev = off.next;
#if !SE_DISABLE_DLS
            digBackLink(&off, sh, root_, next);
#endif

#if SE_DISABLE_SLS
            // allow only DLS abstraction
            if (!isDlsBinding(off))
                return /* continue */ true;
#endif
            // append a candidate
            dst_.push_back(off);
            return /* continue */ true;
        }
};

//bool segOnPath(
//        SymHeap                     &sh,
//        const BindingOff            &off,
//        const TValId                entry,
//        const unsigned              len)
//{
//    TValId cursor = entry;
//
//    for (unsigned pos = 0; pos <= len; ++pos) {
//        if (VT_ABSTRACT == sh.valTarget(cursor))
//            return true;
//
//        cursor = segNextRootObj(sh, cursor, off.next);
//    }
//
//    return false;
//}

struct SegCandidate {
    TValId                      entry;
    TBindingCandidateList       offList;
};

typedef std::vector<SegCandidate> TSegCandidateList;

AbstractionDiscovery* selectBestAbstraction(
        SymHeap                     &sh,
        const TSegCandidateList     &candidates)
{
    if (!candidates.size())
        return NULL;

    CL_DEBUG("--> initiating generic segment discovery, "
            << candidates.size() << " entry candidate(s) given");

    AbstractionDiscovery *best = NULL;
#if !(SE_DISABLE_SLS && SE_DISABLE_DLS) // LIST-SPECIFIC
    ListAbstractionDiscoveryNamespace::ListAbstractionDiscovery *good_list = NULL;
#endif

    BOOST_FOREACH(const SegCandidate &segc, candidates){
      int offsets = segc.offList.size();
      for (int idx_i=0; idx_i < offsets; idx_i++){
#if !(SE_DISABLE_SLS && SE_DISABLE_DLS) // LIST-SPECIFIC
// [TREES] FIXME: Deal with SE_COST_OF_SEG_INTRODUCTION
        if ((good_list = ListAbstractionDiscoveryNamespace::listDiscover(sh, segc.offList[idx_i], segc.entry)) != NULL){
          if (!good_list->goodEnough()){
            delete good_list;
            continue;
          }

          if (!best)
            best = good_list;
          else if (good_list->betterThan(*best)){
            // [TREES] FIXME: Does not seem very efficient
            delete best;
            best = good_list;
          }
        }
#endif
      }
    }

    return best;
}

AbstractionDiscovery* discoverBestAbstraction(
        SymHeap             &sh)
{
    TSegCandidateList candidates;

    // go through all potential segment entries
    TValList addrs;
    sh.gatherRootObjects(addrs, isOnHeap);
    BOOST_FOREACH(const TValId at, addrs) {
        // use ProbeEntryVisitor visitor to validate the potential segment entry
        SegCandidate segc;
        const ProbeEntryVisitor visitor(segc.offList, at);
        traverseLivePtrs(sh, at, visitor);
        if (segc.offList.empty())
            // found nothing
            continue;

        // append a segment candidate
        segc.entry = at;
        candidates.push_back(segc);
    }

    return selectBestAbstraction(sh, candidates);
}
