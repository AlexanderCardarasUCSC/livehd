//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

class LGraph;
class XEdge;
class Node;

#include "mmap_map.hpp"
#include "ann_bitwidth.hpp"
#include "lgedge.hpp"

class Node_pin {
protected:
  friend class LGraph;
  friend class LGraph_Node_Type;
  friend class XEdge;
  friend class Node;
  friend class CFast_edge_iterator;
  friend class Edge_raw_iterator_base;
  friend class CForward_edge_iterator;
  friend class CBackward_edge_iterator;
  friend class Edge_raw;

  LGraph       *top_g;
  LGraph       *current_g;
  Hierarchy_index  hidx;
  Index_ID      idx;
  Port_ID       pid;
  bool          sink;

  Node_pin(LGraph *_g, LGraph *_c_g, const Hierarchy_index &_hidx, Index_ID _idx, Port_ID _pid, bool _sink);

  const Index_ID get_idx() const { I(idx); return idx;    }
public:
  class __attribute__((packed)) Compact {
  protected:
    Hierarchy_index hidx;
    uint32_t idx  : Index_bits;
    uint32_t sink : 1;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
    friend class mmap_map::hash<Node_pin::Compact>;
  public:

    //constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact(const Compact &obj): hidx(obj.hidx), idx(obj.idx), sink(obj.sink) { }
    Compact(const Hierarchy_index _hidx, Index_ID _idx, bool _sink) :hidx(_hidx), idx(_idx) ,sink(_sink) { };
    Compact() :hidx(0), idx(0) ,sink(0) { };
    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      hidx  = obj.hidx;
      idx  = obj.idx;
      sink = obj.sink;

      return *this;
    };

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact &other) const {
      return hidx == other.hidx && idx == other.idx && sink == other.sink;
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact& s) {
      return H::combine(std::move(h), s.hidx, s.idx, s.sink);
    };
  };
  class __attribute__((packed)) Compact_driver {
  protected:
    Hierarchy_index   hidx;
    uint32_t idx  : Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
    friend class mmap_map::hash<Node_pin::Compact_driver>;
  public:

    //constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_driver(const Compact_driver &obj): hidx(obj.hidx), idx(obj.idx) { }
    Compact_driver(const Hierarchy_index _hidx, Index_ID _idx) :hidx(_hidx), idx(_idx) { };
    Compact_driver() :hidx(0), idx(0) { };
    Compact_driver &operator=(const Compact_driver &obj) {
      I(this != &obj);
      hidx  = obj.hidx;
      idx  = obj.idx;

      return *this;
    };

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_driver &other) const {
      return hidx == other.hidx && idx == other.idx;
    }
    constexpr bool operator!=(const Compact_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_driver &s) {
      return H::combine(std::move(h), s.hidx, s.idx);
    };
  };
  class __attribute__((packed)) Compact_class {
  protected:
    uint32_t idx  : Index_bits;
    uint32_t sink : 1;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
    friend class mmap_map::hash<Node_pin::Compact_class>;
  public:

    //constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_class(const Compact_class &obj): idx(obj.idx), sink(obj.sink) { }
    Compact_class(Index_ID _idx, bool _sink) :idx(_idx) ,sink(_sink) { }
    Compact_class():idx(0) ,sink(0) { }
    Compact_class &operator=(const Compact_class &obj) {
      I(this != &obj);
      idx  = obj.idx;
      sink = obj.sink;

      return *this;
    }

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_class &other) const {
      return idx == other.idx && sink == other.sink;
    }
    constexpr bool operator!=(const Compact_class &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class& s) {
      return H::combine(std::move(h), s.idx, s.sink);
    }
  };
  class __attribute__((packed)) Compact_class_driver {
  protected:
    uint32_t idx  : Index_bits;

    friend class LGraph;
    friend class LGraph_Node_Type;
    friend class Node;
    friend class Node_pin;
    friend class XEdge;
    friend class CFast_edge_iterator;
    friend class Edge_raw_iterator_base;
    friend class CForward_edge_iterator;
    friend class CBackward_edge_iterator;
    friend class mmap_map::hash<Node_pin::Compact_class_driver>;
  public:

    //constexpr operator size_t() const { I(0); return idx|(sink<<31); }

    Compact_class_driver(const Compact_class_driver &obj): idx(obj.idx) { }
    Compact_class_driver(Index_ID _idx) :idx(_idx) { }
    Compact_class_driver():idx(0) { }
    Compact_class_driver &operator=(const Compact_class_driver &obj) {
      I(this != &obj);
      idx  = obj.idx;

      return *this;
    }

    constexpr bool is_invalid() const { return idx == 0; }

    constexpr bool operator==(const Compact_class_driver &other) const {
      return idx == other.idx;
    }
    constexpr bool operator!=(const Compact_class_driver &other) const { return !(*this == other); }

    template <typename H>
    friend H AbslHashValue(H h, const Compact_class_driver &s) {
      return H::combine(std::move(h), s.idx);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const Node_pin& s) {
    return H::combine(std::move(h), (int)s.hidx, (int)s.idx, s.sink); // Ignore lgraph pointer in hash
  }

  Node_pin() : top_g(0), current_g(0), hidx(0), idx(0), pid(0), sink(false) { }
  Node_pin(LGraph *_g, Compact comp);
  Node_pin(LGraph *_g, Compact_driver comp);
  Node_pin(LGraph *_g, Compact_class comp);
  Node_pin(LGraph *_g, Compact_class_driver comp);

  Compact get_compact() const {
    return Compact(hidx,idx,sink);
  }

  Compact_driver get_compact_driver() const {
    I(!sink);
    return Compact_driver(hidx,idx);
  }

  Compact_class get_compact_class() const {
    return Compact_class(idx,sink);
  }

  Compact_class_driver get_compact_class_driver() const {
    I(!sink); // Only driver pin allowed
    return Compact_class_driver(idx);
  }

  LGraph       *get_top_lgraph() const { return top_g; };
  LGraph       *get_class_lgraph() const { return current_g; };
  Hierarchy_index  get_hidx() const { return hidx; };

  const Port_ID  get_pid()   const { I(idx); return pid;    }

  bool has_inputs () const;
  bool has_outputs() const;

  bool     is_graph_io()  const;
  bool     is_graph_input()  const;
  bool     is_graph_output() const;

  bool     is_input()  const { I(idx); return sink;  }
  bool     is_output() const { I(idx); return !sink; }

  bool     is_sink()   const { I(idx); return sink;  }
  bool     is_driver() const { I(idx); return !sink; }

  Node get_node() const;

  void connect_sink(Node_pin &dst);
  void connect_sink(Node_pin &&dst) {
    connect_sink(dst);
  }
  void connect_driver(Node_pin &dst);
  void connect(Node_pin &dst) {
    if (dst.is_sink() && is_driver())
      return connect_sink(dst);
    I(dst.is_driver() && is_sink());
    return connect_driver(dst);
  }

  Node_pin &operator=(const Node_pin &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    top_g     = obj.top_g;
    current_g = obj.current_g;
    idx       = obj.idx;
    pid       = obj.pid;
    hidx       = obj.hidx;
    sink      = obj.sink;

    return *this;
  };

  // NOTE: No operator<() needed for std::set std::map to avoid their use. Use flat_map_set for speed

  //static Node_pin get_out_pin(const Edge_raw *edge_raw);
  //static Node_pin get_inp_pin(const Edge_raw *edge_raw);

  constexpr bool is_invalid() const { return idx==0; }

  constexpr bool operator==(const Node_pin &other) const { return (top_g == other.top_g) && (idx == other.idx) && (pid == other.pid) && (sink == other.sink) && (hidx == other.hidx); }
  constexpr bool operator!=(const Node_pin &other) const { return !(*this == other); }

  void nuke(); // Delete all the edges, and attributes of this node_pin

  // BEGIN ATTRIBUTE ACCESSORS
  std::string      debug_name() const;

  void set_name(std::string_view wname);
  std::string_view create_name() const;
  std::string_view get_name() const;
  bool has_name() const;

  void  set_delay(float val);
  float get_delay() const;

  uint16_t get_bits() const;
  void     set_bits(uint16_t bits);

  std::string_view get_type_sub_io_name() const;

  void set_offset(uint16_t offset);
  uint16_t get_offset() const;

  const Ann_bitwidth &get_bitwidth() const;
  Ann_bitwidth       *ref_bitwidth();
  bool                has_bitwidth() const;
  bool                is_connected() const;

  // END ATTRIBUTE ACCESSORS
};

namespace mmap_map {
template <>
struct hash<Node_pin::Compact> {
  size_t operator()(Node_pin::Compact const &o) const {
    uint64_t h = o.idx;
    h = (h<<12) ^ o.hidx ^ o.idx;
    return hash<uint64_t>{}((h<<1) + o.sink);
  }
};

template <>
struct hash<Node_pin::Compact_driver> {
  size_t operator()(Node_pin::Compact_driver const &o) const {
    uint64_t h = o.idx;
    h = (h<<12) ^ o.hidx ^ o.idx;
    return hash<uint64_t>{}(h);
  }
};

template <>
struct hash<Node_pin::Compact_class> {
  size_t operator()(Node_pin::Compact_class const &o) const {
    uint32_t h = o.idx;
    return hash<uint32_t>{}((h<<1) + o.sink);
  }
};

template <>
struct hash<Node_pin::Compact_class_driver> {
  size_t operator()(Node_pin::Compact_class_driver const &o) const {
    return hash<uint32_t>{}(o.idx);
  }
};
}

