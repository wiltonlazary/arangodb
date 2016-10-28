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

#ifndef ARANGOD_V8_SERVER_V8_VOCINDEX_H
#define ARANGOD_V8_SERVER_V8_VOCINDEX_H 1

#include "Basics/Common.h"
#include "Utils/CollectionNameResolver.h"
#include "V8/v8-globals.h"
#include "V8Server/v8-vocbase.h"

namespace arangodb {
class Index;
class LogicalCollection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a index identifier
////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<arangodb::Index> TRI_LookupIndexByHandle(
    v8::Isolate* isolate, arangodb::CollectionNameResolver const* resolver,
    arangodb::LogicalCollection const* collection, v8::Handle<v8::Value> const val,
    bool ignoreNotFound);

void TRI_InitV8indexArangoDB(v8::Isolate* isolate,
                             v8::Handle<v8::ObjectTemplate> ArangoDBNS);

void TRI_InitV8indexCollection(v8::Isolate* isolate,
                               v8::Handle<v8::ObjectTemplate> rt);

// This could be static but is used in enterprise version as well
// Note that this returns a newly allocated object and ownership is transferred
// to the caller, which is expressed by the returned unique_ptr.
std::unique_ptr<arangodb::LogicalCollection> CreateCollectionCoordinator(
    arangodb::LogicalCollection* parameters);

#ifdef USE_ENTERPRISE
std::unique_ptr<arangodb::LogicalCollection> CreateCollectionCoordinatorEnterprise(
    TRI_col_type_e collectionType, TRI_vocbase_t* vocbase,
    arangodb::velocypack::Slice parameters);
#endif

int EnsureIndexCoordinator(std::string const& dbName, std::string const& cid,
                           arangodb::velocypack::Slice const slice, bool create,
                           arangodb::velocypack::Builder& resultBuilder,
                           std::string& errorMessage);

#ifdef USE_ENTERPRISE
int EnsureIndexCoordinatorEnterprise(
    arangodb::LogicalCollection const* collection,
    arangodb::velocypack::Slice const slice, bool create,
    arangodb::velocypack::Builder& resultBuilder, std::string& errorMessage);
#endif

int DropIndexCoordinator(
    std::string const& databaseName,
    std::string const& cid,
    TRI_idx_iid_t const iid);

#ifdef USE_ENTERPRISE
int DropIndexCoordinatorEnterprise(
    arangodb::LogicalCollection const* collection, TRI_idx_iid_t const iid);
#endif

#endif
