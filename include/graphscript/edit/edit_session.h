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

/// Snapshot of a graph-level or module-level edit for undo/redo.
struct EditSnapshot {
    enum class Scope { Graph, Module };

    Scope scope = Scope::Graph;
    Graph graph;
    Module module;
    int active_index = -1;
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

    /// Renames a graph and rewrites graph-as-node type references in the module.
    Result<void, std::string> rename_graph(const std::string& old_name, const std::string& new_name);

    /// Switches active graph by name or index.
    Result<void, std::string> set_active(const std::string& name);
    Result<void, std::string> set_active(int index);

    /// Adds an import declaration to the module.
    void add_import(const std::string& path);

    /// Returns true when a declaration file has already been loaded into the session environment.
    bool is_import_loaded(const std::string& path) const;

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

    /// Renames a graph parameter and rewrites bare parameter references in the active graph.
    Result<void, std::string> rename_param(const std::string& old_name, const std::string& new_name);

    /// Changes a graph parameter type and refreshes the graph-derived node definition.
    Result<void, std::string> set_param_type(const std::string& name, const std::string& type_name);

    /// Sets or clears a graph parameter default value.
    Result<void, std::string> set_param_default(const std::string& name, const std::string& default_val = "");

    /// Sets a graph parameter default value to a constructor call expression.
    Result<void, std::string> set_param_default_constructor(const std::string& name,
                                                            const std::string& constructor_type,
                                                            const std::string& constructor_argument = "");

    /// Sets the argument text of an existing constructor-call graph parameter default value.
    Result<void, std::string> set_param_default_constructor_argument(const std::string& name,
                                                                     const std::string& constructor_argument = "");

    /// Sets the type name of an existing constructor-call graph parameter default value.
    Result<void, std::string> set_param_default_constructor_type(const std::string& name,
                                                                 const std::string& constructor_type);

    /// Adds a node instance to the active graph.
    Result<void, std::string> add_node(const std::string& type_name, const std::string& instance_name,
                                       const std::string& initializer = "");

    /// Sets or clears the raw initializer expression on a node instance.
    Result<void, std::string> set_node_initializer(const std::string& instance_name,
                                                   const std::string& initializer = "");

    /// Sets or appends a field assignment in a node instance initializer.
    Result<void, std::string> set_node_initializer_field(const std::string& instance_name,
                                                         const std::string& field_name,
                                                         const std::string& value);

    /// Sets or appends a constructor-call field assignment in a node instance initializer.
    Result<void, std::string> set_node_initializer_constructor_field(const std::string& instance_name,
                                                                     const std::string& field_name,
                                                                     const std::string& constructor_type,
                                                                     const std::string& constructor_argument = "");

    /// Sets the argument text of an existing constructor-call field assignment in a node instance initializer.
    Result<void, std::string> set_node_initializer_constructor_argument(const std::string& instance_name,
                                                                        const std::string& field_name,
                                                                        const std::string& constructor_argument = "");

    /// Sets the type name of an existing constructor-call field assignment in a node instance initializer.
    Result<void, std::string> set_node_initializer_constructor_type(const std::string& instance_name,
                                                                    const std::string& field_name,
                                                                    const std::string& constructor_type);

    /// Removes a field assignment from a node instance initializer.
    Result<void, std::string> remove_node_initializer_field(const std::string& instance_name,
                                                            const std::string& field_name);

    /// Renames a field assignment in a node instance initializer.
    Result<void, std::string> rename_node_initializer_field(const std::string& instance_name,
                                                            const std::string& old_field_name,
                                                            const std::string& new_field_name);

    /// Removes a node instance and all its connections from the active graph.
    Result<void, std::string> remove_node(const std::string& instance_name);

    /// Renames a node instance and rewrites node-addressed references in the active graph.
    Result<void, std::string> rename_node_instance(const std::string& old_name, const std::string& new_name);

    /// Creates a new event block in the active graph.
    Result<void, std::string> add_event(const std::string& name);

    /// Removes an event block by name.
    Result<void, std::string> remove_event(const std::string& name);

    /// Renames an event block in the active graph.
    Result<void, std::string> rename_event(const std::string& old_name, const std::string& new_name);

    /// Creates a new function block in the active graph.
    Result<void, std::string> add_function(const std::string& name);

    /// Removes a function block by name.
    Result<void, std::string> remove_function(const std::string& name);

    /// Renames a function block in the active graph.
    Result<void, std::string> rename_function(const std::string& old_name, const std::string& new_name);

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

    // ─── Annotations (C# Attribute style) ──────────────────────────

    /// Sets or replaces an annotation on a node instance.
    Result<void, std::string> set_node_annotation(const std::string& instance_name, const Annotation& annot);

    /// Removes an annotation by name from a node instance.
    Result<void, std::string> remove_node_annotation(const std::string& instance_name, const std::string& annot_name);

    /// Sets or replaces an annotation on a graph parameter.
    Result<void, std::string> set_param_annotation(const std::string& param_name, const Annotation& annot);

    /// Removes an annotation by name from a graph parameter.
    Result<void, std::string> remove_param_annotation(const std::string& param_name, const std::string& annot_name);

    /// Sets or replaces an annotation on an event or function block.
    Result<void, std::string> set_block_annotation(const std::string& block_kind,
                                                   const std::string& block_name,
                                                   const Annotation& annot);

    /// Removes an annotation by name from an event or function block.
    Result<void, std::string> remove_block_annotation(const std::string& block_kind,
                                                      const std::string& block_name,
                                                      const std::string& annot_name);

    /// Sets or replaces an annotation on a flow connection in an event or function block.
    Result<void, std::string> set_flow_annotation(const std::string& block_kind,
                                                  const std::string& block_name,
                                                  const std::string& from_node,
                                                  const std::string& from_pin,
                                                  const std::string& to_node,
                                                  const std::string& to_pin,
                                                  const Annotation& annot);

    /// Removes an annotation by name from a flow connection in an event or function block.
    Result<void, std::string> remove_flow_annotation(const std::string& block_kind,
                                                     const std::string& block_name,
                                                     const std::string& from_node,
                                                     const std::string& from_pin,
                                                     const std::string& to_node,
                                                     const std::string& to_pin,
                                                     const std::string& annot_name);

    /// Sets or replaces an annotation on a data link in an event or function block.
    Result<void, std::string> set_link_annotation(const std::string& block_kind,
                                                  const std::string& block_name,
                                                  const std::string& target_node,
                                                  const std::string& target_pin,
                                                  const std::string& source_node,
                                                  const std::string& source_pin,
                                                  const Annotation& annot);

    /// Removes an annotation by name from a data link in an event or function block.
    Result<void, std::string> remove_link_annotation(const std::string& block_kind,
                                                     const std::string& block_name,
                                                     const std::string& target_node,
                                                     const std::string& target_pin,
                                                     const std::string& source_node,
                                                     const std::string& source_pin,
                                                     const std::string& annot_name);

    /// Sets or replaces an annotation on the active graph.
    Result<void, std::string> set_graph_annotation(const Annotation& annot);

    /// Removes an annotation by name from the active graph.
    Result<void, std::string> remove_graph_annotation(const std::string& annot_name);

    /// Sets or replaces an annotation on a module import declaration.
    Result<void, std::string> set_import_annotation(const std::string& path, const Annotation& annot);

    /// Removes an annotation by name from a module import declaration.
    Result<void, std::string> remove_import_annotation(const std::string& path, const std::string& annot_name);

    /// Sets or replaces an annotation on a top-level let declaration.
    Result<void, std::string> set_let_annotation(const std::string& name, const Annotation& annot);

    /// Removes an annotation by name from a top-level let declaration.
    Result<void, std::string> remove_let_annotation(const std::string& name, const std::string& annot_name);

    // ─── Generate block ────────────────────────────────────────────

    /// Adds a comment to the generate block (creates block if needed).
    Result<void, std::string> add_comment(const std::string& instance_name, const std::string& text);

    /// Adds metadata to the generate block.
    Result<void, std::string> add_meta(const std::string& scope, const std::string& node,
                                       const std::string& prop, const std::string& value);

    /// Removes a generate comment. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> remove_comment(const std::string& instance_name,
                                             const std::string& text,
                                             size_t occurrence);

    /// Removes a generate metadata item. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> remove_meta(const std::string& scope,
                                          const std::string& node,
                                          const std::string& prop,
                                          const std::string& value,
                                          size_t occurrence);

    /// Moves a generate comment up or down within the comment list. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> move_comment(const std::string& instance_name,
                                           const std::string& text,
                                           size_t occurrence,
                                           bool move_up);

    /// Moves a generate metadata item up or down within the metadata list. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> move_meta(const std::string& scope,
                                        const std::string& node,
                                        const std::string& prop,
                                        const std::string& value,
                                        size_t occurrence,
                                        bool move_up);

    /// Renames a generate comment text while preserving annotations. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> rename_comment(const std::string& instance_name,
                                             const std::string& text,
                                             size_t occurrence,
                                             const std::string& new_text);

    /// Renames a generate metadata value while preserving annotations. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> rename_meta(const std::string& scope,
                                          const std::string& node,
                                          const std::string& prop,
                                          const std::string& value,
                                          size_t occurrence,
                                          const std::string& new_value);

    /// Renames a generate metadata reference while preserving value and annotations. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> rename_meta_ref(const std::string& scope,
                                              const std::string& node,
                                              const std::string& prop,
                                              const std::string& value,
                                              size_t occurrence,
                                              const std::string& new_scope,
                                              const std::string& new_node,
                                              const std::string& new_prop);

    /// Sets or replaces an annotation on a generate comment. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> set_generate_comment_annotation(const std::string& instance_name,
                                                              const std::string& text,
                                                              size_t occurrence,
                                                              const Annotation& annot);

    /// Removes an annotation from a generate comment. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> remove_generate_comment_annotation(const std::string& instance_name,
                                                                 const std::string& text,
                                                                 size_t occurrence,
                                                                 const std::string& annot_name);

    /// Sets or replaces an annotation on a generate metadata item. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> set_generate_metadata_annotation(const std::string& scope,
                                                               const std::string& node,
                                                               const std::string& prop,
                                                               const std::string& value,
                                                               size_t occurrence,
                                                               const Annotation& annot);

    /// Removes an annotation from a generate metadata item. Occurrence is 1-based; 0 requires a unique match.
    Result<void, std::string> remove_generate_metadata_annotation(const std::string& scope,
                                                                  const std::string& node,
                                                                  const std::string& prop,
                                                                  const std::string& value,
                                                                  size_t occurrence,
                                                                  const std::string& annot_name);

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

    /// Validates every graph in the current module, returns diagnostics.
    std::vector<Diagnostic> validate_all() const;

    /// Returns info about all available node types.
    std::vector<const NodeDefinition*> available_types() const;

    /// Returns the environment.
    Environment& env() { return env_; }
    const Environment& env() const { return env_; }

    /// Whether the module has been modified since last save.
    bool dirty() const { return dirty_; }
    void mark_clean() { dirty_ = false; }

    // ─── File I/O ─────────────────────────────────────────────────

    /// Loads GraphScript source text into the session after parse+compile succeeds.
    /// On failure the current session module is left unchanged.
    Result<void, std::string> load_source(const std::string& source, const std::string& source_name = "");

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

    /// Serializes current active-graph diagnostics to a JSON array.
    std::string diagnostics_to_json() const;

    /// Serializes the full session state (module, types, schemas, diagnostics, undo/redo, log) to JSON.
    std::string state_to_json() const;

private:
    /// Pushes current active graph state onto undo stack with description.
    void push_undo(const std::string& description);

    /// Pushes current module state onto undo stack with description.
    void push_module_undo(const std::string& description);

    /// Adds or updates an import declaration, optionally marking it loaded.
    void add_import(const std::string& path, bool loaded);

    /// Marks module import declarations that correspond to loaded declaration files.
    void mark_loaded_imports();

    /// Finds a logic block (event or function) by name in the active graph.
    LogicBlock* find_block(const std::string& name);

    /// Validates one graph without changing the active graph selection.
    std::vector<Diagnostic> validate_graph(const Graph& graph) const;

    Environment& env_;
    Module       module_;
    int          active_ = -1;
    bool         dirty_  = false;
    std::string  file_path_;

    std::vector<EditSnapshot> undo_stack_;
    std::vector<EditSnapshot> redo_stack_;
    std::vector<std::string>   command_log_;
    std::vector<std::string>   loaded_import_keys_;

    static constexpr size_t kMaxUndoDepth = 100;
};

} // namespace gs
