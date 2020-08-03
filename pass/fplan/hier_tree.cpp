#include "hier_tree.hpp"

std::pair<int, int> Hier_tree::min_wire_cut(Graph_info& info, Set_map& smap, int cut_set) {
  
  auto& g = info.al;
  
  auto not_in_cut_set = [&smap, &cut_set](vertex_t v) -> bool {
    return smap(v) != cut_set;
  };
  
  auto cut_verts = g.verts() | ranges::view::remove_if(not_in_cut_set);

  // TODO: graph lib needs a specific version of range-v3, and that version doesn't have size or conversion utilities yet.
  // this method isn't pretty, but it works and is clear
  unsigned int graph_size = 0;
  for ([[maybe_unused]] const auto& v : cut_verts) {
    graph_size++;
  }
  
  I(graph_size >= 2);
  
  // counter to generate unique set names
  static int set_counter = 1;
  auto new_sets = std::pair(set_counter, set_counter + 1);
  
  // if there are only two elements in the graph, we can exit early.
  if (graph_size == 2) {
    int which_vert = 0;
    for (const auto& v : cut_verts) {
      if (which_vert == 0) {
        smap[v] = new_sets.first;
        which_vert++;
      } else {
        smap[v] = new_sets.second;
      }
    }

    set_counter += 2;
    return new_sets;
  }
  
  // if there are an odd number of elements, we need to insert one to make the graph size even.
  vertex_t temp_vertex = g.null_vert();
  if (graph_size % 2 == 1) {
    temp_vertex = g.insert_vert();
    info.names[temp_vertex] = "temp";
    info.areas[temp_vertex] = 0.0f;
    
    for (const auto& other_v : g.verts()) {
      edge_t temp_edge_t = g.insert_edge(temp_vertex, other_v);
      info.weights[temp_edge_t] = 0;

      temp_edge_t = g.insert_edge(other_v, temp_vertex);
      info.weights[temp_edge_t] = 0;
    }
    
    smap[temp_vertex] = cut_set;
    graph_size++;
  }

  // make new set numbers
  unsigned int set_inc = 0;
  for (auto v : cut_verts) {
    smap[v] = set_counter + set_inc;
    
    if (set_inc == 1) {
      set_inc = 0;
    } else {
      set_inc = 1;
    }
  }

  set_counter += 2;

  /* 
    The reason why I made vert_set a new variable is because views carry no state of their own,
    so the view recomputes what should be contained in it every time we access the view.
    
    In this case, after we adjust the smap values, the original view decides that nothing should
    be in the cut_verts view anymore and removes everything.

    To resolve this, a new view should be created with the correct condition.
  */

  auto is_valid_set = [&smap, new_sets](vertex_t v) -> bool {
    return smap(v) == new_sets.first || smap(v) == new_sets.second;
  };

  auto vert_set = g.verts() | ranges::view::remove_if([=](vertex_t v) { return !is_valid_set(v); });

  const unsigned int set_size = graph_size / 2;

  auto is_in_a = [&smap, new_sets](vertex_t v) -> bool {
    return smap(v) == new_sets.first;
  };

  auto is_in_b = [&smap, new_sets](vertex_t v) -> bool {
    return smap(v) == new_sets.second;
  };

  auto same_set = [&smap, &is_valid_set](vertex_t v1, vertex_t v2) -> bool {
    I(is_valid_set(v1));
    I(is_valid_set(v2));
    return smap(v1) == smap(v2);
  };

  auto cmap = g.vert_map<Min_cut_data>();
  
  // track the best possible decrease in cost between the two sets, so that we can return when there is no more work to do
  int best_decrease = 0;

  do {
    // (re)calculate delta costs for each node
    for (const vertex_t& v : vert_set) {
      int exter = 0;
      int inter = 0;
      for (const edge_t& e : g.out_edges(v)) {
        auto other_v = g.head(e);
        if (is_valid_set(other_v)) {
          if (!same_set(v, other_v)) {
            exter += info.weights(e);
          } else {
            inter += info.weights(e);
          }
        }
      }
      cmap[v].d_cost = exter - inter;
      cmap[v].active = true;
    }
    
    std::vector<vertex_t> av, bv;
    std::vector<int> cv;

    // remove the node pair with the highest global reduction in cost, and add it to cv.
    // also add the nodes used in the reduction to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      
      int cost = std::numeric_limits<int>::min();

      auto a_max = g.null_vert();
      auto b_max = g.null_vert();
      
      for (const vertex_t& v : vert_set) {
        if (cmap(v).active) {
          // row is in the "a" set and hasn't been deleted
          for (const edge_t& e : g.out_edges(v)) {
            auto other_v = g.head(e);
            if (is_in_a(v) && is_in_b(other_v) && cmap(other_v).active) {
              // only select nodes in the other set
              int new_cost = cmap(v).d_cost + cmap(other_v).d_cost - 2 * info.weights(e);
              if (new_cost > cost) {
                cost = new_cost;
                a_max = v;
                b_max = other_v;
              }
            }
          }
        }
      }
      
      I(cost != std::numeric_limits<int>::min()); // there should always be some kind of min cost
      I(a_max != g.null_vert()); // these should be written with legal indices
      I(b_max != g.null_vert());
      
      // save the best swap and mark the swapped nodes as unavailable for future potential swaps
      cv.push_back(cost);
      
      av.push_back(a_max);
      bv.push_back(b_max);

      cmap[a_max].active = false;
      cmap[b_max].active = false;
      
      auto find_edge_to_max = [&g, &cmap](vertex_t v, vertex_t max) -> edge_t {
        if (cmap[v].active) {
          for (const edge_t& e : g.out_edges(v)) {
            vertex_t other_v = g.head(e);
            if (other_v == max) { // no checking if other_v is active since the maxes were just deactivated
              return e;
            }
          }
        }
        return g.null_edge();
      };

      // recalculate costs considering a_max and b_max swapped
      for (const vertex_t& v : vert_set) {
        if (is_in_a(v)) {
          cmap[v].d_cost = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, a_max)) - 2 * info.weights(find_edge_to_max(v, b_max));
        } else {
          cmap[v].d_cost = cmap(v).d_cost + 2 * info.weights(find_edge_to_max(v, b_max)) - 2 * info.weights(find_edge_to_max(v, a_max));
        }
      }
    }
    
    // there should be set_size swaps possible
    I(cv.size() == set_size);

    auto check_sum = [&cv]() -> bool {
      int total = 0;
      for (const unsigned int c : cv) {
        total += c;
      }
      return total == 0;
    };

    // applying all swaps (aka swapping the sets) should have no effect
    I(check_sum());
    
    // calculate the maximum benefit reduction out of all reductions possible in this iteration
    best_decrease = 0;
    size_t decrease_index = 0;
    
    for (size_t k = 0; k < cv.size(); k++) {
      int trial_decrease = 0;
      for (size_t i = 0; i < k; i++) {
        trial_decrease += cv[i];
      }
      if (trial_decrease > best_decrease) {
        best_decrease = trial_decrease;
        decrease_index = k;
      }
    }
    
    // if the maximum reduction has a higher external cost than internal cost, there's something we can do.
    if (best_decrease > 0) {
      for (size_t i = 0; i < decrease_index; i++) {
#ifndef NDEBUG
        std::cout << "swapping " << info.names(av[i]) << " with " << info.names(bv[i]) << std::endl;
#endif
        std::swap(smap[av[i]], smap[bv[i]]);
      }
    }

    for (const vertex_t& v : vert_set) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);

#ifndef NDEBUG
  std::cout << std::endl;
  std::cout << "best partition:" << std::endl;
  for (const vertex_t& v : vert_set) {
    std::cout << info.names(v) << ":\t";
    std::cout << (is_in_a(v) ? "a" : "b") << " (aka " << (is_in_a(v) ? new_sets.first : new_sets.second);
    std::cout << "), cost " << cmap(v).d_cost << std::endl;
  }
#endif
  
  if (temp_vertex != g.null_vert()) {
    g.erase_vert(temp_vertex);
  }
  
  return new_sets;
}

phier Hier_tree::make_hier_tree(phier& t1, phier& t2) {
  auto pnode = std::make_shared<Hier_node>();
  pnode->name = "node_" + std::to_string(node_number);
  pnode->graph_subset = Hier_node::Null_subset;
  
  pnode->children[0] = t1;
  t1->parent = pnode;

  pnode->children[1] = t2;
  t2->parent = pnode;

  node_number++;

  return pnode;
}

phier Hier_tree::make_hier_node(const int set) {
  I(set != Hier_node::Null_subset);

  phier pnode = std::make_shared<Hier_node>();
  pnode->name = "leaf_node_" + std::to_string(node_number);
  pnode->area = 0.0; // TODO: write something to this
  pnode->graph_subset = set;

  node_number++;
  
  return pnode;
}

phier Hier_tree::discover_hierarchy(Graph_info& info, Set_map& smap, int start_set) {
  
  // figure out the number of verts in the set
  unsigned int set_size = 0;
  for (const vertex_t& v : info.al.verts()) {
    if (smap(v) == start_set) {
      set_size++;
    }
  }
  
  if (set_size <= num_components) {
    // set contains less than the minimum number of components, so treat it as a leaf node
    return make_hier_node(start_set);
  }
  
  auto [a, b] = min_wire_cut(info, smap, start_set);

  phier t1 = discover_hierarchy(info, smap, a);
  phier t2 = discover_hierarchy(info, smap, b);
  
  return make_hier_tree(t1, t2);
}

Hier_tree::Hier_tree(Graph_info&& json_info, unsigned int min_num_components, double min_area)
  : area(min_area), num_components(min_num_components), ginfo(std::move(json_info)) {
  
  I(num_components >= 1);
  I(area >= 0.0);
  
  Set_map smap = ginfo.al.vert_map<int>();
  for (const auto& v : ginfo.al.verts()) {
    smap[v] = 0; // everything starts in set 0
  }
  
  root = discover_hierarchy(ginfo, smap, 0);
  
  collapse();
}

void Hier_tree::print_node(const phier& node) const {
  if (node->parent == nullptr) {
    std::cout << "root node ";
  }

  std::cout << node->name << ": area = " << node->area;
  
  if (node->graph_subset == Hier_node::Null_subset) {
    std::cout << ", children = (" << node->children[0]->name << ", " << node->children[1]->name << ")" << std::endl;
    print_node(node->children[0]);
    print_node(node->children[1]);
  } else {
    std::cout << ", containing set " << node->graph_subset << std::endl;
  }
}

void Hier_tree::print() const {
  print_node(root);
}

void Hier_tree::collapse() {
  
}
