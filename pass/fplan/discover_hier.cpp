#include <functional>
#include <limits>  // for std::numeric_limits
#include <vector>
#include <algorithm>  // for std::max, std::min

#include "fmt/core.h"
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"
#include "profile_time.hpp"

std::pair<set_t, set_t> Hier_tree::min_wire_cut(set_t& cut_set) {
  auto& gi = collapsed_gis[0];
  auto& g = collapsed_gis[0].al;

  auto cut_verts = g.verts() | ranges::view::remove_if([&](auto v) -> bool { return !cut_set.contains(v); });

  // TODO: graph lib needs a specific version of range-v3, and that version doesn't have size or conversion utilities yet.
  // this method isn't pretty, but it works.  [[maybe_unused]] directive silences warning about "v" being unused.
  unsigned int cut_size = 0;
  for ([[maybe_unused]] auto v : cut_verts) {
    cut_size++;
  }

  I(cut_size >= 2);

  auto new_sets = std::pair(g.vert_set(), g.vert_set());

  // if there are only two elements in the graph, we can exit early.
  if (cut_size == 2) {
    int      which_vert = 0;
    vertex_t v1, v2;
    for (auto v : cut_verts) {
      if (which_vert == 0) {
        v1 = v;
        which_vert++;
      } else {
        v2 = v;
      }
    }

    new_sets.first.insert(v1);
    new_sets.second.insert(v2);

    if (hier_verbose) {
      fmt::print("\ntrivial partition:\n");
      fmt::print("{:<30}a, area {:.2f}\n", gi.debug_names(v1), gi.areas(v1));
      fmt::print("{:<30}b, area {:.2f}\n", gi.debug_names(v2), gi.areas(v2));
      fmt::print("imb: {:.3f}\n", std::max(gi.areas(v1), gi.areas(v2)) / (gi.areas(v1) + gi.areas(v2)));
    }

    return new_sets;
  }

  // if there are an odd number of elements, we need to insert one to make the graph size even.
  vertex_t temp_even_vertex = g.null_vert();
  if (cut_size % 2 == 1) {
    temp_even_vertex = gi.make_temp_vertex(std::string("temp_even"), 0.0);
    cut_set.insert(temp_even_vertex);
    cut_size++;
  }

  //  The reason why I made vert_set a new variable is because views carry no state of their own,
  //  so the view recomputes what should be contained in it every time we access the view.

  //  In this case, after we adjust the smap values, the original view decides that nothing should
  //  be in the cut_verts view anymore and removes everything.

  //  To resolve this, a new view should be created with the correct condition.

  auto is_valid_set = [&](auto v) -> bool { return cut_set.contains(v); };

  auto vert_set = g.verts() | ranges::view::remove_if([=](auto v) { return !is_valid_set(v); });

  unsigned int set_size = cut_size / 2;

  auto is_in_a = [&](auto v) -> bool { return new_sets.first.contains(v); };

  auto is_in_b = [&](auto v) -> bool { return new_sets.second.contains(v); };

  auto same_set = [&](auto v1, auto v2) -> bool {
    I(is_valid_set(v1));
    I(is_valid_set(v2));
    return (new_sets.first.contains(v1) && new_sets.first.contains(v2))
           || (new_sets.second.contains(v1) && new_sets.second.contains(v2));
  };

  // assign vertices to one of the two new sets we made
  unsigned int which_set = 0;
  for (auto v : cut_verts) {
    if (which_set == 0) {
      new_sets.first.insert(v);
    } else {
      new_sets.second.insert(v);
    }

    // alternate between 0 and 1
    which_set ^= 1;
  }

  I(new_sets.first.size() == new_sets.second.size());

  // find the total area of both the a and b sets.  If the area is > max_imb, correct it.

  double init_a_area = 0.0;
  double init_b_area = 0.0;

  for (auto v : vert_set) {
    if (is_in_a(v)) {
      init_a_area += gi.areas(v);
    } else {
      init_b_area += gi.areas(v);
    }
  }

  constexpr double max_imb  = 2.0 / 3.0;
  auto             area_imb = [](double a1, double a2) -> double { return std::max(a1, a2) / (a1 + a2); };

  if (hier_verbose) {
    fmt::print("\nincoming imb: {:.3f}\n", area_imb(init_a_area, init_b_area));
  }

  auto   add_area_vertex = gi.al.null_vert();
  double add_area_amt    = 0.0;
  if (area_imb(init_a_area, init_b_area) > max_imb) {
    // if the area combo is illegal, add some area to a random node to make it legal.  Not super smart, but it works for now
    set_t& add_area_set = (init_a_area > init_b_area) ? new_sets.second : new_sets.first;
    add_area_amt        = (1.0 / max_imb) * std::max(init_a_area, init_b_area) - init_a_area - init_b_area + 0.01;

    if (hier_verbose) {
      fmt::print("adding area {} to node {:<30}\n", add_area_amt, gi.debug_names[*(add_area_set.begin())]);
    }
    add_area_vertex = *(add_area_set.begin());
    gi.areas[add_area_vertex] += add_area_amt;
  }

  if (hier_verbose) {
    fmt::print("incoming partition:\n");
    for (auto v : vert_set) {
      fmt::print("{:<30}{}, area {:.2f}\n", gi.debug_names(v), (is_in_a(v) ? "a" : "b"), gi.areas(v));
    }
  }

  auto cmap = g.vert_map<Min_cut_data>();

  // track the best possible decrease in cost between the two sets, so that we can return when there is no more work to do
  int best_decrease = 0;

  do {
    // (re)calculate delta costs for each node
    for (auto v : vert_set) {
      int exter = 0;
      int inter = 0;
      for (auto e : g.out_edges(v)) {
        vertex_t other_v = g.head(e);
        if (is_valid_set(other_v)) {
          if (!same_set(v, other_v)) {
            exter += gi.weights(e);
          } else {
            inter += gi.weights(e);
          }
        }
      }
      cmap[v].d_cost = exter - inter;
      cmap[v].active = true;
    }

    std::vector<vertex_t> av, bv;
    std::vector<int>      cv;
    std::vector<double>   aav;
    std::vector<double>   bav;

    double a_area = 0.0;
    double b_area = 0.0;

    for (auto v : vert_set) {
      if (is_in_a(v)) {
        a_area += gi.areas(v);
      } else {
        b_area += gi.areas(v);
      }
    }

    bool prev_imbalanced = area_imb(a_area, b_area) > max_imb;
    I(!prev_imbalanced);

    // remove the node pair with the highest global reduction in cost, and add it to cv.
    // also add the nodes used in the reduction to av and bv, respectively.
    for (unsigned int n = 1; n <= set_size; n++) {
      int cost = std::numeric_limits<int>::min();

      auto a_max = g.null_vert();
      auto b_max = g.null_vert();

      for (auto v : vert_set) {
        if (cmap(v).active) {
          // row is in the "a" set and hasn't been deleted
          for (auto e : g.out_edges(v)) {
            vertex_t other_v = g.head(e);
            if (is_in_a(v) && is_in_b(other_v) && cmap(other_v).active) {
              // only select nodes in the other set
              int new_cost = cmap(v).d_cost + cmap(other_v).d_cost - 2 * gi.weights(e);

              if (new_cost > cost) {
                cost  = new_cost;
                a_max = v;
                b_max = other_v;
              }
            }
          }
        }
      }

      I(cost != std::numeric_limits<int>::min());  // there should always be some kind of min cost
      I(a_max != g.null_vert());                   // these should be written with legal indices
      I(b_max != g.null_vert());

      // save the best swap and mark the swapped nodes as unavailable for future potential swaps
      cv.push_back(cost);

      av.push_back(a_max);
      bv.push_back(b_max);

      double delta_a_area = -gi.areas(a_max) + gi.areas(b_max);
      double delta_b_area = -gi.areas(b_max) + gi.areas(a_max);

      aav.push_back(delta_a_area);
      bav.push_back(delta_b_area);

      cmap[a_max].active = false;
      cmap[b_max].active = false;

      auto find_edge_to_max = [&](auto v, auto v_max) -> edge_t {
        if (cmap[v].active) {
          for (auto e : g.out_edges(v)) {
            vertex_t other_v = g.head(e);
            if (other_v == v_max) {  // no checking if other_v is active since the maxes were just deactivated
              return e;
            }
          }
        }
        return g.null_edge();
      };

      // recalculate costs considering a_max and b_max swapped
      for (auto v : vert_set) {
        if (is_in_a(v)) {
          if (v == a_max) {
            continue;
          }
          cmap[v].d_cost
              = cmap(v).d_cost + 2 * gi.weights(find_edge_to_max(v, a_max)) - 2 * gi.weights(find_edge_to_max(v, b_max));
        } else {
          if (v == b_max) {
            continue;
          }
          cmap[v].d_cost
              = cmap(v).d_cost + 2 * gi.weights(find_edge_to_max(v, b_max)) - 2 * gi.weights(find_edge_to_max(v, a_max));
        }
      }
    }

    // there should be set_size swaps possible
    I(cv.size() == set_size);

    auto check_sum = [&]() -> bool {
      int total = 0;
      for (const unsigned int c : cv) {
        total += c;
      }
      return total == 0;
    };

    // applying all swaps (aka swapping the sets) should have no effect
    I(check_sum());

    // calculate the maximum benefit reduction out of all reductions possible in this iteration
    best_decrease         = 0;
    size_t decrease_index = 0;

    std::vector<bool> swap_vec(cv.size(), false);

    for (size_t k = 0; k < cv.size(); k++) {
      int    trial_decrease = 0;
      double new_a_area     = a_area;
      double new_b_area     = b_area;
      for (size_t i = 0; i < k; i++) {
        // make sure the area imbalance won't climb too high if the swap occurs
        if (area_imb(new_a_area + aav[i], new_b_area + bav[i]) <= max_imb) {
          trial_decrease += cv[i];
          new_a_area += aav[i];
          new_b_area += bav[i];
          swap_vec[i] = true;
        }
      }
      if (trial_decrease > best_decrease) {
        best_decrease  = trial_decrease;
        decrease_index = k;
      }
    }

    // if the maximum reduction has a higher external cost than internal cost, there's something we can do.
    if (best_decrease > 0) {
      for (size_t i = 0; i < decrease_index; i++) {
        if (hier_verbose) {
          fmt::print("  swapping {} with {}.\n", gi.debug_names(av[i]), gi.debug_names(bv[i]));
        }

        if (swap_vec[i]) {
          new_sets.first.erase(av[i]);
          new_sets.second.insert(av[i]);

          new_sets.second.erase(bv[i]);
          new_sets.first.insert(bv[i]);
        }
      }
    }

    for (auto v : vert_set) {
      cmap[v].active = true;
    }

    // use the better sets as inputs to the algorithm, and run another iteration if there looks to be a better decrease
  } while (best_decrease > 0);

  double final_a_area = 0.0;
  double final_b_area = 0.0;

  for (auto v : vert_set) {
    if (is_in_a(v)) {
      final_a_area += gi.areas(v);
    } else {
      final_b_area += gi.areas(v);
    }
  }

  double final_imb = std::max(final_a_area, final_b_area) / (final_a_area + final_b_area);
  I(final_imb > 0.0 && final_imb <= 2.0 / 3.0);

  if (hier_verbose) {
    fmt::print("\nbest partition:\n");
    for (auto v : vert_set) {
      fmt::print("{:<30}{}, cost {}, area {:.2f}\n",
                 gi.debug_names(v),
                 (is_in_a(v) ? "a" : "b"),
                 cmap(v).d_cost,
                 gi.areas(v));
    }

    fmt::print("a area: {:.2f}, b area: {:.2f}, imb: {:.3f}.\n", final_a_area, final_b_area, final_imb);
  }

  // if we inserted a temporary vertex, remove it from the list of active sets and erase it from the graph
  if (temp_even_vertex != g.null_vert()) {
    if (new_sets.first.contains(temp_even_vertex)) {
      new_sets.first.erase(temp_even_vertex);
    } else {
      I(new_sets.second.contains(temp_even_vertex));
      new_sets.second.erase(temp_even_vertex);
    }
    g.erase_vert(temp_even_vertex);
  }

  // if we added some area to a vertex, subtract it out
  if (add_area_vertex != g.null_vert()) {
    gi.areas[add_area_vertex] -= add_area_amt;
  }

  return new_sets;
}

Hier_tree::phier Hier_tree::discover_hierarchy(set_t& set, unsigned int min_size) {
  if (set.size() <= min_size) {
    I(set.size() != 0);
    return make_hier_node(collapsed_gis[0], set);
  }

  auto [a, b] = min_wire_cut(set);

  phier t1 = discover_hierarchy(a, min_size);
  phier t2 = discover_hierarchy(b, min_size);

  return make_hier_tree(collapsed_gis[0], t1, t2);
}

void Hier_tree::discover_hierarchy(const unsigned int min_size) {
  I(min_size >= 1);
  auto& gi = collapsed_gis[0];
  profile_time::timer t;

  if (gi.al.order() > min_size) {
    fmt::print("    wiring zero-cost edges...");
    t.start();
    // if the graph is not fully connected, ker-lin fails to work.
    for (const auto& v : gi.al.verts()) {
      for (const auto& ov : gi.al.verts()) {
        if (gi.find_edge(v, ov) == gi.al.null_edge()) {
          auto temp_e           = gi.al.insert_edge(v, ov);
          gi.weights[temp_e] = 0;
        }
      }
    }
    fmt::print("done. ({} ms).\n", t.time());
  } else {
    fmt::print("    skipping zero-cost edge wiring.\n");
  }

  fmt::print("    discovering hierarchy...");
  t.start();

  auto all_verts = gi.al.vert_set();
  for (auto v : gi.al.verts()) {
    all_verts.insert(v);
  }

  hiers.push_back(discover_hierarchy(all_verts, min_size));
  fmt::print("done. ({} ms).\n", t.time());

  if (min_size < gi.al.order()) {
    fmt::print("    deleting zero-cost edges...");
    t.start();

    // undo temp edge creation because it's really inconvienent elsewhere
    // NOTE: this isn't very efficient, but using edge sets causes segfaults because the underlying graph is changing.
    // using std::set<edge_t> works, but doesn't speed things up that much.
    for (const auto& v : gi.al.verts()) {
      for (const auto& ov : gi.al.verts()) {
        auto e = gi.find_edge(v, ov);
        if (e != gi.al.null_edge() && gi.weights[e] == 0) {
          gi.al.erase_edge(e);
        }
      }
    }

    fmt::print("done. ({} ms).\n", t.time());
  } else {
    fmt::print("    skipping zero-cost edge deletion.\n");
  }
}