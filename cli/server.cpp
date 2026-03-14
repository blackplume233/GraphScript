#include "server.h"
#include "editor.h"
#include <httplib.h>
#include <iostream>
#include <fstream>
#include <sstream>

namespace gs {

WebServer::WebServer(EditSession& session, CLIEditor& cli, int port)
    : session_(session), cli_(cli), port_(port) {}

// Reads a file into a string. Returns empty on failure.
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Escapes a string for JSON output.
static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:   r += c;
        }
    }
    return r;
}

int WebServer::run() {
    httplib::Server svr;

    // ── Serve web UI (Vite build output from webapp/dist) ─────────
    if (!web_dir_.empty()) {
        svr.set_mount_point("/", web_dir_);
    }

    // Fallback: serve index.html for SPA client-side routing
    svr.Get("/", [this](const httplib::Request&, httplib::Response& res) {
        std::string html = read_file(web_dir_ + "/index.html");
        if (html.empty()) {
            res.set_content("<h1>GraphScript Editor</h1>"
                "<p>webapp/dist/index.html not found at: " + web_dir_ + "</p>",
                "text/html");
        } else {
            res.set_content(html, "text/html");
        }
    });

    // ── GET /api/state  → full JSON state ─────────────────────────
    svr.Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        res.set_content(session_.state_to_json(), "application/json");
    });

    // ── POST /api/exec  → execute CLI command ─────────────────────
    svr.Post("/api/exec", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Parse command from body (plain text or JSON { "command": "..." })
        std::string cmd = req.body;
        if (!cmd.empty() && cmd[0] == '{') {
            auto start = cmd.find("\"command\"");
            if (start != std::string::npos) {
                auto q1 = cmd.find('"', start + 9);
                if (q1 != std::string::npos) {
                    q1++;
                    auto q2 = cmd.find('"', q1);
                    if (q2 != std::string::npos) cmd = cmd.substr(q1, q2 - q1);
                }
            }
        }

        if (cmd.empty()) {
            res.set_content("{\"ok\":false,\"error\":\"Empty command\"}", "application/json");
            return;
        }

        // Capture stdout for the command output
        session_.log_command(cmd);
        std::ostringstream capture;
        auto* old_buf = std::cout.rdbuf(capture.rdbuf());
        cli_.execute(cmd);
        std::cout.rdbuf(old_buf);

        std::string output = capture.str();
        std::string json = "{\"ok\":true,\"command\":\"" + esc(cmd) +
                           "\",\"output\":\"" + esc(output) +
                           "\",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── POST /api/undo ────────────────────────────────────────────
    svr.Post("/api/undo", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.log_command("undo");
        auto r = session_.undo();
        std::string desc = r.is_ok() ? r.value() : r.error();
        std::string json = "{\"ok\":" + std::string(r.is_ok() ? "true" : "false") +
                           ",\"description\":\"" + esc(desc) +
                           "\",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── POST /api/redo ────────────────────────────────────────────
    svr.Post("/api/redo", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.log_command("redo");
        auto r = session_.redo();
        std::string desc = r.is_ok() ? r.value() : r.error();
        std::string json = "{\"ok\":" + std::string(r.is_ok() ? "true" : "false") +
                           ",\"description\":\"" + esc(desc) +
                           "\",\"state\":" + session_.state_to_json() + "}";
        res.set_content(json, "application/json");
    });

    // ── GET /api/emit → .gs text ──────────────────────────────────
    svr.Get("/api/emit", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        res.set_content(session_.emit(), "text/plain");
    });

    // ── CORS headers for local development ────────────────────────
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_content("", "text/plain");
    });

    std::cout << "GraphScript Editor Server running at http://localhost:" << port_ << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    if (!svr.listen("0.0.0.0", port_)) {
        std::cerr << "Failed to start server on port " << port_ << "\n";
        return 1;
    }
    return 0;
}

} // namespace gs
