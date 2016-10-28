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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_WAL_DOCUMENT_OPERATION_H
#define ARANGOD_WAL_DOCUMENT_OPERATION_H 1

#include "Basics/Common.h"
#include "VocBase/voc-types.h"

namespace arangodb {
class LogicalCollection;
class Transaction;

namespace wal {

struct DocumentOperation {
  enum class StatusType : uint8_t {
    CREATED,
    INDEXED,
    HANDLED,
    SWAPPED,
    REVERTED
  };
  
  DocumentOperation(LogicalCollection* collection,
                    TRI_voc_document_operation_e type);

  ~DocumentOperation();

  DocumentOperation* swap();

  void setRevisions(DocumentDescriptor const& oldRevision,
                    DocumentDescriptor const& newRevision);
  
  void setVPack(uint8_t const* vpack);

  void setTick(TRI_voc_tick_t tick) { _tick = tick; }
  TRI_voc_tick_t tick() const { return _tick; }
                    
  TRI_voc_document_operation_e type() const { return _type; }

  LogicalCollection* collection() const { return _collection; }
 
  void indexed() noexcept {
    TRI_ASSERT(_status == StatusType::CREATED);
    _status = StatusType::INDEXED;
  }

  void handled() noexcept {
    TRI_ASSERT(!_oldRevision.empty() || !_newRevision.empty());
    TRI_ASSERT(_status == StatusType::INDEXED);

    _status = StatusType::HANDLED;
  }
  
  void done() noexcept {
    _status = StatusType::SWAPPED;
  }

  void revert(arangodb::Transaction*);

 private:
  LogicalCollection* _collection;
  DocumentDescriptor _oldRevision;
  DocumentDescriptor _newRevision;
  TRI_voc_tick_t _tick;
  TRI_voc_document_operation_e _type;
  StatusType _status;
};
}
}

#endif
