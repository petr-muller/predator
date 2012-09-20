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

#ifndef H_GUARD_SYMDISCOVER_H
#define H_GUARD_SYMDISCOVER_H

/**
 * @file symdiscover.hh
 * public interface of generic abstraction discovering algorithms
 */

#include "config.h"
#include "symheap.hh"

// A class representing an abstraction opportunity, defined by an entry point,
// number of elements which will be abstracted, and the cost of doing so.
// It is a pure virtual class: no instances of this class should exist.

typedef enum {
  ABSTRACTION_LIST,
  ABSTRACTION_COUNT,
  ABSTRACTION_INVALID = ABSTRACTION_COUNT
} EAbstractionType;

class AbstractionDiscovery {
    protected:
        // Count of elements collapsed into the abstract object
        unsigned int collapsed;
        // Cost of performing the abstraction
        int cost;
        // Entry point of discovered abstraction.
        TValId entry;

        // [TRESS] FIXME: Duplicate of being a class member?
        //  Thats not my decision to make
        EObjKind kind;

        // Basic comparison criterion
        bool _betterThan(int _cost, unsigned int _collapsed) const { return ((this->cost <= _cost)
                                                                          && (this->collapsed > _collapsed)); }
    public:
        // Constructor: every real discovery needs to be better than the
        // simply initialized instance
        AbstractionDiscovery(TValId entry) :  collapsed(0),
                                              cost(INT_MAX),
                                              entry(entry) {}
        virtual ~AbstractionDiscovery(){}

        void setCollapsed(unsigned int coll) { this->collapsed = coll; }
        void setCost(int cost) { this->cost = cost; }

        unsigned int getCollapsed() const { return this->collapsed; }
        int getCost() const { return this->cost; }
        TValId getEntry() const { return this->entry; }
        virtual EAbstractionType getType() const = 0;
        virtual std::string getName() const = 0;
        EObjKind getKind() const { return this->kind; }


        bool betterThan(const AbstractionDiscovery &other) const { return this->_betterThan(other.getCost(),
                                                                                       other.getCollapsed()); }
        void enlargeIfBetter(int cost, unsigned int collapsed);
};

#include "symdiscover_list.hh"

/**
 * Take the given symbolic heap and look for the best possible abstraction in
 * there.  If nothing is found, zero is returned.  Otherwise it returns total
 * length of the best possible abstraction.  In that case, *bf is set to the
 * corresponding binding fields (head, next, peer) and *entry is set to the
 * corresponding starting point of the abstraction.
 *
 * In case of success (non-zero return value), you can determine the type of the
 * detected abstraction by *bf.  If bf->peer is equal to bf->next, it means a
 * SLS abstraction;  DLS otherwise.  If bf->head is zero, it means a regular
 * list segment abstraction;  Linux list segment otherwise.
 *
 * In case of failure (zero return value), *bf and *entry are undefined.
 */
AbstractionDiscovery* discoverBestAbstraction(SymHeap &sh);

#endif /* H_GUARD_SYMDISCOVER_H */
