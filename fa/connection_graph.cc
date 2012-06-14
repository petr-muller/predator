/*
 * Copyright (C) 2010 Jiri Simacek
 *
 * This file is part of forester.
 *
 * forester is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * forester is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with forester.  If not, see <http://www.gnu.org/licenses/>.
 */

// Forester headers
#include "connection_graph.hh"

void ConnectionGraph::updateStateSignature(
	StateToCutpointSignatureMap& stateMap, size_t state,
	const CutpointSignature& v)
{
	auto p = stateMap.insert(std::make_pair(state, v));

	if (!p.second)
	{
		// Assertions
		assert(v.size() == p.first->second.size());

		for (size_t i = 0; i < v.size(); ++i)
		{
			// Assertions
			assert(v[i].root == p.first->second[i].root);
			assert(v[i].defines == p.first->second[i].defines);

			p.first->second[i].refCount =
				std::max(p.first->second[i].refCount, v[i].refCount);
			p.first->second[i].realRefCount =
				std::max(p.first->second[i].realRefCount, v[i].realRefCount);
			p.first->second[i].fwdSelectors.insert(v[i].fwdSelectors.begin(),
				v[i].fwdSelectors.end());
		}
	}
}


void ConnectionGraph::computeSignatures(
	StateToCutpointSignatureMap& stateMap, const TreeAut& ta)
{
	stateMap.clear();

	std::list<const TT<label_type>*> transitions;

	CutpointSignature v(1);

	for (TreeAut::iterator i = ta.begin(); i != ta.end(); ++i)
	{
		const Data* data;

		if (i->label()->isData(data))
		{
			if (data->isRef())
			{
				v[0] = CutpointInfo(data->d_ref.root);

				ConnectionGraph::updateStateSignature(stateMap, i->rhs(), v);
			} else
			{
				assert(stateMap.find(i->rhs()) == stateMap.end());

				ConnectionGraph::updateStateSignature(stateMap, i->rhs(), CutpointSignature());
			}
		} else
		{
			transitions.push_back(&*i);
		}
	}

	bool changed = true;

	while (transitions.size()/* && changed*/)
	{
		if (!changed)
			assert(false);      // fail gracefully

		changed = false;

		for (auto i = transitions.begin(); i != transitions.end(); )
		{
			const TT<label_type>& t = **i;

			assert(t.label()->isNode());

			v.clear();

			if (!processNode(v, t.lhs(), t.label(), stateMap))
			{
				++i;
				continue;
			}

			ConnectionGraph::normalizeSignature(v);

			ConnectionGraph::updateStateSignature(stateMap, t.rhs(), v);

			changed = true;

			i = transitions.erase(i);
		}
	}
}


void ConnectionGraph::fixSignatures(TreeAut& dst, const TreeAut& ta, size_t& offset)
{
	typedef std::pair<size_t, CutpointSignature> StateCutpointSignaturePair;
	typedef std::unordered_map<StateCutpointSignaturePair, size_t, boost::hash<StateCutpointSignaturePair>> StateCutpointSignatureToStateMap;

	StateToCutpointSignatureMap stateMap;

	StateCutpointSignatureToStateMap signatureMap;

	typedef std::vector<CutpointSignature> CutpointSignatureList;

	std::unordered_map<size_t, CutpointSignatureList> invSignatureMap;

	struct ChoiceElement {

		CutpointSignatureList::const_iterator begin;
		CutpointSignatureList::const_iterator end;
		CutpointSignatureList::const_iterator iter;

		ChoiceElement(
				CutpointSignatureList::const_iterator begin,
				CutpointSignatureList::const_iterator end
				) : begin(begin), end(end), iter(begin) {}

	};

	typedef std::vector<ChoiceElement> ChoiceType;

	struct NextChoice {
		bool operator()(ChoiceType& choice) const {
			auto iter = choice.begin();
			for (; (iter != choice.end()) && (++(iter->iter) == iter->end); ++iter) {
				iter->iter = iter->begin;
			}
			return iter != choice.end();
		}
	};

	std::vector<const TT<label_type>*> transitions;

	CutpointSignature v;

	for (TreeAut::iterator i = ta.begin(); i != ta.end(); ++i)
	{
		const Data* data;

		if (i->label()->isData(data))
		{
			if (data->isRef())
			{
				v = { CutpointInfo(data->d_ref.root) };
			} else
			{
				v.clear();
			}

			auto p = signatureMap.insert(std::make_pair(std::make_pair(i->rhs(), v), i->rhs()));

			if (!p.second)
			{
				assert(false);
			}

			invSignatureMap.insert(
					std::make_pair(i->rhs(), std::vector<CutpointSignature>())
					).first->second.push_back(v);

			ConnectionGraph::updateStateSignature(stateMap, i->rhs(), v);

			dst.addTransition(*i);

		} else
		{
			transitions.push_back(&*i);
		}
	}

	bool changed = true;

	while (changed)
	{
		changed = false;

		for (auto it = transitions.begin(); it != transitions.end(); ++it)
		{
			const TT<label_type>& t = **it;

			assert(t.label()->isNode());

			ChoiceType choice;

			size_t i;

			for (i = 0; i < t.lhs().size(); ++i)
			{
				auto iter = invSignatureMap.find(t.lhs()[i]);

				if (iter == invSignatureMap.end())
					break;

				assert(iter->second.begin() != iter->second.end());

				choice.push_back(ChoiceElement(iter->second.begin(), iter->second.end()));
			}

			if (i < t.lhs().size())
				continue;

			std::vector<std::pair<size_t, CutpointSignature>> buffer;

			do
			{
				std::vector<size_t> lhs(t.lhs().size());

				for (size_t i = 0; i < t.lhs().size(); ++i)
				{
					assert(i < choice.size());

					auto iter = signatureMap.find(std::make_pair(t.lhs()[i], *choice[i].iter));

					assert(iter != signatureMap.end());

					lhs[i] = iter->second;
				}

				v.clear();

				if (!processNode(v, lhs, t.label(), stateMap))
				{
					assert(false);
				}

				ConnectionGraph::normalizeSignature(v);

				auto p = signatureMap.insert(std::make_pair(std::make_pair(t.rhs(), v), offset));

				if (p.second)
				{
					++offset;

					if (ta.isFinalState(t.rhs()))
						dst.addFinalState(p.first->second);

					ConnectionGraph::updateStateSignature(stateMap, p.first->second, v);

					buffer.push_back(std::make_pair(t.rhs(), v));
				}

				dst.addTransition(lhs, t.label(), p.first->second);

			} while (NextChoice()(choice));

			for (auto& item : buffer)
			{
				invSignatureMap.insert(
						std::make_pair(item.first, std::vector<CutpointSignature>())
						).first->second.push_back(item.second);
			}

			changed = changed || buffer.size();
		}
	}
}

void ConnectionGraph::processStateSignature(CutpointSignature& result,
	const StructuralBox* box, size_t input, size_t state,
	const CutpointSignature& signature)
{
	size_t selector = box->outputReachable(input);

	if (ConnectionGraph::isData(state))
	{
		if (signature.empty())
			return;

		// Assertions
		assert(signature.size() == 1);
		assert(signature[0].fwdSelectors.size() == 1);
		assert(*signature[0].fwdSelectors.begin() == static_cast<size_t>(-1));

		result.push_back(signature[0]);
		result.back().realRefCount =
			std::min(signature[0].realRefCount, box->getRealRefCount(input));
		result.back().fwdSelectors.insert(box->selectorToInput(input));

		if (selector != static_cast<size_t>(-1))
			result.back().bwdSelector = selector;

		result.back().defines.insert(
				box->inputCoverage(input).begin(), box->inputCoverage(input).end()
				);

		return;
	}

	for (auto& cutpoint : signature)
	{
		result.push_back(cutpoint);
		result.back().realRefCount =
			std::min(cutpoint.realRefCount, box->getRealRefCount(input));
		result.back().fwdSelectors.clear();
		result.back().fwdSelectors.insert(box->selectorToInput(input));

		if (selector == static_cast<size_t>(-1))
			result.back().bwdSelector = static_cast<size_t>(-1);
	}
}

bool ConnectionGraph::processNode(CutpointSignature& result,
	const std::vector<size_t>& lhs, const label_type& label,
	const StateToCutpointSignatureMap& stateMap)
{
	size_t lhsOffset = 0;

	for (const AbstractBox* box : label->getNode())
	{
		if (!box->isStructural())
			continue;

		const StructuralBox* sBox = static_cast<const StructuralBox*>(box);

		for (size_t j = 0; j < sBox->getArity(); ++j)
		{
			auto k = stateMap.find(lhs[lhsOffset + j]);

			if (k == stateMap.end())
				return false;

			ConnectionGraph::processStateSignature(
				result, sBox, j, lhs[lhsOffset + j], k->second
				);
		}

		lhsOffset += sBox->getArity();
	}

	return true;
}

void ConnectionGraph::mergeCutpoint(size_t dst, size_t src)
{
	// Assertions
	assert(dst < this->data.size());
	assert(src < this->data.size());

	assert(this->isValid());

	const CutpointSignature& dstSignature = this->data[dst].signature;
	const CutpointSignature& srcSignature = this->data[src].signature;

	assert(
			std::find_if(
				dstSignature.begin(),
				dstSignature.end(),
				[&src](const CutpointInfo& cutpoint) { return cutpoint.root == src; }
				) != dstSignature.end()
			);

	CutpointSignature signature;

	for (auto& cutpoint : dstSignature)
	{
		if (cutpoint.root != src)
		{
			// copy original signature
			signature.push_back(cutpoint);

			continue;
		}

		if (cutpoint.bwdSelector == static_cast<size_t>(-1))
		{
			// erase backward selector
			for (auto& tmp : srcSignature)
			{
				signature.push_back(tmp);
				signature.back().fwdSelectors = cutpoint.fwdSelectors;

				if (tmp.bwdSelector == static_cast<size_t>(-1))
					continue;

				signature.back().bwdSelector = static_cast<size_t>(-1);

				assert(tmp.root < this->data.size());

				auto iter = this->data[tmp.root].bwdMap.find(tmp.bwdSelector);

				assert(iter != this->data[tmp.root].bwdMap.end());
				assert(iter->second == src);

				this->data[tmp.root].bwdMap.erase(iter);
			}
		} else
		{
			// update backward selector
			for (auto& tmp : srcSignature)
			{
				signature.push_back(tmp);
				signature.back().fwdSelectors = cutpoint.fwdSelectors;

				if (tmp.bwdSelector == static_cast<size_t>(-1))
					continue;

				assert(tmp.root < this->data.size());

				auto iter = this->data[tmp.root].bwdMap.find(tmp.bwdSelector);

				assert(iter != this->data[tmp.root].bwdMap.end());
				assert(iter->second == src);

				auto i = this->data[tmp.root].bwdMap.begin();

				for (; i != this->data[tmp.root].bwdMap.end(); ++i)
				{
					if (i->second == dst)
						break;
				}

				if (i == this->data[tmp.root].bwdMap.end())
				{
					iter->second = dst;

					continue;
				}

				if (i->first < iter->first)
				{
					this->data[tmp.root].bwdMap.erase(iter);

					continue;
				}

				this->data[tmp.root].bwdMap.erase(i);

				iter->second = dst;
			}
		}
	}

	ConnectionGraph::normalizeSignature(signature);

	std::swap(this->data[dst].signature, signature);
}

void ConnectionGraph::normalizeSignature(CutpointSignature& signature)
{
	std::unordered_map<size_t, CutpointInfo*> m;

	size_t offset = 0;

	for (size_t i = 0; i < signature.size(); ++i)
	{
		auto p = m.insert(std::make_pair(signature[i].root, &signature[offset]));

		if (p.second)
		{
			signature[offset] = signature[i];
			signature[offset].refInherited = signature[offset].refCount > 1;

			++offset;
		} else
		{
			assert(p.first->second);

			p.first->second->refCount = std::min(
				p.first->second->refCount + signature[i].refCount,
				static_cast<size_t>(FA_REF_CNT_TRESHOLD)
			);
			p.first->second->realRefCount = std::min(
				p.first->second->realRefCount + signature[i].realRefCount,
				static_cast<size_t>(FA_REAL_REF_CNT_TRESHOLD)
			);
			p.first->second->refInherited = false;
			p.first->second->fwdSelectors.insert(
					signature[i].fwdSelectors.begin(), signature[i].fwdSelectors.end()
			);

			if (p.first->second->bwdSelector > signature[i].bwdSelector)
				p.first->second->bwdSelector = signature[i].bwdSelector;

			assert(
				ConnectionGraph::areDisjoint(p.first->second->defines, signature[i].defines)
			);

			p.first->second->defines.insert(
				signature[i].defines.begin(), signature[i].defines.end()
			);
		}
	}

	signature.resize(offset);
}

std::ostream& operator<<(std::ostream& os,
	const ConnectionGraph::CutpointInfo& info)
{
	// Assertions
	assert(info.refCount <= FA_REF_CNT_TRESHOLD);

	os << info.root << "x" << info.refCount << ':' << info.realRefCount << "({";

	for (auto& s : info.fwdSelectors)
	{
		if (s == static_cast<size_t>(-1))
			continue;

		os << ' ' << s;
	}

	os << " }, ";

	if (info.bwdSelector == static_cast<size_t>(-1))
		os << '-';
	else
		os << info.bwdSelector;

	os << ", {";

	for (auto& s : info.defines)
		os << " +" << s;

	return os << " })";
}

void ConnectionGraph::visit(size_t c, std::vector<bool>& visited,
	std::vector<size_t>& order, std::vector<bool>& marked) const
{
	// Assertions
	assert(c < visited.size());

	if (visited[c])
	{
		marked[c] = true;

		return;
	}

	visited[c] = true;

	order.push_back(c);

	for (auto& cutpoint : this->data[c].signature)
	{
		this->visit(cutpoint.root, visited, order, marked);

		if (cutpoint.refCount > 1)
			marked[cutpoint.root] = true;
	}

	for (auto& selectorCutpointPair : this->data[c].bwdMap)
	{
		if (visited[selectorCutpointPair.second])
			continue;

		std::vector<bool> mask(this->data.size(), false);

		mask[c] = true;

		size_t tmp = this->climb(selectorCutpointPair.second, visited, mask);

		if (tmp == c)
			continue;

		marked[tmp] = true;

		this->visit(tmp, visited, order, marked);
	}
}

void ConnectionGraph::visit(size_t c, std::vector<bool>& visited) const
{
	// Assertions
	assert(c < visited.size());

	if (visited[c])
		return;

	visited[c] = true;

	for (auto& cutpoint : this->data[c].signature)
		this->visit(cutpoint.root, visited);

	for (auto& selectorCutpointPair : this->data[c].bwdMap)
	{
		if (visited[selectorCutpointPair.second])
			continue;

		std::vector<bool> mask(this->data.size(), false);

		mask[c] = true;

		size_t tmp = this->climb(selectorCutpointPair.second, visited, mask);

		if (tmp == c)
			continue;

		this->visit(tmp, visited);
	}
}

size_t ConnectionGraph::climb(size_t c, const std::vector<bool>& visited,
	std::vector<bool>& mask) const
{
	// Assertions
	assert(c < this->data.size());

	if (mask[c])
		return c;

	if (this->data[c].bwdMap.empty())
		return c;

	mask[c] = true;

	for (auto& selectorCutpointPair : this->data[c].bwdMap)
	{
		assert(selectorCutpointPair.second < visited.size());

		if (!visited[selectorCutpointPair.second])
			return this->climb(selectorCutpointPair.second, visited, mask);
	}

	return c;
}


void ConnectionGraph::finishNormalization(size_t size, const std::vector<size_t>& index)
{
	// Assertions
	assert(size <= this->data.size());
	assert(index.size() == this->data.size());
	assert(this->isValid());

	ConnectionData tmp(size);

	for (size_t i = 0; i < this->data.size(); ++i)
	{
		if (index[i] == static_cast<size_t>(-1))
			continue;

		assert(index[i] < tmp.size());

		tmp[index[i]] = this->data[i];
	}

	std::swap(tmp, this->data);

	for (auto& root : this->data)
	{
		assert(root.valid);

		ConnectionGraph::renameSignature(root.signature, index);

		for (auto& selectorRootPair : root.bwdMap)
		{
			assert(selectorRootPair.second < index.size());

			selectorRootPair.second = index[selectorRootPair.second];
		}
	}
}


void ConnectionGraph::updateRoot(size_t root, const TreeAut& ta)
{
	// Assertions
	assert(root < this->data.size());
	assert(!this->data[root].valid);
	assert(ta.getFinalStates().size());

	StateToCutpointSignatureMap stateMap;

	ConnectionGraph::computeSignatures(stateMap, ta);

	auto iter = ta.getFinalStates().begin();

	assert(stateMap.find(*iter) != stateMap.end());

	this->data[root].signature = stateMap[*iter];

	for (++iter; iter != ta.getFinalStates().end(); ++iter)
	{
		assert(stateMap.find(*iter) != stateMap.end());
		assert(this->data[root].signature == stateMap[*iter]);
	}

	this->updateBackwardData(root);
}


void ConnectionGraph::updateBackwardData(size_t root)
{
	// Assertions
	assert(root < this->data.size());
	assert(!this->data[root].valid);

	for (auto& cutpoint : this->data[root].signature)
	{
		assert(cutpoint.root < this->data.size());

		if (cutpoint.bwdSelector != static_cast<size_t>(-1))
		{
			assert(
				this->data[cutpoint.root].bwdMap.find(
					cutpoint.bwdSelector
					) == this->data[cutpoint.root].bwdMap.end()
			);

			this->data[cutpoint.root].bwdMap.insert(
				std::make_pair(cutpoint.bwdSelector, root));
		}
	}

	this->data[root].valid = true;
}


void ConnectionGraph::invalidate(size_t root)
{
	// Assertions
	assert(root < this->data.size());

	if (!this->data[root].valid)
		return;

	for (auto& cutpoint : this->data[root].signature)
	{
		assert(cutpoint.root < this->data.size());

		if (cutpoint.bwdSelector != static_cast<size_t>(-1))
		{
			assert(this->data[cutpoint.root].backwardLookup(cutpoint.bwdSelector) == root);

			this->data[cutpoint.root].bwdMap.erase(cutpoint.bwdSelector);
		}
	}

	this->data[root].valid = false;
}


void ConnectionGraph::updateIfNeeded(const std::vector<std::shared_ptr<TreeAut>>& roots)
{
	// Assertions
	assert(this->data.size() == roots.size());

	for (size_t i = 0; i < this->data.size(); ++i)
	{
		if (this->data[i].valid)
			continue;

		if (!roots[i])
		{
			this->data[i].valid = true;

			continue;
		}

		this->updateRoot(i, *roots[i]);
	}
}


std::ostream& operator<<(std::ostream& os, const ConnectionGraph::RootInfo& info)
{
	if (!info.valid)
		return os << "<invalid>";

	os << info.signature;

	for (auto& p : info.bwdMap)
		os << '|' << p.first << ':' << p.second;

	return os;
}
