/// QA Stress Tests: 100+ node graphs with alternating editor/text editing cycles.
/// Validates text-graph isomorphism at scale through repeated emit-reparse-edit loops.

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <chrono>

#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/edit/edit_graph.h"
#include "graphscript/runtime/runtime_graph.h"
#include "graphscript/emit/emitter.h"
#include "graphscript/registry/environment.h"
#include "graphscript/debug/dump.h"

using namespace gs;

// ─── Helpers ───────────────────────────────────────────────────────

static std::string read_file(const std::string& dir, const std::string& filename) {
    std::string path = dir + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string read_fixture(const std::string& filename) {
    return read_file(GS_TEST_FIXTURES_DIR, filename);
}

static std::string read_preset(const std::string& filename) {
    return read_file(GS_PRESETS_DIR, filename);
}

static std::unique_ptr<ModuleNode> do_parse(const std::string& src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (result.is_err()) return nullptr;
    return std::move(result).value();
}

static Module do_compile(const std::string& src, Environment& env, const std::string& path = "") {
    auto ast = do_parse(src);
    if (!ast) return {};
    Compiler compiler(env);
    auto result = compiler.compile(*ast, path);
    if (result.is_err()) return {};
    return std::move(result).value();
}

static void load_core(Environment& env) {
    do_compile(read_preset("ue_core.d.gs"), env, "ue_core.d.gs");
}

static void assert_modules_eq(const Module& a, const Module& b,
                               const char* file, int line) {
    auto diff = debug::diff_modules(a, b);
    if (!diff.equal) {
        std::string summary = std::string(file) + ":" + std::to_string(line) + " — ";
        summary += std::to_string(diff.differences.size()) + " differences. First 5:\n";
        for (size_t i = 0; i < std::min(diff.differences.size(), size_t(5)); ++i)
            summary += "  " + diff.differences[i] + "\n";
        FAIL() << summary;
    }
}
#define ASSERT_MODULES_EQ(a, b) assert_modules_eq(a, b, __FILE__, __LINE__)

/// Emit → reparse → recompile → structural comparison.
static void assert_round_trip(const Module& mod, Environment& fresh_env,
                               const char* file, int line) {
    Emitter emitter;
    std::string emitted = emitter.emit(mod);
    if (emitted.empty()) { ADD_FAILURE_AT(file, line) << "Emitter produced empty output"; return; }

    auto ast2 = do_parse(emitted);
    if (!ast2) { ADD_FAILURE_AT(file, line) << "Re-parse failed"; return; }

    Compiler c2(fresh_env);
    auto r2 = c2.compile(*ast2);
    if (r2.is_err()) { ADD_FAILURE_AT(file, line) << "Re-compile failed: " << r2.error(); return; }

    assert_modules_eq(mod, r2.value(), file, line);
}
#define ASSERT_ROUND_TRIP(mod, env) assert_round_trip(mod, env, __FILE__, __LINE__)

/// Node type names available from ue_core.d.gs (with exec pins).
static const char* kExecNodeTypes[] = {"PrintString", "Delay"};
static const int kExecNodeCount = 2;

/// Generates a unique instance name from a prefix and index.
static std::string name_of(const std::string& prefix, int i) {
    return prefix + std::to_string(i);
}

template <typename Fn>
static long long elapsed_ms(Fn&& fn) {
    auto start = std::chrono::steady_clock::now();
    fn();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 1: Build 120-node graph via EditSession, emit, reparse, verify
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S1_120Nodes_EditorBuild_RoundTrip) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");

    ASSERT_TRUE(session.new_graph("Mega").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "input_msg", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "wait_time", "float").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "done_flag", "bool").is_ok());

    // Add 120 nodes: alternating PrintString and Delay
    for (int i = 0; i < 120; ++i) {
        const char* type = kExecNodeTypes[i % kExecNodeCount];
        ASSERT_TRUE(session.add_node(type, name_of("n", i)).is_ok())
            << "Failed to add node n" << i;
    }

    // Add 4 events, each wiring a chain of 30 nodes
    for (int ev = 0; ev < 4; ++ev) {
        std::string ev_name = "Event" + std::to_string(ev);
        ASSERT_TRUE(session.add_event(ev_name).is_ok());
        int base = ev * 30;
        ASSERT_TRUE(session.add_flow(ev_name, "context", "start",
                                      name_of("n", base), "enter").is_ok());
        for (int i = 0; i < 29; ++i) {
            std::string from = name_of("n", base + i);
            std::string to = name_of("n", base + i + 1);
            std::string from_pin = (i % 2 == 0) ? "exit" : "completed";
            ASSERT_TRUE(session.add_flow(ev_name, from, from_pin, to, "enter").is_ok())
                << "Failed flow " << from << " -> " << to;
        }
        // Data links: wire every 3rd node's input to a parameter
        for (int i = base; i < base + 30; i += 3) {
            std::string node = name_of("n", i);
            if (i % 2 == 0) {
                ASSERT_TRUE(session.add_link(ev_name, node, "message", "input_msg").is_ok());
            } else {
                ASSERT_TRUE(session.add_link(ev_name, node, "duration", "wait_time").is_ok());
            }
        }
    }

    auto& mod = session.module();
    ASSERT_EQ(mod.graphs.size(), 1u);
    ASSERT_EQ(mod.graphs[0].node_instances.size(), 120u);
    ASSERT_EQ(mod.graphs[0].events.size(), 4u);

    // Emit and measure text size
    Emitter emitter;
    std::string text = emitter.emit(mod);
    EXPECT_GT(text.size(), 3000u) << "120-node graph should produce substantial text";

    // Round-trip
    Environment env2;
    load_core(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    // Bake
    auto eg = EditGraph::build(mod.graphs[0], env);
    EXPECT_EQ(eg.node_count(), 120u);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 120u);
    EXPECT_GT(rg.flow_edge_count(), 100u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 2: Build 100-node graph via GS text, then edit via session
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S2_100Nodes_TextBuild_ThenEditorMutate) {
    // Programmatically generate GS source with 100 nodes
    std::ostringstream gs;
    gs << "import \"ue_core.d.gs\";\n";
    gs << "Graph BigText {\n";
    gs << "    in msg : FString;\n";
    gs << "    in dur : float;\n";
    for (int i = 0; i < 100; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        gs << "    " << type << " n" << i << "{};\n";
    }
    // 5 events, each chaining 20 nodes
    for (int ev = 0; ev < 5; ++ev) {
        gs << "    event Ev" << ev << " {\n";
        int base = ev * 20;
        gs << "        context.start(n" << base << ".enter);\n";
        for (int i = 0; i < 19; ++i) {
            std::string from_pin = ((base + i) % 2 == 0) ? "exit" : "completed";
            gs << "        n" << (base + i) << "." << from_pin << "(n" << (base + i + 1) << ".enter);\n";
        }
        for (int i = base; i < base + 20; i += 4) {
            if (i % 2 == 0) gs << "        link n" << i << ".message = msg;\n";
            else            gs << "        link n" << i << ".duration = dur;\n";
        }
        gs << "    }\n";
    }
    gs << "}\n";

    std::string src = gs.str();

    Environment env1;
    load_core(env1);
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].node_instances.size(), 100u);
    ASSERT_EQ(mod_file.graphs[0].events.size(), 5u);

    // Load into EditSession via emit → reload
    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");

    // Rebuild the same module in the session by emitting then re-loading
    Emitter emitter;
    std::string emitted = emitter.emit(mod_file);
    auto ast2 = do_parse(emitted);
    ASSERT_NE(ast2, nullptr);
    Compiler c2(env2);
    auto r2 = c2.compile(*ast2);
    ASSERT_TRUE(r2.is_ok()) << r2.error();
    session.module_mut() = std::move(r2).value();
    session.set_active(0);

    // --- Editor mutations: add 20 more nodes, a new event ---
    for (int i = 100; i < 120; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        ASSERT_TRUE(session.add_node(type, name_of("n", i)).is_ok());
    }
    ASSERT_TRUE(session.add_event("EvExtra").is_ok());
    ASSERT_TRUE(session.add_flow("EvExtra", "context", "start", "n100", "enter").is_ok());
    for (int i = 100; i < 119; ++i) {
        std::string from_pin = (i % 2 == 0) ? "exit" : "completed";
        ASSERT_TRUE(session.add_flow("EvExtra", name_of("n", i), from_pin,
                                      name_of("n", i + 1), "enter").is_ok());
    }

    auto& mod_edited = session.module();
    ASSERT_EQ(mod_edited.graphs[0].node_instances.size(), 120u);
    ASSERT_EQ(mod_edited.graphs[0].events.size(), 6u);

    // Round-trip the mutated state
    Environment env3;
    load_core(env3);
    ASSERT_ROUND_TRIP(mod_edited, env3);

    // Bake
    auto eg = EditGraph::build(mod_edited.graphs[0], env2);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 120u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 3: Alternating editor/text cycles — 5 rounds of mutation
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S3_AlternatingCycles_5Rounds) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("Cyclic").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "dur", "float").is_ok());

    int next_node_id = 0;
    int next_event_id = 0;

    auto add_batch = [&](int count) {
        std::string ev_name = "Ev" + std::to_string(next_event_id++);
        EXPECT_TRUE(session.add_event(ev_name).is_ok());

        int start = next_node_id;
        for (int i = 0; i < count; ++i) {
            const char* type = (next_node_id % 2 == 0) ? "PrintString" : "Delay";
            EXPECT_TRUE(session.add_node(type, name_of("n", next_node_id)).is_ok());
            next_node_id++;
        }

        EXPECT_TRUE(session.add_flow(ev_name, "context", "start",
                                      name_of("n", start), "enter").is_ok());
        for (int i = start; i < next_node_id - 1; ++i) {
            std::string pin = (i % 2 == 0) ? "exit" : "completed";
            EXPECT_TRUE(session.add_flow(ev_name, name_of("n", i), pin,
                                          name_of("n", i + 1), "enter").is_ok());
        }
        for (int i = start; i < next_node_id; i += 5) {
            if (i % 2 == 0)
                EXPECT_TRUE(session.add_link(ev_name, name_of("n", i), "message", "msg").is_ok());
            else
                EXPECT_TRUE(session.add_link(ev_name, name_of("n", i), "duration", "dur").is_ok());
        }
    };

    // Round 1: Editor — add 25 nodes
    add_batch(25);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 25u);

    // Round 1: Text — emit, reparse, reload into session
    {
        Emitter emitter;
        std::string text = emitter.emit(session.module());
        Environment env_r;
        load_core(env_r);
        auto mod_r = do_compile(text, env_r);
        ASSERT_EQ(mod_r.graphs.size(), 1u);
        ASSERT_MODULES_EQ(session.module(), mod_r);
    }

    // Round 2: Editor — add 25 more nodes
    add_batch(25);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 50u);

    // Round 2: Text round-trip
    {
        Environment env_r;
        load_core(env_r);
        ASSERT_ROUND_TRIP(session.module(), env_r);
    }

    // Round 3: Editor — add 30 nodes
    add_batch(30);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 80u);

    // Round 3: Text — emit, modify text (add annotation), reparse
    {
        Emitter emitter;
        std::string text = emitter.emit(session.module());
        // Inject a graph-level annotation by modifying the text
        size_t graph_pos = text.find("Graph Cyclic");
        ASSERT_NE(graph_pos, std::string::npos);
        std::string modified = text.substr(0, graph_pos) +
                               "[Comment(\"round3\", \"After 80 nodes\")]\n" +
                               text.substr(graph_pos);
        Environment env_r;
        load_core(env_r);
        auto mod_r = do_compile(modified, env_r);
        ASSERT_EQ(mod_r.graphs.size(), 1u);
        ASSERT_EQ(mod_r.graphs[0].annotations.size(), 1u);
        ASSERT_EQ(mod_r.graphs[0].annotations[0].name, "Comment");
        ASSERT_EQ(mod_r.graphs[0].node_instances.size(), 80u);

        // Reload into session
        session.module_mut() = mod_r;
        session.set_active(0);
    }

    // Round 4: Editor — add 25 more nodes (total 105)
    add_batch(25);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 105u);
    EXPECT_EQ(session.module().graphs[0].annotations.size(), 1u);

    // Round 4: Text round-trip preserves annotations + 105 nodes
    {
        Environment env_r;
        load_core(env_r);
        ASSERT_ROUND_TRIP(session.module(), env_r);
    }

    // Round 5: Editor — add final 20 nodes (total 125), add annotations on nodes
    add_batch(20);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 125u);

    // Annotate every 10th node with Position
    auto* g = session.active_graph();
    for (size_t i = 0; i < g->node_instances.size(); i += 10) {
        g->node_instances[i].annotations.push_back(
            {"Position", {{"X", std::to_string(i * 10)}, {"Y", std::to_string(i * 5)}}});
    }

    // Round 5: Text — final round-trip
    {
        Environment env_r;
        load_core(env_r);
        ASSERT_ROUND_TRIP(session.module(), env_r);
    }

    // Final: Bake
    auto eg = EditGraph::build(session.module().graphs[0], env);
    EXPECT_EQ(eg.node_count(), 125u);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 125u);

    // Verify annotation survived all cycles
    EXPECT_EQ(session.module().graphs[0].annotations.size(), 1u);
    int annotated_nodes = 0;
    for (auto& ni : session.module().graphs[0].node_instances) {
        if (!ni.annotations.empty()) annotated_nodes++;
    }
    EXPECT_EQ(annotated_nodes, 13); // 0,10,20,...,120 = 13 nodes
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 4: 150 nodes, multi-graph, text build → editor delete → text verify
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S4_150Nodes_MultiGraph_DeleteCycle) {
    // Generate two graphs: Alpha (100 nodes) and Beta (50 nodes)
    std::ostringstream gs;
    gs << "import \"ue_core.d.gs\";\n";

    auto emit_graph = [&](const std::string& gname, int count, int event_size) {
        gs << "Graph " << gname << " {\n";
        gs << "    in msg : FString;\n";
        for (int i = 0; i < count; ++i) {
            const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
            gs << "    " << type << " " << gname[0] << i << "{};\n";
        }
        int events = count / event_size;
        for (int ev = 0; ev < events; ++ev) {
            gs << "    event E" << ev << " {\n";
            int base = ev * event_size;
            gs << "        context.start(" << gname[0] << base << ".enter);\n";
            for (int i = 0; i < event_size - 1; ++i) {
                std::string pin = ((base + i) % 2 == 0) ? "exit" : "completed";
                gs << "        " << gname[0] << (base + i) << "." << pin
                   << "(" << gname[0] << (base + i + 1) << ".enter);\n";
            }
            gs << "    }\n";
        }
        gs << "}\n";
    };

    emit_graph("Alpha", 100, 20);
    emit_graph("Beta", 50, 10);

    std::string src = gs.str();

    Environment env1;
    load_core(env1);
    auto mod = do_compile(src, env1);
    ASSERT_EQ(mod.graphs.size(), 2u);
    ASSERT_EQ(mod.graphs[0].node_instances.size(), 100u);
    ASSERT_EQ(mod.graphs[1].node_instances.size(), 50u);

    // Round-trip the original
    Environment env_rt;
    load_core(env_rt);
    ASSERT_ROUND_TRIP(mod, env_rt);

    // Load into EditSession
    Environment env2;
    load_core(env2);
    EditSession session(env2);
    Emitter emitter;
    std::string emitted = emitter.emit(mod);
    auto ast = do_parse(emitted);
    ASSERT_NE(ast, nullptr);
    Compiler c(env2);
    auto r = c.compile(*ast);
    ASSERT_TRUE(r.is_ok());
    session.module_mut() = std::move(r).value();

    // Switch to Alpha and delete 30 nodes from the end
    ASSERT_TRUE(session.set_active("Alpha").is_ok());
    for (int i = 99; i >= 70; --i) {
        std::string inst = "A" + std::to_string(i);
        ASSERT_TRUE(session.remove_node(inst).is_ok()) << "Failed to remove " << inst;
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 70u);

    // Switch to Beta and delete 20 nodes
    ASSERT_TRUE(session.set_active("Beta").is_ok());
    for (int i = 49; i >= 30; --i) {
        std::string inst = "B" + std::to_string(i);
        ASSERT_TRUE(session.remove_node(inst).is_ok()) << "Failed to remove " << inst;
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 30u);

    // Round-trip after deletion
    Environment env3;
    load_core(env3);
    ASSERT_ROUND_TRIP(session.module(), env3);

    // Add new nodes to Alpha via editor
    ASSERT_TRUE(session.set_active("Alpha").is_ok());
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(session.add_node("PrintString", "new_" + std::to_string(i)).is_ok());
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 80u);

    // Emit to text, verify text parse yields same structure
    {
        std::string text2 = emitter.emit(session.module());
        Environment env4;
        load_core(env4);
        auto mod2 = do_compile(text2, env4);
        ASSERT_MODULES_EQ(session.module(), mod2);
    }

    // Bake both graphs
    for (auto& graph : session.module().graphs) {
        auto eg = EditGraph::build(graph, env2);
        auto rg = RuntimeGraph::bake(eg);
        EXPECT_GT(rg.node_count(), 0u);
    }
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 5: 200-node single event, deep chain, undo 50 ops + redo
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S5_200Nodes_UndoRedo_DeepChain) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("DeepChain").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg", "FString").is_ok());

    // Phase 1: Build 200 nodes
    for (int i = 0; i < 200; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        ASSERT_TRUE(session.add_node(type, name_of("d", i)).is_ok());
    }

    ASSERT_TRUE(session.add_event("Run").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "context", "start", "d0", "enter").is_ok());
    for (int i = 0; i < 199; ++i) {
        std::string pin = (i % 2 == 0) ? "exit" : "completed";
        ASSERT_TRUE(session.add_flow("Run", name_of("d", i), pin,
                                      name_of("d", i + 1), "enter").is_ok());
    }

    Module state_200 = session.module();
    ASSERT_EQ(state_200.graphs[0].node_instances.size(), 200u);
    ASSERT_EQ(state_200.graphs[0].events[0].flow_connections.size(), 200u);

    // Phase 2: Delete the last 50 nodes (undo-able operations)
    for (int i = 199; i >= 150; --i) {
        ASSERT_TRUE(session.remove_node(name_of("d", i)).is_ok());
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 150u);

    // Round-trip at 150 nodes
    {
        Environment env_r;
        load_core(env_r);
        ASSERT_ROUND_TRIP(session.module(), env_r);
    }

    // Phase 3: Undo all 50 deletions
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(session.can_undo());
        auto r = session.undo();
        ASSERT_TRUE(r.is_ok()) << "Undo #" << i << " failed: " << r.error();
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 200u);
    ASSERT_MODULES_EQ(state_200, session.module());

    // Phase 4: Redo all 50 deletions
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(session.can_redo());
        auto r = session.redo();
        ASSERT_TRUE(r.is_ok()) << "Redo #" << i << " failed: " << r.error();
    }
    EXPECT_EQ(session.active_graph()->node_instances.size(), 150u);

    // Final round-trip
    {
        Environment env_r;
        load_core(env_r);
        ASSERT_ROUND_TRIP(session.module(), env_r);
    }

    // Bake the 150-node graph
    auto eg = EditGraph::build(session.module().graphs[0], env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 150u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 6: Text → Editor → Text → Editor cycles with annotation churn
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S6_AnnotationChurn_100Nodes_4Cycles) {
    Environment env;
    load_core(env);

    // Cycle 1: Build 100 nodes via text
    std::ostringstream gs;
    gs << "import \"ue_core.d.gs\";\n";
    gs << "Graph Annotated {\n";
    gs << "    in msg : FString;\n";
    for (int i = 0; i < 100; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        if (i % 10 == 0)
            gs << "    [Position(X = " << i * 10 << ", Y = " << i * 5 << ")]\n";
        gs << "    " << type << " n" << i << "{};\n";
    }
    gs << "    event Run {\n";
    gs << "        context.start(n0.enter);\n";
    for (int i = 0; i < 99; ++i) {
        std::string pin = (i % 2 == 0) ? "exit" : "completed";
        gs << "        n" << i << "." << pin << "(n" << (i + 1) << ".enter);\n";
    }
    gs << "    }\n";
    gs << "}\n";

    auto mod1 = do_compile(gs.str(), env);
    ASSERT_EQ(mod1.graphs.size(), 1u);
    ASSERT_EQ(mod1.graphs[0].node_instances.size(), 100u);

    // Count initial annotations
    int initial_annot = 0;
    for (auto& ni : mod1.graphs[0].node_instances)
        if (!ni.annotations.empty()) initial_annot++;
    EXPECT_EQ(initial_annot, 10); // every 10th node

    // Cycle 2: Load into editor, add annotations to every 5th node
    EditSession session(env);
    session.module_mut() = mod1;
    session.set_active(0);
    auto* g = session.active_graph();
    for (size_t i = 0; i < g->node_instances.size(); i += 5) {
        bool already = false;
        for (auto& a : g->node_instances[i].annotations)
            if (a.name == "Color") { already = true; break; }
        if (!already)
            g->node_instances[i].annotations.push_back(
                {"Color", {{"", "blue"}}});
    }

    // Count annotations now: every 10th has Position, every 5th has Color
    int pos_count = 0, color_count = 0;
    for (auto& ni : g->node_instances) {
        for (auto& a : ni.annotations) {
            if (a.name == "Position") pos_count++;
            if (a.name == "Color") color_count++;
        }
    }
    EXPECT_EQ(pos_count, 10);
    EXPECT_EQ(color_count, 20); // 0,5,10,...,95

    // Cycle 3: Emit to text, reparse, verify annotations preserved
    {
        Environment env2;
        load_core(env2);
        ASSERT_ROUND_TRIP(session.module(), env2);
    }

    // Cycle 4: Remove all Color annotations via editor, add Tooltip to graph
    g = session.active_graph();
    for (auto& ni : g->node_instances) {
        ni.annotations.erase(
            std::remove_if(ni.annotations.begin(), ni.annotations.end(),
                [](const Annotation& a) { return a.name == "Color"; }),
            ni.annotations.end());
    }
    g->annotations.push_back({"Tooltip", {{"", "Annotation churn test"}}});

    // Verify Color gone, Position preserved
    color_count = 0;
    pos_count = 0;
    for (auto& ni : g->node_instances) {
        for (auto& a : ni.annotations) {
            if (a.name == "Color") color_count++;
            if (a.name == "Position") pos_count++;
        }
    }
    EXPECT_EQ(color_count, 0);
    EXPECT_EQ(pos_count, 10);
    EXPECT_EQ(g->annotations.size(), 1u);

    // Final round-trip
    {
        Environment env3;
        load_core(env3);
        ASSERT_ROUND_TRIP(session.module(), env3);
    }

    // Bake
    auto eg = EditGraph::build(session.module().graphs[0], env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 100u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 7: 100 nodes with functions + events mixed, multiple params
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S7_100Nodes_MixedBlockTypes) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("Mixed").is_ok());

    // 10 params (5 in, 3 out, 2 var)
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(session.add_param(ParamDirection::In, "in" + std::to_string(i), "FString").is_ok());
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(session.add_param(ParamDirection::Out, "out" + std::to_string(i), "bool").is_ok());
    for (int i = 0; i < 2; ++i)
        ASSERT_TRUE(session.add_param(ParamDirection::Var, "var" + std::to_string(i), "float").is_ok());

    // 100 nodes
    for (int i = 0; i < 100; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        ASSERT_TRUE(session.add_node(type, name_of("m", i)).is_ok());
    }

    // 5 events, each with 10-node chains
    for (int ev = 0; ev < 5; ++ev) {
        std::string ev_name = "Event" + std::to_string(ev);
        ASSERT_TRUE(session.add_event(ev_name).is_ok());
        int base = ev * 10;
        ASSERT_TRUE(session.add_flow(ev_name, "context", "start",
                                      name_of("m", base), "enter").is_ok());
        for (int i = 0; i < 9; ++i) {
            std::string pin = ((base + i) % 2 == 0) ? "exit" : "completed";
            ASSERT_TRUE(session.add_flow(ev_name, name_of("m", base + i), pin,
                                          name_of("m", base + i + 1), "enter").is_ok());
        }
        // Links using different in-params per event
        for (int i = base; i < base + 10; i += 2) {
            if (i % 2 == 0)
                ASSERT_TRUE(session.add_link(ev_name, name_of("m", i), "message",
                                              "in" + std::to_string(ev % 5)).is_ok());
        }
    }

    // 5 functions using only context + params (scope isolation)
    for (int fn = 0; fn < 5; ++fn) {
        std::string fn_name = "Func" + std::to_string(fn);
        ASSERT_TRUE(session.add_function(fn_name).is_ok());
        ASSERT_TRUE(session.add_flow(fn_name, "context", "start", "context", "done").is_ok());
        ASSERT_TRUE(session.add_link(fn_name, "context", "result",
                                      "out" + std::to_string(fn % 3)).is_ok());
    }

    auto& mod = session.module();
    ASSERT_EQ(mod.graphs[0].node_instances.size(), 100u);
    ASSERT_EQ(mod.graphs[0].events.size(), 5u);
    ASSERT_EQ(mod.graphs[0].functions.size(), 5u);
    ASSERT_EQ(mod.graphs[0].parameters.size(), 10u);

    // Round-trip
    {
        Environment env2;
        load_core(env2);
        ASSERT_ROUND_TRIP(mod, env2);
    }

    // Text → editor → verify cycle
    {
        Emitter emitter;
        std::string text = emitter.emit(mod);
        Environment env3;
        load_core(env3);
        auto mod2 = do_compile(text, env3);
        ASSERT_MODULES_EQ(mod, mod2);
    }

    // Bake
    auto eg = EditGraph::build(mod.graphs[0], env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 100u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 8: Massive text generation, parse time sanity check
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S8_300Nodes_ParsePerformance) {
    std::ostringstream gs;
    gs << "import \"ue_core.d.gs\";\n";
    gs << "[Comment(\"stress\", \"perf-test-large-graph\")]\n";
    gs << "Graph Perf {\n";
    gs << "    in msg : FString;\n";
    gs << "    in dur : float;\n";

    for (int i = 0; i < 300; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        if (i % 20 == 0)
            gs << "    [Position(X = " << i << ", Y = " << i * 2 << ")]\n";
        gs << "    " << type << " n" << i << "{};\n";
    }

    // 10 events, each with 30-node chains
    for (int ev = 0; ev < 10; ++ev) {
        gs << "    event E" << ev << " {\n";
        int base = ev * 30;
        gs << "        context.start(n" << base << ".enter);\n";
        for (int i = 0; i < 29; ++i) {
            std::string pin = ((base + i) % 2 == 0) ? "exit" : "completed";
            gs << "        n" << (base + i) << "." << pin << "(n" << (base + i + 1) << ".enter);\n";
        }
        // Data links
        for (int i = base; i < base + 30; i += 5) {
            if (i % 2 == 0) gs << "        link n" << i << ".message = msg;\n";
            else            gs << "        link n" << i << ".duration = dur;\n";
        }
        gs << "    }\n";
    }
    gs << "}\n";

    std::string src = gs.str();
    EXPECT_GT(src.size(), 10000u); // sanity: large file

    Environment env;
    load_core(env);
    auto mod = do_compile(src, env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    ASSERT_EQ(mod.graphs[0].node_instances.size(), 300u);
    ASSERT_EQ(mod.graphs[0].events.size(), 10u);

    // Verify annotation count
    int annot_count = 0;
    for (auto& ni : mod.graphs[0].node_instances)
        if (!ni.annotations.empty()) annot_count++;
    EXPECT_EQ(annot_count, 15); // 0,20,40,...,280

    EXPECT_EQ(mod.graphs[0].annotations.size(), 1u);

    // Round-trip
    Environment env2;
    load_core(env2);
    ASSERT_ROUND_TRIP(mod, env2);

    // Bake
    auto eg = EditGraph::build(mod.graphs[0], env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_EQ(rg.node_count(), 300u);
    EXPECT_GT(rg.flow_edge_count(), 250u);

    // Dump sizes as sanity check
    auto eg_dump = debug::dump_edit_graph(eg);
    auto rg_dump = debug::dump_runtime_graph(rg);
    EXPECT_GT(eg_dump.size(), 5000u);
    EXPECT_GT(rg_dump.size(), 5000u);
}

// ═══════════════════════════════════════════════════════════════════
// STRESS 9: 1000-node save/load/emit/validate minimum scalability gate
// ═══════════════════════════════════════════════════════════════════

TEST(QAStress, S9_1000Nodes_RoundTripAndBakePerformanceBudget) {
    constexpr int kNodeCount = 1000;
    constexpr int kEventCount = 20;
    constexpr int kNodesPerEvent = kNodeCount / kEventCount;

    std::ostringstream gs;
    gs << "import \"ue_core.d.gs\";\n";
    gs << "[Comment(\"title\", \"Thousand node stress\")]\n";
    gs << "Graph Thousand {\n";
    gs << "    in msg : FString;\n";
    gs << "    in dur : float;\n";

    for (int i = 0; i < kNodeCount; ++i) {
        const char* type = (i % 2 == 0) ? "PrintString" : "Delay";
        if (i % 50 == 0)
            gs << "    [Position(X = " << (i * 12) << ", Y = " << (i / 50) * 180 << ")]\n";
        gs << "    " << type << " n" << i << "{};\n";
    }

    for (int ev = 0; ev < kEventCount; ++ev) {
        gs << "    event E" << ev << " {\n";
        int base = ev * kNodesPerEvent;
        gs << "        context.start(n" << base << ".enter);\n";
        for (int i = 0; i < kNodesPerEvent - 1; ++i) {
            int index = base + i;
            std::string pin = (index % 2 == 0) ? "exit" : "completed";
            gs << "        n" << index << "." << pin << "(n" << (index + 1) << ".enter);\n";
        }
        for (int i = base; i < base + kNodesPerEvent; i += 10) {
            if (i % 2 == 0) gs << "        link n" << i << ".message = msg;\n";
            else            gs << "        link n" << i << ".duration = dur;\n";
        }
        gs << "    }\n";
    }
    gs << "}\n";

    std::string src = gs.str();
    EXPECT_GT(src.size(), 35000u);

    Environment env;
    load_core(env);

    Module mod;
    long long compile_ms = elapsed_ms([&] {
        mod = do_compile(src, env);
    });
    ASSERT_EQ(mod.graphs.size(), 1u);
    ASSERT_EQ(mod.graphs[0].node_instances.size(), static_cast<size_t>(kNodeCount));
    ASSERT_EQ(mod.graphs[0].events.size(), static_cast<size_t>(kEventCount));
    EXPECT_LT(compile_ms, 5000) << "1000-node parse+compile should stay within an interactive sanity budget";

    int annot_count = 0;
    for (auto& ni : mod.graphs[0].node_instances)
        if (!ni.annotations.empty()) annot_count++;
    EXPECT_EQ(annot_count, 20);
    ASSERT_EQ(mod.graphs[0].annotations.size(), 1u);
    EXPECT_EQ(mod.graphs[0].annotations[0].name, "Comment");
    ASSERT_EQ(mod.graphs[0].annotations[0].args.size(), 2u);
    EXPECT_EQ(mod.graphs[0].annotations[0].args[0].value, "title");
    EXPECT_EQ(mod.graphs[0].annotations[0].args[1].value, "Thousand node stress");

    Emitter emitter;
    std::string emitted;
    long long emit_ms = elapsed_ms([&] {
        emitted = emitter.emit(mod);
    });
    EXPECT_GT(emitted.size(), 35000u);
    auto import_pos = emitted.find("import \"ue_core.d.gs\";");
    auto annotation_pos = emitted.find("[Comment(\"title\", \"Thousand node stress\")]");
    auto graph_pos = emitted.find("Graph Thousand");
    ASSERT_NE(import_pos, std::string::npos);
    ASSERT_NE(annotation_pos, std::string::npos);
    ASSERT_NE(graph_pos, std::string::npos);
    EXPECT_LT(import_pos, annotation_pos);
    EXPECT_LT(annotation_pos, graph_pos);
    EXPECT_LT(emit_ms, 3000) << "1000-node emit should stay within an interactive sanity budget";

    Environment env2;
    load_core(env2);
    Module reparsed;
    long long roundtrip_ms = elapsed_ms([&] {
        Lexer lexer(emitted);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto parsed = parser.parse();
        ASSERT_TRUE(parsed.is_ok()) << parsed.error();
        auto ast = std::move(parsed).value();
        Compiler compiler(env2);
        auto result = compiler.compile(*ast);
        ASSERT_TRUE(result.is_ok()) << result.error();
        reparsed = std::move(result).value();
    });
    ASSERT_MODULES_EQ(mod, reparsed);
    EXPECT_LT(roundtrip_ms, 5000) << "1000-node emitted source should reparse/recompile quickly enough for CI";

    EditGraph eg("unused", nullptr, nullptr);
    RuntimeGraph rg;
    long long bake_ms = elapsed_ms([&] {
        eg = EditGraph::build(mod.graphs[0], env);
        rg = RuntimeGraph::bake(eg);
    });
    EXPECT_EQ(eg.node_count(), static_cast<size_t>(kNodeCount));
    EXPECT_EQ(rg.node_count(), static_cast<size_t>(kNodeCount));
    EXPECT_GT(rg.flow_edge_count(), 900u);
    EXPECT_LT(bake_ms, 3000) << "1000-node edit/runtime graph bake should stay within an interactive sanity budget";
}
