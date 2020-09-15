#pragma once

#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>  // for std::pair
#include <vector>

#include "graph_info.hpp"
#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"
#include "pattern.hpp"

class Dag_node {
public:
  using pdag = std::shared_ptr<Dag_node>;

  pdag              parent;
  std::vector<pdag> children;

  Lg_type_id::type label;
  std::vector<Dim> dims;

  Dag_node() : parent(nullptr), children(), label(0), dims() {}
  bool is_leaf() {
    return children.size() == 0;
  }
};

class Dag {
public:
  Dag() : root(std::make_shared<Dag_node>()) {}

  // initialize a dag from a vector of patterns with all leaves being unique,
  // and all patterns either containing leaves or other patterns.
  void init(std::vector<Pattern> hier_patterns, std::unordered_map<Lg_type_id::type, Dim> leaf_dims,
            const Graph_info<g_type>& ginfo);

  void select_points();

  void dump();

private:
  using pdag = std::shared_ptr<Dag_node>;
  pdag root;
};