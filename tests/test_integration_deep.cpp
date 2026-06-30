// Deep cycle integration tests: 11 complex scenarios exercising the full pipeline
// Parse → Compile → Graph-as-Node → EditGraph → Schema enforcement → Bake → Emit → Re-parse

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

// Helper to lex+parse source into AST.
static std::unique_ptr<ModuleNode> do_parse(std::string_view src) {
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

static void load_preset(Environment& env, const std::string& filename) {
    EditSession session(env);
    const std::string path = std::string(GS_PRESETS_DIR) + "/" + filename;
    auto loaded = session.load_import(path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
}

static void load_core(Environment& env) {
    load_preset(env, "ue_core.d.gs");
}

static void load_htn(Environment& env) {
    load_preset(env, "htn_nodes.d.gs");
}

static void load_task(Environment& env) {
    load_preset(env, "task_nodes.d.gs");
}

static void load_level(Environment& env) {
    load_preset(env, "levelscript_nodes.d.gs");
}

static void load_mixed(Environment& env) {
    do_compile(read_fixture("mixed_declarations.d.gs"), env, "mixed_declarations.d.gs");
}

// Verifies emit → re-parse → re-compile structural equivalence.
static void assert_round_trip(const Module& mod, Environment& fresh_env) {
    Emitter emitter;
    std::string emitted = emitter.emit(mod);
    ASSERT_FALSE(emitted.empty()) << "Emitter produced empty output";

    auto ast2 = do_parse(emitted);
    ASSERT_NE(ast2, nullptr) << "Re-parse of emitted text failed";

    Compiler compiler2(fresh_env);
    auto result2 = compiler2.compile(*ast2);
    ASSERT_TRUE(result2.is_ok()) << "Re-compile failed: " << result2.error();

    auto& mod2 = result2.value();
    ASSERT_EQ(mod.graphs.size(), mod2.graphs.size()) << "Graph count mismatch after round-trip";

    for (size_t i = 0; i < mod.graphs.size(); i++) {
        EXPECT_EQ(mod.graphs[i].name, mod2.graphs[i].name)
            << "Graph name mismatch at index " << i;
        EXPECT_EQ(mod.graphs[i].parameters.size(), mod2.graphs[i].parameters.size())
            << "Param count mismatch for " << mod.graphs[i].name;
        EXPECT_EQ(mod.graphs[i].node_instances.size(), mod2.graphs[i].node_instances.size())
            << "Node count mismatch for " << mod.graphs[i].name;
        EXPECT_EQ(mod.graphs[i].events.size(), mod2.graphs[i].events.size())
            << "Event count mismatch for " << mod.graphs[i].name;
        EXPECT_EQ(mod.graphs[i].functions.size(), mod2.graphs[i].functions.size())
            << "Function count mismatch for " << mod.graphs[i].name;
        EXPECT_EQ(mod.graphs[i].base_type.has_value(), mod2.graphs[i].base_type.has_value())
            << "Base type presence mismatch for " << mod.graphs[i].name;
        if (mod.graphs[i].base_type && mod2.graphs[i].base_type) {
            EXPECT_EQ(*mod.graphs[i].base_type, *mod2.graphs[i].base_type);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 1: 3-Level Graph-as-Node Chain (Leaf → Middle → Root)
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S1_ThreeLevelChain_Parse) {
    auto src = read_fixture("deep_chain.gs");
    ASSERT_FALSE(src.empty());
    auto ast = do_parse(src);
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->graphs.size(), 3u);
}

TEST(DeepCycle, S1_ThreeLevelChain_GraphAsNodeDerivation) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("deep_chain.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 3u);

    // Leaf should be registered as node
    auto* leaf = env.nodes().find("Leaf");
    ASSERT_NE(leaf, nullptr);
    EXPECT_FALSE(leaf->is_native);
    EXPECT_EQ(leaf->source_graph, "Leaf");
    // Leaf: in seed(data in), out value(data out), event Compute(exec in)
    EXPECT_EQ(leaf->data_inputs().size(), 1u);
    EXPECT_EQ(leaf->data_outputs().size(), 1u);
    EXPECT_EQ(leaf->exec_inputs().size(), 1u);

    // Middle uses Leaf as node, so Middle should also be registered
    auto* middle = env.nodes().find("Middle");
    ASSERT_NE(middle, nullptr);
    // Middle: in input(data in), out output(data out), event Process(exec in)
    EXPECT_EQ(middle->data_inputs().size(), 1u);
    EXPECT_EQ(middle->data_outputs().size(), 1u);
    EXPECT_EQ(middle->exec_inputs().size(), 1u);

    // Root uses Middle
    auto* root = env.nodes().find("Root");
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->data_inputs().size(), 1u);  // start_value
}

TEST(DeepCycle, S1_ThreeLevelChain_EditBakeRoundTrip) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("deep_chain.gs"), env);

    // Build each graph → EditGraph → Bake → RuntimeGraph
    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        EXPECT_EQ(eg.name(), g.name) << "EditGraph name mismatch for " << g.name;
        auto diags = eg.validate();
        EXPECT_TRUE(diags.empty()) << g.name << " has validation issues";

        auto rt = RuntimeGraph::bake(eg);
        EXPECT_EQ(rt.name(), g.name);
        EXPECT_GE(rt.node_count(), 0u);
    }

    // Round-trip
    Environment env2;
    load_core(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 2: Complex HTN with Branching Fan-out
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S2_HTNComplex_FullPipeline) {
    Environment env;
    load_core(env);
    load_htn(env);
    auto mod = do_compile(read_fixture("htn_complex.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "PatrolAndEngage");
    ASSERT_TRUE(g.base_type.has_value());
    EXPECT_EQ(*g.base_type, "HTNGraph");
    EXPECT_EQ(g.parameters.size(), 5u);
    EXPECT_EQ(g.node_instances.size(), 5u);
    EXPECT_EQ(g.events.size(), 1u);

    // Verify connections
    auto& ev = g.events[0];
    EXPECT_EQ(ev.name, "OnPlan");
    EXPECT_GE(ev.flow_connections.size(), 5u);
    EXPECT_GE(ev.data_links.size(), 8u);
}

TEST(DeepCycle, S2_HTNComplex_SchemaEnforcement) {
    Environment env;
    load_core(env);
    load_htn(env);

    auto* schema = env.schemas().find("HTNGraph");
    ASSERT_NE(schema, nullptr);

    // Manually test: unlimited fan-out should be allowed
    EditGraph eg("Test", &env, schema);
    auto h1 = eg.add_node("HTN_MoveToTarget", "m1");
    auto h2 = eg.add_node("PrintString", "p1");
    auto h3 = eg.add_node("PrintString", "p2");
    ASSERT_TRUE(h1.is_ok());
    ASSERT_TRUE(h2.is_ok());
    ASSERT_TRUE(h3.is_ok());

    // Unlimited fan-out: connect success to two targets
    auto r1 = eg.connect(h1.value(), "success", h2.value(), "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();
    auto r2 = eg.connect(h1.value(), "success", h3.value(), "enter");
    ASSERT_TRUE(r2.is_ok()) << r2.error();

    // No fan-in: second connection to same exec input should fail
    auto h4 = eg.add_node("HTN_MoveToTarget", "m2");
    ASSERT_TRUE(h4.is_ok());
    auto r3 = eg.connect(h4.value(), "success", h2.value(), "enter");
    EXPECT_TRUE(r3.is_err()) << "HTN should reject exec fan-in";
}

TEST(DeepCycle, S2_HTNComplex_BakeAndRoundTrip) {
    Environment env;
    load_core(env);
    load_htn(env);
    auto mod = do_compile(read_fixture("htn_complex.gs"), env);

    auto eg = EditGraph::build(mod.graphs[0], env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "HTNGraph");
    EXPECT_GE(rt.flow_edge_count(), 1u);

    Environment env2;
    load_core(env2);
    load_htn(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 3: Complex Task Graph with Multiple Events + Functions
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S3_TaskComplex_FullPipeline) {
    Environment env;
    load_core(env);
    load_task(env);
    auto mod = do_compile(read_fixture("task_complex.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "MultiStageQuest");
    EXPECT_EQ(*g.base_type, "TaskGraph");
    EXPECT_EQ(g.parameters.size(), 4u);
    EXPECT_EQ(g.node_instances.size(), 5u);
    EXPECT_EQ(g.events.size(), 1u);
    EXPECT_EQ(g.functions.size(), 2u);
    // Graph-level annotations (was Comment in generate)
    EXPECT_GE(g.annotations.size(), 1u);
    // Node-level annotations (was position in generate)
    size_t nodes_with_position = 0;
    for (auto& ni : g.node_instances) {
        for (auto& a : ni.annotations) {
            if (a.name == "Position") { nodes_with_position++; break; }
        }
    }
    EXPECT_GE(nodes_with_position, 1u);

    // Bake
    auto eg = EditGraph::build(g, env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "TaskGraph");

    // Round-trip
    Environment env2;
    load_core(env2);
    load_task(env2);
    assert_round_trip(mod, env2);
}

TEST(DeepCycle, S3_TaskSchema_FanOutLimit) {
    Environment env;
    load_core(env);
    load_task(env);

    auto* schema = env.schemas().find("TaskGraph");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->connection_policy.max_exec_fan_out, 1);

    EditGraph eg("Test", &env, schema);
    auto h1 = eg.add_node("ShowDialogue", "d");
    auto h2 = eg.add_node("TaskComplete", "c1");
    auto h3 = eg.add_node("TaskComplete", "c2");
    ASSERT_TRUE(h1.is_ok());
    ASSERT_TRUE(h2.is_ok());
    ASSERT_TRUE(h3.is_ok());

    auto r1 = eg.connect(h1.value(), "exit", h2.value(), "finish");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    // Fan-out = 1: second connection should fail
    auto r2 = eg.connect(h1.value(), "exit", h3.value(), "finish");
    EXPECT_TRUE(r2.is_err()) << "TaskGraph should reject exec fan-out > 1";
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 4: Complex Level Script with Multiple Events
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S4_LevelScriptComplex_FullPipeline) {
    Environment env;
    load_core(env);
    load_level(env);
    auto mod = do_compile(read_fixture("levelscript_complex.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "ArenaEncounter");
    EXPECT_EQ(*g.base_type, "LevelScriptGraph");
    EXPECT_EQ(g.events.size(), 2u);
    EXPECT_EQ(g.node_instances.size(), 6u);
    // Graph-level annotations (was Comment in generate)
    EXPECT_GE(g.annotations.size(), 1u);

    // spawn_loc_a/b/c are now graph params, no top-level lets
    EXPECT_EQ(mod.top_level_lets.size(), 0u);

    auto eg = EditGraph::build(g, env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "LevelScriptGraph");
    EXPECT_GE(rt.node_count(), 1u);

    Environment env2;
    load_core(env2);
    load_level(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 5: Multiple Graphs in One File with Cross-References
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S5_MultiGraph_CrossReference) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("multi_graph_file.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 3u);
    EXPECT_EQ(mod.graphs[0].name, "Utility_ClampAndLog");
    EXPECT_EQ(mod.graphs[1].name, "Utility_FormatMessage");
    EXPECT_EQ(mod.graphs[2].name, "MainController");

    // MainController uses both utility graphs as nodes
    auto& mc = mod.graphs[2];
    EXPECT_EQ(mc.node_instances.size(), 4u);
    bool has_clamp = false, has_fmt = false;
    for (auto& ni : mc.node_instances) {
        if (ni.type_name == "Utility_ClampAndLog") has_clamp = true;
        if (ni.type_name == "Utility_FormatMessage") has_fmt = true;
    }
    EXPECT_TRUE(has_clamp);
    EXPECT_TRUE(has_fmt);

    // Both utility graphs should be registered as nodes
    EXPECT_NE(env.nodes().find("Utility_ClampAndLog"), nullptr);
    EXPECT_NE(env.nodes().find("Utility_FormatMessage"), nullptr);
    EXPECT_NE(env.nodes().find("MainController"), nullptr);

    // Verify pin derivation on Utility_ClampAndLog
    auto* clamp_node = env.nodes().find("Utility_ClampAndLog");
    EXPECT_EQ(clamp_node->data_inputs().size(), 1u);   // raw_value
    EXPECT_EQ(clamp_node->data_outputs().size(), 1u);   // clamped
    EXPECT_EQ(clamp_node->exec_inputs().size(), 1u);    // Execute

    // Bake all graphs
    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        auto rt = RuntimeGraph::bake(eg);
        EXPECT_EQ(rt.name(), g.name);
    }

    Environment env2;
    load_core(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 6: Edge Cases — Empty and Minimal Graphs
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S6_EmptyGraphs_Parse) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("empty_graphs.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 4u);
    EXPECT_EQ(mod.graphs[0].name, "EmptyGraph");
    EXPECT_EQ(mod.graphs[1].name, "ParamOnly");
    EXPECT_EQ(mod.graphs[2].name, "EventOnly");
    EXPECT_EQ(mod.graphs[3].name, "NodeOnly");
}

TEST(DeepCycle, S6_EmptyGraphs_Derivation) {
    Environment env;
    load_core(env);
    do_compile(read_fixture("empty_graphs.gs"), env);

    // EmptyGraph → no pins at all
    auto* empty = env.nodes().find("EmptyGraph");
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->pins.size(), 0u);

    // ParamOnly → 2 data pins (in x, out y); var excluded
    auto* param_only = env.nodes().find("ParamOnly");
    ASSERT_NE(param_only, nullptr);
    EXPECT_EQ(param_only->data_inputs().size(), 1u);
    EXPECT_EQ(param_only->data_outputs().size(), 1u);
    EXPECT_EQ(param_only->exec_inputs().size(), 0u);

    // EventOnly → 2 exec input pins (Tick, OnDestroy)
    auto* event_only = env.nodes().find("EventOnly");
    ASSERT_NE(event_only, nullptr);
    EXPECT_EQ(event_only->exec_inputs().size(), 2u);
    EXPECT_EQ(event_only->data_inputs().size(), 0u);

    // NodeOnly → no params, no events → no pins
    auto* node_only = env.nodes().find("NodeOnly");
    ASSERT_NE(node_only, nullptr);
    EXPECT_EQ(node_only->pins.size(), 0u);
}

TEST(DeepCycle, S6_EmptyGraphs_BakeAllValid) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("empty_graphs.gs"), env);

    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        auto diags = eg.validate();
        EXPECT_TRUE(diags.empty()) << "Validation failed for " << g.name;
        auto rt = RuntimeGraph::bake(eg);
        EXPECT_EQ(rt.name(), g.name);
    }
}

TEST(DeepCycle, S6_EmptyGraphs_RoundTrip) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("empty_graphs.gs"), env);
    Environment env2;
    load_core(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 7: Kitchen Sink — Every Language Feature
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S7_AllFeatures_FullPipeline) {
    Environment env;
    load_core(env);
    load_htn(env);  // for extended type coverage
    auto mod = do_compile(read_fixture("all_features.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    EXPECT_EQ(mod.top_level_lets.size(), 2u);
    EXPECT_EQ(mod.imports.size(), 2u);

    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "KitchenSink");
    EXPECT_EQ(g.parameters.size(), 5u);
    EXPECT_EQ(g.node_instances.size(), 4u);
    EXPECT_EQ(g.events.size(), 2u);
    EXPECT_EQ(g.functions.size(), 2u);
    // Graph-level annotations (was Comment in generate)
    EXPECT_GE(g.annotations.size(), 2u);
    // Node-level annotations (was position in generate)
    size_t nodes_with_position = 0;
    for (auto& ni : g.node_instances) {
        for (auto& a : ni.annotations) {
            if (a.name == "Position") { nodes_with_position++; break; }
        }
    }
    EXPECT_GE(nodes_with_position, 1u);

    // Verify flow connections in OnStart
    auto& on_start = g.events[0];
    EXPECT_EQ(on_start.name, "OnStart");
    EXPECT_GE(on_start.flow_connections.size(), 3u);
    EXPECT_GE(on_start.data_links.size(), 3u);

    // Build → Bake
    auto eg = EditGraph::build(g, env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_GE(rt.node_count(), 4u);
    EXPECT_GE(rt.flow_edge_count(), 1u);

    // Round-trip
    Environment env2;
    load_core(env2);
    load_htn(env2);
    assert_round_trip(mod, env2);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 8: Complex .d.gs with Multiple Schemas
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S8_MixedDeclarations_FullLoad) {
    Environment env;
    load_core(env);
    load_mixed(env);

    // Types
    EXPECT_NE(env.types().find("GameplayTag"), nullptr);
    EXPECT_NE(env.types().find("FTransform"), nullptr);
    auto* ft = env.types().find("FTransform");
    EXPECT_TRUE(ft->constructible);
    EXPECT_NE(env.types().find("UAnimMontage"), nullptr);

    // Nodes
    EXPECT_NE(env.nodes().find("PlayMontage"), nullptr);
    auto* pm = env.nodes().find("PlayMontage");
    EXPECT_EQ(pm->pins.size(), 6u);  // 3 exec (play, completed, interrupted) + 3 data
    EXPECT_EQ(pm->exec_inputs().size(), 1u);
    EXPECT_EQ(pm->exec_outputs().size(), 2u);  // completed, interrupted

    EXPECT_NE(env.nodes().find("PlaySound"), nullptr);
    EXPECT_NE(env.nodes().find("SpawnParticle"), nullptr);
    EXPECT_NE(env.nodes().find("BranchOnTag"), nullptr);

    auto* branch = env.nodes().find("BranchOnTag");
    EXPECT_EQ(branch->exec_inputs().size(), 1u);
    EXPECT_EQ(branch->exec_outputs().size(), 2u);  // matched, notMatched
    EXPECT_EQ(branch->data_inputs().size(), 2u);    // tag, target

    // Schemas
    auto* cin = env.schemas().find("CinematicGraph");
    ASSERT_NE(cin, nullptr);
    EXPECT_EQ(cin->connection_policy.max_exec_fan_out, 1);
    EXPECT_TRUE(cin->connection_policy.allow_exec_fan_in);
    EXPECT_TRUE(cin->connection_policy.strict_type_match);

    auto* abl = env.schemas().find("AbilityGraph");
    ASSERT_NE(abl, nullptr);
    EXPECT_EQ(abl->connection_policy.max_exec_fan_out, -1);
    EXPECT_FALSE(abl->connection_policy.allow_exec_fan_in);
    EXPECT_FALSE(abl->connection_policy.strict_type_match);
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 9: Cinematic Graph (CinematicGraph schema: strict, fan-out=1)
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S9_Cinematic_FullPipeline) {
    Environment env;
    load_core(env);
    load_mixed(env);
    auto mod = do_compile(read_fixture("cinematic_sequence.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "CutsceneIntro");
    EXPECT_EQ(*g.base_type, "CinematicGraph");
    EXPECT_EQ(g.parameters.size(), 8u);
    EXPECT_EQ(g.node_instances.size(), 4u);

    // Build with schema
    auto eg = EditGraph::build(g, env);
    EXPECT_NE(eg.schema(), nullptr);
    EXPECT_EQ(eg.schema()->name, "CinematicGraph");

    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "CinematicGraph");

    // Round-trip
    Environment env2;
    load_core(env2);
    load_mixed(env2);
    assert_round_trip(mod, env2);
}

TEST(DeepCycle, S9_CinematicSchema_FanOutLimit) {
    Environment env;
    load_core(env);
    load_mixed(env);

    auto* schema = env.schemas().find("CinematicGraph");
    ASSERT_NE(schema, nullptr);

    EditGraph eg("Test", &env, schema);
    auto h1 = eg.add_node("PlayMontage", "anim");
    auto h2 = eg.add_node("PrintString", "log1");
    auto h3 = eg.add_node("PrintString", "log2");
    ASSERT_TRUE(h1.is_ok());
    ASSERT_TRUE(h2.is_ok());
    ASSERT_TRUE(h3.is_ok());

    auto r1 = eg.connect(h1.value(), "completed", h2.value(), "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    // Fan-out = 1: second connection from same exec output should fail
    auto r2 = eg.connect(h1.value(), "completed", h3.value(), "enter");
    EXPECT_TRUE(r2.is_err());

    // But fan-in is allowed: connect from different sources to same input
    auto h4 = eg.add_node("PlaySound", "snd");
    ASSERT_TRUE(h4.is_ok());
    auto r3 = eg.connect(h4.value(), "finished", h2.value(), "enter");
    EXPECT_TRUE(r3.is_ok()) << r3.error();
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 10: Ability Graph (unlimited fan-out, no fan-in)
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S10_AbilityBranching_FullPipeline) {
    Environment env;
    load_core(env);
    load_mixed(env);
    auto mod = do_compile(read_fixture("ability_branching.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 1u);
    auto& g = mod.graphs[0];
    EXPECT_EQ(g.name, "FireballAbility");
    EXPECT_EQ(*g.base_type, "AbilityGraph");
    EXPECT_EQ(g.parameters.size(), 3u);
    EXPECT_EQ(g.node_instances.size(), 5u);

    // Check: the event has multiple connections from matched (unlimited fan-out)
    auto& ev = g.events[0];
    int matched_fan_out = 0;
    for (auto& fc : ev.flow_connections) {
        if (fc.from.pin_name == "matched") matched_fan_out++;
    }
    EXPECT_GE(matched_fan_out, 3) << "Ability should have unlimited exec fan-out";

    auto eg = EditGraph::build(g, env);
    auto rt = RuntimeGraph::bake(eg);
    EXPECT_EQ(rt.domain_name(), "AbilityGraph");

    Environment env2;
    load_core(env2);
    load_mixed(env2);
    assert_round_trip(mod, env2);
}

TEST(DeepCycle, S10_AbilitySchema_NoFanIn) {
    Environment env;
    load_core(env);
    load_mixed(env);

    auto* schema = env.schemas().find("AbilityGraph");
    ASSERT_NE(schema, nullptr);

    EditGraph eg("Test", &env, schema);
    auto h1 = eg.add_node("BranchOnTag", "b1");
    auto h2 = eg.add_node("BranchOnTag", "b2");
    auto h3 = eg.add_node("PrintString", "target");
    ASSERT_TRUE(h1.is_ok());
    ASSERT_TRUE(h2.is_ok());
    ASSERT_TRUE(h3.is_ok());

    // Unlimited fan-out: b1.matched → target, b1.notMatched → target is fine (different source pins)
    auto r1 = eg.connect(h1.value(), "matched", h3.value(), "enter");
    ASSERT_TRUE(r1.is_ok()) << r1.error();

    // No fan-in: b2 → target.enter should fail
    auto r2 = eg.connect(h2.value(), "matched", h3.value(), "enter");
    EXPECT_TRUE(r2.is_err()) << "AbilityGraph should reject exec fan-in";
}

// ═══════════════════════════════════════════════════════════════════
// Scenario 11: Parameters with Defaults + Graph-as-Node Reuse
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, S11_ParamDefaults_FullPipeline) {
    Environment env;
    load_core(env);
    auto mod = do_compile(read_fixture("param_defaults.gs"), env);
    ASSERT_EQ(mod.graphs.size(), 2u);

    // ConfigurableNode
    auto& cfg = mod.graphs[0];
    EXPECT_EQ(cfg.name, "ConfigurableNode");
    EXPECT_EQ(cfg.parameters.size(), 4u);
    EXPECT_EQ(cfg.parameters[0].default_value, "1.0");
    EXPECT_EQ(cfg.parameters[1].default_value, "100");
    EXPECT_TRUE(cfg.parameters[2].default_value.empty());

    // UseConfigurable references ConfigurableNode as a node
    auto* cfg_node = env.nodes().find("ConfigurableNode");
    ASSERT_NE(cfg_node, nullptr);
    EXPECT_EQ(cfg_node->data_inputs().size(), 3u);   // speed, health, name
    EXPECT_EQ(cfg_node->data_outputs().size(), 1u);   // result
    EXPECT_EQ(cfg_node->exec_inputs().size(), 1u);    // OnRun

    auto& use = mod.graphs[1];
    EXPECT_EQ(use.node_instances[0].type_name, "ConfigurableNode");

    // Full Bake
    for (auto& g : mod.graphs) {
        auto eg = EditGraph::build(g, env);
        auto rt = RuntimeGraph::bake(eg);
        EXPECT_EQ(rt.name(), g.name);
    }

    // Round-trip: verify defaults survive
    Environment env2;
    load_core(env2);
    Emitter emitter;
    auto emitted = emitter.emit(mod);
    EXPECT_NE(emitted.find("= 1.0"), std::string::npos) << "Default value 1.0 lost in round-trip";
    EXPECT_NE(emitted.find("= 100"), std::string::npos) << "Default value 100 lost in round-trip";

    auto ast2 = do_parse(emitted);
    ASSERT_NE(ast2, nullptr);
    Compiler c2(env2);
    auto result2 = c2.compile(*ast2);
    ASSERT_TRUE(result2.is_ok()) << result2.error();
    EXPECT_EQ(result2.value().graphs.size(), 2u);
}

// ═══════════════════════════════════════════════════════════════════
// Cross-Cutting: Full Pipeline Stress — All Fixtures at Once
// ═══════════════════════════════════════════════════════════════════

TEST(DeepCycle, StressTest_AllFixturesParseCompile) {
    // Load ALL .d.gs into one environment, then compile ALL .gs files
    Environment env;
    load_core(env);
    load_htn(env);
    load_task(env);
    load_level(env);
    load_mixed(env);

    std::vector<std::string> gs_files = {
        "minimal.gs", "htn_basic.gs", "task_basic.gs", "levelscript_basic.gs",
        "graph_as_node.gs", "invalid_connection.gs", "round_trip.gs",
        "deep_chain.gs", "htn_complex.gs", "task_complex.gs",
        "levelscript_complex.gs", "multi_graph_file.gs", "empty_graphs.gs",
        "all_features.gs", "cinematic_sequence.gs", "ability_branching.gs",
        "param_defaults.gs"
    };

    int total_graphs = 0;
    int total_nodes_registered = 0;

    for (auto& file : gs_files) {
        auto src = read_fixture(file);
        ASSERT_FALSE(src.empty()) << "Failed to read " << file;

        auto ast = do_parse(src);
        ASSERT_NE(ast, nullptr) << "Parse failed for " << file;

        Compiler compiler(env);
        auto result = compiler.compile(*ast, file);
        ASSERT_TRUE(result.is_ok()) << "Compile failed for " << file << ": " << result.error();

        total_graphs += static_cast<int>(result.value().graphs.size());
    }

    EXPECT_GE(total_graphs, 20) << "Should have compiled at least 20 graphs total";
    EXPECT_GE(env.nodes().all().size(), 15u) << "Should have registered many nodes";
    EXPECT_GE(env.schemas().all().size(), 5u) << "Should have at least 5 schemas";
}

TEST(DeepCycle, StressTest_BakeAll) {
    Environment env;
    load_core(env);
    load_htn(env);
    load_task(env);
    load_level(env);
    load_mixed(env);

    std::vector<std::string> gs_files = {
        "minimal.gs", "htn_basic.gs", "task_basic.gs", "levelscript_basic.gs",
        "deep_chain.gs", "multi_graph_file.gs", "empty_graphs.gs",
        "all_features.gs", "cinematic_sequence.gs", "param_defaults.gs"
    };

    int total_baked = 0;
    for (auto& file : gs_files) {
        auto mod = do_compile(read_fixture(file), env, file);
        for (auto& g : mod.graphs) {
            auto eg = EditGraph::build(g, env);
            auto diags = eg.validate();
            // We don't assert empty diags since some fixture data links reference params
            auto rt = RuntimeGraph::bake(eg);
            EXPECT_EQ(rt.name(), g.name) << "Bake name mismatch for " << file << "/" << g.name;
            total_baked++;
        }
    }
    EXPECT_GE(total_baked, 15) << "Should have baked at least 15 graphs";
}
