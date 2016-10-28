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

#include "CollectionExport.h"
#include "Basics/WriteLocker.h"
#include "Indexes/PrimaryIndex.h"
#include "StorageEngine/EngineSelectorFeature.h"
#include "StorageEngine/StorageEngine.h"
#include "Utils/CollectionGuard.h"
#include "Utils/SingleCollectionTransaction.h"
#include "Utils/StandaloneTransactionContext.h"
#include "VocBase/Ditch.h"
#include "VocBase/LogicalCollection.h"
#include "VocBase/RevisionCacheChunk.h"
#include "VocBase/vocbase.h"

using namespace arangodb;

CollectionExport::CollectionExport(TRI_vocbase_t* vocbase,
                                   std::string const& name,
                                   Restrictions const& restrictions)
    : _collection(nullptr),
      _ditch(nullptr),
      _name(name),
      _resolver(vocbase),
      _restrictions(restrictions) {
  // prevent the collection from being unloaded while the export is ongoing
  // this may throw
  _guard.reset(new arangodb::CollectionGuard(vocbase, _name.c_str(), false));

  _collection = _guard->collection();
  TRI_ASSERT(_collection != nullptr);
}

CollectionExport::~CollectionExport() {
  if (_ditch != nullptr) {
    _ditch->ditches()->freeDocumentDitch(_ditch, false);
  }

  for (auto& chunk : _chunks) {
    chunk->release();
  }
}

void CollectionExport::run(uint64_t maxWaitTime, size_t limit) {
  StorageEngine* engine = EngineSelectorFeature::ENGINE;

  // try to acquire the exclusive lock on the compaction
  engine->preventCompaction(_collection->vocbase(), [this](TRI_vocbase_t* vocbase) {
    // create a ditch under the compaction lock
    _ditch = _collection->ditches()->createDocumentDitch(false, __FILE__, __LINE__);
  });

  // now we either have a ditch or not
  if (_ditch == nullptr) {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_OUT_OF_MEMORY);
  }

  {
    static uint64_t const SleepTime = 10000;

    uint64_t tries = 0;
    uint64_t const maxTries = maxWaitTime / SleepTime;

    while (++tries < maxTries) {
      if (_collection->isFullyCollected()) {
        break;
      }
      usleep(SleepTime);
    }
  }

  {
    SingleCollectionTransaction trx(
        StandaloneTransactionContext::Create(_collection->vocbase()), _name,
        TRI_TRANSACTION_READ);

    // already locked by guard above
    trx.addHint(TRI_TRANSACTION_HINT_NO_USAGE_LOCK, true);
    int res = trx.begin();

    if (res != TRI_ERROR_NO_ERROR) {
      THROW_ARANGO_EXCEPTION(res);
    }
    
    size_t maxDocuments = _collection->primaryIndex()->size();
    if (limit > 0 && limit < maxDocuments) {
      maxDocuments = limit;
    } else {
      limit = maxDocuments;
    }

    _vpack.reserve(limit);

    ManagedDocumentResult mmdr(&trx);
    trx.invokeOnAllElements(_collection->name(), [this, &limit, &trx, &mmdr](SimpleIndexElement const& element) {
      if (limit == 0) {
        return false;
      }
      if (_collection->readRevisionConditional(&trx, mmdr, element.revisionId(), 0, true)) {
        _vpack.emplace_back(mmdr.vpack());
        --limit;
      }
      return true;
    });

    trx.transactionContext()->stealChunks(_chunks);

    trx.finish(res);
  }

  // delete guard right now as we're about to return
  // if we would continue holding the guard's collection lock and return,
  // and the export object gets later freed in a different thread, then all
  // would be lost. so we'll release the lock here and rely on the cleanup
  // thread not unloading the collection (as we've acquired a document ditch
  // for the collection already - this will prevent unloading of the collection's
  // datafiles etc.)
  _guard.reset();
}
