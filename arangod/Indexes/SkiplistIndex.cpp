////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#include "SkiplistIndex.h"
#include "Aql/AstNode.h"
#include "Aql/SortCondition.h"
#include "Basics/AttributeNameParser.h"
#include "Basics/StaticStrings.h"
#include "Basics/VelocyPackHelper.h"
#include "Indexes/IndexLookupContext.h"
#include "Utils/Transaction.h"
#include "VocBase/LogicalCollection.h"

#include <velocypack/Iterator.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;

static size_t sortWeight(arangodb::aql::AstNode const* node) {
  switch (node->type) {
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
      return 1;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
      return 2;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
      return 3;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
      return 4;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
      return 5;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
      return 6;
    default:
      return 42; /* OPST_CIRCUS */
  }
}

// .............................................................................
// recall for all of the following comparison functions:
//
// left < right  return -1
// left > right  return  1
// left == right return  0
//
// furthermore:
//
// the following order is currently defined for placing an order on documents
// undef < null < boolean < number < strings < lists < hash arrays
// note: undefined will be treated as NULL pointer not NULL JSON OBJECT
// within each type class we have the following order
// boolean: false < true
// number: natural order
// strings: lexicographical
// lists: lexicographically and within each slot according to these rules.
// ...........................................................................

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a key with an element, version with proper types
////////////////////////////////////////////////////////////////////////////////

static int CompareKeyElement(void* userData, 
                             VPackSlice const* left,
                             SkiplistIndexElement const* right,
                             size_t rightPosition) {
  IndexLookupContext* context = static_cast<IndexLookupContext*>(userData);
  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);
  return arangodb::basics::VelocyPackHelper::compare(
      *left, right->slice(context, rightPosition), true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares elements, version with proper types
////////////////////////////////////////////////////////////////////////////////

static int CompareElementElement(void* userData, 
                                 SkiplistIndexElement const* left,
                                 size_t leftPosition,
                                 SkiplistIndexElement const* right,
                                 size_t rightPosition) {
  IndexLookupContext* context = static_cast<IndexLookupContext*>(userData);
  TRI_ASSERT(nullptr != left);
  TRI_ASSERT(nullptr != right);

  VPackSlice l = left->slice(context, leftPosition);
  VPackSlice r = right->slice(context, rightPosition);
  return arangodb::basics::VelocyPackHelper::compare(l, r, true);
}

bool BaseSkiplistLookupBuilder::isEquality() const { return _isEquality; }

VPackSlice const* BaseSkiplistLookupBuilder::getLowerLookup() const {
  return &_lowerSlice;
}

bool BaseSkiplistLookupBuilder::includeLower() const { return _includeLower; }

VPackSlice const* BaseSkiplistLookupBuilder::getUpperLookup() const {
  return &_upperSlice;
}

bool BaseSkiplistLookupBuilder::includeUpper() const { return _includeUpper; }

SkiplistLookupBuilder::SkiplistLookupBuilder(
    Transaction* trx,
    std::vector<std::vector<arangodb::aql::AstNode const*>>& ops,
    arangodb::aql::Variable const* var, bool reverse)
    : BaseSkiplistLookupBuilder(trx) {
  _lowerBuilder->openArray();
  if (ops.empty()) {
    // We only use this skiplist to sort. use empty array for lookup
    _lowerBuilder->close();
    _lowerSlice = _lowerBuilder->slice();
    _upperSlice = _lowerBuilder->slice();
    return;
  }

  auto const& last = ops.back();
  TRI_ASSERT(!last.empty());

  std::pair<arangodb::aql::Variable const*,
            std::vector<arangodb::basics::AttributeName>> paramPair;

  if (last[0]->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ &&
      last[0]->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
    _isEquality = false;
    _upperBuilder->openArray();
    for (size_t i = 0; i < ops.size() - 1; ++i) {
      auto const& oplist = ops[i];
      TRI_ASSERT(oplist.size() == 1);
      auto const& op = oplist[0];
      TRI_ASSERT(op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
                 op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN);
      TRI_ASSERT(op->numMembers() == 2);
      auto value = op->getMember(0);
      if (value->isAttributeAccessForVariable(paramPair) &&
          paramPair.first == var) {
        value = op->getMember(1);
        TRI_ASSERT(!(value->isAttributeAccessForVariable(paramPair) &&
                     paramPair.first == var));
      }
      value->toVelocyPackValue(*(_lowerBuilder.get()));
      value->toVelocyPackValue(*(_upperBuilder.get()));
    }

    TRI_IF_FAILURE("SkiplistIndex::permutationEQ") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }

    TRI_IF_FAILURE("SkiplistIndex::permutationArrayIN") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }

    auto const& last = ops.back();
    for (auto const& op : last) {
      bool isReverseOrder = true;
      TRI_ASSERT(op->numMembers() == 2);

      auto value = op->getMember(0);
      if (value->isAttributeAccessForVariable(paramPair) &&
          paramPair.first == var) {
        value = op->getMember(1);
        TRI_ASSERT(!(value->isAttributeAccessForVariable(paramPair) &&
                     paramPair.first == var));
        isReverseOrder = false;
      }
      switch (op->type) {
        case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
          if (isReverseOrder) {
            _includeLower = false;
          } else {
            _includeUpper = false;
          }
        // Fall through intentional
        case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
          if (isReverseOrder) {
            value->toVelocyPackValue(*(_lowerBuilder.get()));
          } else {
            value->toVelocyPackValue(*(_upperBuilder.get()));
          }
          break;
        case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
          if (isReverseOrder) {
            _includeUpper = false;
          } else {
            _includeLower = false;
          }
        // Fall through intentional
        case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
          if (isReverseOrder) {
            value->toVelocyPackValue(*(_upperBuilder.get()));
          } else {
            value->toVelocyPackValue(*(_lowerBuilder.get()));
          }
          break;
        default:
          TRI_ASSERT(false);
      }
    }
    _lowerBuilder->close();
    _lowerSlice = _lowerBuilder->slice();

    _upperBuilder->close();
    _upperSlice = _upperBuilder->slice();
  } else {
    for (auto const& oplist : ops) {
      TRI_ASSERT(oplist.size() == 1);
      auto const& op = oplist[0];
      TRI_ASSERT(op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
                 op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN);
      TRI_ASSERT(op->numMembers() == 2);
      auto value = op->getMember(0);
      if (value->isAttributeAccessForVariable(paramPair) &&
          paramPair.first == var) {
        value = op->getMember(1);
        TRI_ASSERT(!(value->isAttributeAccessForVariable(paramPair) &&
                     paramPair.first == var));
      }
      value->toVelocyPackValue(*(_lowerBuilder.get()));
    }

    TRI_IF_FAILURE("SkiplistIndex::permutationEQ") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }

    TRI_IF_FAILURE("SkiplistIndex::permutationArrayIN") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }
    _lowerBuilder->close();
    _lowerSlice = _lowerBuilder->slice();
    _upperSlice = _lowerBuilder->slice();
  }
}

bool SkiplistLookupBuilder::next() {
  // The first search value is created during creation.
  // So next is always false.
  return false;
}

SkiplistInLookupBuilder::SkiplistInLookupBuilder(
    Transaction* trx,
    std::vector<std::vector<arangodb::aql::AstNode const*>>& ops,
    arangodb::aql::Variable const* var, bool reverse)
    : BaseSkiplistLookupBuilder(trx), _dataBuilder(trx), _done(false) {
  TRI_ASSERT(!ops.empty());  // We certainly do not need IN here
  TransactionBuilderLeaser tmp(trx);
  std::set<VPackSlice, arangodb::basics::VelocyPackHelper::VPackSorted<true>>
      unique_set(
          (arangodb::basics::VelocyPackHelper::VPackSorted<true>(reverse)));
  std::pair<arangodb::aql::Variable const*,
            std::vector<arangodb::basics::AttributeName>> paramPair;

  _dataBuilder->clear();
  _dataBuilder->openArray();

  // The == and IN part
  for (size_t i = 0; i < ops.size() - 1; ++i) {
    auto const& oplist = ops[i];
    TRI_ASSERT(oplist.size() == 1);
    auto const& op = oplist[0];
    TRI_ASSERT(op->numMembers() == 2);
    auto value = op->getMember(0);
    bool valueLeft = true;
    if (value->isAttributeAccessForVariable(paramPair) &&
        paramPair.first == var) {
      valueLeft = false;
      value = op->getMember(1);
      TRI_ASSERT(!(value->isAttributeAccessForVariable(paramPair) &&
                   paramPair.first == var));
    }
    if (op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
      if (valueLeft) {
        // Case: value IN x.a
        // This is identical to == for the index.
        value->toVelocyPackValue(*(_dataBuilder.get()));
      } else {
        // Case: x.a IN value
        TRI_ASSERT(value->numMembers() > 0);
        tmp->clear();
        unique_set.clear();
        value->toVelocyPackValue(*(tmp.get()));
        for (auto const& it : VPackArrayIterator(tmp->slice())) {
          unique_set.emplace(it);
        }
        TRI_IF_FAILURE("SkiplistIndex::permutationIN") {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
        }
        _inPositions.emplace_back(i, 0, unique_set.size());
        _dataBuilder->openArray();
        for (auto const& it : unique_set) {
          _dataBuilder->add(it);
        }
        _dataBuilder->close();
      }
    } else {
      TRI_ASSERT(op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ);
      value->toVelocyPackValue(*(_dataBuilder.get()));
    }
  }
  auto const& last = ops.back();
  arangodb::aql::AstNode const* lower = nullptr;
  arangodb::aql::AstNode const* upper = nullptr;

  _isEquality = false;

  for (auto const& op : last) {
    bool isReverseOrder = true;
    TRI_ASSERT(op->numMembers() == 2);

    auto value = op->getMember(0);
    if (value->isAttributeAccessForVariable(paramPair) &&
        paramPair.first == var) {
      value = op->getMember(1);
      TRI_ASSERT(!(value->isAttributeAccessForVariable(paramPair) &&
                   paramPair.first == var));
      isReverseOrder = false;
    }

    switch (op->type) {
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
        if (isReverseOrder) {
          _includeLower = false;
        } else {
          _includeUpper = false;
        }
      // Fall through intentional
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
        if (isReverseOrder) {
          TRI_ASSERT(lower == nullptr);
          lower = value;
        } else {
          TRI_ASSERT(upper == nullptr);
          upper = value;
        }
        break;
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
        if (isReverseOrder) {
          _includeUpper = false;
        } else {
          _includeLower = false;
        }
      // Fall through intentional
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
        if (isReverseOrder) {
          TRI_ASSERT(upper == nullptr);
          upper = value;
        } else {
          TRI_ASSERT(lower == nullptr);
          lower = value;
        }
        break;
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
        TRI_ASSERT(upper == nullptr);
        TRI_ASSERT(lower == nullptr);
        TRI_ASSERT(value->numMembers() > 0);
        tmp->clear();
        unique_set.clear();
        value->toVelocyPackValue(*(tmp.get()));
        for (auto const& it : VPackArrayIterator(tmp->slice())) {
          unique_set.emplace(it);
        }
        TRI_IF_FAILURE("Index::permutationIN") {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
        }
        _inPositions.emplace_back(ops.size() - 1, 0, unique_set.size());
        _dataBuilder->openArray();
        for (auto const& it : unique_set) {
          _dataBuilder->add(it);
        }
        _dataBuilder->close();
        _isEquality = true;
        _dataBuilder->close();

        buildSearchValues();
        return;
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
        TRI_ASSERT(upper == nullptr);
        TRI_ASSERT(lower == nullptr);
        value->toVelocyPackValue(*(_dataBuilder.get()));
        _isEquality = true;
        _dataBuilder->close();

        buildSearchValues();
        return;
      default:
        TRI_ASSERT(false);
    }
  }
  _dataBuilder->openArray();
  if (lower == nullptr) {
    _dataBuilder->add(arangodb::basics::VelocyPackHelper::NullValue());
  } else {
    lower->toVelocyPackValue(*(_dataBuilder.get()));
  }

  if (upper == nullptr) {
    _dataBuilder->add(arangodb::basics::VelocyPackHelper::NullValue());
  } else {
    upper->toVelocyPackValue(*(_dataBuilder.get()));
  }
  _dataBuilder->close();
  _dataBuilder->close();

  buildSearchValues();
}

bool SkiplistInLookupBuilder::next() {
  if (_done || !forwardInPosition()) {
    return false;
  }
  buildSearchValues();
  return true;
}

bool SkiplistInLookupBuilder::forwardInPosition() {
  std::list<PosStruct>::reverse_iterator it = _inPositions.rbegin();
  while (it != _inPositions.rend()) {
    it->current++;
    TRI_ASSERT(it->_max > 0);
    if (it->current < it->_max) {
      return true;
      // Okay we increased this, next search value;
    }
    it->current = 0;
    ++it;
  }
  _done = true;
  // If we get here all positions are reset to 0.
  // We are done, no further combination
  return false;
}

void SkiplistInLookupBuilder::buildSearchValues() {
  auto inPos = _inPositions.begin();
  _lowerBuilder->clear();
  _lowerBuilder->openArray();

  VPackSlice data = _dataBuilder->slice();
  if (!_isEquality) {
    _upperBuilder->clear();
    _upperBuilder->openArray();

    for (size_t i = 0; i < data.length() - 1; ++i) {
      if (inPos != _inPositions.end() && i == inPos->field) {
        _lowerBuilder->add(data.at(i).at(inPos->current));
        _upperBuilder->add(data.at(i).at(inPos->current));
        inPos++;
      } else {
        _lowerBuilder->add(data.at(i));
        _upperBuilder->add(data.at(i));
      }
    }

    VPackSlice bounds = data.at(data.length() - 1);
    TRI_ASSERT(bounds.isArray());
    TRI_ASSERT(bounds.length() == 2);
    VPackSlice b = bounds.at(0);
    if (!b.isNull()) {
      _lowerBuilder->add(b);
    }
    _lowerBuilder->close();
    _lowerSlice = _lowerBuilder->slice();

    b = bounds.at(1);
    if (!b.isNull()) {
      _upperBuilder->add(b);
    }

    _upperBuilder->close();
    _upperSlice = _upperBuilder->slice();
  } else {
    for (size_t i = 0; i < data.length(); ++i) {
      if (inPos != _inPositions.end() && i == inPos->field) {
        _lowerBuilder->add(data.at(i).at(inPos->current));
        inPos++;
      } else {
        _lowerBuilder->add(data.at(i));
      }
    }
    _lowerBuilder->close();
    _lowerSlice = _lowerBuilder->slice();
    _upperSlice = _lowerBuilder->slice();
  }
}
  
SkiplistIterator::SkiplistIterator(LogicalCollection* collection, arangodb::Transaction* trx,
                                   ManagedDocumentResult* mmdr,
                                   arangodb::SkiplistIndex const* index,
                                   bool reverse, Node* left, Node* right)
    : IndexIterator(collection, trx, mmdr, index),
      _reverse(reverse),
      _leftEndPoint(left),
      _rightEndPoint(right) {
  reset(); // Initializes the cursor
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Reset the cursor
////////////////////////////////////////////////////////////////////////////////

void SkiplistIterator::reset() {
  if (_reverse) {
    _cursor = _rightEndPoint;
  } else {
    _cursor = _leftEndPoint;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Get the next element in the skiplist
////////////////////////////////////////////////////////////////////////////////

IndexLookupResult SkiplistIterator::next() {
  if (_cursor == nullptr) {
    // We are exhausted already, sorry
    return IndexLookupResult();
  }
  Node* tmp = _cursor;
  if (_reverse) {
    if (_cursor == _leftEndPoint) {
      _cursor = nullptr;
    } else {
      _cursor = _cursor->prevNode();
    }
  } else {
    if (_cursor == _rightEndPoint) {
      _cursor = nullptr;
    } else {
      _cursor = _cursor->nextNode();
    }
  }
  TRI_ASSERT(tmp != nullptr);
  TRI_ASSERT(tmp->document() != nullptr);
  return IndexLookupResult(tmp->document()->revisionId());
}
  
SkiplistIterator2::SkiplistIterator2(LogicalCollection* collection, arangodb::Transaction* trx,
    ManagedDocumentResult* mmdr,
    arangodb::SkiplistIndex const* index,
    TRI_Skiplist const* skiplist, size_t numPaths,
    std::function<int(void*, SkiplistIndexElement const*, SkiplistIndexElement const*,
                      arangodb::basics::SkipListCmpType)> const& CmpElmElm,
    bool reverse, BaseSkiplistLookupBuilder* builder)
    : IndexIterator(collection, trx, mmdr, index),
      _skiplistIndex(skiplist),
      _numPaths(numPaths),
      _reverse(reverse),
      _cursor(nullptr),
      _currentInterval(0),
      _builder(builder),
      _CmpElmElm(CmpElmElm) {
  TRI_ASSERT(_builder != nullptr);
  initNextInterval(); // Initializes the cursor
  TRI_ASSERT((_intervals.empty() && _cursor == nullptr) ||
             (!_intervals.empty() && _cursor != nullptr));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Checks if the interval is valid. It is declared invalid if
///        one border is nullptr or the right is lower than left.
////////////////////////////////////////////////////////////////////////////////

bool SkiplistIterator2::intervalValid(void* userData, Node* left, Node* right) const {
  if (left == nullptr) {
    return false;
  }
  if (right == nullptr) {
    return false;
  }
  if (left == right) {
    // Exactly one result. Improve speed on unique indexes
    return true;
  }
  if (_CmpElmElm(userData, left->document(), right->document(),
                 arangodb::basics::SKIPLIST_CMP_TOTORDER) > 0) {
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Reset the cursor
////////////////////////////////////////////////////////////////////////////////

void SkiplistIterator2::reset() {
  // If _intervals is empty at this point
  // the cursor does not contain any
  // document at all. Reset is pointless
  if (!_intervals.empty()) {
    // We reset to the first interval and reset the cursor
    _currentInterval = 0;
    if (_reverse) {
      _cursor = _intervals[0].second;
    } else {
      _cursor = _intervals[0].first;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Get the next element in the skiplist
////////////////////////////////////////////////////////////////////////////////

IndexLookupResult SkiplistIterator2::next() {
  if (_cursor == nullptr) {
    // We are exhausted already, sorry
    return IndexLookupResult();
  }
  TRI_ASSERT(_currentInterval < _intervals.size());
  auto const& interval = _intervals[_currentInterval];
  Node* tmp = _cursor;
  if (_reverse) {
    if (_cursor == interval.first) {
      forwardCursor();
    } else {
      _cursor = _cursor->prevNode();
    }
  } else {
    if (_cursor == interval.second) {
      forwardCursor();
    } else {
      _cursor = _cursor->nextNode();
    }
  }
  TRI_ASSERT(tmp != nullptr);
  TRI_ASSERT(tmp->document() != nullptr);
  return IndexLookupResult(tmp->document()->revisionId());
}

void SkiplistIterator2::forwardCursor() {
  _currentInterval++;
  if (_currentInterval < _intervals.size()) {
    auto const& interval = _intervals[_currentInterval];
    if (_reverse) {
      _cursor = interval.second;
    } else {
      _cursor = interval.first;
    }
    return;
  }
  _cursor = nullptr;
  if (_builder->next()) {
    initNextInterval();
  }
}

void SkiplistIterator2::initNextInterval() {
  // We will always point the cursor to the resulting interval if any.
  // We do not take responsibility for the Nodes!
  Node* rightBorder = nullptr;
  Node* leftBorder = nullptr;
  
  while (true) {
    if (_builder->isEquality()) {
      rightBorder = _skiplistIndex->rightKeyLookup(&_context, _builder->getLowerLookup());
      if (rightBorder == _skiplistIndex->startNode()) {
        // No matching elements. Next interval
        if (!_builder->next()) {
          // No next interval. We are done.
          return;
        }
        // Builder moved forward. Try again.
        continue;
      }
      leftBorder = _skiplistIndex->leftKeyLookup(&_context, _builder->getLowerLookup());
      leftBorder = leftBorder->nextNode();
      // NOTE: rightBorder < leftBorder => no Match.
      // Will be checked by interval valid
    } else {
      if (_builder->includeLower()) {
        leftBorder = _skiplistIndex->leftKeyLookup(&_context, _builder->getLowerLookup());
        // leftKeyLookup guarantees that we find the element before search.
      } else {
        leftBorder = _skiplistIndex->rightKeyLookup(&_context, _builder->getLowerLookup());
        // leftBorder is identical or smaller than search
      }
      // This is the first element not to be returned, but the next one
      // Also save for the startNode, it should never be contained in the index.
      leftBorder = leftBorder->nextNode();

      if (_builder->includeUpper()) {
        rightBorder =
            _skiplistIndex->rightKeyLookup(&_context, _builder->getUpperLookup());
      } else {
        rightBorder = _skiplistIndex->leftKeyLookup(&_context, _builder->getUpperLookup());
      }
      if (rightBorder == _skiplistIndex->startNode()) {
        // No match make interval invalid
        rightBorder = nullptr;
      }
      // else rightBorder is correct
    }
    if (!intervalValid(&_context, leftBorder, rightBorder)) {
      // No matching elements. Next interval
      if (!_builder->next()) {
        // No next interval. We are done.
        return;
      }
      // Builder moved forward. Try again.
      continue;
    }
    TRI_ASSERT(_currentInterval == _intervals.size());
    _intervals.emplace_back(leftBorder, rightBorder);
    if (_reverse) {
      _cursor = rightBorder;
    } else {
      _cursor = leftBorder;
    }
    // Next valid interal initialized. Return;
    return;
  }
}

/// @brief create the skiplist index
SkiplistIndex::SkiplistIndex(TRI_idx_iid_t iid,
                             arangodb::LogicalCollection* collection,
                             VPackSlice const& info)
    : PathBasedIndex(iid, collection, info, true),
      CmpElmElm(this),
      CmpKeyElm(this),
      _skiplistIndex(nullptr) {
  _skiplistIndex =
      new TRI_Skiplist(CmpElmElm, CmpKeyElm, [this](SkiplistIndexElement* element) { element->free(); }, _unique, _useExpansion);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the skiplist index
////////////////////////////////////////////////////////////////////////////////

SkiplistIndex::~SkiplistIndex() { delete _skiplistIndex; }

size_t SkiplistIndex::memory() const {
  return _skiplistIndex->memoryUsage() +
         static_cast<size_t>(_skiplistIndex->getNrUsed()) * SkiplistIndexElement::baseMemoryUsage(_paths.size());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return a VelocyPack representation of the index
////////////////////////////////////////////////////////////////////////////////

void SkiplistIndex::toVelocyPack(VPackBuilder& builder,
                                 bool withFigures) const {
  Index::toVelocyPack(builder, withFigures);
  builder.add("unique", VPackValue(_unique));
  builder.add("sparse", VPackValue(_sparse));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return a VelocyPack representation of the index figures
////////////////////////////////////////////////////////////////////////////////

void SkiplistIndex::toVelocyPackFigures(VPackBuilder& builder) const {
  TRI_ASSERT(builder.isOpenObject());
  builder.add("memory", VPackValue(memory()));
  _skiplistIndex->appendToVelocyPack(builder);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into a skiplist index
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex::insert(arangodb::Transaction* trx, TRI_voc_rid_t revisionId, 
                          VPackSlice const& doc, bool isRollback) {
  std::vector<SkiplistIndexElement*> elements;

  int res;
  try {
    res = fillElement<SkiplistIndexElement>(elements, revisionId, doc);
  } catch (...) {
    res = TRI_ERROR_OUT_OF_MEMORY;
  }

  if (res != TRI_ERROR_NO_ERROR) {
    for (auto& element : elements) {
      // free all elements to prevent leak
      element->free();
    }
    return res;
  }
  
  ManagedDocumentResult result(trx); 
  IndexLookupContext context(trx, _collection, &result, numPaths()); 

  // insert into the index. the memory for the element will be owned or freed
  // by the index
  size_t const count = elements.size();

  for (size_t i = 0; i < count; ++i) {
    res = _skiplistIndex->insert(&context, elements[i]);

    if (res != TRI_ERROR_NO_ERROR) {
      // Note: this element is freed already
      for (size_t j = i; j < count; ++j) {
        elements[j]->free();
      }
      for (size_t j = 0; j < i; ++j) {
        _skiplistIndex->remove(&context, elements[j]);
        // No need to free elements[j] skiplist has taken over already
      }

      if (res == TRI_ERROR_ARANGO_UNIQUE_CONSTRAINT_VIOLATED && !_unique) {
        // We ignore unique_constraint violated if we are not unique
        res = TRI_ERROR_NO_ERROR;
      }
      break;
    }
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a skiplist index
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex::remove(arangodb::Transaction* trx, TRI_voc_rid_t revisionId,
                          VPackSlice const& doc, bool isRollback) {
  std::vector<SkiplistIndexElement*> elements;

  int res;
  try {
    res = fillElement<SkiplistIndexElement>(elements, revisionId, doc);
  } catch (...) {
    res = TRI_ERROR_OUT_OF_MEMORY;
  }

  if (res != TRI_ERROR_NO_ERROR) {
    for (auto& element : elements) {
      // free all elements to prevent leak
      element->free();
    }
    return res;
  }
  
  ManagedDocumentResult result(trx); 
  IndexLookupContext context(trx, _collection, &result, numPaths()); 

  // attempt the removal for skiplist indexes
  // ownership for the index element is transferred to the index
  size_t const count = elements.size();

  for (size_t i = 0; i < count; ++i) {
    int result = _skiplistIndex->remove(&context, elements[i]);

    // we may be looping through this multiple times, and if an error
    // occurs, we want to keep it
    if (result != TRI_ERROR_NO_ERROR) {
      res = result;
    }
    
    elements[i]->free();
  }

  return res;
}

int SkiplistIndex::unload() {
  _skiplistIndex->truncate(true);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Checks if the interval is valid. It is declared invalid if
///        one border is nullptr or the right is lower than left.
////////////////////////////////////////////////////////////////////////////////

bool SkiplistIndex::intervalValid(void* userData, Node* left, Node* right) const {
  if (left == nullptr) {
    return false;
  }
  if (right == nullptr) {
    return false;
  }
  if (left == right) {
    // Exactly one result. Improve speed on unique indexes
    return true;
  }
  if (CmpElmElm(userData, left->document(), right->document(),
                arangodb::basics::SKIPLIST_CMP_TOTORDER) > 0) {
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a key with an element in a skip list, generic callback
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex::KeyElementComparator::operator()(void* userData,
    VPackSlice const* leftKey, SkiplistIndexElement const* rightElement) const {
  TRI_ASSERT(nullptr != leftKey);
  TRI_ASSERT(nullptr != rightElement);

  // Note that the key might contain fewer fields than there are indexed
  // attributes, therefore we only run the following loop to
  // leftKey->_numFields.
  TRI_ASSERT(leftKey->isArray());
  size_t numFields = leftKey->length();
  for (size_t j = 0; j < numFields; j++) {
    VPackSlice field = leftKey->at(j);
    int compareResult = CompareKeyElement(userData, &field, rightElement, j);
    if (compareResult != 0) {
      return compareResult;
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares two elements in a skip list, this is the generic callback
////////////////////////////////////////////////////////////////////////////////

int SkiplistIndex::ElementElementComparator::operator()(
    void* userData,
    SkiplistIndexElement const* leftElement,
    SkiplistIndexElement const* rightElement,
    arangodb::basics::SkipListCmpType cmptype) const {
  TRI_ASSERT(nullptr != leftElement);
  TRI_ASSERT(nullptr != rightElement);

  // ..........................................................................
  // The document could be the same -- so no further comparison is required.
  // ..........................................................................

  if (leftElement == rightElement ||
      (!_idx->_skiplistIndex->isArray() &&
       leftElement->revisionId() == rightElement->revisionId())) {
    return 0;
  }

  for (size_t j = 0; j < _idx->numPaths(); j++) {
    int compareResult = CompareElementElement(userData, leftElement, j, rightElement, j);

    if (compareResult != 0) {
      return compareResult;
    }
  }

  // ...........................................................................
  // This is where the difference between the preorder and the proper total
  // order comes into play. Here if the 'keys' are the same,
  // but the doc ptr is different (which it is since we are here), then
  // we return 0 if we use the preorder and look at the _key attribute
  // otherwise.
  // ...........................................................................

  if (arangodb::basics::SKIPLIST_CMP_PREORDER == cmptype) {
    return 0;
  }
    
  // We break this tie in the key comparison by looking at the key:
  if (leftElement->revisionId() < rightElement->revisionId()) {
    return -1;
  }
  if (leftElement->revisionId() > rightElement->revisionId()) {
    return 1;
  }
  return 0;
}

bool SkiplistIndex::accessFitsIndex(
    arangodb::aql::AstNode const* access, arangodb::aql::AstNode const* other,
    arangodb::aql::AstNode const* op, arangodb::aql::Variable const* reference,
    std::unordered_map<size_t, std::vector<arangodb::aql::AstNode const*>>&
        found,
    std::unordered_set<std::string>& nonNullAttributes,
    bool isExecution) const {
  if (!this->canUseConditionPart(access, other, op, reference, nonNullAttributes, isExecution)) {
    return false;
  }

  arangodb::aql::AstNode const* what = access;
  std::pair<arangodb::aql::Variable const*,
            std::vector<arangodb::basics::AttributeName>> attributeData;

  if (op->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
    if (!what->isAttributeAccessForVariable(attributeData) ||
        attributeData.first != reference) {
      // this access is not referencing this collection
      return false;
    }
    if (arangodb::basics::TRI_AttributeNamesHaveExpansion(
            attributeData.second)) {
      // doc.value[*] == 'value'
      return false;
    }
    if (isAttributeExpanded(attributeData.second)) {
      // doc.value == 'value' (with an array index)
      return false;
    }
  } else {
    // ok, we do have an IN here... check if it's something like 'value' IN
    // doc.value[*]
    TRI_ASSERT(op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN);
    bool canUse = false;

    if (what->isAttributeAccessForVariable(attributeData) &&
        attributeData.first == reference &&
        !arangodb::basics::TRI_AttributeNamesHaveExpansion(
            attributeData.second) &&
        attributeMatches(attributeData.second)) {
      // doc.value IN 'value'
      // can use this index
      canUse = true;
    } else {
      // check for  'value' IN doc.value  AND  'value' IN doc.value[*]
      what = other;
      if (what->isAttributeAccessForVariable(attributeData) &&
          attributeData.first == reference &&
          isAttributeExpanded(attributeData.second) &&
          attributeMatches(attributeData.second)) {
        canUse = true;
      }
    }

    if (!canUse) {
      return false;
    }
  }

  std::vector<arangodb::basics::AttributeName> const& fieldNames =
      attributeData.second;

  for (size_t i = 0; i < _fields.size(); ++i) {
    if (_fields[i].size() != fieldNames.size()) {
      // attribute path length differs
      continue;
    }

    if (this->isAttributeExpanded(i) &&
        op->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
      // If this attribute is correct or not, it could only serve for IN
      continue;
    }

    bool match = arangodb::basics::AttributeName::isIdentical(_fields[i],
                                                              fieldNames, true);

    if (match) {
      // mark ith attribute as being covered
      auto it = found.find(i);

      if (it == found.end()) {
        found.emplace(i, std::vector<arangodb::aql::AstNode const*>{op});
      } else {
        (*it).second.emplace_back(op);
      }
      TRI_IF_FAILURE("SkiplistIndex::accessFitsIndex") {
        THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
      }

      return true;
    }
  }

  return false;
}

void SkiplistIndex::matchAttributes(
    arangodb::aql::AstNode const* node,
    arangodb::aql::Variable const* reference,
    std::unordered_map<size_t, std::vector<arangodb::aql::AstNode const*>>&
        found,
    size_t& values, 
    std::unordered_set<std::string>& nonNullAttributes,
    bool isExecution) const {
  for (size_t i = 0; i < node->numMembers(); ++i) {
    auto op = node->getMember(i);

    switch (op->type) {
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
        TRI_ASSERT(op->numMembers() == 2);
        accessFitsIndex(op->getMember(0), op->getMember(1), op, reference,
                        found, nonNullAttributes, isExecution);
        accessFitsIndex(op->getMember(1), op->getMember(0), op, reference,
                        found, nonNullAttributes, isExecution);
        break;

      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
        if (accessFitsIndex(op->getMember(0), op->getMember(1), op, reference,
                            found, nonNullAttributes, isExecution)) {
          auto m = op->getMember(1);
          if (m->isArray() && m->numMembers() > 1) {
            // attr IN [ a, b, c ]  =>  this will produce multiple items, so
            // count them!
            values += m->numMembers() - 1;
          }
        }
        break;

      default:
        break;
    }
  }
}

bool SkiplistIndex::accessFitsIndex(
    arangodb::aql::AstNode const* access, arangodb::aql::AstNode const* other,
    arangodb::aql::AstNode const* op, arangodb::aql::Variable const* reference,
    std::vector<std::vector<arangodb::aql::AstNode const*>>& found,
    std::unordered_set<std::string>& nonNullAttributes) const {
  if (!this->canUseConditionPart(access, other, op, reference, nonNullAttributes, true)) {
    return false;
  }

  arangodb::aql::AstNode const* what = access;
  std::pair<arangodb::aql::Variable const*,
            std::vector<arangodb::basics::AttributeName>> attributeData;

  if (op->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
    if (!what->isAttributeAccessForVariable(attributeData) ||
        attributeData.first != reference) {
      // this access is not referencing this collection
      return false;
    }
    if (arangodb::basics::TRI_AttributeNamesHaveExpansion(
            attributeData.second)) {
      // doc.value[*] == 'value'
      return false;
    }
    if (isAttributeExpanded(attributeData.second)) {
      // doc.value == 'value' (with an array index)
      return false;
    }
  } else {
    // ok, we do have an IN here... check if it's something like 'value' IN
    // doc.value[*]
    TRI_ASSERT(op->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN);
    bool canUse = false;

    if (what->isAttributeAccessForVariable(attributeData) &&
        attributeData.first == reference &&
        !arangodb::basics::TRI_AttributeNamesHaveExpansion(
            attributeData.second) &&
        attributeMatches(attributeData.second)) {
      // doc.value IN 'value'
      // can use this index
      canUse = true;
    } else {
      // check for  'value' IN doc.value  AND  'value' IN doc.value[*]
      what = other;
      if (what->isAttributeAccessForVariable(attributeData) &&
          attributeData.first == reference &&
          isAttributeExpanded(attributeData.second) &&
          attributeMatches(attributeData.second)) {
        canUse = true;
      }
    }

    if (!canUse) {
      return false;
    }
  }

  std::vector<arangodb::basics::AttributeName> const& fieldNames =
      attributeData.second;

  for (size_t i = 0; i < _fields.size(); ++i) {
    if (_fields[i].size() != fieldNames.size()) {
      // attribute path length differs
      continue;
    }

    if (this->isAttributeExpanded(i) &&
        op->type != arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
      // If this attribute is correct or not, it could only serve for IN
      continue;
    }

    bool match = arangodb::basics::AttributeName::isIdentical(_fields[i],
                                                              fieldNames, true);

    if (match) {
      // mark ith attribute as being covered
      found[i].emplace_back(op);

      TRI_IF_FAILURE("SkiplistIndex::accessFitsIndex") {
        THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
      }

      return true;
    }
  }

  return false;
}

bool SkiplistIndex::findMatchingConditions(
    arangodb::aql::AstNode const* node,
    arangodb::aql::Variable const* reference,
    std::vector<std::vector<arangodb::aql::AstNode const*>>& mapping,
    bool& usesIn) const {
  std::unordered_set<std::string> nonNullAttributes;
  usesIn = false;

  for (size_t i = 0; i < node->numMembers(); ++i) {
    auto op = node->getMember(i);

    switch (op->type) {
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE: {
        TRI_ASSERT(op->numMembers() == 2);
        accessFitsIndex(op->getMember(0), op->getMember(1), op, reference,
                        mapping, nonNullAttributes);
        accessFitsIndex(op->getMember(1), op->getMember(0), op, reference,
                        mapping, nonNullAttributes);
        break;
      }
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN: {
        auto m = op->getMember(1);
        if (accessFitsIndex(op->getMember(0), m, op, reference, mapping, nonNullAttributes)) {
          if (m->numMembers() == 0) {
            // We want to do an IN [].
            // No results
            // Even if we cannot use the index.
            return false;
          }
        }
        break;
      }

      default: {
        TRI_ASSERT(false);
        break;
      }
    }
  }

  for (size_t i = 0; i < mapping.size(); ++i) {
    auto const& conditions = mapping[i];
    if (conditions.empty()) {
      // We do not have any condition for this field.
      // Remove it and everything afterwards.
      mapping.resize(i);
      TRI_ASSERT(i == mapping.size());
      break;
    }
    TRI_ASSERT(conditions.size() <= 2);
    auto const& first = conditions[0];
    switch (first->type) {
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
        if (first->getMember(1)->isArray()) {
          usesIn = true;
        }
      // Fall through intentional
      case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
        TRI_ASSERT(conditions.size() == 1);
        break;

      default: {
        // All conditions after this cannot be used.
        // shrink and break outer for loop
        mapping.resize(i + 1);
        TRI_ASSERT(i + 1 == mapping.size());
        return true;
      }
    }
  }
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
  for (auto const& it : mapping) {
    TRI_ASSERT(!it.empty());
  }
#endif

  return true;
}

IndexIterator* SkiplistIndex::iteratorForCondition(
    arangodb::Transaction* trx, 
    ManagedDocumentResult* mmdr,
    arangodb::aql::AstNode const* node,
    arangodb::aql::Variable const* reference, bool reverse) const {
  std::vector<std::vector<arangodb::aql::AstNode const*>> mapping;
  bool usesIn = false;
  if (node != nullptr) {
    mapping.resize(_fields.size());  // We use the default constructor. Mapping
                                     // will have _fields many entries.
    TRI_ASSERT(mapping.size() == _fields.size());
    if (!findMatchingConditions(node, reference, mapping, usesIn)) {
      return new EmptyIndexIterator(_collection, trx, mmdr, this);
    }
  } else {
    TRI_IF_FAILURE("SkiplistIndex::noSortIterator") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }
  }

  TRI_IF_FAILURE("SkiplistIndex::noIterator") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }

  if (usesIn) {
    auto builder = std::make_unique<SkiplistInLookupBuilder>(
        trx, mapping, reference, reverse);
    return new SkiplistIterator2(_collection, trx, mmdr, this, _skiplistIndex, numPaths(), CmpElmElm, reverse,
                                 builder.release());
  }
  auto builder =
      std::make_unique<SkiplistLookupBuilder>(trx, mapping, reference, reverse);
  return new SkiplistIterator2(_collection, trx, mmdr, this, _skiplistIndex, numPaths(), CmpElmElm, reverse,
                               builder.release());
}

bool SkiplistIndex::supportsFilterCondition(
    arangodb::aql::AstNode const* node,
    arangodb::aql::Variable const* reference, size_t itemsInIndex,
    size_t& estimatedItems, double& estimatedCost) const {
  std::unordered_map<size_t, std::vector<arangodb::aql::AstNode const*>> found;
  std::unordered_set<std::string> nonNullAttributes;
  size_t values = 0;
  matchAttributes(node, reference, found, values, nonNullAttributes, false);

  bool lastContainsEquality = true;
  size_t attributesCovered = 0;
  size_t attributesCoveredByEquality = 0;
  double equalityReductionFactor = 20.0;
  estimatedCost = static_cast<double>(itemsInIndex);

  for (size_t i = 0; i < _fields.size(); ++i) {
    auto it = found.find(i);

    if (it == found.end()) {
      // index attribute not covered by condition
      break;
    }

    // check if the current condition contains an equality condition
    auto const& nodes = (*it).second;
    bool containsEquality = false;
    for (size_t j = 0; j < nodes.size(); ++j) {
      if (nodes[j]->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
          nodes[j]->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
        containsEquality = true;
        break;
      }
    }

    if (!lastContainsEquality) {
      // unsupported condition. must abort
      break;
    }

    ++attributesCovered;
    if (containsEquality) {
      ++attributesCoveredByEquality;
      estimatedCost /= equalityReductionFactor;

      // decrease the effect of the equality reduction factor
      equalityReductionFactor *= 0.25;
      if (equalityReductionFactor < 2.0) {
        // equalityReductionFactor shouldn't get too low
        equalityReductionFactor = 2.0;
      }
    } else {
      // quick estimate for the potential reductions caused by the conditions
      if (nodes.size() >= 2) {
        // at least two (non-equality) conditions. probably a range with lower
        // and upper bound defined
        estimatedCost /= 7.5;
      } else {
        // one (non-equality). this is either a lower or a higher bound
        estimatedCost /= 2.0;
      }
    }

    lastContainsEquality = containsEquality;
  }

  if (values == 0) {
    values = 1;
  }

  if (attributesCoveredByEquality == _fields.size() && unique()) {
    // index is unique and condition covers all attributes by equality
    if (estimatedItems >= values) {
      // reduce costs due to uniqueness
      estimatedItems = values;
      estimatedCost = static_cast<double>(estimatedItems);
    } else {
      // cost is already low... now slightly prioritize the unique index
      estimatedCost *= 0.995;
    }
    return true;
  }

  if (attributesCovered > 0 &&
      (!_sparse || attributesCovered == _fields.size())) {
    // if the condition contains at least one index attribute and is not sparse,
    // or the index is sparse and all attributes are covered by the condition,
    // then it can be used (note: additional checks for condition parts in
    // sparse indexes are contained in Index::canUseConditionPart)
    estimatedItems = static_cast<size_t>((std::max)(
        static_cast<size_t>(estimatedCost * values), static_cast<size_t>(1)));
    estimatedCost *= static_cast<double>(values);
    return true;
  }

  // no condition
  estimatedItems = itemsInIndex;
  estimatedCost = static_cast<double>(estimatedItems);
  return false;
}

bool SkiplistIndex::supportsSortCondition(
    arangodb::aql::SortCondition const* sortCondition,
    arangodb::aql::Variable const* reference, size_t itemsInIndex,
    double& estimatedCost, size_t& coveredAttributes) const {
  TRI_ASSERT(sortCondition != nullptr);

  if (!_sparse) {
    // only non-sparse indexes can be used for sorting
    if (!_useExpansion && sortCondition->isUnidirectional() &&
        sortCondition->isOnlyAttributeAccess()) {
      coveredAttributes = sortCondition->coveredAttributes(reference, _fields);

      if (coveredAttributes >= sortCondition->numAttributes()) {
        // sort is fully covered by index. no additional sort costs!
        estimatedCost = 0.0;
        return true;
      } else if (coveredAttributes > 0) {
        estimatedCost = (itemsInIndex / coveredAttributes) *
                        std::log2(static_cast<double>(itemsInIndex));
        return true;
      }
    }
  }

  coveredAttributes = 0;
  // by default no sort conditions are supported
  if (itemsInIndex > 0) {
    estimatedCost = itemsInIndex * std::log2(static_cast<double>(itemsInIndex));
  } else {
    estimatedCost = 0.0;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief specializes the condition for use with the index
////////////////////////////////////////////////////////////////////////////////

arangodb::aql::AstNode* SkiplistIndex::specializeCondition(
    arangodb::aql::AstNode* node,
    arangodb::aql::Variable const* reference) const {
  std::unordered_map<size_t, std::vector<arangodb::aql::AstNode const*>> found;
  std::unordered_set<std::string> nonNullAttributes;
  size_t values = 0;
  matchAttributes(node, reference, found, values, nonNullAttributes, false);

  std::vector<arangodb::aql::AstNode const*> children;
  bool lastContainsEquality = true;

  for (size_t i = 0; i < _fields.size(); ++i) {
    auto it = found.find(i);

    if (it == found.end()) {
      // index attribute not covered by condition
      break;
    }

    // check if the current condition contains an equality condition
    auto& nodes = (*it).second;
    bool containsEquality = false;
    for (size_t j = 0; j < nodes.size(); ++j) {
      if (nodes[j]->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ ||
          nodes[j]->type == arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN) {
        containsEquality = true;
        break;
      }
    }

    if (!lastContainsEquality) {
      // unsupported condition. must abort
      break;
    }

    std::sort(
        nodes.begin(), nodes.end(),
        [](arangodb::aql::AstNode const* lhs, arangodb::aql::AstNode const* rhs)
            -> bool { return sortWeight(lhs) < sortWeight(rhs); });

    lastContainsEquality = containsEquality;
    std::unordered_set<int> operatorsFound;
    for (auto& it : nodes) {
      // do not let duplicate or related operators pass
      if (isDuplicateOperator(it, operatorsFound)) {
        continue;
      }
      operatorsFound.emplace(static_cast<int>(it->type));
      children.emplace_back(it);
    }
  }

  while (node->numMembers() > 0) {
    node->removeMemberUnchecked(0);
  }

  for (auto& it : children) {
    node->addMember(it);
  }
  return node;
}

bool SkiplistIndex::isDuplicateOperator(
    arangodb::aql::AstNode const* node,
    std::unordered_set<int> const& operatorsFound) const {
  auto type = node->type;
  if (operatorsFound.find(static_cast<int>(type)) != operatorsFound.end()) {
    // duplicate operator
    return true;
  }

  if (operatorsFound.find(
          static_cast<int>(arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ)) !=
          operatorsFound.end() ||
      operatorsFound.find(
          static_cast<int>(arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN)) !=
          operatorsFound.end()) {
    return true;
  }

  bool duplicate = false;
  switch (type) {
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE)) !=
                  operatorsFound.end();
      break;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LE:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_LT)) !=
                  operatorsFound.end();
      break;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE)) !=
                  operatorsFound.end();
      break;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GE:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_GT)) !=
                  operatorsFound.end();
      break;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN)) !=
                  operatorsFound.end();
      break;
    case arangodb::aql::NODE_TYPE_OPERATOR_BINARY_IN:
      duplicate = operatorsFound.find(static_cast<int>(
                      arangodb::aql::NODE_TYPE_OPERATOR_BINARY_EQ)) !=
                  operatorsFound.end();
      break;
    default: {
      // ignore
    }
  }

  return duplicate;
}
