#pragma once

#include <string>
#include <vector>

namespace gs {

struct Module;
class  EditGraph;
class  GraphRuntimeIR;

namespace debug {

/// Dumps compiled Module (IR) to human-readable hierarchical text.
std::string dump_module(const Module& mod);

/// Dumps EditGraph (mutable editor representation) to text.
std::string dump_edit_graph(const EditGraph& eg);

/// Dumps graph runtime IR (baked flat arrays) to text.
std::string dump_graph_runtime_ir(const GraphRuntimeIR& ir);

/// Result of a structural diff between two Modules.
struct DiffResult {
    bool equal = true;
    std::vector<std::string> differences;

    void add(const std::string& path, const std::string& detail) {
        equal = false;
        differences.push_back(path + ": " + detail);
    }
};

/// Deep structural comparison of two compiled Modules.
/// Ignores file_path; compares imports, lets, graphs and all nested fields.
DiffResult diff_modules(const Module& a, const Module& b);

} // namespace debug
} // namespace gs
