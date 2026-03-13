#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "graphscript/edit/handle.h"
#include "graphscript/core/pin.h"
#include "graphscript/core/node.h"
#include "graphscript/core/graph.h"
#include "graphscript/core/result.h"
#include "graphscript/schema/graph_schema.h"
#include "graphscript/schema/validator.h"

namespace gs {

class Environment;

/// Pin descriptor for an edit-time node.
struct EditPin {
    std::string  name;
    PinKind      kind;
    PinDirection direction;
    std::string  type_name;
};

/// Node descriptor in edit mode (type, instance name, pins).
struct EditNode {
    std::string            type_name;
    std::string            instance_name;
    std::string            initializer;
    std::vector<EditPin>   pins;
};

/// Connection between two pins (Exec or Data).
struct EditConnection {
    Handle      from_node;
    std::string from_pin;
    Handle      to_node;
    std::string to_pin;
    PinKind     kind;  // Exec or Data
};

/// Mutable graph representation for editing; uses handles and schema validation.
class EditGraph {
public:
    EditGraph(const std::string& name, const Environment* env, const GraphSchema* schema = nullptr);

    const std::string& name() const { return name_; }
    const GraphSchema* schema() const { return schema_; }

    // Node CRUD
    /// Adds a node. Returns error string if type unknown or instance name duplicate.
    Result<Handle, std::string> add_node(const std::string& type_name, const std::string& instance_name, const std::string& initializer = "");
    /// Removes node and all its connections. Returns false if handle invalid.
    bool remove_node(Handle h);
    EditNode* get_node(Handle h);
    const EditNode* get_node(Handle h) const;

    // Parameter management
    void add_parameter(const GraphParameter& param);
    const std::vector<GraphParameter>& parameters() const { return parameters_; }

    // Connection CRUD
    /// Connects two pins. Returns error if validation fails (schema, fan-out, etc.).
    Result<Handle, std::string> connect(Handle from_node, const std::string& from_pin, Handle to_node, const std::string& to_pin);
    /// Removes connection. Returns false if handle invalid.
    bool disconnect(Handle connection_handle);
    EditConnection* get_connection(Handle h);
    const EditConnection* get_connection(Handle h) const;

    // Query
    std::vector<const EditConnection*> connections_from(Handle node_handle, const std::string& pin_name) const;
    std::vector<const EditConnection*> connections_to(Handle node_handle, const std::string& pin_name) const;
    int exec_fan_out_count(Handle node_handle, const std::string& pin_name) const;
    int exec_fan_in_count(Handle node_handle, const std::string& pin_name) const;

    // Node filtering
    /// Returns node types available for this graph (from env and schema).
    std::vector<const NodeDefinition*> available_node_types() const;

    // Validation
    /// Runs common and schema validation; returns all diagnostics.
    std::vector<Diagnostic> validate() const;

    // Iteration
    size_t node_count() const { return nodes_.size(); }
    size_t connection_count() const { return connections_.size(); }

    template<typename Fn>
    void for_each_node(Fn&& fn) const { nodes_.for_each(std::forward<Fn>(fn)); }

    template<typename Fn>
    void for_each_connection(Fn&& fn) const { connections_.for_each(std::forward<Fn>(fn)); }

    /// Builds an EditGraph from a compiled Graph and environment.
    static EditGraph build(const Graph& graph, const Environment& env);

private:
    const EditPin* find_pin(const EditNode& node, const std::string& pin_name) const;

    std::string         name_;
    const Environment*  env_;
    const GraphSchema*  schema_;
    std::vector<GraphParameter> parameters_;
    SlotMap<EditNode>       nodes_;
    SlotMap<EditConnection> connections_;
};

} // namespace gs
