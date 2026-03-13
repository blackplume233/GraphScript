#pragma once

#include <string>
#include <mutex>
#include "graphscript/edit/edit_session.h"

namespace gs {

class CLIEditor;

/// Embedded HTTP server for the GraphScript web editor.
/// Wraps EditSession behind REST endpoints, serves the web UI,
/// and records every GUI action as a CLI command.
class WebServer {
public:
    WebServer(EditSession& session, CLIEditor& cli, int port = 8080);

    /// Starts the server (blocking). Returns exit code.
    int run();

    /// Sets the directory where web assets (index.html) are located.
    void set_web_dir(const std::string& dir) { web_dir_ = dir; }

private:
    EditSession& session_;
    CLIEditor&   cli_;
    int          port_;
    std::string  web_dir_;
    std::mutex   mutex_;
};

} // namespace gs
