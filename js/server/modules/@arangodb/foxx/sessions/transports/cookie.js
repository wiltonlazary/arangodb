'use strict';
////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2015 ArangoDB GmbH, Cologne, Germany
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
/// @author Alan Plum
////////////////////////////////////////////////////////////////////////////////

module.exports = function cookieTransport(cfg) {
  if (!cfg) {
    cfg = 'sid';
  }
  if (typeof cfg === 'string') {
    cfg = {name: cfg};
  }
  const ttl = cfg.ttl;
  const opts = cfg.secret ? {
    secret: cfg.secret,
    algorithm: cfg.algorithm
  } : undefined;
  return {
    get(req) {
      return req.cookie(cfg.name, opts);
    },
    set(res, value) {
      res.cookie(cfg.name, value, Object.assign({}, opts, {ttl}));
    },
    clear(res) {
      res.cookie(cfg.name, '', Object.assign({}, opts, {ttl: -1 * 60 * 60 * 24}));
    }
  };
};
