/*jshint strict: false */

////////////////////////////////////////////////////////////////////////////////
/// @brief Graph functionality
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler, Lucas Dohmen
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var is = require("@arangodb/is"),
  propertyKeys = require("@arangodb/util").propertyKeys,
  shallowCopy = require("@arangodb/util").shallowCopy,
  Edge,
  Graph,
  Vertex,
  GraphArray,
  Iterator;


Iterator = function (wrapper, cursor, stringRepresentation) {
  this.next = function next() {
    if (cursor.hasNext()) {
      return wrapper(cursor.next());
    }

    return undefined;
  };

  this.hasNext = function hasNext() {
    return cursor.hasNext();
  };

  this._PRINT = function (context) {
    context.output += stringRepresentation;
  };
};




////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a graph arrays
////////////////////////////////////////////////////////////////////////////////

GraphArray = function (len) {
  if (len !== undefined) {
    this.length = len;
  }
};

GraphArray.prototype = new Array(0);


////////////////////////////////////////////////////////////////////////////////
/// @brief map
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.map = function (fun, thisp) {
  var len = this.length;
  var i;

  if (typeof fun !== "function") {
    throw new TypeError();
  }

  var res = new GraphArray(len);

  for (i = 0;  i < len;  i++) {
    if (this.hasOwnProperty(i)) {
      res[i] = fun.call(thisp, this[i], i, this);
    }
  }

  return res;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the in vertices
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getInVertex = function () {
  return this.map(function(a) {return a.getInVertex();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the out vertices
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getOutVertex = function () {
  return this.map(function(a) {return a.getOutVertex();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the peer vertices
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getPeerVertex = function (vertex) {
  return this.map(function(a) {return a.getPeerVertex(vertex);});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the property
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.setProperty = function (name, value) {
  return this.map(function(a) {return a.setProperty(name, value);});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.edges = function () {
  return this.map(function(a) {return a.edges();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get outbound edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.outbound = function () {
  return this.map(function(a) {return a.outbound();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get inbound edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.inbound = function () {
  return this.map(function(a) {return a.inbound();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the in edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getInEdges = function () {
  var args = arguments;
  return this.map(function(a) {return a.getInEdges.apply(a, args);});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the out edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getOutEdges = function () {
  var args = arguments;
  return this.map(function(a) {return a.getOutEdges.apply(a, args);});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get the edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.getEdges = function () {
  var args = arguments;
  return this.map(function(a) {return a.getEdges.apply(a, args);});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.degree = function () {
  return this.map(function(a) {return a.degree();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of in-edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.inDegree = function () {
  return this.map(function(a) {return a.inDegree();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of out-edges
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.inDegree = function () {
  return this.map(function(a) {return a.outDegree();});
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the properties
////////////////////////////////////////////////////////////////////////////////

GraphArray.prototype.properties = function () {
  return this.map(function(a) {return a.properties();});
};


Edge = function (graph, properties) {
  this._graph = graph;
  this._id = properties._key;
  this._properties = properties;
};


////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getId = function () {
  return this._properties._key;
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getLabel = function () {
  return this._properties.$label;
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getProperty = function (name) {
  return this._properties[name];
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getPropertyKeys = function () {
  return propertyKeys(this._properties);
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.properties = function () {
  return shallowCopy(this._properties);
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getInVertex = function () {
  return this._graph.getVertex(this._properties._to);
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/EdgeMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getOutVertex = function () {
  return this._graph.getVertex(this._properties._from);
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start Docu Block edgeGetPeerVertex
///
/// `edge*.getPeerVertex(vertex)`
///
/// Returns the peer vertex of the *edge* and the *vertex*.
///
/// @EXAMPLES
///
/// @EXAMPLE_ARANGOSH_OUTPUT{edgeGetPeerVertex}
/// ~ db._drop("v");
/// ~ db._drop("e");
///   Graph = require("@arangodb/graph-blueprint").Graph;
///   g = new Graph("example", "v", "e");
///   v1 = g.addVertex("1");
///   v2 = g.addVertex("2");
///   e = g.addEdge(v1, v2, "1-to-2", "knows");
///   e.getPeerVertex(v1);
/// ~ Graph.drop("example");
/// ~ db._drop("v");
/// ~ db._drop("e");
/// @END_EXAMPLE_ARANGOSH_OUTPUT
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getPeerVertex = function (vertex) {
  if (vertex._properties._id === this._properties._to) {
    return this._graph.getVertex(this._properties._from);
  }

  if (vertex._properties._id === this._properties._from) {
    return this._graph.getVertex(this._properties._to);
  }

  return null;
};


////////////////////////////////////////////////////////////////////////////////
/// @brief edge printing
////////////////////////////////////////////////////////////////////////////////

Edge.prototype._PRINT = function (context) {
  if (!this._properties._id) {
    context.output += "[deleted Edge]";
  }
  else if (this._properties._key !== undefined) {
    if (typeof this._properties._key === "string") {
      context.output += "Edge(\"" + this._properties._key + "\")";
    }
    else {
      context.output += "Edge(" + this._properties._key + ")";
    }
  }
  else {
    context.output += "Edge(<" + this._id + ">)";
  }
};


Vertex = function (graph, properties) {
  this._graph = graph;
  this._id = properties._key;
  this._properties = properties;
};


////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/VertexMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.addInEdge = function (out, id, label, data) {
  return this._graph.addEdge(out, this, id, label, data);
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/VertexMethods.mdpp
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.addOutEdge = function (ine, id, label, data) {
  return this._graph.addEdge(this, ine, id, label, data);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.degree = function () {
  return this.getEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of in-edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.inDegree = function () {
  return this.getInEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of out-edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.outDegree = function () {
  return this.getOutEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start Docu Block peerGetId
///
/// `peer.getId()`
///
/// Returns the identifier of the *peer*. If the vertex was deleted, then
/// *undefined* is returned.
///
/// @EXAMPLES
///
/// @verbinclude graph-vertex-get-id
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getId = function () {
  return this._properties._key;
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start Docu Block peerGetProperty
///
/// `peer.getProperty(edge)`
///
/// Returns the property *edge* a *peer*.
///
/// @EXAMPLES
///
/// @verbinclude graph-vertex-get-property
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getProperty = function (name) {
  return this._properties[name];
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start Docu Block peerGetPropertyKeys
///
/// `peer.getPropertyKeys()`
///
/// Returns all propety names a *peer*.
///
/// @EXAMPLES
///
/// @verbinclude graph-vertex-get-property-keys
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getPropertyKeys = function () {
  return propertyKeys(this._properties);
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start Docu Block peerproperties
///
/// `peer.properties()`
///
/// Returns all properties and their values of a *peer*
///
/// @EXAMPLES
///
/// @verbinclude graph-vertex-properties
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.properties = function () {
  return shallowCopy(this._properties);
};


////////////////////////////////////////////////////////////////////////////////
/// @brief vertex representation
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype._PRINT = function (context) {
  if (! this._properties._id) {
    context.output += "[deleted Vertex]";
  }
  else if (this._properties._key !== undefined) {
    if (typeof this._properties._key === "string") {
      context.output += "Vertex(\"" + this._properties._key + "\")";
    }
    else {
      context.output += "Vertex(" + this._properties._key + ")";
    }
  }
  else {
    context.output += "Vertex(<" + this._id + ">)";
  }
};


Graph = function (name, vertices, edges, waitForSync) {
  this.initialize(name, vertices, edges, waitForSync);
};


Graph.prototype._prepareEdgeData = function (data, label) {
  var edgeData;

  if (is.notExisty(data) && is.object(label)) {
    data = label;
    label = null;
  }

  if (is.notExisty(label) && is.existy(data) && is.existy(data.$label)) {
    label = data.$label;
  }

  if (is.notExisty(data) || is.noObject(data)) {
    edgeData = {};
  } else {
    edgeData = shallowCopy(data) || {};
  }

  edgeData.$label = label;

  return edgeData;
};

Graph.prototype._prepareVertexData = function (data) {
  var vertexData;

  if (is.notExisty(data) || is.noObject(data)) {
    vertexData = {};
  } else {
    vertexData = shallowCopy(data) || {};
  }

  return vertexData;
};


////////////////////////////////////////////////////////////////////////////////
/// @brief get a vertex from the graph, create it if it doesn't exist
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getOrAddVertex = function (id) {
  var v = this.getVertex(id);

  if (v === null) {
    v = this.addVertex(id);
  }

  return v;
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/GraphConstructor.mdpp
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.addEdge = function (out_vertex, in_vertex, id, label, data, waitForSync) {
  var out_vertex_id, in_vertex_id;

  if (is.string(out_vertex)) {
    out_vertex_id = out_vertex;
  } else {
    out_vertex_id = out_vertex._properties._id;
  }

  if (is.string(in_vertex)) {
    in_vertex_id = in_vertex;
  } else {
    in_vertex_id = in_vertex._properties._id;
  }

  return this._saveEdge(id,
                        out_vertex_id,
                        in_vertex_id,
                        this._prepareEdgeData(data, label),
                        waitForSync);
};

////////////////////////////////////////////////////////////////////////////////
/// the real docu is in Users/ModuleGraph/GraphConstructor.mdpp
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.addVertex = function (id, data, waitForSync) {
  return this._saveVertex(id, this._prepareVertexData(data), waitForSync);
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @brief replaces an existing vertex by ID
///
/// @FUN{@FA{graph}.replaceVertex(*peer*, *peer*)}
///
/// Replaces an existing vertex by ID
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.replaceVertex = function (id, data) {
  this._replaceVertex(id, data);
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @start DocuBlock graphReplaceEdge
///
/// `graph.replaceEdge(peer, peer)`
///
/// Replaces an existing edge by ID
/// @end Docu Block
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.replaceEdge = function (id, data) {
  this._replaceEdge(id, data);
};

////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @brief returns the number of vertices
///
/// @FUN{@FA{graph}.order()}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.order = function () {
  return this._vertices.count();
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of edges
///
/// @FUN{@FA{graph}.size()}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.size = function () {
  return this._edges.count();
};


////////////////////////////////////////////////////////////////////////////////
/// this is no documentation.
/// @brief empties the internal cache for Predecessors
///
/// @FUN{@FA{graph}.emptyCachedPredecessors()}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.emptyCachedPredecessors = function () {
  this.predecessors = {};
};

////////////////////////////////////////////////////////////////////////////////
/// @brief gets Predecessors for a pair from the internal cache
///
/// @FUN{@FA{graph}.getCachedPredecessors(@FA{target}), @FA{source})}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getCachedPredecessors = function (target, source) {
  var predecessors;

  if (this.predecessors[target.getId()]) {
    predecessors = this.predecessors[target.getId()][source.getId()];
  }

  return predecessors;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief sets Predecessors for a pair in the internal cache
///
/// @FUN{@FA{graph}.setCachedPredecessors(@FA{target}), @FA{source}, @FA{value})}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.setCachedPredecessors = function (target, source, value) {
  if (!this.predecessors[target.getId()]) {
    this.predecessors[target.getId()] = {};
  }

  this.predecessors[target.getId()][source.getId()] = value;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a vertex
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.constructVertex = function (data) {
  var id, rev;

  if (typeof data === "string") {
    id = data;
  } else {
    id = data._id;
    rev = data._rev;
  }

  var vertex = this._verticesCache[id];

  if (vertex === undefined || vertex._rev !== rev) {
    var properties = this._vertices.document(id);

    if (! properties) {
      throw "accessing a deleted vertex";
    }

    this._verticesCache[id] = vertex = new Vertex(this, properties);
  }

  return vertex;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs an edge
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.constructEdge = function (data) {
  var id, rev, edge, properties;

  if (typeof data === "string") {
    id = data;
  } else {
    id = data._id;
    rev = data._rev;
  }

  edge = this._edgesCache[id];

  if (edge === undefined || edge._rev !== rev) {
    properties = this._edges.document(id);

    if (!properties) {
      throw "accessing a deleted edge";
    }

    this._edgesCache[id] = edge = new Edge(this, properties);
  }

  return edge;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief graph printing
////////////////////////////////////////////////////////////////////////////////

Graph.prototype._PRINT = function (context) {
  context.output += "Graph(\"" + this._properties._key + "\")";
};


exports.Edge = Edge;
exports.Graph = Graph;
exports.Vertex = Vertex;
exports.GraphArray = GraphArray;
exports.Iterator = Iterator;


