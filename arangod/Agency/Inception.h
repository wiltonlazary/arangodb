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
/// @author Kaveh Vahedipour
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_CONSENSUS_INCEPTION_H
#define ARANGOD_CONSENSUS_INCEPTION_H 1

#include <memory>

#include "Agency/AgencyCommon.h"
#include "Basics/Common.h"
#include "Basics/ConditionVariable.h"
#include "Basics/Mutex.h"
#include "Basics/Thread.h"

#include <velocypack/Builder.h>
#include <velocypack/velocypack-aliases.h>

namespace arangodb {
namespace consensus {

class Agent;

/// @brief This class organises the startup of the agency until the point
///        where the RAFT implementation can commence function
class Inception : public Thread {

public:

  /// @brief Default ctor
  Inception();

  /// @brief Construct with agent
  explicit Inception(Agent*);

  /// @brief Defualt dtor
  virtual ~Inception();

  /// @brief Report in from callbacks
  void reportIn(std::string const&, uint64_t);

  /// @brief Report in from other agents measurements
  void reportIn(query_t const&);

  void beginShutdown() override;
  void run() override;

 private:

  /// @brief Find active agency from persisted 
  bool activeAgencyFromPersistence();

  /// @brief We are a restarting active RAFT agent
  bool restartingActiveAgent();
  
  /// @brief Find active agency from command line
  bool activeAgencyFromCommandLine();

  /// @brief Try to estimate good RAFT min/max timeouts
  bool estimateRAFTInterval();

  /// @brief Gossip your way into the agency
  void gossip();

  Agent* _agent;                           //< @brief The agent
  arangodb::basics::ConditionVariable _cv; //< @brief For proper shutdown
  std::vector<double> _pings;              //< @brief pings
  mutable arangodb::Mutex _pLock;          //< @brief Guard pings
  std::vector<std::vector<double>> _measurements; //< @brief measurements
  mutable arangodb::Mutex _mLock;          //< @brief Guard measurements
  
};

}}

#endif
