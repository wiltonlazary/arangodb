////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
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

#include "ApplicationFeatures/ConfigFeature.h"

#include <iostream>

#include "Logger/Logger.h"
#include "Basics/FileUtils.h"
#include "Basics/StringUtils.h"
#include "ProgramOptions/IniFileParser.h"
#include "ProgramOptions/ProgramOptions.h"
#include "ProgramOptions/Section.h"

using namespace arangodb;
using namespace arangodb::basics;
using namespace arangodb::rest;
using namespace arangodb::options;

ConfigFeature::ConfigFeature(application_features::ApplicationServer* server,
                             std::string const& progname)
    : ApplicationFeature(server, "Config"),
      _file(""),
      _checkConfiguration(false),
      _progname(progname) {
  setOptional(false);
  requiresElevatedPrivileges(false);
  startsAfter("Logger");
}

void ConfigFeature::collectOptions(std::shared_ptr<ProgramOptions> options) {
  options->addOption("--configuration,-c", "the configuration file or 'none'",
                     new StringParameter(&_file));

  // add --config as an alias for --configuration. both point to the same variable!
  options->addHiddenOption("--config", "the configuration file or 'none'",
                     new StringParameter(&_file));

  options->addOption("--check-configuration", "check the configuration and exists",
                     new BooleanParameter(&_checkConfiguration));
}

void ConfigFeature::loadOptions(std::shared_ptr<ProgramOptions> options) {
  loadConfigFile(options);

  if (_checkConfiguration) {
    exit(EXIT_SUCCESS);
  }
}

void ConfigFeature::loadConfigFile(std::shared_ptr<ProgramOptions> options) {
  if (StringUtils::tolower(_file) == "none") {
    LOG_TOPIC(DEBUG, Logger::CONFIG) << "use no config file at all";
    return;
  }

  IniFileParser parser(options.get());

  // always prefer an explicitly given config file
  if (!_file.empty()) {
    LOG_TOPIC(DEBUG, Logger::CONFIG) << "using user supplied conifg file '"
                                     << _file << "'";

    if (!parser.parse(_file)) {
      exit(EXIT_FAILURE);
    }

    return;
  }

  // clang-format off
  //
  // check in order:
  //
  //   <PRGNAME>.conf
  //   ./etc/relative/<PRGNAME>.conf
  //   ${HOME}/.arangodb/<PRGNAME>.conf
  //   /etc/arangodb/<PRGNAME>.conf
  //
  // clang-format on

  std::string basename = _progname + ".conf";
  std::string filename =
      FileUtils::buildFilename(FileUtils::currentDirectory(), basename);

  LOG_TOPIC(DEBUG, Logger::CONFIG) << "checking '" << filename << "'";

  if (!FileUtils::exists(filename)) {
    filename = FileUtils::buildFilename(FileUtils::currentDirectory(),
                                        "etc/relative/" + basename);

    LOG_TOPIC(DEBUG, Logger::CONFIG) << "checking '" << filename << "'";

    if (!FileUtils::exists(filename)) {
      filename = FileUtils::buildFilename(FileUtils::homeDirectory(), basename);

      LOG_TOPIC(DEBUG, Logger::CONFIG) << "checking '" << filename << "'";

      if (!FileUtils::exists(filename)) {
        filename =
            FileUtils::buildFilename(FileUtils::configDirectory(), basename);

        LOG_TOPIC(DEBUG, Logger::CONFIG) << "checking '" << filename << "'";

        if (!FileUtils::exists(filename)) {
          LOG_TOPIC(DEBUG, Logger::CONFIG) << "cannot find any config file";
          return;
        }
      }
    }
  }

  std::string local = filename + ".local";

  LOG_TOPIC(DEBUG, Logger::CONFIG) << "checking override '" << local << "'";

  if (FileUtils::exists(local)) {
    LOG_TOPIC(DEBUG, Logger::CONFIG) << "loading '" << local << "'";

    if (!parser.parse(local)) {
      exit(EXIT_FAILURE);
    }
  }

  LOG_TOPIC(DEBUG, Logger::CONFIG) << "loading '" << filename << "'";

  if (!parser.parse(filename)) {
    exit(EXIT_FAILURE);
  }
}
