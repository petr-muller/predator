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

#ifndef H_GUARD_SYMDISCOVER_LIST_H
#define H_GUARD_SYMDISCOVER_LIST_H

/**
 * @file symdiscover_list.hh
 * public interface of list segment discovering algorithms
 */

#include "config.h"
#include "symheap.hh"
#include "symdiscover.hh"

//FIXME: [TREES] Does not really seem to belong here
/// return true if the given binding is a DLS binding
inline bool isDlsBinding(const BindingOff &off)
{
    return (off.next != off.prev);
}

namespace ListAbstractionDiscoveryNamespace {

  unsigned int minLengthByCost(int cost);

  // [TREES] FIXME: Should this be converted to SLS/DLS? See above.
  class ListAbstractionDiscovery : public AbstractionDiscovery {
      private:
          BindingOff off;
      public:

          ListAbstractionDiscovery(TValId entry, const BindingOff &off);
          virtual ~ListAbstractionDiscovery() {};
          virtual bool goodEnough() { return (this->collapsed >= minLengthByCost(this->cost)); }
          virtual EAbstractionType getType() const { return ABSTRACTION_LIST; }
          virtual std::string getName() const { return std::string("Binary tree"); }

          BindingOff getBinding() const { return this->off; }
  };

  ListAbstractionDiscovery* listDiscover(
          SymHeap           &sh,
          const BindingOff  &off,
          const TValId      entry);
}

#endif
