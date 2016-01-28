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

#include "CollectionIds.h"
#include "Basics/conversions.h"
#include "Cluster/ClusterMethods.h"
#include "Utils/transactions.h"
#include "VocBase/document-collection.h"
#include "VocBase/update-policy.h"
#include "VocBase/vocbase.h"

#include <velocypack/Builder.h>
#include <velocypack/Slice.h>
#include <velocypack/Value.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;

////////////////////////////////////////////////////////////////////////////////
/// @brief the collection we store data in
////////////////////////////////////////////////////////////////////////////////

static char const* CollectionName = "_repository";

////////////////////////////////////////////////////////////////////////////////
/// @brief lookup the id for a collection name, local case
////////////////////////////////////////////////////////////////////////////////

static TRI_voc_cid_t LookupLocal(TRI_vocbase_t* vocbase,
                                 char const* name) {

  SingleCollectionReadOnlyTransaction trx(new StandaloneTransactionContext(),
                                          vocbase, CollectionName);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    return 0; // some error occurred, but don't block creation of collections by it
  }
  
  TRI_doc_mptr_copy_t mptr;
  res = trx.read(&mptr, name);

  if (res != TRI_ERROR_NO_ERROR) {
    return 0; // some error occurred, but don't block creation of collections by it
  }
  
  TRI_document_collection_t* document = trx.documentCollection();
  auto shaper = document->getShaper();  // PROTECTED by trx here
  
  TRI_df_marker_t const* marker = static_cast<TRI_df_marker_t const*>(mptr.getDataPtr());
  TRI_shaped_json_t shaped;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, marker);
  
  if (shaped._sid == TRI_SHAPE_ILLEGAL) {
    return 0;
  }

  std::unique_ptr<TRI_json_t> json(TRI_JsonShapedJson(shaper, &shaped));
  
  trx.finish(TRI_ERROR_NO_ERROR);

  if (json != nullptr) {
    auto idValue = TRI_LookupObjectJson(json.get(), "id");

    if (TRI_IsNumberJson(idValue)) {
      return idValue->_value._number;
    }

    if (TRI_IsStringJson(idValue)) {
      return TRI_UInt64String2(idValue->_value._string.data, idValue->_value._string.length - 1);
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief lookup the id for a collection name, coordinator case
////////////////////////////////////////////////////////////////////////////////

static TRI_voc_cid_t LookupCoordinator(TRI_vocbase_t* vocbase,
                                       char const* name) {

  auto headers = std::make_unique<std::map<std::string, std::string>>();
  std::map<std::string, std::string> responseHeaders;
  arangodb::rest::HttpResponse::HttpResponseCode responseCode;
  std::string resultBody;

  // check if an id for the collection is present 
  int res = arangodb::getDocumentOnCoordinator(vocbase->_name, CollectionName, name, 0, headers, true, responseCode, responseHeaders, resultBody);

  if (res != TRI_ERROR_NO_ERROR) {
    return 0;
  }

  auto builder = VPackParser::fromJson(resultBody);
  auto slice = builder->slice();
  
  if (slice.isObject() && slice.hasKey("id")) {
    return TRI_UInt64String(slice.get("id").copyString().c_str());
  }

  // we can safely ignore any errors
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the id for a collection name, local case
////////////////////////////////////////////////////////////////////////////////

static int StoreLocal(TRI_vocbase_t* vocbase,
                      char const* name,
                      TRI_voc_cid_t cid) {

  VPackBuilder builder;
  builder.openObject();
  builder.add(TRI_VOC_ATTRIBUTE_KEY, VPackValue(name));
  builder.add("id", VPackValue(std::to_string(cid)));
  builder.close();

  VPackSlice slice = builder.slice();

  SingleCollectionWriteTransaction<2> trx(new StandaloneTransactionContext(),
                                          vocbase, CollectionName);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    return res; // some error occurred, but don't block creation of collections by it
  }
  
  TRI_document_collection_t* document = trx.documentCollection();
  auto shaper = document->getShaper();  // PROTECTED by trx here
  
  TRI_shaped_json_t* shaped =
      TRI_ShapedJsonVelocyPack(shaper, slice, true);
  
  if (shaped == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // delete an existing object first
  res = trx.deleteDocument(name, TRI_DOC_UPDATE_LAST_WRITE, false, 0, nullptr);
  
  // and now store the new object
  TRI_doc_mptr_copy_t mptr;
  res = trx.createDocument((TRI_voc_key_t) name, &mptr, shaped, false);

  res = trx.finish(res);
  
  TRI_FreeShapedJson(TRI_UNKNOWN_MEM_ZONE, shaped);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the id for a collection name, coordinator case
////////////////////////////////////////////////////////////////////////////////

static int StoreCoordinator(TRI_vocbase_t* vocbase,
                            char const* name,
                            TRI_voc_cid_t cid) {

  // delete previously stored id first
  auto headers = std::make_unique<std::map<std::string, std::string>>();
  std::map<std::string, std::string> responseHeaders;
  arangodb::rest::HttpResponse::HttpResponseCode responseCode;
  std::string resultBody;

  // we will ignore the response code completely, as we'll be inserting directly after
  deleteDocumentOnCoordinator(vocbase->_name, CollectionName, name, 0, TRI_DOC_UPDATE_LAST_WRITE, false, headers, responseCode, responseHeaders, resultBody);
  
  // now store new id
  VPackBuilder builder;
  builder.openObject();
  builder.add(TRI_VOC_ATTRIBUTE_KEY, VPackValue(name));
  builder.add("id", VPackValue(std::to_string(cid)));
  builder.close();

  VPackSlice slice = builder.slice();

  responseHeaders.clear();

  // we'll ignore any errors here, because creation of the collection has already succeeded
  std::map<std::string, std::string> emptyHeaders;
  createDocumentOnCoordinator(vocbase->_name, CollectionName, false, slice, emptyHeaders, responseCode, responseHeaders, resultBody);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief lookup the id for a collection name
////////////////////////////////////////////////////////////////////////////////

TRI_voc_cid_t TRI_LookupIdCollectionRepository(TRI_vocbase_t* vocbase, char const* name) {
  if (ServerState::instance()->isCoordinator()) {
    return LookupCoordinator(vocbase, name);
  }
  return LookupLocal(vocbase, name);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the id for a collection name
////////////////////////////////////////////////////////////////////////////////

int TRI_StoreIdCollectionRepository(TRI_vocbase_t* vocbase, char const* name, TRI_voc_cid_t cid) {
  if (ServerState::instance()->isCoordinator()) {
    return StoreCoordinator(vocbase, name, cid);
  }
  return StoreLocal(vocbase, name, cid);
}

