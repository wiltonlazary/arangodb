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

#ifndef ARANGOD_INDEXES_GEO_INDEX_H
#define ARANGOD_INDEXES_GEO_INDEX_H 1

#include "Basics/Common.h"
#include "GeoIndex/GeoIndex.h"
#include "Indexes/Index.h"
#include "VocBase/vocbase.h"
#include "VocBase/voc-types.h"

#include <velocypack/Builder.h>
#include <velocypack/velocypack-aliases.h>

// GeoCoordinate.data must be capable of storing revision ids
static_assert(sizeof(GeoCoordinate::data) >= sizeof(TRI_voc_rid_t), "invalid size of GeoCoordinate.data");

namespace arangodb {

class GeoIndex final : public Index {
 public:
  GeoIndex() = delete;

  GeoIndex(TRI_idx_iid_t, LogicalCollection*,
            arangodb::velocypack::Slice const&);

  ~GeoIndex();

 public:
  //////////////////////////////////////////////////////////////////////////////
  /// @brief geo index variants
  //////////////////////////////////////////////////////////////////////////////

  enum IndexVariant {
    INDEX_GEO_NONE = 0,
    INDEX_GEO_INDIVIDUAL_LAT_LON,
    INDEX_GEO_COMBINED_LAT_LON,
    INDEX_GEO_COMBINED_LON_LAT
  };

 public:
  IndexType type() const override {
    if (_variant == INDEX_GEO_COMBINED_LAT_LON ||
        _variant == INDEX_GEO_COMBINED_LON_LAT) {
      return TRI_IDX_TYPE_GEO1_INDEX;
    }

    return TRI_IDX_TYPE_GEO2_INDEX;
  }
  
  bool allowExpansion() const override { return false; }
  
  bool canBeDropped() const override { return true; }

  bool isSorted() const override { return false; }

  bool hasSelectivityEstimate() const override { return false; }

  size_t memory() const override;

  void toVelocyPack(VPackBuilder&, bool) const override;
  // Uses default toVelocyPackFigures

  bool matchesDefinition(VPackSlice const& info) const override;

  int insert(arangodb::Transaction*, TRI_voc_rid_t, arangodb::velocypack::Slice const&, bool isRollback) override;

  int remove(arangodb::Transaction*, TRI_voc_rid_t, arangodb::velocypack::Slice const&, bool isRollback) override;

  int unload() override;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief looks up all points within a given radius
  //////////////////////////////////////////////////////////////////////////////

  GeoCoordinates* withinQuery(arangodb::Transaction*, double, double,
                              double) const;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief looks up the nearest points
  //////////////////////////////////////////////////////////////////////////////

  GeoCoordinates* nearQuery(arangodb::Transaction*, double, double,
                            size_t) const;

  bool isSame(std::vector<std::string> const& location, bool geoJson) const {
    return (!_location.empty() && _location == location && _geoJson == geoJson);
  }

  bool isSame(std::vector<std::string> const& latitude,
              std::vector<std::string> const& longitude) const {
    return (!_latitude.empty() && !_longitude.empty() &&
            _latitude == latitude && _longitude == longitude);
  }
  
  static uint64_t fromRevision(TRI_voc_rid_t revisionId) {
    return static_cast<uint64_t>(revisionId);
  }

  static TRI_voc_rid_t toRevision(uint64_t internal) {
    return static_cast<TRI_voc_rid_t>(internal);
  }

 private:

  //////////////////////////////////////////////////////////////////////////////
  /// @brief attribute paths
  //////////////////////////////////////////////////////////////////////////////

  std::vector<std::string>  _location;
  std::vector<std::string>  _latitude;
  std::vector<std::string>  _longitude;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief the geo index variant (geo1 or geo2)
  //////////////////////////////////////////////////////////////////////////////

  IndexVariant _variant;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief whether the index is a geoJson index (latitude / longitude
  /// reversed)
  //////////////////////////////////////////////////////////////////////////////

  bool _geoJson;

  //////////////////////////////////////////////////////////////////////////////
  /// @brief the actual geo index
  //////////////////////////////////////////////////////////////////////////////

  GeoIdx* _geoIndex;
};
}

#endif
