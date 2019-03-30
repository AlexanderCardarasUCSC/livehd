//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph.hpp"

#include "node_pin.hpp"

#include "annotate.hpp"

Node_pin::Node_pin(LGraph *_g, Hierarchy_id _hid, Compact comp)
  :idx(comp.idx)
  ,pid(g->get_dst_pid(comp.idx))
  ,g(_g)
  ,hid(_hid)
  ,sink(comp.sink) {
}

Node Node_pin::get_node() const {
  I(hid==0);
  return g->get_node(idx);
}

uint16_t Node_pin::get_bits() const {
  I(hid==0);
  return g->get_bits(idx);
}

void Node_pin::set_bits(uint16_t bits) {
  I(hid==0);
  I(is_driver());
  g->set_bits(idx, bits);
}

void Node_pin::set_name(std::string_view wname) {
  Ann_node_pin_name::set(*this, wname);
}

std::string_view Node_pin::get_name() const {
  return Ann_node_pin_name::get(*this);
}

bool Node_pin::has_name() const {
  return Ann_node_pin_name::has(*this);
}

void Node_pin::set_offset(uint16_t offset) {
  //I(0);
  // FIXME33
}

uint16_t Node_pin::get_offset() const {
  //I(0);
  return 0; // FIXME33
}