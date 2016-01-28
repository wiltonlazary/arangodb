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

#ifndef ARANGOD_VOC_BASE_COLLECTION_IDS_H
#define ARANGOD_VOC_BASE_COLLECTION_IDS_H 1

#include "Basics/Common.h"
#include "VocBase/voc-types.h"

struct TRI_vocbase_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief lookup the id for a collection name
////////////////////////////////////////////////////////////////////////////////

TRI_voc_cid_t TRI_LookupIdCollectionRepository(TRI_vocbase_t* vocbase, char const* name);

////////////////////////////////////////////////////////////////////////////////
/// @brief save the id for a collection name
////////////////////////////////////////////////////////////////////////////////

int TRI_StoreIdCollectionRepository(TRI_vocbase_t* vocbase, char const* name, TRI_voc_cid_t cid);

#endif

