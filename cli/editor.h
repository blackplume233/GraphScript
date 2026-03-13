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
    void cmd_delete_graph(const std::vector<std::string>& args);
    void cmd_graphs();
    void cmd_import(const std::vector<std::string>& args);
    void cmd_let(const std::vector<std::string>& args);

    void cmd_param(const std::vector<std::string>& args);
    void cmd_params();
    void cmd_add(const std::vector<std::string>& args);
    void cmd_rm(const std::vector<std::string>& args);
    void cmd_nodes();
    void cmd_info(const std::vector<std::string>& args);
    void cmd_pins(const std::vector<std::string>& args);

    void cmd_event(const std::vector<std::string>& args);
    void cmd_fn(const std::vector<std::string>& args);
    void cmd_flow(const std::vector<std::string>& args);
    void cmd_link(const std::vector<std::string>& args);
    void cmd_unflow(const std::vector<std::string>& args);
    void cmd_unlink(const std::vector<std::string>& args);
    void cmd_done();
    void cmd_connections();

    void cmd_comment(const std::vector<std::string>& args);
    void cmd_meta(const std::vector<std::string>& args);

    void cmd_undo();
    void cmd_redo();
    void cmd_history();

    void cmd_emit();
    void cmd_validate();
    void cmd_bake();
    void cmd_types();
    void cmd_schemas();

    void cmd_load(const std::vector<std::string>& args);
    void cmd_save(const std::vector<std::string>& args);

    // Parse "node.pin" format. Returns {node, pin}.
    std::pair<std::string, std::string> parse_pin_ref(const std::string& ref) const;

    void print_error(const std::string& msg);
    void print_ok(const std::string& msg);

    EditSession& session_;
    std::string  current_block_;  // Currently editing event/function name (empty = graph level)
    bool         running_ = true;
};

} // namespace gs
