#pragma once

#include <string>
#include <vector>
#include "graphscript/edit/edit_session.h"

namespace gs {

/// Interactive CLI graph editor — FlowGraph-style REPL.
/// Supports full graph editing, undo/redo, validation, and round-trip emit.
class CLIEditor {
public:
    explicit CLIEditor(EditSession& session);

    /// Runs the interactive REPL. Returns exit code.
    int run();

    /// Executes a single command line. Returns false to quit.
    bool execute(const std::string& line);

    /// Returns whether the most recently executed command completed without a CLI error.
    bool last_command_succeeded() const { return last_command_ok_; }

private:
    // Tokenizes a command line respecting quoted strings.
    std::vector<std::string> tokenize(const std::string& line) const;

    // Builds the prompt string showing current context.
    std::string prompt() const;

    // ─── Command handlers ─────────────────────────────────────────
    void cmd_help();
    void cmd_status();
    void cmd_new(const std::vector<std::string>& args);
    void cmd_open(const std::vector<std::string>& args);
    void cmd_rename_graph(const std::vector<std::string>& args);
    void cmd_switch_graph(const std::vector<std::string>& args);
    void cmd_delete_graph(const std::vector<std::string>& args);
    void cmd_graphs();
    void cmd_import(const std::vector<std::string>& args);
    void cmd_let(const std::vector<std::string>& args);

    void cmd_param(const std::vector<std::string>& args);
    void cmd_rename_param(const std::vector<std::string>& args);
    void cmd_set_param_type(const std::vector<std::string>& args);
    void cmd_set_param_default(const std::vector<std::string>& args);
    void cmd_set_param_default_ctor(const std::vector<std::string>& args);
    void cmd_set_param_default_ctor_arg(const std::vector<std::string>& args);
    void cmd_set_param_default_ctor_type(const std::vector<std::string>& args);
    void cmd_params();
    void cmd_add(const std::vector<std::string>& args);
    void cmd_set_init_expr(const std::vector<std::string>& args);
    void cmd_set_init(const std::vector<std::string>& args);
    void cmd_set_init_ctor(const std::vector<std::string>& args);
    void cmd_set_init_ctor_arg(const std::vector<std::string>& args);
    void cmd_set_init_ctor_type(const std::vector<std::string>& args);
    void cmd_unset_init(const std::vector<std::string>& args);
    void cmd_rename_init(const std::vector<std::string>& args);
    void cmd_rm(const std::vector<std::string>& args);
    void cmd_rename_node(const std::vector<std::string>& args);
    void cmd_nodes();
    void cmd_info(const std::vector<std::string>& args);
    void cmd_pins(const std::vector<std::string>& args);

    void cmd_event(const std::vector<std::string>& args);
    void cmd_fn(const std::vector<std::string>& args);
    void cmd_rename_event(const std::vector<std::string>& args);
    void cmd_rename_function(const std::vector<std::string>& args);
    void cmd_delete_event(const std::vector<std::string>& args);
    void cmd_delete_function(const std::vector<std::string>& args);
    void cmd_flow(const std::vector<std::string>& args);
    void cmd_link(const std::vector<std::string>& args);
    void cmd_unflow(const std::vector<std::string>& args);
    void cmd_unlink(const std::vector<std::string>& args);
    void cmd_done();
    void cmd_connections();

    void cmd_annotate(const std::vector<std::string>& args);
    void cmd_unannotate(const std::vector<std::string>& args);
    void cmd_comment(const std::vector<std::string>& args);
    void cmd_meta(const std::vector<std::string>& args);
    void cmd_remove_comment(const std::vector<std::string>& args);
    void cmd_remove_meta(const std::vector<std::string>& args);
    void cmd_move_comment(const std::vector<std::string>& args);
    void cmd_move_meta(const std::vector<std::string>& args);
    void cmd_rename_comment(const std::vector<std::string>& args);
    void cmd_rename_meta(const std::vector<std::string>& args);
    void cmd_rename_meta_ref(const std::vector<std::string>& args);

    void cmd_undo();
    void cmd_redo();
    void cmd_history();

    void cmd_emit();
    void cmd_diagram();
    void cmd_validate();
    void cmd_bake();
    void cmd_types();
    void cmd_schemas();

    void cmd_load(const std::vector<std::string>& args);
    void cmd_save(const std::vector<std::string>& args);
    void cmd_apply_source_b64(const std::vector<std::string>& args);
    void cmd_apply_source_patch(const std::vector<std::string>& args);
    void cmd_apply_source_patches_b64(const std::vector<std::string>& args);
    void cmd_apply_source_identifier_rename(const std::vector<std::string>& args);
    void cmd_apply_source_param_rename(const std::vector<std::string>& args);
    void cmd_apply_source_event_rename(const std::vector<std::string>& args);
    void cmd_apply_source_function_rename(const std::vector<std::string>& args);
    void cmd_apply_source_node_rename(const std::vector<std::string>& args);
    void cmd_apply_source_graph_rename(const std::vector<std::string>& args);
    void cmd_apply_source_node_type_rename(const std::vector<std::string>& args);
    void cmd_apply_source_node_pin_rename(const std::vector<std::string>& args);
    void cmd_apply_source_schema_rename(const std::vector<std::string>& args);
    void cmd_apply_source_type_rename(const std::vector<std::string>& args);
    void cmd_apply_import_node_rename(const std::vector<std::string>& args);
    void cmd_apply_import_node_pin_rename(const std::vector<std::string>& args);
    void cmd_apply_import_schema_rename(const std::vector<std::string>& args);
    void cmd_apply_import_schema_field_rename(const std::vector<std::string>& args);
    void cmd_apply_import_type_rename(const std::vector<std::string>& args);
    void cmd_apply_files_graph_rename(const std::vector<std::string>& args);
    void cmd_apply_files_graph_param_rename(const std::vector<std::string>& args);
    void cmd_apply_files_graph_event_rename(const std::vector<std::string>& args);
    void cmd_apply_files_graph_function_rename(const std::vector<std::string>& args);
    void cmd_apply_files_node_type_rename(const std::vector<std::string>& args);
    void cmd_apply_files_node_pin_rename(const std::vector<std::string>& args);
    void cmd_apply_files_schema_rename(const std::vector<std::string>& args);
    void cmd_apply_files_type_rename(const std::vector<std::string>& args);

    // Parse "node.pin" format. Returns {node, pin}.
    std::pair<std::string, std::string> parse_pin_ref(const std::string& ref) const;

    // Parses annotation tokens: <Name> [arg | key=value | key = value]...
    Annotation parse_annotation_args(const std::vector<std::string>& args, size_t start) const;

    void print_error(const std::string& msg);
    void print_ok(const std::string& msg);

    EditSession& session_;
    std::string  current_block_;  // Currently editing event/function name (empty = graph level)
    bool         running_ = true;
    bool         last_command_ok_ = true;
};

} // namespace gs
