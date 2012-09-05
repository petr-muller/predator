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

#ifndef H_GUARD_SYMABSTRACT_H
#define H_GUARD_SYMABSTRACT_H

/**
 * @file symabstract.hh
 * list segment based abstraction/concretization of heap objects
 */

#include "config.h"
#include "symheap.hh"

#include <list>

/**
 * container for symbolic heaps scheduled for processing.  It's feed by
 * concretizeObj() and consumed by SymExecCore::concretizeLoop().
 */
typedef std::list<SymHeap> TSymHeapList;

/**
 * concretize the given @b abstract object.  If the result is non-deterministic,
 * more than one symbolic heap can be produced.
 * @param sh an instance of symbolic heap, used in read/write mode
 * @param addr address of the @b abstract heap object that should be concretized
 * @param dst a container for the results caused by non-deterministic decisions
 * @param leakList if not NULL, push all leaked root objects to the list
 * @note the first result is always stored into sh, the use of dst is optional
 */
void concretizeObj(
        SymHeap                     &sh,
        const TValId                 addr,
        TSymHeapList                &dst,
        TValList                    *leakList = 0);

/// splice out a possibly empty list segment
void spliceOutListSegment(
        SymHeap                &sh,
        const TValId            seg,
        const TValId            peer,
        const TValId            valNext,
        TValList               *leakList);

/// replace a DLS by a single concrete object
void dlSegReplaceByConcrete(SymHeap &sh, TValId seg, TValId peer);

/**
 * analyze the given symbolic heap and consider abstraction of some shapes that
 * we know ho to rewrite to their more abstract way of existence
 * @param sh an instance of symbolic heap, used in read/write mode
 */
void abstractIfNeeded(SymHeap &sh);

/// enable/disable debugging of symabstract
void debugSymAbstract(const bool enable);

class AbstractionHint {
    protected:
        unsigned int collapsed;
        int cost;
        TValId entry;

        // [TRESS] FIXME: Duplicate of being a class member?
        //  Thats not my decision to make
        EObjKind kind;
        std::string name;

        bool _betterThan(int _cost, unsigned int _collapsed) const { return ((this->cost <= _cost)
                                                                          && (this->collapsed > _collapsed)); }
    public:
        AbstractionHint(TValId entry) { this->entry = entry; }
        virtual ~AbstractionHint(){};
        void setCollapsed(unsigned int coll) { this->collapsed = coll; }
        void setCost(int cost) { this->cost = cost; }

        unsigned int getCollapsed() const { return this->collapsed; }
        int getCost() const { return this->cost; }

        bool betterThan(const AbstractionHint &other) const { return this->_betterThan(other.getCost(),
                                                                                       other.getCollapsed()); }
        virtual bool fireAbstraction(SymHeap  &sh) = 0;
};

// [TREES] FIXME: Should this be converted to SLS/DLS? See above.
class AbstractionHintList : public AbstractionHint {
    private:
        BindingOff off;
    public:
        AbstractionHintList(const TValId entry, const BindingOff &off);
        virtual ~AbstractionHintList() {};
        virtual bool fireAbstraction(SymHeap &sh);
};

#endif /* H_GUARD_SYMABSTRACT_H */
