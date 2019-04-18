//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <atomic>
#include <fstream>

#include "eprp_utils.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

#include "inou_graphviz.hpp"

void setup_inou_graphviz() {
  Inou_graphviz p;
  p.setup();
}

void Inou_graphviz::setup() {

  Eprp_method m2("inou.graphviz", "export lgraph to graphviz dot format", &Inou_graphviz::fromlg);

  m2.add_label_optional("bits", "dump bits (true/false)", "false");
  m2.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");

  register_inou(m2);
}

Inou_graphviz::Inou_graphviz()
    : Pass("graphviz") {

  bits = false;
  verbose = false;
}

void Inou_graphviz::fromlg(Eprp_var &var) {

  Inou_graphviz p;

  p.odir = var.get("odir");
  p.bits = var.get("bits") == "true";
  p.verbose = var.get("verbose") == "true";

  bool ok = p.setup_directory(p.odir);
  if(!ok)
    return;

  std::vector<LGraph *> lgs;
  for(const auto &l : var.lgs) {
    lgs.push_back(l);
  }

  p.do_fromlg(lgs);
}

void Inou_graphviz::do_fromlg(std::vector<LGraph *> &lgs) {

  for(const auto g : lgs) {
    const auto hier = g->get_hierarchy();
    fmt::print("hierarchy for {}\n",g->get_name());
    for(auto &[name,lgid]:hier) {
      fmt::print("  {} {}\n",name,lgid);
    }
  }
  for(const auto lg_parent : lgs) {
    populate_data(lg_parent);
    lg_parent->each_sub_graph_fast([lg_parent,this](Node node, Lg_type_id lgid){
      fmt::print("subgraph lgid:{}\n", lgid);
      LGraph *lg_child = LGraph::open(lg_parent->get_path(), lgid);
      populate_data(lg_child);
    });
  }
}

void Inou_graphviz::populate_data(LGraph* g){
  std::string data = "digraph {\n";

  g->each_node_fast([&data](const Node &node) {
    const auto &ntype = node.get_type();
      data += fmt::format(" {} [label=\"{}:{}\"];\n", node.create_name(), ntype.get_name(), node.get_name());

    for(auto &out : node.out_edges()){
      Node_pin dpin = out.driver;
      Node_pin spin = out.sink;
      auto dname = dpin.get_node().create_name();
      auto sname = spin.get_node().create_name();
      auto bits = dpin.get_bits();
        data += fmt::format(" {}->{}[label=\"{}b: {}:{}\"];\n", dname, sname, bits, dpin.get_pid(), spin.get_pid());
    }
  });

  g->each_graph_output([&data](const Node_pin &pin) {
    std::string_view dst_str = "virtual_dst_module";
    auto bits = pin.get_bits();
      data += fmt::format(" {}->{}[label=\"{}b\"];\n", pin.get_node().create_name(), dst_str, bits);
  });


  data += "}\n";

  std::string file = absl::StrCat(odir, "/", g->get_name(), ".dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if(fd < 0) {
    Pass::error("inou.graphviz unable to create {}", file);
    return;
  }
  write(fd, data.c_str(), data.size());
  close(fd);
}

