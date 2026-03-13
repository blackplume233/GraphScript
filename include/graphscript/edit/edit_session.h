#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

#include "graphscript/core/result.h"
#include "graphscript/core/graph.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/emit/emitter.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/registry/environment.h"

namespace gs {

/// Snapshot of a single graph state for undo/redo.
struct GraphSnapshot {
    Graph graph;
    std::string description;
};

/// Interactive editing session operating on a Module.
/// Maintains undo/redo history, provides all graph editing operations,
/// and ensures text↔graph isomorphism at all times.
class EditSession {
public:
    explicit EditSession(Environment& env);

    // ─── Module-level ──────────────────────────────────────────────

    /// Creates a new empty graph and sets it as active.
    Result<int, std::string> new_graph(const std::string& name, const std::string& base_type = "");

    /// Deletes a graph by name. Fails if it's the active graph or not found.
    Result<void, std::string> delete_graph(const std::string& name);

    /// Switches active graph by name or index.
    Result<void, std::string> set_active(const std::string& name);
    Result<void, std::string> set_active(int index);

    /// Adds an import declaration to the module.
    void add_import(const std::string& path);

    /// Adds a top-level let declaration.
    void add_let(const std::string& name, const std::string& type_name, const std::string& ctor_arg = "");

    /// Returns the current module (read-only).
    const Module& module() const { return module_; }
    Module& module_mut() { return module_; }

    /// Returns active graph index (-1 if none).
    int active_index() const { return active_; }

    /// Returns active graph or nullptr.
    Graph* active_graph();
    const Graph* active_graph() const;

    // ─── Graph-level (operates on active graph) ────────────────────

    /// Adds a parameter to the active graph.
    Result<void, std::string> add_param(ParamDirection dir, const std::string& name,
                                        const std::string& type_name, const std::string& default_val = "");

    /// Removes a parameter by name from the active graph.
    Result<void, std::string> remove_param(const std::string& name);

    /// Adds a node instance to the active graph.
    Result<void, std::string> add_node(const std::string& type_name, const std::string& instance_name,
                                       const std::string& initializer = "");

    /// Removes a node instance and all its connections from the active graph.
    Result<void, std::string> remove_node(const std::string& instance_name);

    /// Creates a new event block in the active graph.
    Result<void, std::string> add_event(const std::string& name);

    /// Removes an event block by name.
    Result<void, std::string> remove_event(const std::string& name);

    /// Creates a new function block in the active graph.
    Result<void, std::string> add_function(const std::string& name);

    /// Removes a function block by name.
    Result<void, std::string> remove_function(const std::string& name);

    // ─── Connection-level (operates within event/function) ─────────

    /// Adds a flow connection inside a logic block.
    /// @param block_name  Event or function name.
    Result<void, std::string> add_flow(const std::string& block_name,
                                       const std::string& from_node, const std::string& from_pin,
                                       const std::string& to_node, const std::string& to_pin);

    /// Removes a flow connection from a logic block.
    Result<void, std::string> remove_flow(const std::string& block_name,
                                          const std::string& from_node, const std::string& from_pin,
                                          const std::string& to_node, const std::string& to_pin);

    /// Adds a data link inside a logic block.
    /// If source_pin is empty, source_node is treated as a bare parameter reference.
    Result<void, std::string> add_link(const std::string& block_name,
                                       const std::string& target_node, const std::string& target_pin,
                                       const std::string& source_node, const std::string& source_pin = "");

    /// Removes a data link by target node+pin from a logic block.
    Result<void, std::string> remove_link(const std::string& block_name,
                                          const std::string& target_node, const std::string& target_pin);

    // ─── Generate block ────────────────────────────────────────────

    /// Adds a comment to the generate block (creates block if needed).
    Result<void, std::string> add_comment(const std::string& instance_name, const std::string& text);

    /// Adds metadata to the generate block.
    Result<void, std::string> add_meta(const std::string& scope, const std::string& node,
                                       const std::string& prop, const std::string& value);

    // ─── Undo / Redo ──────────────────────────────────────────────

    /// Undoes last operation on the active graph. Returns description of undone op.
    Result<std::string, std::string> undo();

    /// Redoes last undone operation. Returns description of redone op.
    Result<std::string, std::string> redo();

    /// Returns undo stack descriptions (most recent first).
    std::vector<std::string> undo_history() const;

    /// Returns redo stack descriptions (most recent first).
    std::vector<std::string> redo_history() const;

    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }

    // ─── Query / Output ───────────────────────────────────────────

    /// Emits the entire module as .gs text.
    std::string emit() const;

    /// Emits only the active graph as .gs text.
    std::string emit_active() const;

    /// Builds an EditGraph from the active graph for validation/query.
    std::optional<EditGraph> build_edit_graph() const;

    /// Validates the active graph, returns diagnostics.
    std::vector<Diagnostic> validate() const;

    /// Returns info about all available node types.
    std::vector<const NodeDefinition*> available_types() const;

    /// Returns the environment.
    Environment& env() { return env_; }
    const Environment& env() const { return env_; }

    /// Whether the module has been modified since last save.
    bool dirty() const { return dirty_; }
    void mark_clean() { dirty_ = false; }

    // ─── File I/O ─────────────────────────────────────────────────

    /// Loads a .gs file, compiling it into the session.
    Result<void, std::string> load_file(const std::string& path);

    /// Loads a .d.gs declaration file into the environment.
    Result<void, std::string> load_import(const std::string& path);

    /// Saves the module to a .gs file.
    Result<void, std::string> save_file(const std::string& path);

    /// Current file path (set by load/save).
    const std::string& file_path() const { return file_path_; }

    // ─── Command Log ─────────────────────────────────────────────

    /// Records a CLI command string into the session log.
    void log_command(const std::string& cmd);

    /// Returns the full command history.
    const std::vector<std::string>& command_log() const { return command_log_; }

    /// Clears the command log.
    void clear_log() { command_log_.clear(); }

    // ─── JSON State Export ────────────────────────────────────────

    /// Serializes the full session state (module, types, schemas, undo/redo, log) to JSON.
    std::string state_to_json() const;

private:
    /// Pushes current active graph state onto undo stack with description.
    void push_undo(const std::string& description);

    /// Finds a logic block (event or function) by name in the active graph.
    LogicBlock* find_block(const std::string& name);

    Environment& env_;
    Module       module_;
    int          active_ = -1;
    bool         dirty_  = false;
    std::string  file_path_;

    std::vector<GraphSnapshot> undo_stack_;
    std::vector<GraphSnapshot> redo_stack_;
    std::vector<std::string>   command_log_;

    static constexpr size_t kMaxUndoDepth = 100;
};

} // namespace gs
