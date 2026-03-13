/// QA Loop: cross-validates File-path (parse→compile) vs Editor-path (EditSession API)
/// for 12 complex scenarios. Each scenario dumps AST/Module/EditGraph/RuntimeGraph and
/// asserts structural equivalence at every layer.

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>

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

static std::string read_fixture(const std::string& filename) {
    std::string path = std::string(GS_TEST_FIXTURES_DIR) + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
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
    do_compile(read_fixture("ue_core.d.gs"), env, "ue_core.d.gs");
}

static void load_htn(Environment& env) {
    do_compile(read_fixture("htn_nodes.d.gs"), env, "htn_nodes.d.gs");
}

static void load_task(Environment& env) {
    do_compile(read_fixture("task_nodes.d.gs"), env, "task_nodes.d.gs");
}

/// Asserts two Modules are structurally equivalent; prints dumps on failure.
static void assert_modules_eq(const Module& a, const Module& b,
                               const char* file, int line) {
    auto diff = debug::diff_modules(a, b);
    if (!diff.equal) {
        std::string ctx = std::string(file) + ":" + std::to_string(line) +
                          "\n=== Module A ===\n" + debug::dump_module(a) +
                          "\n=== Module B ===\n" + debug::dump_module(b);
        for (auto& d : diff.differences)
            ADD_FAILURE_AT(file, line) << d << "\n" << ctx;
    }
}
#define ASSERT_MODULES_EQ(a, b) assert_modules_eq(a, b, __FILE__, __LINE__)

/// Full pipeline for a single graph: EditGraph build + validation + RuntimeGraph bake.
/// Returns (edit_graph_dump, runtime_graph_dump) for diagnostic output.
static std::pair<std::string, std::string>
bake_and_dump(const Graph& graph, const Environment& env) {
    auto eg = EditGraph::build(graph, env);
    auto diags = eg.validate();
    for (auto& d : diags)
        ADD_FAILURE() << "Validation: " << d.message;
    auto rg = RuntimeGraph::bake(eg);
    return {debug::dump_edit_graph(eg), debug::dump_runtime_graph(rg)};
}

/// Round-trip: emit Module → reparse → recompile → diff with original.
static void assert_round_trip(const Module& mod, Environment& fresh_env) {
    Emitter emitter;
    std::string emitted = emitter.emit(mod);
    ASSERT_FALSE(emitted.empty()) << "Emitter produced empty output";

    auto ast2 = do_parse(emitted);
    ASSERT_NE(ast2, nullptr) << "Re-parse failed for:\n" << emitted;

    Compiler c2(fresh_env);
    auto r2 = c2.compile(*ast2);
    ASSERT_TRUE(r2.is_ok()) << "Re-compile failed: " << r2.error();

    auto& mod2 = r2.value();
    ASSERT_MODULES_EQ(mod, mod2);
}

// ═══════════════════════════════════════════════════════════════════
// S1: BasicSingleNode — 1 node, 1 event, 1 flow
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S1_BasicSingleNode) {
    // --- File path ---
    Environment env1;
    load_core(env1);
    const char* src = R"(
import "ue_core.d.gs";
Graph Simple {
    in msg : FString;
    PrintString p{};
    event OnStart {
        context.start(p.enter);
        link p.message = msg;
    }
}
)";
    auto ast = do_parse(src);
    ASSERT_NE(ast, nullptr);
    std::string ast_dump = debug::dump_ast(*ast);
    EXPECT_FALSE(ast_dump.empty());

    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    std::string mod_file_dump = debug::dump_module(mod_file);

    // --- Editor path ---
    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("Simple").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "p", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p", "message", "msg").is_ok());

    auto& mod_editor = session.module();
    std::string mod_editor_dump = debug::dump_module(mod_editor);

    // --- Compare ---
    ASSERT_MODULES_EQ(mod_file, mod_editor);

    // --- Round-trip from editor ---
    Environment env3;
    load_core(env3);
    assert_round_trip(mod_editor, env3);

    // --- Bake ---
    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S2: MultiNodeChain — 3 nodes chained sequentially
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S2_MultiNodeChain) {
    Environment env1;
    load_core(env1);
    const char* src = R"(
import "ue_core.d.gs";
Graph Chain {
    PrintString a{};
    Delay d{};
    PrintString b{};
    event OnStart {
        context.start(a.enter);
        a.exit(d.enter);
        d.completed(b.enter);
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].events[0].flow_connections.size(), 3u);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("Chain").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "a").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "d").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "b").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "a", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "a", "exit", "d", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "d", "completed", "b", "enter").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S3: DataLinkWeb — multiple data links + parameter references
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S3_DataLinkWeb) {
    Environment env1;
    load_core(env1);
    const char* src = R"(
import "ue_core.d.gs";
Graph DataWeb {
    in msg1 : FString;
    in msg2 : FString;
    in wait : float;
    PrintString p1{};
    PrintString p2{};
    Delay d{};
    event OnStart {
        context.start(p1.enter);
        p1.exit(d.enter);
        d.completed(p2.enter);
        link p1.message = msg1;
        link p2.message = msg2;
        link d.duration = wait;
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].events[0].data_links.size(), 3u);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("DataWeb").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg1", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg2", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "wait", "float").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p1").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p2").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "d").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "p1", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "p1", "exit", "d", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "d", "completed", "p2", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p1", "message", "msg1").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p2", "message", "msg2").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "d", "duration", "wait").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S4: MultiEventFunction — 2 events + 2 functions
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S4_MultiEventFunction) {
    Environment env1;
    load_core(env1);
    const char* src = R"(
import "ue_core.d.gs";
Graph MultiBlock {
    in name : FString;
    in hp : int;
    out result : bool;
    PrintString logger{};
    Delay timer{};
    event OnStart {
        context.start(logger.enter);
        link logger.message = name;
    }
    event OnDamage {
        context.start(timer.enter);
        link timer.duration = hp;
    }
    function CalcResult {
        context.start(context.done);
        link context.result = result;
    }
    function LogStatus {
        context.start(context.done);
        link context.result = name;
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].events.size(), 2u);
    ASSERT_EQ(mod_file.graphs[0].functions.size(), 2u);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("MultiBlock").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "name", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "hp", "int").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "result", "bool").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "logger").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "timer").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "logger", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "logger", "message", "name").is_ok());
    ASSERT_TRUE(session.add_event("OnDamage").is_ok());
    ASSERT_TRUE(session.add_flow("OnDamage", "context", "start", "timer", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnDamage", "timer", "duration", "hp").is_ok());
    ASSERT_TRUE(session.add_function("CalcResult").is_ok());
    ASSERT_TRUE(session.add_flow("CalcResult", "context", "start", "context", "done").is_ok());
    ASSERT_TRUE(session.add_link("CalcResult", "context", "result", "result").is_ok());
    ASSERT_TRUE(session.add_function("LogStatus").is_ok());
    ASSERT_TRUE(session.add_flow("LogStatus", "context", "start", "context", "done").is_ok());
    ASSERT_TRUE(session.add_link("LogStatus", "context", "result", "name").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S5: AnnotatedGraph — Graph/Node/Param annotations
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S5_AnnotatedGraph) {
    Environment env1;
    load_core(env1);
    const char* src = R"(
import "ue_core.d.gs";
[Comment("title", "Annotated test graph")]
Graph Annotated {
    [Tooltip("Player name")]
    in name : FString;
    out alive : bool;
    [Position(X = 100, Y = 200)]
    PrintString logger{};
    [Position(X = 300, Y = 100)]
    Delay timer{};
    event OnStart {
        context.start(logger.enter);
        link logger.message = name;
    }
}
)";
    auto ast = do_parse(src);
    ASSERT_NE(ast, nullptr);
    auto ast_dump = debug::dump_ast(*ast);

    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].annotations.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].annotations[0].name, "Comment");
    ASSERT_EQ(mod_file.graphs[0].node_instances[0].annotations.size(), 1u);
    ASSERT_EQ(mod_file.graphs[0].parameters[0].annotations.size(), 1u);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("Annotated").is_ok());

    auto* g = session.active_graph();
    ASSERT_NE(g, nullptr);
    g->annotations.push_back({"Comment", {{"", "title"}, {"", "Annotated test graph"}}});

    ASSERT_TRUE(session.add_param(ParamDirection::In, "name", "FString").is_ok());
    g->parameters[0].annotations.push_back({"Tooltip", {{"", "Player name"}}});

    ASSERT_TRUE(session.add_param(ParamDirection::Out, "alive", "bool").is_ok());

    ASSERT_TRUE(session.add_node("PrintString", "logger").is_ok());
    g->node_instances[0].annotations.push_back({"Position", {{"X", "100"}, {"Y", "200"}}});

    ASSERT_TRUE(session.add_node("Delay", "timer").is_ok());
    g->node_instances[1].annotations.push_back({"Position", {{"X", "300"}, {"Y", "100"}}});

    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "logger", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "logger", "message", "name").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S6: InheritedGraph — base_type + graph-as-node
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S6_InheritedGraph) {
    Environment env1;
    load_core(env1);

    const char* src = R"(
import "ue_core.d.gs";
Graph Base {
    in seed : int;
    out value : int;
    PrintString debug{};
    event Compute {
        context.start(debug.enter);
        link debug.message = seed;
    }
}
Graph Derived {
    in input : int;
    Base worker{};
    event Run {
        context.start(worker.Compute);
        link worker.seed = input;
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 2u);
    ASSERT_EQ(mod_file.graphs[1].node_instances[0].type_name, "Base");

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");

    ASSERT_TRUE(session.new_graph("Base").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "seed", "int").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "value", "int").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "debug").is_ok());
    ASSERT_TRUE(session.add_event("Compute").is_ok());
    ASSERT_TRUE(session.add_flow("Compute", "context", "start", "debug", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Compute", "debug", "message", "seed").is_ok());

    // Manually derive graph-as-node definition (compiler does this automatically)
    {
        NodeDefinition base_def;
        base_def.type_name = "Base";
        base_def.is_native = false;
        base_def.source_graph = "Base";
        base_def.pins.push_back({"seed", PinKind::Data, PinDirection::Input, "int"});
        base_def.pins.push_back({"value", PinKind::Data, PinDirection::Output, "int"});
        base_def.pins.push_back({"Compute", PinKind::Exec, PinDirection::Input, ""});
        env2.nodes().register_graph_node(std::move(base_def));
    }

    ASSERT_TRUE(session.new_graph("Derived").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "input", "int").is_ok());
    ASSERT_TRUE(session.add_node("Base", "worker").is_ok());
    ASSERT_TRUE(session.add_event("Run").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "context", "start", "worker", "Compute").is_ok());
    ASSERT_TRUE(session.add_link("Run", "worker", "seed", "input").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    for (auto& g : mod_file.graphs) {
        auto [eg_dump, rg_dump] = bake_and_dump(g, env1);
        EXPECT_FALSE(eg_dump.empty());
        EXPECT_FALSE(rg_dump.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════
// S7: MultiGraphModule — multi-graph + import + let
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S7_MultiGraphModule) {
    Environment env1;
    load_core(env1);

    const char* src = R"(
import "ue_core.d.gs";
let constant_path = SoftObjectPath("/Game/Maps/Test");
Graph Alpha {
    in x : int;
    PrintString p{};
    event OnStart {
        context.start(p.enter);
        link p.message = x;
    }
}
Graph Beta {
    in y : FString;
    Delay d{};
    event OnRun {
        context.start(d.enter);
        link d.duration = y;
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 2u);
    ASSERT_EQ(mod_file.top_level_lets.size(), 1u);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    session.add_let("constant_path", "SoftObjectPath", "/Game/Maps/Test");

    ASSERT_TRUE(session.new_graph("Alpha").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "x", "int").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "p", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p", "message", "x").is_ok());

    ASSERT_TRUE(session.new_graph("Beta").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "y", "FString").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "d").is_ok());
    ASSERT_TRUE(session.add_event("OnRun").is_ok());
    ASSERT_TRUE(session.add_flow("OnRun", "context", "start", "d", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnRun", "d", "duration", "y").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    for (auto& g : mod_file.graphs) {
        auto [eg_dump, rg_dump] = bake_and_dump(g, env1);
        EXPECT_FALSE(eg_dump.empty());
        EXPECT_FALSE(rg_dump.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════
// S8: ParameterVariants — in/out/var + defaults
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S8_ParameterVariants) {
    Environment env1;
    load_core(env1);

    const char* src = R"(
import "ue_core.d.gs";
Graph ParamTest {
    in speed : float = 1.0;
    in health : int = 100;
    in name : FString;
    out result : bool;
    var temp : float;
    PrintString p{};
    event OnStart {
        context.start(p.enter);
        link p.message = name;
    }
}
)";
    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs[0].parameters.size(), 5u);
    ASSERT_EQ(mod_file.graphs[0].parameters[0].default_value, "1.0");
    ASSERT_EQ(mod_file.graphs[0].parameters[1].default_value, "100");
    ASSERT_EQ(mod_file.graphs[0].parameters[3].direction, ParamDirection::Out);
    ASSERT_EQ(mod_file.graphs[0].parameters[4].direction, ParamDirection::Var);

    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("ParamTest").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "speed", "float", "1.0").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "health", "int", "100").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "name", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "result", "bool").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Var, "temp", "float").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "p", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p", "message", "name").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    auto [eg_dump, rg_dump] = bake_and_dump(mod_file.graphs[0], env1);
    EXPECT_FALSE(eg_dump.empty());
    EXPECT_FALSE(rg_dump.empty());
}

// ═══════════════════════════════════════════════════════════════════
// S9: HeavyMutation — build graph then heavily mutate via editor
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S9_HeavyMutation) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");

    ASSERT_TRUE(session.new_graph("Mutable").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "x", "int").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "a").is_ok());
    ASSERT_TRUE(session.add_node("Delay", "d").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "b").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "a", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "a", "exit", "d", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "d", "completed", "b", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "a", "message", "x").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "b", "message", "x").is_ok());

    Module snapshot_before = session.module();

    // Mutate: remove node 'b' and its connections, add new node 'c'
    ASSERT_TRUE(session.remove_node("b").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "c").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "d", "completed", "c", "enter").is_ok());

    // Remove old links referencing param 'x' before removing the param
    ASSERT_TRUE(session.remove_link("OnStart", "a", "message").is_ok());

    // Remove a param, add a new one
    ASSERT_TRUE(session.remove_param("x").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "y", "FString").is_ok());

    // Re-add links using the new param
    ASSERT_TRUE(session.add_link("OnStart", "a", "message", "y").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "c", "message", "y").is_ok());

    // Add a function
    ASSERT_TRUE(session.add_function("Helper").is_ok());
    ASSERT_TRUE(session.add_flow("Helper", "context", "start", "context", "done").is_ok());

    // Emit and round-trip the mutated state
    Environment env2;
    load_core(env2);
    assert_round_trip(session.module(), env2);

    // Verify mutation actually changed things
    auto diff = debug::diff_modules(snapshot_before, session.module());
    EXPECT_FALSE(diff.equal) << "Mutation should produce differences";

    // Bake the mutated graph
    auto& g = session.module().graphs[0];
    auto eg = EditGraph::build(g, env);
    auto rg = RuntimeGraph::bake(eg);
    EXPECT_GT(rg.node_count(), 0u);
}

// ═══════════════════════════════════════════════════════════════════
// S10: UndoRedoCycle — build, mutate, undo all, redo all
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S10_UndoRedoCycle) {
    Environment env;
    load_core(env);
    EditSession session(env);
    session.add_import("ue_core.d.gs");
    ASSERT_TRUE(session.new_graph("UndoTest").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "x", "int").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p").is_ok());
    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "p", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "p", "message", "x").is_ok());

    Module initial_state = session.module();

    // Apply 3 mutations
    ASSERT_TRUE(session.add_node("Delay", "d").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "p", "exit", "d", "enter").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "y", "bool").is_ok());

    Module mutated_state = session.module();
    auto diff_mut = debug::diff_modules(initial_state, mutated_state);
    EXPECT_FALSE(diff_mut.equal) << "Mutations should cause differences";

    // Undo exactly the 3 mutation operations back to initial state
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(session.can_undo());
        auto r = session.undo();
        ASSERT_TRUE(r.is_ok()) << "Undo failed: " << r.error();
    }

    ASSERT_MODULES_EQ(initial_state, session.module());

    // Redo all 3 operations back to mutated state
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(session.can_redo());
        auto r = session.redo();
        ASSERT_TRUE(r.is_ok()) << "Redo failed: " << r.error();
    }

    ASSERT_MODULES_EQ(mutated_state, session.module());

    // Round-trip the final state
    Environment env2;
    load_core(env2);
    assert_round_trip(session.module(), env2);
}

// ═══════════════════════════════════════════════════════════════════
// S11: SchemaEnforcement — EditGraph with schema validation
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S11_SchemaEnforcement) {
    Environment env;
    load_core(env);
    load_task(env);

    const char* src = R"(
import "ue_core.d.gs";
import "task_nodes.d.gs";
Graph MyTask : TaskGraph {
    in label : FString;
    TaskStart starter{};
    ShowDialogue dlg{};
    TaskComplete finish{};
    event OnStart {
        context.start(starter.begin);
        starter.begin(dlg.enter);
        dlg.exit(finish.finish);
        link dlg.text = label;
    }
}
)";
    auto mod_file = do_compile(src, env);
    ASSERT_EQ(mod_file.graphs.size(), 1u);
    ASSERT_TRUE(mod_file.graphs[0].base_type.has_value());
    ASSERT_EQ(mod_file.graphs[0].base_type.value(), "TaskGraph");

    auto eg = EditGraph::build(mod_file.graphs[0], env);
    auto diags = eg.validate();
    auto eg_dump = debug::dump_edit_graph(eg);
    EXPECT_FALSE(eg_dump.empty());

    auto rg = RuntimeGraph::bake(eg);
    auto rg_dump = debug::dump_runtime_graph(rg);
    EXPECT_FALSE(rg_dump.empty());
    EXPECT_GT(rg.node_count(), 0u);

    // Emit and round-trip
    Environment env2;
    load_core(env2);
    load_task(env2);
    assert_round_trip(mod_file, env2);
}

// ═══════════════════════════════════════════════════════════════════
// S12: KitchenSink — all features combined
// ═══════════════════════════════════════════════════════════════════

TEST(QALoop, S12_KitchenSink) {
    Environment env1;
    load_core(env1);

    const char* src = R"(
import "ue_core.d.gs";
let path = SoftObjectPath("/Game/Maps/KitchenSink");
let origin = FVector("0,0,0");
[Comment("title", "Full feature test"), Comment("author", "QA Bot")]
Graph Kitchen {
    in health : int;
    in name : FString;
    out alive : bool;
    out score : int;
    var temp : float;
    [Position(X = 10, Y = 20)]
    PrintString logger1{};
    [Position(X = 30, Y = 20)]
    PrintString logger2{};
    [Position(X = 20, Y = 10)]
    Delay timer{};
    GetActorLocation locator{};
    event OnStart {
        context.start(logger1.enter);
        logger1.exit(timer.enter);
        timer.completed(logger2.enter);
        link logger1.message = name;
        link timer.duration = temp;
        link logger2.message = health;
    }
    event OnDamage {
        context.start(logger2.enter);
        link logger2.message = health;
    }
    function CalcScore {
        context.start(context.done);
        link context.result = score;
    }
    function LogLocation {
        context.start(context.done);
        link context.result = name;
    }
}
Graph Helper {
    in msg : FString;
    PrintString p{};
    event Run {
        context.start(p.enter);
        link p.message = msg;
    }
}
)";
    auto ast = do_parse(src);
    ASSERT_NE(ast, nullptr);
    auto ast_dump = debug::dump_ast(*ast);
    EXPECT_FALSE(ast_dump.empty());

    auto mod_file = do_compile(src, env1);
    ASSERT_EQ(mod_file.graphs.size(), 2u);
    ASSERT_EQ(mod_file.top_level_lets.size(), 2u);
    ASSERT_EQ(mod_file.graphs[0].annotations.size(), 2u);
    ASSERT_EQ(mod_file.graphs[0].events.size(), 2u);
    ASSERT_EQ(mod_file.graphs[0].functions.size(), 2u);
    ASSERT_EQ(mod_file.graphs[0].node_instances.size(), 4u);

    // --- Editor path ---
    Environment env2;
    load_core(env2);
    EditSession session(env2);
    session.add_import("ue_core.d.gs");
    session.add_let("path", "SoftObjectPath", "/Game/Maps/KitchenSink");
    session.add_let("origin", "FVector", "0,0,0");

    ASSERT_TRUE(session.new_graph("Kitchen").is_ok());
    auto* g = session.active_graph();
    g->annotations.push_back({"Comment", {{"", "title"}, {"", "Full feature test"}}});
    g->annotations.push_back({"Comment", {{"", "author"}, {"", "QA Bot"}}});

    ASSERT_TRUE(session.add_param(ParamDirection::In, "health", "int").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "name", "FString").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "alive", "bool").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Out, "score", "int").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::Var, "temp", "float").is_ok());

    ASSERT_TRUE(session.add_node("PrintString", "logger1").is_ok());
    g->node_instances[0].annotations.push_back({"Position", {{"X", "10"}, {"Y", "20"}}});
    ASSERT_TRUE(session.add_node("PrintString", "logger2").is_ok());
    g->node_instances[1].annotations.push_back({"Position", {{"X", "30"}, {"Y", "20"}}});
    ASSERT_TRUE(session.add_node("Delay", "timer").is_ok());
    g->node_instances[2].annotations.push_back({"Position", {{"X", "20"}, {"Y", "10"}}});
    ASSERT_TRUE(session.add_node("GetActorLocation", "locator").is_ok());

    ASSERT_TRUE(session.add_event("OnStart").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "context", "start", "logger1", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "logger1", "exit", "timer", "enter").is_ok());
    ASSERT_TRUE(session.add_flow("OnStart", "timer", "completed", "logger2", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "logger1", "message", "name").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "timer", "duration", "temp").is_ok());
    ASSERT_TRUE(session.add_link("OnStart", "logger2", "message", "health").is_ok());

    ASSERT_TRUE(session.add_event("OnDamage").is_ok());
    ASSERT_TRUE(session.add_flow("OnDamage", "context", "start", "logger2", "enter").is_ok());
    ASSERT_TRUE(session.add_link("OnDamage", "logger2", "message", "health").is_ok());

    ASSERT_TRUE(session.add_function("CalcScore").is_ok());
    ASSERT_TRUE(session.add_flow("CalcScore", "context", "start", "context", "done").is_ok());
    ASSERT_TRUE(session.add_link("CalcScore", "context", "result", "score").is_ok());

    ASSERT_TRUE(session.add_function("LogLocation").is_ok());
    ASSERT_TRUE(session.add_flow("LogLocation", "context", "start", "context", "done").is_ok());
    ASSERT_TRUE(session.add_link("LogLocation", "context", "result", "name").is_ok());

    // Second graph
    ASSERT_TRUE(session.new_graph("Helper").is_ok());
    ASSERT_TRUE(session.add_param(ParamDirection::In, "msg", "FString").is_ok());
    ASSERT_TRUE(session.add_node("PrintString", "p").is_ok());
    ASSERT_TRUE(session.add_event("Run").is_ok());
    ASSERT_TRUE(session.add_flow("Run", "context", "start", "p", "enter").is_ok());
    ASSERT_TRUE(session.add_link("Run", "p", "message", "msg").is_ok());

    ASSERT_MODULES_EQ(mod_file, session.module());

    // Round-trip
    Environment env3;
    load_core(env3);
    assert_round_trip(session.module(), env3);

    // Bake all graphs
    for (auto& graph : mod_file.graphs) {
        auto [eg_dump, rg_dump] = bake_and_dump(graph, env1);
        EXPECT_FALSE(eg_dump.empty());
        EXPECT_FALSE(rg_dump.empty());
    }
}
