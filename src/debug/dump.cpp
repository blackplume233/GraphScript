/// Debug dump & diff utilities for GraphScript data structures.
/// Produces human-readable hierarchical text from Module, EditGraph, and RuntimeGraph.

#include "graphscript/debug/dump.h"

#include "graphscript/core/module.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/runtime/runtime_graph.h"

#include <sstream>
#include <algorithm>

namespace gs {
namespace debug {

// ─── Internal helpers ──────────────────────────────────────────────

static std::string indent(int depth) {
    return std::string(static_cast<size_t>(depth) * 2, ' ');
}

static std::string quote(const std::string& s) {
    return "\"" + s + "\"";
}

static std::string dump_annotation_args(const std::vector<AnnotationArg>& args) {
    std::string out = "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += ", ";
        if (!args[i].name.empty()) out += args[i].name + " = ";
        out += args[i].value;
    }
    out += ")";
    return out;
}

static std::string dump_annotations_inline(const std::vector<Annotation>& annots) {
    if (annots.empty()) return "";
    std::string out = " [";
    for (size_t i = 0; i < annots.size(); ++i) {
        if (i > 0) out += ", ";
        out += annots[i].name + dump_annotation_args(annots[i].args);
    }
    out += "]";
    return out;
}

static std::string dump_annotations_block(const std::vector<Annotation>& annots, int depth) {
    if (annots.empty()) return "";
    std::string out = indent(depth) + "annotations: [";
    for (size_t i = 0; i < annots.size(); ++i) {
        if (i > 0) out += ", ";
        out += annots[i].name + dump_annotation_args(annots[i].args);
    }
    out += "]\n";
    return out;
}

static std::string param_dir_str(ParamDirection d) {
    switch (d) {
        case ParamDirection::In:  return "in";
        case ParamDirection::Out: return "out";
        case ParamDirection::Var: return "var";
    }
    return "?";
}

static std::string pin_kind_str(PinKind k) { return k == PinKind::Exec ? "Exec" : "Data"; }
static std::string pin_dir_str(PinDirection d) { return d == PinDirection::Input ? "Input" : "Output"; }
static std::string pin_kind_str(uint8_t k) { return k == 0 ? "Exec" : "Data"; }
static std::string pin_dir_str(uint8_t d) { return d == 0 ? "Input" : "Output"; }

// ─── dump_module ───────────────────────────────────────────────────

static void dump_logic_block(std::ostringstream& os, const std::string& kind,
                             const LogicBlock& block, int depth) {
    os << indent(depth) << kind << " " << block.name << " {\n";
    if (!block.flow_connections.empty()) {
        os << indent(depth + 1) << "flows:\n";
        for (auto& fc : block.flow_connections)
            os << indent(depth + 2) << fc.from.node_instance << "." << fc.from.pin_name
               << " -> " << fc.to.node_instance << "." << fc.to.pin_name << "\n";
    }
    if (!block.data_links.empty()) {
        os << indent(depth + 1) << "links:\n";
        for (auto& dl : block.data_links)
            os << indent(depth + 2) << dl.target.node_instance << "." << dl.target.pin_name
               << " <- " << dl.source.node_instance
               << (dl.source.pin_name.empty() ? "" : "." + dl.source.pin_name) << "\n";
    }
    os << indent(depth) << "}\n";
}

std::string dump_module(const Module& mod) {
    std::ostringstream os;
    os << "Module {\n";

    if (!mod.file_path.empty())
        os << indent(1) << "file: " << quote(mod.file_path) << "\n";

    if (!mod.imports.empty()) {
        os << indent(1) << "imports: [";
        for (size_t i = 0; i < mod.imports.size(); ++i) {
            if (i > 0) os << ", ";
            os << quote(mod.imports[i].path);
        }
        os << "]\n";
    }

    if (!mod.top_level_lets.empty()) {
        os << indent(1) << "lets:\n";
        for (auto& l : mod.top_level_lets)
            os << indent(2) << "- " << l.name << " : " << l.type_name
               << (l.constructor_arg.empty() ? "" : " = " + quote(l.constructor_arg)) << "\n";
    }

    for (auto& g : mod.graphs) {
        os << indent(1) << "Graph " << quote(g.name);
        if (g.base_type.has_value()) os << " : " << g.base_type.value();
        os << " {\n";
        os << dump_annotations_block(g.annotations, 2);

        if (!g.parameters.empty()) {
            os << indent(2) << "params:\n";
            for (auto& p : g.parameters) {
                os << indent(3) << "- " << param_dir_str(p.direction) << " " << p.name << " : " << p.type_name;
                if (!p.default_value.empty()) os << " = " << p.default_value;
                os << dump_annotations_inline(p.annotations) << "\n";
            }
        }

        if (!g.node_instances.empty()) {
            os << indent(2) << "nodes:\n";
            for (auto& ni : g.node_instances) {
                os << indent(3) << "- " << ni.type_name << " " << ni.instance_name;
                if (!ni.initializer.empty()) os << "{" << ni.initializer << "}";
                else os << "{}";
                os << dump_annotations_inline(ni.annotations) << "\n";
            }
        }

        for (auto& ev : g.events)    dump_logic_block(os, "event", ev, 2);
        for (auto& fn : g.functions) dump_logic_block(os, "function", fn, 2);

        if (g.generate.has_value()) {
            os << indent(2) << "generate {\n";
            for (auto& c : g.generate->comments)
                os << indent(3) << "Comment " << c.instance_name << " = " << quote(c.text) << "\n";
            for (auto& m : g.generate->metadata)
                os << indent(3) << m.scope << ":" << m.node << "." << m.property
                   << "(" << m.value << ")\n";
            os << indent(2) << "}\n";
        }

        os << indent(1) << "}\n";
    }

    os << "}\n";
    return os.str();
}

// ─── dump_edit_graph ───────────────────────────────────────────────

std::string dump_edit_graph(const EditGraph& eg) {
    std::ostringstream os;
    os << "EditGraph " << quote(eg.name()) << " {\n";

    if (!eg.parameters().empty()) {
        os << indent(1) << "params:\n";
        for (auto& p : eg.parameters()) {
            os << indent(2) << "- " << param_dir_str(p.direction) << " " << p.name << " : " << p.type_name;
            if (!p.default_value.empty()) os << " = " << p.default_value;
            os << dump_annotations_inline(p.annotations) << "\n";
        }
    }

    os << indent(1) << "nodes (" << eg.node_count() << "):\n";
    eg.for_each_node([&](Handle h, const EditNode& n) {
        os << indent(2) << "- [" << h.index << ":" << h.generation << "] "
           << n.type_name << " " << n.instance_name;
        if (!n.initializer.empty()) os << "{" << n.initializer << "}";
        os << "\n";
        for (auto& pin : n.pins)
            os << indent(3) << pin_kind_str(pin.kind) << " " << pin_dir_str(pin.direction)
               << " " << pin.name << " : " << pin.type_name << "\n";
    });

    os << indent(1) << "connections (" << eg.connection_count() << "):\n";
    eg.for_each_connection([&](Handle /*h*/, const EditConnection& c) {
        os << indent(2) << "- [" << c.from_node.index << "]." << c.from_pin
           << " -> [" << c.to_node.index << "]." << c.to_pin
           << " (" << pin_kind_str(c.kind) << ")\n";
    });

    os << "}\n";
    return os.str();
}

// ─── dump_runtime_graph ────────────────────────────────────────────

std::string dump_runtime_graph(const RuntimeGraph& rg) {
    std::ostringstream os;
    os << "RuntimeGraph " << quote(rg.name()) << " {\n";
    if (!rg.domain_name().empty())
        os << indent(1) << "domain: " << quote(rg.domain_name()) << "\n";

    os << indent(1) << "nodes (" << rg.node_count() << "):\n";
    for (auto& n : rg.nodes()) {
        os << indent(2) << "- " << n.type_name << " " << n.instance_name
           << " [pins " << n.first_pin << ".." << (n.first_pin + n.pin_count - 1) << "]\n";
    }

    os << indent(1) << "pins (" << rg.pins().size() << "):\n";
    for (size_t i = 0; i < rg.pins().size(); ++i) {
        auto& p = rg.pins()[i];
        os << indent(2) << "[" << i << "] node" << p.node_index << "." << p.name
           << " " << pin_kind_str(p.kind) << " " << pin_dir_str(p.direction)
           << " : " << p.type_name << "\n";
    }

    os << indent(1) << "flow_edges (" << rg.flow_edge_count() << "):\n";
    for (auto& e : rg.flow_edges())
        os << indent(2) << "node" << e.from_node << ".pin" << (int)e.from_pin
           << " -> node" << e.to_node << ".pin" << (int)e.to_pin << "\n";

    os << indent(1) << "data_edges (" << rg.data_edge_count() << "):\n";
    for (auto& e : rg.data_edges())
        os << indent(2) << "node" << e.source_node << ".pin" << (int)e.source_pin
           << " -> node" << e.target_node << ".pin" << (int)e.target_pin << "\n";

    os << "}\n";
    return os.str();
}

// ─── diff_modules ──────────────────────────────────────────────────

static void diff_annotations(DiffResult& r, const std::string& path,
                              const std::vector<Annotation>& a, const std::vector<Annotation>& b) {
    if (a.size() != b.size()) {
        r.add(path, "annotation count " + std::to_string(a.size()) + " vs " + std::to_string(b.size()));
        return;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        std::string ap = path + ".annot[" + std::to_string(i) + "]";
        if (a[i].name != b[i].name)
            r.add(ap, "name " + quote(a[i].name) + " vs " + quote(b[i].name));
        if (a[i].args.size() != b[i].args.size()) {
            r.add(ap, "arg count " + std::to_string(a[i].args.size()) + " vs " + std::to_string(b[i].args.size()));
            continue;
        }
        for (size_t j = 0; j < a[i].args.size(); ++j) {
            std::string aap = ap + ".arg[" + std::to_string(j) + "]";
            if (a[i].args[j].name != b[i].args[j].name)
                r.add(aap, "name " + quote(a[i].args[j].name) + " vs " + quote(b[i].args[j].name));
            if (a[i].args[j].value != b[i].args[j].value)
                r.add(aap, "value " + quote(a[i].args[j].value) + " vs " + quote(b[i].args[j].value));
        }
    }
}

static void diff_logic_block(DiffResult& r, const std::string& path,
                              const LogicBlock& a, const LogicBlock& b) {
    if (a.name != b.name)
        r.add(path, "name " + quote(a.name) + " vs " + quote(b.name));
    if (a.flow_connections.size() != b.flow_connections.size()) {
        r.add(path + ".flows", "count " + std::to_string(a.flow_connections.size())
              + " vs " + std::to_string(b.flow_connections.size()));
    } else {
        for (size_t i = 0; i < a.flow_connections.size(); ++i) {
            std::string fp = path + ".flows[" + std::to_string(i) + "]";
            auto& fa = a.flow_connections[i];
            auto& fb = b.flow_connections[i];
            if (fa.from.node_instance != fb.from.node_instance)
                r.add(fp, "from_node " + quote(fa.from.node_instance) + " vs " + quote(fb.from.node_instance));
            if (fa.from.pin_name != fb.from.pin_name)
                r.add(fp, "from_pin " + quote(fa.from.pin_name) + " vs " + quote(fb.from.pin_name));
            if (fa.to.node_instance != fb.to.node_instance)
                r.add(fp, "to_node " + quote(fa.to.node_instance) + " vs " + quote(fb.to.node_instance));
            if (fa.to.pin_name != fb.to.pin_name)
                r.add(fp, "to_pin " + quote(fa.to.pin_name) + " vs " + quote(fb.to.pin_name));
        }
    }
    if (a.data_links.size() != b.data_links.size()) {
        r.add(path + ".links", "count " + std::to_string(a.data_links.size())
              + " vs " + std::to_string(b.data_links.size()));
    } else {
        for (size_t i = 0; i < a.data_links.size(); ++i) {
            std::string lp = path + ".links[" + std::to_string(i) + "]";
            auto& la = a.data_links[i];
            auto& lb = b.data_links[i];
            if (la.target.node_instance != lb.target.node_instance)
                r.add(lp, "target_node " + quote(la.target.node_instance) + " vs " + quote(lb.target.node_instance));
            if (la.target.pin_name != lb.target.pin_name)
                r.add(lp, "target_pin " + quote(la.target.pin_name) + " vs " + quote(lb.target.pin_name));
            if (la.source.node_instance != lb.source.node_instance)
                r.add(lp, "source_node " + quote(la.source.node_instance) + " vs " + quote(lb.source.node_instance));
            if (la.source.pin_name != lb.source.pin_name)
                r.add(lp, "source_pin " + quote(la.source.pin_name) + " vs " + quote(lb.source.pin_name));
        }
    }
}

DiffResult diff_modules(const Module& a, const Module& b) {
    DiffResult r;

    // imports
    if (a.imports.size() != b.imports.size()) {
        r.add("imports", "count " + std::to_string(a.imports.size()) + " vs " + std::to_string(b.imports.size()));
    } else {
        for (size_t i = 0; i < a.imports.size(); ++i) {
            if (a.imports[i].path != b.imports[i].path)
                r.add("imports[" + std::to_string(i) + "]",
                      "path " + quote(a.imports[i].path) + " vs " + quote(b.imports[i].path));
        }
    }

    // lets
    if (a.top_level_lets.size() != b.top_level_lets.size()) {
        r.add("lets", "count " + std::to_string(a.top_level_lets.size())
              + " vs " + std::to_string(b.top_level_lets.size()));
    } else {
        for (size_t i = 0; i < a.top_level_lets.size(); ++i) {
            std::string lp = "lets[" + std::to_string(i) + "]";
            if (a.top_level_lets[i].name != b.top_level_lets[i].name)
                r.add(lp, "name " + quote(a.top_level_lets[i].name) + " vs " + quote(b.top_level_lets[i].name));
            if (a.top_level_lets[i].type_name != b.top_level_lets[i].type_name)
                r.add(lp, "type " + quote(a.top_level_lets[i].type_name) + " vs " + quote(b.top_level_lets[i].type_name));
            if (a.top_level_lets[i].constructor_arg != b.top_level_lets[i].constructor_arg)
                r.add(lp, "ctor " + quote(a.top_level_lets[i].constructor_arg) + " vs " + quote(b.top_level_lets[i].constructor_arg));
        }
    }

    // graphs
    if (a.graphs.size() != b.graphs.size()) {
        r.add("graphs", "count " + std::to_string(a.graphs.size()) + " vs " + std::to_string(b.graphs.size()));
        return r;
    }

    for (size_t gi = 0; gi < a.graphs.size(); ++gi) {
        std::string gp = "graphs[" + std::to_string(gi) + "]";
        auto& ga = a.graphs[gi];
        auto& gb = b.graphs[gi];

        if (ga.name != gb.name)
            r.add(gp, "name " + quote(ga.name) + " vs " + quote(gb.name));
        if (ga.base_type != gb.base_type)
            r.add(gp, "base_type " + ga.base_type.value_or("(none)") + " vs " + gb.base_type.value_or("(none)"));

        diff_annotations(r, gp, ga.annotations, gb.annotations);

        // parameters
        if (ga.parameters.size() != gb.parameters.size()) {
            r.add(gp + ".params", "count " + std::to_string(ga.parameters.size())
                  + " vs " + std::to_string(gb.parameters.size()));
        } else {
            for (size_t i = 0; i < ga.parameters.size(); ++i) {
                std::string pp = gp + ".params[" + std::to_string(i) + "]";
                auto& pa = ga.parameters[i];
                auto& pb = gb.parameters[i];
                if (pa.name != pb.name) r.add(pp, "name " + quote(pa.name) + " vs " + quote(pb.name));
                if (pa.type_name != pb.type_name) r.add(pp, "type " + quote(pa.type_name) + " vs " + quote(pb.type_name));
                if (pa.direction != pb.direction) r.add(pp, "direction mismatch");
                if (pa.default_value != pb.default_value) r.add(pp, "default " + quote(pa.default_value) + " vs " + quote(pb.default_value));
                diff_annotations(r, pp, pa.annotations, pb.annotations);
            }
        }

        // node_instances
        if (ga.node_instances.size() != gb.node_instances.size()) {
            r.add(gp + ".nodes", "count " + std::to_string(ga.node_instances.size())
                  + " vs " + std::to_string(gb.node_instances.size()));
        } else {
            for (size_t i = 0; i < ga.node_instances.size(); ++i) {
                std::string np = gp + ".nodes[" + std::to_string(i) + "]";
                auto& na = ga.node_instances[i];
                auto& nb = gb.node_instances[i];
                if (na.type_name != nb.type_name) r.add(np, "type " + quote(na.type_name) + " vs " + quote(nb.type_name));
                if (na.instance_name != nb.instance_name) r.add(np, "instance " + quote(na.instance_name) + " vs " + quote(nb.instance_name));
                if (na.initializer != nb.initializer) r.add(np, "init " + quote(na.initializer) + " vs " + quote(nb.initializer));
                diff_annotations(r, np, na.annotations, nb.annotations);
            }
        }

        // events
        if (ga.events.size() != gb.events.size()) {
            r.add(gp + ".events", "count " + std::to_string(ga.events.size())
                  + " vs " + std::to_string(gb.events.size()));
        } else {
            for (size_t i = 0; i < ga.events.size(); ++i)
                diff_logic_block(r, gp + ".events[" + std::to_string(i) + "]", ga.events[i], gb.events[i]);
        }

        // functions
        if (ga.functions.size() != gb.functions.size()) {
            r.add(gp + ".functions", "count " + std::to_string(ga.functions.size())
                  + " vs " + std::to_string(gb.functions.size()));
        } else {
            for (size_t i = 0; i < ga.functions.size(); ++i)
                diff_logic_block(r, gp + ".functions[" + std::to_string(i) + "]", ga.functions[i], gb.functions[i]);
        }
    }

    return r;
}

} // namespace debug
} // namespace gs
