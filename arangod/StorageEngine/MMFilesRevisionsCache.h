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

#ifndef ARANGOD_STORAGE_ENGINE_MMFILES_REVISIONS_CACHE_H
#define ARANGOD_STORAGE_ENGINE_MMFILES_REVISIONS_CACHE_H 1

#include "Basics/Common.h"
#include "Basics/AssocUnique.h"
#include "Basics/ReadWriteLock.h"
#include "StorageEngine/MMFilesDocumentPosition.h"
#include "VocBase/voc-types.h"

struct TRI_df_marker_t;

namespace arangodb {

class MMFilesRevisionsCache {
 public:
  MMFilesRevisionsCache();
  ~MMFilesRevisionsCache();
  
 public:
  void sizeHint(int64_t hint);
  void clear();
  MMFilesDocumentPosition lookup(TRI_voc_rid_t revisionId) const;
  void insert(TRI_voc_rid_t revisionId, uint8_t const* dataptr, TRI_voc_fid_t fid, bool isInWal);
  void update(TRI_voc_rid_t revisionId, uint8_t const* dataptr, TRI_voc_fid_t fid, bool isInWal);
  bool updateConditional(TRI_voc_rid_t revisionId, TRI_df_marker_t const* oldPosition, TRI_df_marker_t const* newPosition, TRI_voc_fid_t newFid, bool isInWal);
  void remove(TRI_voc_rid_t revisionId);
  MMFilesDocumentPosition fetchAndRemove(TRI_voc_rid_t revisionId);

 private:
  mutable arangodb::basics::ReadWriteLock _lock; 
  
  arangodb::basics::AssocUnique<TRI_voc_rid_t, MMFilesDocumentPosition> _positions;
};

} // namespace arangodb

#endif

