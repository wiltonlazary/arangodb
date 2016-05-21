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

#include "Supervision.h"

#include "Agent.h"
#include "Store.h"

#include "Basics/ConditionLocker.h"
#include "VocBase/server.h"

#include <thread>

using namespace arangodb;

namespace arangodb {
namespace consensus {

std::string printTimestamp(Supervision::TimePoint const& t) {
  time_t tt = std::chrono::system_clock::to_time_t(t);
  struct tm tb;
  size_t const len (21);
  char buffer[len];
  TRI_gmtime(tt, &tb);
  ::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tb);
  return std::string(buffer, len);
}

inline arangodb::consensus::write_ret_t makeReport(Agent* _agent,
                                                   Builder const& report) {
  query_t envelope = std::make_shared<Builder>();
  try {
    envelope->openArray();
    envelope->add(report.slice());
    envelope->close();
  } catch (std::exception const& e) {
    LOG_TOPIC(ERR, Logger::AGENCY) << "Supervision failed to make report.";
    LOG_TOPIC(ERR, Logger::AGENCY) << e.what();
  }
  return _agent->write(envelope);
}

static std::string const pendingPrefix = "/Supervision/Jobs/Pending/";
static std::string const collectionsPrefix = "/Plan/Collections/";
static std::string const toDoPrefix = "/Target/ToDo";

struct MoveShard : public Job {

  MoveShard (std::string const& creator,    std::string const& database,
             std::string const& collection, std::string const& shard,
             std::string const& fromServer, std::string const& toServer,
             uint64_t const& jobId, std::string const& agencyPrefix,
             Agent* agent) {

    todoEntry (creator, database, collection, shard, fromServer, toServer,
               jobId, agencyPrefix, agent);
    
  }

  void todoEntry (std::string const& creator,    std::string const& database,
                  std::string const& collection, std::string const& shard,
                  std::string const& fromServer, std::string const& toServer,
                  uint64_t const& jobId, std::string const& agencyPrefix,
                  Agent* agent) {
    Builder todo;
    todo.openArray(); todo.openObject();
    todo.add(VPackValue(agencyPrefix + toDoPrefix + "/"
                        + std::to_string(jobId)));
    {
      VPackObjectBuilder entry(&todo);
      todo.add("creator", VPackValue(creator));
      todo.add("type", VPackValue("moveShard"));
      todo.add("database", VPackValue(database));
      todo.add("collection", VPackValue(collection));
      todo.add("shard", VPackValue(shard));
      todo.add("fromServer", VPackValue(fromServer));
      todo.add("toServer", VPackValue(toServer));
    }
    todo.close(); todo.close();
    write_ret_t ret = makeReport(agent, todo);
    
  }

  
};

struct FailedServer : public Job {
  FailedServer(Node const& snapshot, Agent* agent, uint64_t jobId,
               std::string const& failed, std::string agencyPrefix) {
    // 1. find all shards in plan, where failed was leader.
    // 2. swap positions in plan between failed and a random in sync follower

    Node::Children const& databases =
        snapshot("/Plan/Collections").children();

    for (auto const& database : databases) {
      for (auto const& collptr : database.second->children()) {
        Node const& collection = *(collptr.second);
        Node const& replicationFactor = collection("replicationFactor");
        if (replicationFactor.slice().getUInt() > 1) {
          for (auto const& shard : collection("shards").children()) {
            VPackArrayIterator dbsit(shard.second->slice());

            if ((*dbsit.begin()).copyString() != failed) {  // Cannot do much
              continue;
            }

            //MoveShard ()
            reportJobInSupervision(jobId, shard, failed, agencyPrefix);
            planChanges(collptr, database, shard, agencyPrefix);
          }
        }
      }
    }
  }

  void reportJobInSupervision(uint64_t jobId,
                              std::pair<std::string,
                              std::shared_ptr<Node>> const& shard,
                              std::string const& serverID,
                              std::string const& agencyPrefix) {

    std::string const& shardId = shard.first;
    VPackSlice const& dbservers = shard.second->slice();
    std::string path = agencyPrefix + pendingPrefix
      + arangodb::basics::StringUtils::itoa(jobId);
    query_t envelope = std::make_shared<Builder>();

    Builder report;
    report.openArray();
    report.openObject();
    report.add(path, VPackValue(VPackValueType::Object));
    report.add("type", VPackValue("shard"));
    report.add("action", VPackValue("DB server fail"));
    report.add("failed", VPackValue(serverID));
    report.add("shard", VPackValue(shardId));
    report.add("dbservers", VPackValue(serverID));
    report.add("old", dbservers);  // old order

    report.add("new", VPackValue(VPackValueType::Array));
    for (size_t i = 1; i < dbservers.length(); ++i) {  // new order
      report.add(dbservers[i]);
    }
    report.add(dbservers[0]);
    report.close();

    report.close();
    report.close();
    report.close();
    // makeReport(envelope, report);

  }

  void planChanges(
      std::pair<std::string, std::shared_ptr<Node>> const& database,
      std::pair<std::string, std::shared_ptr<Node>> const& collection,
      std::pair<std::string, std::shared_ptr<Node>> const& shard,
      std::string const& agencyPrefix) {
    std::string path = agencyPrefix + collectionsPrefix + database.first + "/" +
                       collection.first + "/shards/" + shard.first;

  }
};
}
}

using namespace arangodb::consensus;

std::string Supervision::_agencyPrefix = "/arango";

Supervision::Supervision()
    : arangodb::Thread("Supervision"),
      _agent(nullptr),
      _snapshot("Supervision"),
      _frequency(5),
      _gracePeriod(10),
      _jobId(0),
      _jobIdMax(0) {}

Supervision::~Supervision() { shutdown(); };

void Supervision::wakeUp() {
  TRI_ASSERT(_agent != nullptr);
  _snapshot = _agent->readDB().get(_agencyPrefix);
  _cv.signal();
}

static std::string const syncPrefix = "/Sync/ServerStates/";
static std::string const supervisionPrefix = "/Supervision/Health/";
static std::string const planDBServersPrefix = "/Plan/DBServers";

std::vector<check_t> Supervision::checkDBServers() {
  std::vector<check_t> ret;
  Node::Children const& machinesPlanned =
      _snapshot(planDBServersPrefix).children();

  for (auto const& machine : machinesPlanned) {
    ServerID const& serverID = machine.first;
    auto it = _vitalSigns.find(serverID);
    std::string lastHeartbeatTime =
        _snapshot(syncPrefix + serverID + "/time").toJson();
    std::string lastHeartbeatStatus =
        _snapshot(syncPrefix + serverID + "/status").toJson();

    if (it != _vitalSigns.end()) {  // Existing server

      query_t report = std::make_shared<Builder>();
      report->openArray();
      report->openArray();
      report->openObject();
      report->add(_agencyPrefix + supervisionPrefix + serverID,
                  VPackValue(VPackValueType::Object));
      report->add("LastHearbeatReceived",
                  VPackValue(printTimestamp(it->second->myTimestamp)));
      report->add("LastHearbeatSent", VPackValue(it->second->serverTimestamp));
      report->add("LastHearbeatStatus", VPackValue(lastHeartbeatStatus));

      if (it->second->serverTimestamp == lastHeartbeatTime) {
        report->add("Status", VPackValue("DOWN"));
        std::chrono::seconds t{0};
        t = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - it->second->myTimestamp);
        if (t.count() > _gracePeriod) {  // Failure
          if (it->second->maintenance() == 0) {
            it->second->maintenance(TRI_NewTickServer());
            FailedServer fsj(_snapshot, _agent, it->second->maintenance(),
                             serverID, _agencyPrefix);
          }
        }

      } else {
        report->add("Status", VPackValue("UP"));
        it->second->update(lastHeartbeatStatus, lastHeartbeatTime);
      }

      report->close();
      report->close();
      report->close();
      report->close();
      _agent->write(report);

    } else {  // New server
      _vitalSigns[serverID] =
          std::make_shared<VitalSign>(lastHeartbeatStatus, lastHeartbeatTime);
    }
  }

  auto itr = _vitalSigns.begin();
  while (itr != _vitalSigns.end()) {
    if (machinesPlanned.find(itr->first) == machinesPlanned.end()) {
      itr = _vitalSigns.erase(itr);
    } else {
      ++itr;
    }
  }

  return ret;
}

bool Supervision::doChecks(bool timedout) {
  if (_agent == nullptr) {
    return false;
  }

  _snapshot = _agent->readDB().get(_agencyPrefix);

  LOG_TOPIC(DEBUG, Logger::AGENCY) << "Sanity checks";
  /*std::vector<check_t> ret = */checkDBServers();

  return true;
}

void Supervision::run() {

  CONDITION_LOCKER(guard, _cv);
  TRI_ASSERT(_agent != nullptr);
  bool timedout = false;

  while (!this->isStopping()) {

    // Get agency prefix after cluster init
    if (_jobId == 0) {
      if (!updateAgencyPrefix(10)) {
        LOG_TOPIC(ERR, Logger::AGENCY)
          << "Cannot get prefix from Agency. Stopping supervision for good.";
        break;
      }
    }

    // Get bunch of job IDs from agency for future jobs
    if (_jobId == 0 || _jobId == _jobIdMax) {
      if (!getUniqueIds()) {
        LOG_TOPIC(ERR, Logger::AGENCY)
          << "Cannot get unique IDs from Agency. Stopping supervision for good.";
        break;
      }

      MoveShard ("coordinator1", "_system", "41", "s42", "DBServer1",
                 "DBServer2", _jobId++, _agencyPrefix, _agent);

    }

    // Wait unless leader 
    if (_agent->leading()) {
      timedout = _cv.wait(_frequency * 1000000);  // quarter second
    } else {
      _cv.wait();
    }

    // Do supervision
    doChecks(timedout);

    

  }
  
}

// Start thread
bool Supervision::start() {
  Thread::start();
  return true;
}

// Start thread with agent
bool Supervision::start(Agent* agent) {
  _agent = agent;
  _frequency = static_cast<long>(_agent->config().supervisionFrequency);

  return start();
}

// Get agency prefix fron agency
bool Supervision::updateAgencyPrefix (size_t nTries, int intervalSec) {

  // Try nTries to get agency's prefix in intervals 
  for (size_t i = 0; i < nTries; i++) {
    _snapshot = _agent->readDB().get("/");
    if (_snapshot.children().size() > 0) {
      _agencyPrefix = _snapshot.children().begin()->first;
      LOG_TOPIC(DEBUG, Logger::AGENCY) << "Agency prefix is " << _agencyPrefix;
      return true;
    }
    std::this_thread::sleep_for (std::chrono::seconds(intervalSec));
  }

  // Stand-alone agency
  return false;
  
}

static std::string const syncLatest = "/Sync/LatestID";
// Get bunch of cluster's unique ids from agency 
bool Supervision::getUniqueIds() {
  uint64_t latestId;

  try {
    latestId = std::stoul(
        _agent->readDB().get(_agencyPrefix + "/Sync/LatestID").slice().toJson());
  } catch (std::exception const& e) {
    LOG(WARN) << e.what();
    return false;
  }

  bool success = false;
  while (!success) {
    Builder uniq;
    uniq.openArray();
    uniq.openObject();
    uniq.add(_agencyPrefix + syncLatest, VPackValue(latestId + 100000)); // new 
    uniq.close();
    uniq.openObject();
    uniq.add(_agencyPrefix + syncLatest, VPackValue(latestId));      // precond
    uniq.close();
    uniq.close();

    auto result = makeReport(_agent, uniq);
    if (result.indices[0]) {
      _agent->waitFor(result.indices[0]);
      success = true;
      _jobId = latestId;
      _jobIdMax = latestId + 100000;
    }

    latestId = std::stoul(
        _agent->readDB().get(_agencyPrefix + "/Sync/LatestID").slice().toJson());
  }

  return success;
}

void Supervision::updateFromAgency() {
  auto const& jobsPending =
      _snapshot("/Supervision/Jobs/Pending").children();

  for (auto const& jobent : jobsPending) {
    auto const& job = *(jobent.second);

    LOG_TOPIC(WARN, Logger::AGENCY)
      << job.name() << " " << job("failed").toJson() << job("");
  }
}

void Supervision::beginShutdown() {
  // Personal hygiene
  Thread::beginShutdown();
}

Store const& Supervision::store() const {
  return _agent->readDB();
}
