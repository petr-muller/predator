/*
 * Copyright (C) 2010 Jiri Simacek
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

#ifndef ANTICHAIN_EXT_H
#define ANTICHAIN_EXT_H

#include "cache.hh"
#include "treeaut.hh"
#include "antichain.hh"

template <class T>
class AntichainExt : public Antichain {

protected:

//	state_cache_type stateCache;

	typedef std::vector<typename TA<T>::trans_cache_type::value_type*> trans_list_type;

private:

	std::vector<std::vector<trans_list_type> > aTransIndex;

	void simInsert(std::pair<size_t, std::set<size_t> >& el, bool& isAccepting, size_t rhs, const TA<T>& aut) {
		// minimization
		for (std::vector<size_t>::const_iterator i = this->relIndex[rhs].begin(); i != this->relIndex[rhs].end(); ++i) {
			if (el.second.find(*i) != el.second.end())
				return;
		}
		for (std::vector<size_t>::const_iterator i = this->invRelIndex[rhs].begin(); i != this->invRelIndex[rhs].end(); ++i)
			el.second.erase(*i);
		el.second.insert(rhs);
		isAccepting = isAccepting && !aut.isFinalState(rhs);
	}

public:

	void aAddTransition(typename TA<T>::trans_cache_type::value_type* t, size_t bSize) {
		std::unordered_set<size_t> s;
		for (size_t i = 0; i < t->first._lhs->first.size(); ++i) {
			if (s.insert(t->first._lhs->first[i]).second) {
				if (this->aTransIndex[t->first._lhs->first[i] - bSize].size() < i + 1)
					this->aTransIndex[t->first._lhs->first[i] - bSize].resize(i + 1);
				this->aTransIndex[t->first._lhs->first[i] - bSize][i].push_back(t);
			}
		}
	}

	void finalizeTransitions() {
		for (typename std::vector<std::vector<trans_list_type> >::iterator i = this->aTransIndex.begin(); i != this->aTransIndex.end(); ++i) {
			for (typename std::vector<trans_list_type>::iterator j = i->begin(); j != i->end(); ++j)
				utils::unique(*j, trans_list_type(*j));
		}
	}

public:

	AntichainExt(const std::vector<std::vector<bool> >& rel)
		: Antichain(rel), aTransIndex{} {}
	
	void initIndex(size_t aSize, size_t /* bSize */) {
		this->aTransIndex.resize(aSize);
	}
	
	std::vector<trans_list_type>& getATrans(size_t q, size_t bSize)  {
		return this->aTransIndex[q - bSize];
	}

	class ResponseExt {
		
			struct State {

				antichain_item_type* trans;
				antichain_item_type::iterator current;

				bool next() {
					if (++this->current != this->trans->end())
						return true;
					this->current = this->trans->begin();
					return false;
				}

				State() :
					trans{},
					current{}
				{ }
			};
		
			AntichainExt& ac;
			std::vector<State> state;
			antichain_item_type fixed;
			
		public:

			ResponseExt(AntichainExt& ac) : ac(ac), state(), fixed(1) {}
		
			bool get(const std::pair<size_t, state_cache_type::value_type*>& el, const typename TA<T>::trans_cache_type::value_type* t, size_t index) {
				this->state.clear();
				this->fixed.front() = el.second;
				for (size_t i = 0; i < t->first._lhs->first.size(); ++i) {
					State state;
					if (i == index) {
						state.trans = &this->fixed;
					} else {
						antichain_type::iterator j = ac.processed.find(t->first._lhs->first[i]);
						if (j == ac.processed.end())
							return false;
						state.trans = &j->second;
					}
					state.current = state.trans->begin();
					this->state.push_back(state);
				}
				return true;
			}
		
			bool next() {
				for (typename std::vector<State>::iterator i = this->state.begin(); i != this->state.end(); ++i) {
					if (i->next())
						return true;
				}
				return false;
			}
			
			bool match(const typename TA<T>::trans_cache_type::value_type* t) {
				for (size_t i = 0; i < t->first._lhs->first.size(); ++i) {
					if ((*this->state[i].current)->first.count(t->first._lhs->first[i]) == 0)
						return false;
				}
				return true;
			}
			
	};

	static bool subseteq(const TA<T>& a, const TA<T>& b) {

		typename TA<T>::Backend backend;
		TA<T> c(backend);
		size_t countB;
		TA<T>::renamedUnion(c, b, a, countB);
//		c.toStream(std::cout);
		Index<size_t> stateIndex;
//		SLIndex slIndex;
//		c.buildIndex(slIndex);
		c.buildStateIndex(stateIndex);
		size_t cSize = stateIndex.size();
		stateIndex.clear();
		for (size_t i = 0; i < cSize; ++i)
			stateIndex.add(i);
		// compute simulation
		std::vector<std::vector<bool> > upsim, dwnsim, ident(cSize, std::vector<bool>(cSize, false));
		for (size_t i = 0; i < cSize; ++i)
			ident[i][i] = true;		
//		computeUp(upsim, c, slIndex, ident);
		upsim = ident;
		std::vector<std::vector<size_t> > upsimIndex;
		utils::relIndex(upsimIndex, upsim);
		AntichainExt<T> antichain(upsim);
		typename AntichainExt<T>::ResponseExt response(antichain);
		antichain.initIndex(cSize - countB, countB);
		std::vector<typename TA<T>::trans_cache_type::value_type*> aLeaves;
		std::unordered_map<T, std::vector<typename TA<T>::trans_cache_type::value_type*> > bTrans, bLeaves;
		for (typename TA<T>::trans_set_type::const_iterator i = c.transitions.begin(); i != c.transitions.end(); ++i) {
			size_t arity = (*i)->first._lhs->first.size();
			if ((*i)->first._rhs >= countB) {
				if (arity == 0) {
					aLeaves.push_back(*i);
				} else {
					antichain.aAddTransition(*i, countB);
				}
			} else {	
				if (arity == 0) {
					bLeaves.insert(make_pair((*i)->first._label, std::vector<typename TA<T>::trans_cache_type::value_type*>())).first->second.push_back(*i);
				} else {
					bTrans.insert(make_pair((*i)->first._label, std::vector<typename TA<T>::trans_cache_type::value_type*>())).first->second.push_back(*i);
				}
			}
		}
		antichain.finalizeTransitions();
		// initialization
		// Post(\emptyset)
		std::vector<std::pair<size_t, std::set<size_t> > > post;
		for (typename std::vector<typename TA<T>::trans_cache_type::value_type*>::iterator i = aLeaves.begin(); i != aLeaves.end(); ++i) {
			typename std::unordered_map<T, std::vector<typename TA<T>::trans_cache_type::value_type*> >::iterator range = bLeaves.find((*i)->first._label);
			// careful
			if (range == bLeaves.end())
				return false;
			std::pair<size_t, std::set<size_t> > newEl((*i)->first._rhs, std::set<size_t>());
			bool isAccepting = c.isFinalState(newEl.first);
			for (typename std::vector<typename TA<T>::trans_cache_type::value_type*>::iterator j = range->second.begin(); j != range->second.end(); ++j)
				antichain.simInsert(newEl, isAccepting, (*j)->first._rhs, c);
			if (isAccepting)
				return false;
			// cross-automata check
			if (!utils::checkIntersection(newEl.second, upsimIndex[newEl.first]))
				post.push_back(newEl);
		}
		antichain.initialize(post);
		// main loop
//		antichain_type::value_type* el;
		std::pair<size_t, state_cache_type::value_type*> el;
		//size_t iter = 0;
		while (antichain.nextElement(el)) {

//			std::cout << iter++ << ": ";
//			antichain.printStatus();
		
			// Post(Processed)
			post.clear();
			std::vector<std::vector<typename TA<T>::trans_cache_type::value_type*> >& aTrans = antichain.getATrans(el.first, countB);
			for (size_t i = 0; i < aTrans.size(); ++i) {
				trans_list_type& aTransList = aTrans[i];
				for (typename trans_list_type::iterator j = aTransList.begin(); j != aTransList.end(); ++j) {
					if (!response.get(el, *j, i))
						continue;
					typename std::unordered_map<T, std::vector<typename TA<T>::trans_cache_type::value_type*> >::iterator range = bTrans.find((*j)->first._label);
					// careful
					if (range == bTrans.end())
						return false;
					do {
						std::pair<size_t, std::set<size_t> > newEl((*j)->first._rhs, std::set<size_t>());
						bool isAccepting = c.isFinalState(newEl.first);
						for (typename std::vector<typename TA<T>::trans_cache_type::value_type*>::iterator k = range->second.begin(); k != range->second.end(); ++k) {
							if (response.match(*k))
								antichain.simInsert(newEl, isAccepting, (*k)->first._rhs, c);
						}
						if (isAccepting) {
//							std::cout << "accepting\n";
							return false;
						}
						if (newEl.second.empty()) {
//							std::cout << "empty\n";
							return false;
						}
						// cross-automata check
						if (!utils::checkIntersection(newEl.second, upsimIndex[newEl.first]))
							post.push_back(newEl);
					} while (response.next());
				}			
			}
			antichain.update(post);
		}
		return true;
	}

};

#endif
