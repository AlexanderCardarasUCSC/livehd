//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <math.h>
#include <algorithm>

#include "lgyosys_dump.hpp"
#include "lgedgeiter.hpp"

RTLIL::Wire *Lgyosys_dump::get_wire(const Node_pin &pin) {
  auto inp_it = input_map.find(pin.get_compact());
  if (inp_it != input_map.end()) {
    return inp_it->second;
  }

  auto out_it = output_map.find(pin.get_compact());
  if (out_it != output_map.end()) {
    return out_it->second;
  }

  auto cell_it = cell_output_map.find(pin.get_compact());
  if (cell_it != cell_output_map.end()) {
    return cell_it->second;
  }

  fmt::print("trying to get wire for non existing driver pin {}\n", pin.debug_name());
  assert(pin.is_driver());
  assert(false);

  return nullptr;
}

RTLIL::Wire *Lgyosys_dump::add_wire(RTLIL::Module *module, const Node_pin &pin) {
  assert(pin.is_driver());
  if (pin.has_name()) {
    auto name = absl::StrCat("\\", pin.get_name());
    // fmt::print("pin{} has name:{}\n", pin.debug_name(), name);
    return module->addWire(name, pin.get_bits());
  } else {
    return module->addWire(next_id(pin.get_class_lgraph()), pin.get_bits());
  }
}

RTLIL::Wire *Lgyosys_dump::create_tree(LGraph *g, const std::vector<RTLIL::Wire *> &wires, RTLIL::Module *mod,
                                       add_cell_fnc_sign add_fnc, bool sign, RTLIL::Wire *result_wire, int width) {
  assert(mod);

  if (wires.size() == 0)
    return nullptr;

  if (result_wire)
    width = result_wire->width;
  assert(width);

  if (wires.size() == 1) {
    if (result_wire) {                 // only in top level call
      if (wires[0]->width == width) {  // SIZE Match
        mod->connect(result_wire, wires[0]);
      } else if (wires[0]->width > width) {  // drop bits
        mod->connect(result_wire, RTLIL::SigSpec(wires[0], 0, width));
      } else {  // extend bits
        auto w2 = RTLIL::SigSpec(wires[0]);
        w2.extend_u0(width, true);  // signed extend
        mod->connect(result_wire, w2);
      }
      return result_wire;
    } else {
      return wires[0];
    }
  }

  RTLIL::Wire *l;
  RTLIL::Wire *r;

  if (wires.size() == 2) {
    l = wires[0];
    r = wires[1];
  } else {
    std::vector<RTLIL::Wire *> l_wires(wires.begin(), wires.begin() + wires.size() / 2);
    std::vector<RTLIL::Wire *> r_wires(wires.begin() + wires.size() / 2, wires.end());
    assert(l_wires.size() + r_wires.size() == wires.size());

    l = create_tree(g, l_wires, mod, add_fnc, sign, nullptr, width);
    r = create_tree(g, r_wires, mod, add_fnc, sign, nullptr, width);
  }

  auto name = next_id(g);
  if (result_wire == nullptr)
    result_wire = mod->addWire(next_id(g), width);

#if 0
  if (sign && (unsigned_wire.contains(result_wire) || unsigned_wire.contains(l) || unsigned_wire.contains(r)))
    sign = false;
#endif

  (mod->*add_fnc)(name, l, r, result_wire, sign, "");

  return result_wire;
}

RTLIL::Wire *Lgyosys_dump::create_io_wire(const Node_pin &pin, RTLIL::Module *module, Port_ID pos) {
  assert(pin.has_name());  // IO must have name
  RTLIL::IdString name = absl::StrCat("\\", pin.get_name());

  RTLIL::Wire *new_wire  = module->addWire(name, pin.get_bits());
  // WARNING: In yosys, the IOs can be signed or unsigned
  // unsigned_wire.insert(new_wire);
  new_wire->start_offset = pin.get_offset();

  module->ports.push_back(name);
  assert(pos == module->ports.size());
  new_wire->port_id = module->ports.size();

  return new_wire;
}

void Lgyosys_dump::create_blackbox(const Sub_node &sub, RTLIL::Design *design) {
  if (created_sub.find(sub.get_name()) != created_sub.end())
    return;
  created_sub.insert(sub.get_name());

  RTLIL::Module *mod            = new RTLIL::Module;
  mod->name                     = absl::StrCat("\\", sub.get_name());
  mod->attributes["\\blackbox"] = RTLIL::Const(1);

  design->add(mod);

  int port_id = 0;
  for (const auto *io_pin : sub.get_io_pins()) { // no need to be sorted if pins are named
    //fmt::print("bbox:{} name:{}\n", sub.get_name(), io_pin->name);
    std::string  name = absl::StrCat("\\", io_pin->name);
    RTLIL::Wire *wire = mod->addWire(name);  // , pin.get_bits());
    wire->port_id     = port_id++;
    if (io_pin->is_input()) {
      wire->port_input  = false;
      wire->port_output = true;
    } else {
      wire->port_input  = true;
      wire->port_output = false;
    }
  }

  mod->fixup_ports();
}

void Lgyosys_dump::create_memory(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type_op() == Ntype_op::Memory);

  RTLIL::Cell *memory = module->addCell(absl::StrCat("\\", node.create_name()), RTLIL::IdString("$mem"));

  RTLIL::SigSpec wr_addr;
  RTLIL::SigSpec wr_data;
  RTLIL::SigSpec wr_en;
  RTLIL::SigSpec wr_clk;
  uint64_t wr_clk_enable=0;
  uint64_t wr_clk_polarity=0;

  RTLIL::SigSpec rd_addr;
  RTLIL::SigSpec rd_data;
  RTLIL::SigSpec rd_en;
  RTLIL::SigSpec rd_clk;
  uint64_t rd_clk_enable =0;
  uint64_t rd_clk_polarity=0;

  RTLIL::State   rd_posedge = RTLIL::State::Sx;

  auto mem_size  = node.get_sink_pin("size").get_driver_node().get_type_const().to_i();
  auto data_bits = node.get_sink_pin("bits").get_driver_node().get_type_const().to_i();
  auto addr_dpin = node.get_sink_pin("addr").get_driver_pin();
  auto mode_node = node.get_sink_pin("mode").get_driver_node();
  if (!mode_node.is_type_const()) {
    log_error("TODO: Yosys does not rd/wr ports in memories\n");
  }

  int  addr_bits = ceill(log2(mem_size));

  int n_ports = addr_dpin.get_bits()/addr_bits;
  assert(n_ports*addr_bits == addr_dpin.get_bits()); // exact match

  auto mode_mask = mode_node.get_type_const().to_i();

  auto do_fwd = node.get_sink_pin("fwd").get_driver_node().get_type_const().to_i();
  assert(do_fwd == 0 || do_fwd==1);

  auto latency_value = node.get_sink_pin("latency").get_driver_node().get_type_const().to_i();

  memory->setParam("\\SIZE" , mem_size);
  memory->setParam("\\MEMID", RTLIL::Const::from_string(std::string(node.get_name())));
  memory->setParam("\\WIDTH", data_bits);

  memory->setParam("\\OFFSET", RTLIL::Const(0)); // mem[addr-OFFSET]. Why????
  memory->setParam("\\INIT", RTLIL::Const::from_string("x"));

  auto posedge_value = node.get_sink_pin("posclk").get_driver_node().get_type_const().to_i();

  auto clock_dpin = node.get_sink_pin("clock").get_driver_pin();
  auto *clk_wire = get_wire(clock_dpin);

  auto *addr_wire    = get_wire(addr_dpin);
  auto *data_in_wire = get_wire(node.get_sink_pin("data_in").get_driver_pin());
  auto *enable_wire  = get_wire(node.get_sink_pin("enable").get_driver_pin());
  int nrd = 0;
  int nwr = 0;
  {
    int tmp         = mode_mask;
    int addr_pos    = 0;
    int data_in_pos = 0;
    int enable_pos  = 0;
    int posedge     = posedge_value;
    int latency     = latency_value;
    while(tmp) {
      auto lat = latency & 3;
      auto pos = posedge & 1;
      if (tmp&1) { // WR_PORT
        nwr++;
        wr_addr.append( RTLIL::SigSpec(addr_wire   ,addr_pos   ,addr_pos   +addr_bits));
        wr_data.append( RTLIL::SigSpec(data_in_wire,data_in_pos,data_in_pos+data_bits));
        wr_en.append  ( RTLIL::SigSpec(enable_wire ,enable_pos ,enable_pos +1        ));

        wr_clk_polarity <<= 1;
        wr_clk_enable   <<= 1;
        if (lat==0) {
          wr_clk.append(RTLIL::State::Sx);
        }else if (lat==1) {
          wr_clk_polarity |= pos?1:0;
          wr_clk_enable   |= 1;

          if (clock_dpin.get_bits()==1) {
            wr_clk.append(RTLIL::SigSpec(RTLIL::SigBit(clk_wire)));
          }else{
            wr_clk.append(RTLIL::SigSpec(clk_wire, enable_pos, 1));
          }
        }else{
          log_error("latency of %d not currently supported by yosys memory wr bridge\n", lat);
        }

        data_in_pos += data_bits;
      }else{ // RD_PORT
        nrd++;
        rd_addr.append( RTLIL::SigSpec(addr_wire,addr_pos,addr_pos+addr_bits));
        rd_en.append  ( RTLIL::SigSpec(enable_wire ,enable_pos ,enable_pos +1        ));

        rd_clk_polarity <<= 1;
        rd_clk_enable   <<= 1;
        if (lat==0) {
          rd_clk.append(RTLIL::State::Sx);
        }else if (lat==1) {
          rd_clk_enable |= 1;
          if (clock_dpin.get_bits()==1) {
            rd_clk.append(RTLIL::SigSpec(RTLIL::SigBit(clk_wire)));
          }else{
            rd_clk.append(RTLIL::SigSpec(clk_wire, enable_pos, 1));
          }
          rd_clk_polarity <<= pos?1:0;
        }else{
          log_error("latency of %d not currently supported by yosys memory rd bridge\n", lat);
        }
      }
      tmp = tmp>>1;

      addr_pos += addr_bits;
      enable_pos++;
      posedge >>=1;
      latency >>=2;
    }
  }

  memory->setParam("\\RD_PORTS", RTLIL::Const(nrd));
  memory->setParam("\\WR_PORTS", RTLIL::Const(nwr));
  memory->setParam("\\RD_TRANSPARENT", RTLIL::Const(do_fwd, nrd)); // all the rd ports do

  memory->setParam("\\WR_CLK_POLARITY", RTLIL::Const(wr_clk_polarity));
  memory->setParam("\\WR_CLK_ENABLE",   RTLIL::Const(wr_clk_enable));

  memory->setParam("\\RD_CLK_POLARITY", RTLIL::Const(rd_clk_polarity));
  memory->setParam("\\RD_CLK_ENABLE",   RTLIL::Const(rd_clk_enable));

  memory->setParam("\\RD_CLK_POLARITY", rd_posedge);

  memory->setPort("\\WR_CLK",          wr_clk);
  memory->setPort("\\WR_DATA", wr_data);
  memory->setPort("\\WR_ADDR", wr_addr);
  memory->setPort("\\WR_EN",   wr_en);

  memory->setPort("\\RD_CLK",          rd_clk);
  memory->setPort("\\RD_DATA", RTLIL::SigSpec(mem_output_map[node.get_compact()]));
  memory->setPort("\\RD_ADDR", rd_addr);
  memory->setPort("\\RD_EN",   rd_en);
}

void Lgyosys_dump::create_subgraph(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type_op() == Ntype_op::Sub);

  const auto &sub = node.get_type_sub_node();

  create_blackbox(sub, module->design);

  RTLIL::Cell *new_cell = module->addCell(absl::StrCat("\\", node.create_name()), absl::StrCat("\\", sub.get_name()));

  fmt::print("inou_yosys instance_name:{}, subgraph->get_name():{}\n", node.get_name(), sub.get_name());
  for (const auto &e : node.inp_edges_ordered()) {
    auto  port_name = e.sink.get_type_sub_pin_name();
    fmt::print("input:{} pin:{}\n", port_name, e.driver.debug_name());
    RTLIL::Wire *input = get_wire(e.driver);
    new_cell->setPort(absl::StrCat("\\", port_name).c_str(), input);
  }
  for (const auto &dpin : node.out_connected_pins()) {
    auto  port_name = dpin.get_type_sub_pin_name();
    fmt::print("output:{} pin:{}\n", port_name, dpin.debug_name());
    RTLIL::Wire *output = get_wire(dpin);
    new_cell->setPort(absl::StrCat("\\", port_name).c_str(), output);
  }
}

void Lgyosys_dump::create_subgraph_outputs(LGraph *g, RTLIL::Module *module, Node &node) {
  assert(node.get_type_op() == Ntype_op::Sub);

  for (auto &dpin : node.out_connected_pins()) {
    cell_output_map[dpin.get_compact()] = add_wire(module, dpin);
  }
}

void Lgyosys_dump::create_wires(LGraph *g, RTLIL::Module *module) {
  // first create all the output wires

  uint32_t port_id = 0;
  g->each_sorted_graph_io([&port_id, module, this](const Node_pin &pin, Port_ID pos) {
    port_id++;
    if (pin.is_graph_output()) {
      output_map[pin.get_compact()]              = create_io_wire(pin, module, port_id);
      output_map[pin.get_compact()]->port_input  = false;
      output_map[pin.get_compact()]->port_output = true;
    } else {
      assert(pin.is_graph_input());
      input_map[pin.get_compact()]              = create_io_wire(pin, module, port_id);
      input_map[pin.get_compact()]->port_input  = true;
      input_map[pin.get_compact()]->port_output = false;
    }
  }, hierarchy);

  for (auto node : g->fast(hierarchy)) {
    I(!node.is_invalid());
    I(!node.is_type_io());

    if (!node.has_inputs() && !node.has_outputs())
      continue;  // DCE code

    auto op = node.get_type_op();

    if (op == Ntype_op::AttrSet || op == Ntype_op::AttrGet || op == Ntype_op::TupAdd || op == Ntype_op::TupGet)
      continue;

    if (op == Ntype_op::Const) {
      auto lc        = node.get_type_const();
      auto *new_wire = module->addWire(next_id(node.get_class_lgraph()), lc.get_bits());

      if (lc.get_bits()<31 && lc.is_i()) { // 32bit in yosys const
        module->connect(new_wire, RTLIL::SigSpec(RTLIL::Const(lc.to_i(), lc.get_bits())));
      } else {
        fmt::print("add:{} prp:{}\n",lc.to_yosys(), lc.to_pyrope());
        module->connect(new_wire, RTLIL::SigSpec(RTLIL::Const::from_string(lc.to_yosys())));
      }

      input_map[node.get_driver_pin().get_compact()] = new_wire;
      if (!lc.is_negative() && !lc.is_explicit_sign()) {
        unsigned_wire.insert(new_wire);
      }
    } else if (op == Ntype_op::Sub) {
      create_subgraph_outputs(g, module, node);
    } else if (op == Ntype_op::Memory) {
      for (auto &dpin : node.out_connected_pins()) {
        RTLIL::Wire *new_wire = add_wire(module, dpin);

        cell_output_map[dpin.get_compact()] = new_wire;
        mem_output_map[node.get_compact()].push_back(RTLIL::SigChunk(new_wire));
      }
    }else{
      auto         dpin                   = node.get_driver_pin();
      RTLIL::Wire *result                 = add_wire(module, dpin);
      cell_output_map[dpin.get_compact()] = result;
    }
  }
}

void Lgyosys_dump::to_yosys(LGraph *g) {
  std::string name(g->get_name());

#if 0
  if (g->is_empty()) {
    fprintf(stderr, "Warning: lgraph %s is empty. Skiping dump\n", name.c_str());
    return;
  }
#endif

  RTLIL::Module *module = design->addModule(absl::StrCat("\\", name));

  created_sub.clear();
  input_map.clear();
  output_map.clear();
  cell_output_map.clear();
  mem_output_map.clear();
  unsigned_wire.clear();

  create_wires(g, module); // 1st create all the dpin wires to be used

  g->each_graph_output([this, module](const Node_pin &pin) {
    assert(pin.is_graph_output());

    RTLIL::SigSpec lhs = RTLIL::SigSpec(output_map[pin.get_compact()]);

    for (const auto &e : pin.get_sink_from_output().inp_edges()) {

      RTLIL::SigSpec rhs = RTLIL::SigSpec(get_wire(e.driver));
      if(rhs == lhs)
        continue;
      if (lhs.size() == rhs.size()) {
        module->connect(lhs, rhs);
      } else if (lhs.size() < rhs.size()) {  // Drop bits
        assert(rhs.is_wire());
        module->connect(lhs, RTLIL::SigSpec(rhs.as_wire(), 0, lhs.size()));
      } else {
        rhs.extend_u0(lhs.size(), false);  // FIXME: missing Tposs unsigned extend
        module->connect(lhs, rhs);
      }
    }
  }, hierarchy);

  // now create nodes and make connections
  for (auto node : g->fast(hierarchy)) {

    if (!node.has_inputs() && !node.has_outputs())
      continue;  // DCE code

    auto op = node.get_type_op();
    I(op != Ntype_op::IO);

    if (op != Ntype_op::Memory && op != Ntype_op::Sub)
      if (!node.has_inputs() || !node.has_outputs())
        continue;

    assert(op != Ntype_op::Const); // filtered before

    Bits_t size = 0;
    switch (op) {
      case Ntype_op::Sum: {
        std::vector<RTLIL::Wire *> add_signed;
        std::vector<RTLIL::Wire *> sub_signed;

        size = 0; // max IO size
        for (const auto &e : node.inp_edges()) {
          size = (e.get_bits() > size) ? e.get_bits() : size;

          if (e.sink.get_pid()==0) {
            assert(e.sink.get_pin_name()=="A");
            add_signed.push_back(get_wire(e.driver));
          }else{
            assert(e.sink.get_pin_name()=="B");
            sub_signed.push_back(get_wire(e.driver));
          }
        }

        RTLIL::Wire *adds_result = nullptr;
        if (add_signed.size() > 1) {
          if (sub_signed.size() > 0) {
            adds_result = module->addWire(next_id(g), size);
          } else {
            adds_result = cell_output_map[node.get_driver_pin().get_compact()];
          }
          create_tree(g, add_signed, module, &RTLIL::Module::addAdd, true, adds_result);
        } else if (add_signed.size() == 1) {
          adds_result = add_signed[0];
        }

        RTLIL::Wire *subs_result = nullptr;
        if (sub_signed.size() > 1) {
          if (add_signed.size() > 0) {
            subs_result = module->addWire(next_id(g), size);
          } else {
            subs_result = cell_output_map[node.get_driver_pin().get_compact()];
          }
          create_tree(g, sub_signed, module, &RTLIL::Module::addAdd, true, subs_result);
        } else if (sub_signed.size() == 1) {
          subs_result = sub_signed[0];
        }

        RTLIL::Wire *a_result = adds_result;
        RTLIL::Wire *s_result = subs_result;

        if (a_result != nullptr && s_result != nullptr) {
          module->addSub(next_id(g), a_result, s_result, cell_output_map[node.get_driver_pin().get_compact()], true);
        } else if (s_result != nullptr) {
          module->addSub(next_id(g), RTLIL::Const(0), s_result, cell_output_map[node.get_driver_pin().get_compact()], true);
        } else {
          if (cell_output_map[node.get_driver_pin().get_compact()]->name != a_result->name)
            module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(a_result));
        }

      }
      break;
      case Ntype_op::Mult: {
        std::vector<RTLIL::Wire *> m_signed;

        size = 0;
        for (const auto &e : node.inp_edges()) {
          size = (e.get_bits() > size) ? e.get_bits() : size;

          m_signed.push_back(get_wire(e.driver));
        }

        RTLIL::Wire *ms_result = nullptr;

        if (m_signed.size() > 1) {
          ms_result = cell_output_map[node.get_driver_pin().get_compact()];
          create_tree(g, m_signed, module, &RTLIL::Module::addMul, true, ms_result);
        } else if (m_signed.size() == 1) {
          ms_result = m_signed[0];
        }

        if (cell_output_map[node.get_driver_pin().get_compact()]->name != ms_result->name)
          module->connect(cell_output_map[node.get_driver_pin().get_compact()], RTLIL::SigSpec(ms_result));

      }
      break;
      case Ntype_op::Div: {
        auto   a_dpin = node.get_sink_pin("a").get_driver_pin();
        auto   b_dpin = node.get_sink_pin("b").get_driver_pin();

        auto *lhs  = get_wire(a_dpin);
        auto *rhs  = get_wire(b_dpin);

        module->addDiv(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], true); // signed
      }
      break;
      case Ntype_op::Not: {
        auto *wire = get_wire(node.get_sink_pin().get_driver_pin());
        module->addNot(next_id(g), wire, cell_output_map[node.get_driver_pin().get_compact()]);
      }
      break;
      case Ntype_op::Tposs: {
        auto *in_wire  = get_wire(node.get_sink_pin().get_driver_pin());
        auto *out_wire = cell_output_map[node.get_driver_pin().get_compact()];

        unsigned_wire.insert(out_wire);

        if (in_wire->width == out_wire->width) {
          module->connect(out_wire, in_wire);
        }else if (out_wire->width>in_wire->width) {
          auto w2 = RTLIL::SigSpec(in_wire);
          w2.extend_u0(out_wire->width, false);  // unsigned extend
          module->connect(out_wire, w2);
        }else{
          Lconst mask = (Lconst(1)<<Lconst(out_wire->width))-1;
          module->addAnd(next_id(g), in_wire, RTLIL::Const::from_string(mask.to_yosys()), out_wire);
        }
      }
      break;
      case Ntype_op::LUT: {
        RTLIL::SigSpec joined_inp_wires;
        bool           has_inputs = false;
        uint8_t        inp_num    = 0;
        for (const auto &e : node.inp_edges_ordered()) {
          RTLIL::Wire *join = get_wire(e.driver);
          joined_inp_wires.append(RTLIL::SigSpec(join));
          has_inputs = true;
          inp_num += 1;
        }

        if (!has_inputs)
          continue;

        assert(cell_output_map.find(node.get_driver_pin().get_compact()) != cell_output_map.end());

        auto lut_code = RTLIL::Const::from_string(node.get_type_lut().to_yosys());

        module->addLut(next_id(g), joined_inp_wires, cell_output_map[node.get_driver_pin().get_compact()], lut_code);
      }
      break;
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor: {
        std::vector<RTLIL::Wire *> inps;

        bool must_be_signed = false;
        auto y_bits = node.get_bits();
        auto y_mask = (Lconst(1)<<(y_bits))-1;
        const auto inp_edges = node.inp_edges();
        for (const auto &e : inp_edges) {
          if (e.driver.get_bits() != y_bits)
            must_be_signed = true;

          if (op == Ntype_op::And) {
            auto dnode = e.driver.get_node();
            if (dnode.is_type_const()) {
              auto v = dnode.get_type_const();
              if (v == y_mask) {
                continue;
              }
            }
          }
          inps.push_back(get_wire(e.driver));
        }
        if (inps.empty() && !inp_edges.empty()) { // maybe it was a AND(63,63)
          inps.push_back(get_wire(inp_edges[0].driver));
        }

        add_cell_fnc_sign yop = &RTLIL::Module::addAnd;
        if (op == Ntype_op::Or)
          yop = &RTLIL::Module::addOr;
        else if (op == Ntype_op::Xor)
          yop = &RTLIL::Module::addXor;


        create_tree(g, inps, module, yop, true, cell_output_map[node.get_driver_pin().get_compact()]);
      }
      break;
      case Ntype_op::Ror: {
        std::vector<RTLIL::Wire *> inps;

        for (const auto &e : node.inp_edges()) {
          inps.push_back(get_wire(e.driver));
        }

        RTLIL::Wire *or_input_wires = module->addWire(next_id(g), inps[0]->width);
        create_tree(g, inps, module, &RTLIL::Module::addOr, false, or_input_wires);
        module->addReduceOr(next_id(g), or_input_wires, cell_output_map[node.get_driver_pin().get_compact()]);
      }
      break;
      case Ntype_op::Latch: {
        auto     din_dpin = node.get_sink_pin("din"   ).get_driver_pin();
        auto  enable_dpin = node.get_sink_pin("enable").get_driver_pin();
        auto  posclk_dpin = node.get_sink_pin("posclk").get_driver_pin();

        assert(!    din_dpin.is_invalid());
        assert(! enable_dpin.is_invalid());

        RTLIL::Wire *    din_wire = get_wire(    din_dpin);
        RTLIL::Wire * enable_wire = get_wire( enable_dpin);
        bool polarity = true;

        if (!posclk_dpin.is_invalid()) {
          auto v = posclk_dpin.get_node().get_type_const();
          polarity = v.to_i() != 0;
        }

        module->addDlatch(next_id(g), enable_wire, din_wire, cell_output_map[node.get_driver_pin().get_compact()], polarity);
      }
      break;
      case Ntype_op::Sflop:
      case Ntype_op::Aflop: {

        Node_pin    reset_dpin;
        Node_pin  initial_dpin;
        Node_pin    clock_dpin;
        Node_pin      din_dpin;
        Node_pin   enable_dpin;
        Node_pin   posclk_dpin;
        Node_pin negreset_dpin;
        Node_pin    async_dpin;

        if (node.is_sink_connected("reset"   ))    reset_dpin = node.get_sink_pin("reset"   ).get_driver_pin();
        if (node.is_sink_connected("initial" ))  initial_dpin = node.get_sink_pin("initial" ).get_driver_pin();
        if (node.is_sink_connected("clock"   ))    clock_dpin = node.get_sink_pin("clock"   ).get_driver_pin();
        if (node.is_sink_connected("din"     ))      din_dpin = node.get_sink_pin("din"     ).get_driver_pin();
        if (node.is_sink_connected("enable"  ))   enable_dpin = node.get_sink_pin("enable"  ).get_driver_pin();
        if (node.is_sink_connected("posclk"  ))   posclk_dpin = node.get_sink_pin("posclk"  ).get_driver_pin();
        if (node.is_sink_connected("negreset")) negreset_dpin = node.get_sink_pin("negreset").get_driver_pin();
        if (node.is_sink_connected("async"   ))    async_dpin = node.get_sink_pin("async"   ).get_driver_pin();

        RTLIL::Wire *  reset_wire = nullptr;
        RTLIL::Wire *  clock_wire = nullptr;
        RTLIL::Wire *    din_wire = nullptr;
        RTLIL::Wire * enable_wire = nullptr;

        if (!  reset_dpin.is_invalid()) {   reset_wire = get_wire(  reset_dpin); }
        if (!  clock_dpin.is_invalid()) {   clock_wire = get_wire(  clock_dpin); }
        if (!    din_dpin.is_invalid()) {     din_wire = get_wire(    din_dpin); }
        if (! enable_dpin.is_invalid()) {  enable_wire = get_wire( enable_dpin); }
        if (!initial_dpin.is_invalid()) { assert(reset_wire); } //  no reset value in yosys, add mux before

        bool posclk   =    posclk_dpin.is_invalid() ||   posclk_dpin.get_node().get_type_const().to_i()!=0;
        bool async    = !   async_dpin.is_invalid() &&    async_dpin.get_node().get_type_const().to_i()!=0;
        bool negreset = !negreset_dpin.is_invalid() && negreset_dpin.get_node().get_type_const().to_i()!=0;

        assert(clock_wire);
        assert(din_wire);

        if (reset_wire==nullptr) {
          assert(initial_dpin.is_invalid()); // no reset here
          assert(!async); // only if reset is present, it can be async

          if (enable_wire == nullptr) {
            module->addDff(next_id(g), clock_wire, din_wire, cell_output_map[node.get_driver_pin().get_compact()], posclk);
          } else {
            module->addDffe(next_id(g),
                clock_wire,
                enable_wire,
                din_wire,
                cell_output_map[node.get_driver_pin().get_compact()],
                posclk,
                true); // enable is always active high in LiveHD
          }
        }else{ // reset wire
          RTLIL::Const initial_const(0, node.get_bits());
          if (!initial_dpin.is_invalid()) {
            initial_const = RTLIL::Const::from_string(initial_dpin.get_node().get_type_const().to_yosys());
          }

          if (enable_wire == nullptr) {
            if (async) {
              module->addAdff(next_id(g),
                clock_wire,
                reset_wire,
                din_wire,
                cell_output_map[node.get_driver_pin().get_compact()],
                initial_const,
                posclk,
                !negreset); // reset_polarity
            }else{
              module->addSdff(next_id(g),
                clock_wire,
                reset_wire,
                din_wire,
                cell_output_map[node.get_driver_pin().get_compact()],
                initial_const,
                posclk,
                !negreset); // reset_polarity
            }
          }else{
            if (async) {
              module->addAdffe(next_id(g),
                  clock_wire,
                  enable_wire,
                  reset_wire,
                  din_wire,
                  cell_output_map[node.get_driver_pin().get_compact()],
                  initial_const,
                  posclk,
                  true,   // only posedge enable polarity supported in LiveHD
                  !negreset); // reset_polarity
            }else{
              module->addSdffe(next_id(g),
                  clock_wire,
                  enable_wire,
                  reset_wire,
                  din_wire,
                  cell_output_map[node.get_driver_pin().get_compact()],
                  initial_const,
                  posclk,
                  true,   // only posedge enable polarity supported in LiveHD
                  !negreset); // reset_polarity
            }
          }
        }
      }
      break;
      case Ntype_op::EQ: {
        std::vector<RTLIL::Wire *> rest;
        RTLIL::Wire *first=nullptr;
        for (const auto &e : node.inp_edges()) {
          auto *w= get_wire(e.driver);
          if (first)
            rest.push_back(w);
          else
            first = w;
        }
        assert(rest.size()>0); // otherwise nothing to compare
        RTLIL::Wire *final_wire = cell_output_map[node.get_driver_pin().get_compact()];

        bool multi_comparator = rest.size()>1;
        std::vector<RTLIL::Wire *> cmp_wires;
        for(auto *lhs:rest) {
          bool must_be_signed = true;
          if (first->width == lhs->width)
            must_be_signed = false;

          RTLIL::Wire *wire;
          if (multi_comparator) {
            auto *wire = module->addWire(next_id(g), size);
            module->addEq(next_id(g), first, lhs, wire, must_be_signed);
            cmp_wires.emplace_back(wire);
          }else{
            module->addEq(next_id(g), first, lhs, final_wire, must_be_signed);
          }
        }
        if (multi_comparator)
          create_tree(g, cmp_wires, module, &RTLIL::Module::addAnd, false, final_wire);
      }
      break;
      case Ntype_op::LT:
      case Ntype_op::GT: {
        std::vector<RTLIL::Wire *> v_lhs;
        std::vector<RTLIL::Wire *> v_rhs;

        for (const auto &e : node.inp_edges()) {
          switch (e.sink.get_pid()) {
            case 0:
              v_lhs.emplace_back(get_wire(e.driver));
              break;
            case 1:
              v_rhs.emplace_back(get_wire(e.driver));
              break;
          }
        }

        if (v_lhs.empty() || v_rhs.empty())
          log_error("Internal error: found no connections to compare.\n");

        RTLIL::Wire *final_wire = cell_output_map[node.get_driver_pin().get_compact()];
        assert(final_wire);

        bool multi_comparator = v_lhs.size()>1 || v_rhs.size()>1;
        std::vector<RTLIL::Wire *> cmp_wires;

        for(auto *lhs:v_lhs) {
          for( auto *rhs:v_rhs) {
            bool must_be_signed = true;
            if (unsigned_wire.contains(rhs) && unsigned_wire.contains(lhs))
              must_be_signed = false;

            RTLIL::Wire *wire;
            if (multi_comparator) {
              wire = module->addWire(next_id(g), size);
              cmp_wires.emplace_back(wire);
            }else{
              wire = final_wire;
            }
            if (node.is_type(Ntype_op::LT)) {
              module->addLt(next_id(g), lhs, rhs, wire, must_be_signed); break;
            }else{
              assert(node.is_type(Ntype_op::GT));
              module->addGt(next_id(g), lhs, rhs, wire, must_be_signed); break;
            }
          }
        }
        if (multi_comparator) {
          create_tree(g, cmp_wires, module, &RTLIL::Module::addAnd, false, final_wire);
        }
      }
      break;
      case Ntype_op::SRA: {
        auto   a_dpin = node.get_sink_pin("a").get_driver_pin();
        auto   b_dpin = node.get_sink_pin("b").get_driver_pin();

        auto *lhs  = get_wire(a_dpin);
        auto *rhs  = get_wire(b_dpin);

        auto dpin   = cell_output_map[node.get_driver_pin().get_compact()];

        if (unsigned_wire.contains(lhs)) {
          if (b_dpin.get_node().is_type_const()) { // common optimization
            auto amount = RTLIL::Const::from_string(b_dpin.get_node().get_type_const().to_yosys());
            module->addShr(next_id(g), lhs, amount, dpin, false);
          }else{
            module->addShr(next_id(g), lhs, rhs, dpin, false);
          }
        }else{
          if (b_dpin.get_node().is_type_const()) { // common optimization
            auto amount = RTLIL::Const::from_string(b_dpin.get_node().get_type_const().to_yosys());
            module->addSshr(next_id(g), lhs, amount, dpin, true);
          }else{
            module->addSshr(next_id(g), lhs, rhs, dpin, true);
          }
        }
      }
      break;
      case Ntype_op::SHL: {
        auto   a_dpin = node.get_sink_pin("a").get_driver_pin();
        auto   b_dpin = node.get_sink_pin("b").get_driver_pin();

        auto *lhs  = get_wire(a_dpin);
        auto *rhs  = get_wire(b_dpin);

        if (b_dpin.get_node().is_type_const()) { // common optimization
          auto amount = RTLIL::Const::from_string(b_dpin.get_node().get_type_const().to_yosys());
          module->addSshl(next_id(g), lhs, amount, cell_output_map[node.get_driver_pin().get_compact()], true);
        }else{
          module->addSshl(next_id(g), lhs, rhs, cell_output_map[node.get_driver_pin().get_compact()], true);
        }
      }
      break;
      case Ntype_op::Mux: {
        std::vector<RTLIL::Wire *> port;
        RTLIL::Wire *sel   = nullptr;

        auto out_width = cell_output_map[node.get_driver_pin().get_compact()]->width;

        for (const auto &e : node.inp_edges_ordered()) {
          if (e.sink.get_pid()==0) {
            sel = get_wire(e.sink.get_driver_pin());
            if (sel->width>1) { // drop upper bits (Tposs)
              RTLIL::Wire *new_wire  = module->addWire(next_id(g), 1);
              module->addAnd(next_id(g), sel, RTLIL::Const(1), new_wire);
              sel = new_wire;
              assert(sel->width==1);
            }
            continue;
          }
          auto *wire = get_wire(e.sink.get_driver_pin());

          if (wire->width == out_width) {
            port.emplace_back(wire);
          }else{
            auto w2 = RTLIL::SigSpec(wire);
            auto *new_wire = module->addWire(next_id(g), out_width);

            if (wire->width < out_width) {
              if (unsigned_wire.contains(wire))
                w2.extend_u0(out_width, false);  // zero extend
              else
                w2.extend_u0(out_width, true);  // sign extend
            }else{
              w2 = w2.extract(0, out_width);  // drop bits
            }
            module->connect(new_wire, w2);
            port.emplace_back(new_wire);
          }
        }

        assert(port.size()==2); // FIXME: add more ports (tree of muxes)

        module->addMux(next_id(g), port[0], port[1], sel, cell_output_map[node.get_driver_pin().get_compact()]);
      }
      break;
      case Ntype_op::Memory: {
        create_memory(g, module, node);
      }
      break;
      case Ntype_op::Sub: {
        create_subgraph(g, module, node);
      }
      break;
      default: {
        std::string name(node.get_type_name());
        log_error("Operation %s not supported, please add to lgyosys_dump.\n", name.c_str());
      }
    }
  }
}

