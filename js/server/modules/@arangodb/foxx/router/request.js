'use strict';

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
/// @author Alan Plum
////////////////////////////////////////////////////////////////////////////////

const parseUrl = require('url').parse;
const formatUrl = require('url').format;
const joinPath = require('path').posix.join;
const typeIs = require('type-is').is;
const accepts = require('accepts');
const parseRange = require('range-parser');
const querystring = require('querystring');
const getRawBodyBuffer = require('internal').rawRequestBody;
const crypto = require('@arangodb/crypto');


module.exports = class SyntheticRequest {
  constructor(req, context) {
    this._url = parseUrl(req.url);
    this._raw = req;
    this.context = context;
    this.suffix = req.suffix.join('/');
    this.baseUrl = joinPath('/_db', encodeURIComponent(this._raw.database));
    this.path = this._url.pathname;
    this.pathParams = {};
    this.queryParams = querystring.decode(this._url.query);
    this.body = getRawBodyBuffer(req);
    this.rawBody = this.body;

    const server = extractServer(req, context.trustProxy);
    this.protocol = server.protocol;
    this.hostname = server.hostname;
    this.port = server.port;

    const client = extractClient(req, context.trustProxy);
    this.remoteAddress = client.ip;
    this.remoteAddresses = client.ips;
    this.remotePort = client.port;
  }

  // Node compat

  get headers() {
    return this._raw.headers;
  }

  get method() {
    return this._raw.requestType;
  }

  // Express compat

  get originalUrl() {
    return joinPath(
      '/_db',
      encodeURIComponent(this._raw.database),
      this._url.pathname
    ) + (this._url.search || '');
  }

  get secure() {
    return this.protocol === 'https';
  }

  get url() {
    return this.path + (this._url.search || '');
  }

  get xhr() {
    const header = this.headers['x-requested-with'];
    return Boolean(header && header.toLowerCase() === 'xmlhttprequest');
  }

  accepts() {
    const accept = accepts(this);
    return accept.types.apply(accept, arguments);
  }

  acceptsCharsets() {
    const accept = accepts(this);
    return accept.charsets.apply(accept, arguments);
  }

  acceptsEncodings() {
    const accept = accepts(this);
    return accept.encodings.apply(accept, arguments);
  }

  acceptsLanguages() {
    const accept = accepts(this);
    return accept.languages.apply(accept, arguments);
  }

  range(size) {
    const range = this.headers.range;
    if (!range) {
      return undefined;
    }
    return parseRange(size, range);
  }

  get(name) {
    const lc = name.toLowerCase();
    if (lc === 'referer' || lc === 'referrer') {
      return this.headers.referer || this.headers.referrer;
    }
    return this.headers[lc];
  }

  header(name) {
    return this.get(name);
  }

  is(mediaType) {
    if (!this.headers['content-type']) {
      return false;
    }
    if (!Array.isArray(mediaType)) {
      mediaType = Array.prototype.slice.call(arguments);
    }
    return typeIs(this, mediaType);
  }

  // idiosyncratic

  get database() {
    return this._raw.database;
  }

  json() {
    if (!this.rawBody) {
      return undefined;
    }
    return JSON.parse(this.rawBody.toString('utf-8'));
  }

  params(name) {
    if (hasOwnProperty.call(this.pathParams, name)) {
      return this.pathParams[name];
    }
    return this.queryParams[name];
  }

  cookie(name, opts) {
    if (typeof opts === 'string') {
      opts = {secret: opts};
    } else if (!opts) {
      opts = {};
    }
    const value = this._raw.cookies[name];
    if (value && opts.secret) {
      const signature = this._raw.cookies[`${name}.sig`];
      const valid = crypto.constantEquals(
        signature || '',
        crypto.hmac(opts.secret, value, opts.algorithm)
      );
      if (!valid) {
        return undefined;
      }
    }
    return value;
  }

  makeAbsolute(path, query) {
    const opts = {
      protocol: this.protocol,
      hostname: this.hostname,
      port: (this.secure ? this.port !== 443 : this.port !== 80) && this.port,
      pathname: joinPath(
        '/_db',
        encodeURIComponent(this._raw.database),
        this.context.mount,
        path
      )
    };
    if (query) {
      if (typeof query === 'string') {
        opts.search = query;
      } else {
        opts.query = query;
      }
    }
    return formatUrl(opts);
  }
};


function extractServer(req, trustProxy) {
  let hostname = req.server.address;
  let port = req.server.port;
  const protocol = (
    (trustProxy && req.headers['x-forwarded-proto'])
    || req.protocol
  );
  const secure = protocol === 'https';
  const hostHeader = (
    (trustProxy && req.headers['x-forwarded-host'])
    || req.headers.host
  );
  if (hostHeader) {
    const match = hostHeader.match(/^(.*):(\d+)$/) || [hostHeader, hostHeader];
    if (match) {
      hostname = match[1];
      port = match[2] ? Number(match[2]) : secure ? 443 : 80;
    }
  }
  return {protocol, hostname, port};
}


function extractClient(req, trustProxy) {
  let ip = req.client.address;
  let ips = [ip];
  const port = Number(
    (trustProxy && req.headers['x-forwarded-port'])
    || req.client.port
  );
  const forwardedFor = req.headers['x-forwarded-for'];
  if (trustProxy && forwardedFor) {
    const tokens = forwardedFor.split(/\s*,\s*/g).filter(Boolean);
    if (tokens.length) {
      ips = tokens;
      ip = tokens[0];
    }
  }
  return {ips, ip, port};
}
