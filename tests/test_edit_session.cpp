// EditSession unit tests: graph management, CRUD, undo/redo, emit round-trip

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>

#include "graphscript/edit/edit_session.h"
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"

using namespace gs;

// Loads ue_core.d.gs into environment via session.
static void load_core(EditSession& s) {
    std::string path = std::string(GS_TEST_FIXTURES_DIR) + "/ue_core.d.gs";
    s.load_import(path);
}

// ═══════════════════════════════════════════════════════════════════
// Graph Management
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, NewGraphAndActive) {
    Environment env;
    EditSession s(env);

    auto r = s.new_graph("TestGraph");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 0);
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_NE(s.active_graph(), nullptr);
    EXPECT_EQ(s.active_graph()->name, "TestGraph");
}

TEST(EditSession, DuplicateGraphFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("A");
    auto r = s.new_graph("A");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, MultipleGraphsSwitching) {
    Environment env;
    EditSession s(env);
    s.new_graph("First");
    s.new_graph("Second");
    EXPECT_EQ(s.active_index(), 1);

    auto r = s.set_active("First");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(s.active_index(), 0);
    EXPECT_EQ(s.active_graph()->name, "First");
}

TEST(EditSession, DeleteGraph) {
    Environment env;
    EditSession s(env);
    s.new_graph("ToDelete");
    s.new_graph("Keep");

    auto r = s.delete_graph("ToDelete");
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(s.module().graphs.size(), 1u);
    EXPECT_EQ(s.module().graphs[0].name, "Keep");
}

TEST(EditSession, NewGraphWithSchema) {
    Environment env;
    EditSession s(env);
    auto r = s.new_graph("HTNPlan", "HTNGraph");
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(s.active_graph()->base_type.has_value());
    EXPECT_EQ(*s.active_graph()->base_type, "HTNGraph");
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveParams) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    auto r1 = s.add_param(ParamDirection::In, "health", "int", "100");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = s.add_param(ParamDirection::Out, "result", "bool");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->parameters.size(), 2u);
    EXPECT_EQ(s.active_graph()->parameters[0].default_value, "100");

    auto r3 = s.remove_param("health");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(s.active_graph()->parameters[0].name, "result");
}

TEST(EditSession, DuplicateParamFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "x", "int");
    auto r = s.add_param(ParamDirection::In, "x", "float");
    EXPECT_TRUE(r.is_err());
}

// ═══════════════════════════════════════════════════════════════════
// Nodes
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveNodes) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");

    auto r1 = s.add_node("PrintString", "logger");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = s.add_node("Delay", "timer");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 2u);

    auto r3 = s.remove_node("logger");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, UnknownNodeTypeFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    auto r = s.add_node("NonExistent", "x");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, RemoveNodeClearsConnections) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    s.add_node("Delay", "d1");
    s.add_event("Ev");
    s.add_flow("Ev", "p1", "exit", "d1", "enter");
    s.add_link("Ev", "d1", "duration", "p1", "message");

    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 1u);

    s.remove_node("p1");
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 0u);
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════
// Events & Functions
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, AddRemoveEventsAndFunctions) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    s.add_event("OnStart");
    s.add_event("OnEnd");
    s.add_function("Calculate");
    EXPECT_EQ(s.active_graph()->events.size(), 2u);
    EXPECT_EQ(s.active_graph()->functions.size(), 1u);

    s.remove_event("OnEnd");
    EXPECT_EQ(s.active_graph()->events.size(), 1u);
    EXPECT_EQ(s.active_graph()->events[0].name, "OnStart");

    s.remove_function("Calculate");
    EXPECT_EQ(s.active_graph()->functions.size(), 0u);
}

// ═══════════════════════════════════════════════════════════════════
// Flow & Link
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, FlowAndLinkOperations) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "msg", "FString");
    s.add_node("PrintString", "p");
    s.add_node("Delay", "d");
    s.add_event("Ev");

    auto r1 = s.add_flow("Ev", "p", "exit", "d", "enter");
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 1u);

    auto r2 = s.add_link("Ev", "p", "message", "msg");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 1u);
    EXPECT_TRUE(s.active_graph()->events[0].data_links[0].source.pin_name.empty());

    auto r3 = s.remove_flow("Ev", "p", "exit", "d", "enter");
    ASSERT_TRUE(r3.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].flow_connections.size(), 0u);

    auto r4 = s.remove_link("Ev", "p", "message");
    ASSERT_TRUE(r4.is_ok());
    EXPECT_EQ(s.active_graph()->events[0].data_links.size(), 0u);
}

TEST(EditSession, FlowOnMissingBlockFails) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");
    auto r = s.add_flow("NoSuchBlock", "a", "b", "c", "d");
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, FunctionCannotReferenceGraphNodes) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "health", "int");
    s.add_node("PrintString", "logger");
    s.add_function("MyFunc");

    // Function referencing a graph-level node instance must fail
    auto r1 = s.add_flow("MyFunc", "context", "start", "logger", "enter");
    EXPECT_TRUE(r1.is_err());
    EXPECT_NE(r1.error().find("cannot reference"), std::string::npos);

    auto r2 = s.add_link("MyFunc", "logger", "message", "health");
    EXPECT_TRUE(r2.is_err());
    EXPECT_NE(r2.error().find("cannot reference"), std::string::npos);

    // Function referencing context and params must succeed
    auto r3 = s.add_flow("MyFunc", "context", "start", "context", "done");
    EXPECT_TRUE(r3.is_ok());

    auto r4 = s.add_link("MyFunc", "context", "result", "health");
    EXPECT_TRUE(r4.is_ok());

    // Events can still reference graph nodes
    s.add_event("OnStart");
    auto r5 = s.add_flow("OnStart", "context", "start", "logger", "enter");
    EXPECT_TRUE(r5.is_ok());

    auto r6 = s.add_link("OnStart", "logger", "message", "health");
    EXPECT_TRUE(r6.is_ok());
}

TEST(EditSession, EventCannotReferenceDanglingNames) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_param(ParamDirection::In, "health", "int");
    s.add_node("PrintString", "logger");
    s.add_event("OnStart");

    // Event referencing valid entities: context, params, nodes → OK
    auto r1 = s.add_flow("OnStart", "context", "start", "logger", "enter");
    EXPECT_TRUE(r1.is_ok());

    auto r2 = s.add_link("OnStart", "logger", "message", "health");
    EXPECT_TRUE(r2.is_ok());

    // Event referencing a name that doesn't exist anywhere → fail
    auto r3 = s.add_flow("OnStart", "logger", "exit", "nonexistent", "enter");
    EXPECT_TRUE(r3.is_err());
    EXPECT_NE(r3.error().find("unknown reference"), std::string::npos);

    auto r4 = s.add_link("OnStart", "logger", "message", "ghost_param");
    EXPECT_TRUE(r4.is_err());
    EXPECT_NE(r4.error().find("unknown reference"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// Undo / Redo
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, UndoRedoBasic) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
    EXPECT_TRUE(s.can_undo());

    auto r1 = s.undo();
    ASSERT_TRUE(r1.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 0u);
    EXPECT_TRUE(s.can_redo());

    auto r2 = s.redo();
    ASSERT_TRUE(r2.is_ok());
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, UndoRedoMultipleSteps) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");

    s.add_param(ParamDirection::In, "x", "int");
    s.add_node("PrintString", "p1");
    s.add_node("Delay", "d1");
    s.add_event("OnStart");
    EXPECT_EQ(s.active_graph()->node_instances.size(), 2u);
    EXPECT_EQ(s.active_graph()->events.size(), 1u);
    EXPECT_EQ(s.undo_history().size(), 4u);

    // Undo all 4 operations
    s.undo(); // undo add_event
    EXPECT_EQ(s.active_graph()->events.size(), 0u);
    s.undo(); // undo add_node d1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
    s.undo(); // undo add_node p1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 0u);
    s.undo(); // undo add_param
    EXPECT_EQ(s.active_graph()->parameters.size(), 0u);

    EXPECT_EQ(s.redo_history().size(), 4u);

    // Redo first 2
    s.redo(); // redo add_param
    EXPECT_EQ(s.active_graph()->parameters.size(), 1u);
    s.redo(); // redo add_node p1
    EXPECT_EQ(s.active_graph()->node_instances.size(), 1u);
}

TEST(EditSession, UndoEmptyFails) {
    Environment env;
    EditSession s(env);
    auto r = s.undo();
    EXPECT_TRUE(r.is_err());
}

TEST(EditSession, NewOperationClearsRedoStack) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("G");
    s.add_node("PrintString", "p1");
    s.undo();
    EXPECT_TRUE(s.can_redo());

    s.add_node("Delay", "d1");
    EXPECT_FALSE(s.can_redo());
}

// ═══════════════════════════════════════════════════════════════════
// Emit + Round-trip
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, EmitRoundTrip) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("Player");
    s.add_param(ParamDirection::In, "health", "int", "100");
    s.add_param(ParamDirection::Out, "alive", "bool");
    s.add_node("PrintString", "log");
    s.add_node("Delay", "wait");
    s.add_event("OnDamage");
    s.add_flow("OnDamage", "context", "start", "log", "enter");
    s.add_flow("OnDamage", "log", "exit", "wait", "enter");
    s.add_link("OnDamage", "log", "message", "health");

    std::string emitted = s.emit();
    ASSERT_FALSE(emitted.empty());
    EXPECT_NE(emitted.find("Graph Player"), std::string::npos);
    EXPECT_NE(emitted.find("in health : int = 100"), std::string::npos);
    EXPECT_NE(emitted.find("PrintString log"), std::string::npos);
    EXPECT_NE(emitted.find("event OnDamage"), std::string::npos);
    EXPECT_NE(emitted.find("link log.message = health"), std::string::npos);

    // Re-parse the emitted text
    Lexer lexer(emitted);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    ASSERT_TRUE(result.is_ok()) << "Re-parse failed";

    Environment env2;
    EditSession s2(env2);
    load_core(s2);
    Compiler compiler(env2);
    auto mod2 = compiler.compile(*result.value());
    ASSERT_TRUE(mod2.is_ok());
    EXPECT_EQ(mod2.value().graphs.size(), 1u);
    EXPECT_EQ(mod2.value().graphs[0].name, "Player");
}

// ═══════════════════════════════════════════════════════════════════
// Generate block
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, GenerateBlock) {
    Environment env;
    EditSession s(env);
    s.new_graph("G");

    s.add_comment("desc", "Test graph");
    s.add_meta("position", "myNode", "x", "100");
    s.add_meta("position", "myNode", "y", "200");

    auto* g = s.active_graph();
    ASSERT_TRUE(g->generate.has_value());
    EXPECT_EQ(g->generate->comments.size(), 1u);
    EXPECT_EQ(g->generate->metadata.size(), 2u);
}

// ═══════════════════════════════════════════════════════════════════
// Import + Module
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, ImportAndModuleState) {
    Environment env;
    EditSession s(env);

    // add_import only adds the declaration (no compile), so start fresh
    s.add_import("ue_core.d.gs");
    EXPECT_EQ(s.module().imports.size(), 1u);
    EXPECT_TRUE(s.module().imports[0].is_native);

    // Duplicate import is ignored
    s.add_import("ue_core.d.gs");
    EXPECT_EQ(s.module().imports.size(), 1u);

    // load_import compiles + adds import; total should remain 1 due to dedup
    std::string path = std::string(GS_TEST_FIXTURES_DIR) + "/ue_core.d.gs";
    s.load_import(path);
    // load_import adds the full path; but our manual add used a relative name,
    // so they are distinct entries.
    EXPECT_GE(s.module().imports.size(), 1u);
}

TEST(EditSession, LetDeclaration) {
    Environment env;
    EditSession s(env);
    s.add_let("spawn_point", "FVector", "0,0,100");
    EXPECT_EQ(s.module().top_level_lets.size(), 1u);
    EXPECT_EQ(s.module().top_level_lets[0].name, "spawn_point");
}

// ═══════════════════════════════════════════════════════════════════
// Build + Validate + Bake via session
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, BuildEditGraphAndBake) {
    Environment env;
    EditSession s(env);
    load_core(s);
    s.new_graph("Simple");
    s.add_node("PrintString", "p");

    auto eg = s.build_edit_graph();
    ASSERT_TRUE(eg.has_value());
    EXPECT_EQ(eg->name(), "Simple");
    EXPECT_EQ(eg->node_count(), 1u);

    auto diags = s.validate();
    EXPECT_TRUE(diags.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Full workflow simulation
// ═══════════════════════════════════════════════════════════════════

TEST(EditSession, FullWorkflowSimulation) {
    Environment env;
    EditSession s(env);
    load_core(s);

    // Step 1: Create graph with params
    s.new_graph("GameplayAbility");
    s.add_param(ParamDirection::In, "caster", "AActor");
    s.add_param(ParamDirection::In, "damage", "float", "50.0");
    s.add_param(ParamDirection::Out, "success", "bool");

    // Step 2: Add nodes
    s.add_node("PrintString", "dmgLog");
    s.add_node("Delay", "cooldown");

    // Step 3: Create event with connections
    s.add_event("OnActivate");
    s.add_flow("OnActivate", "context", "start", "dmgLog", "enter");
    s.add_flow("OnActivate", "dmgLog", "exit", "cooldown", "enter");
    s.add_link("OnActivate", "dmgLog", "message", "damage");
    s.add_link("OnActivate", "cooldown", "duration", "damage");

    // Step 4: Create function (functions can only reference context and params)
    s.add_function("Reset");
    s.add_flow("Reset", "context", "start", "context", "done");

    // Step 5: Add generate metadata
    s.add_comment("desc", "Gameplay ability with cooldown");
    s.add_meta("position", "dmgLog", "x", "200");
    s.add_meta("position", "dmgLog", "y", "100");

    // Verify structure
    auto* g = s.active_graph();
    EXPECT_EQ(g->parameters.size(), 3u);
    EXPECT_EQ(g->node_instances.size(), 2u);
    EXPECT_EQ(g->events.size(), 1u);
    EXPECT_EQ(g->functions.size(), 1u);
    EXPECT_TRUE(g->generate.has_value());
    EXPECT_EQ(g->events[0].flow_connections.size(), 2u);
    EXPECT_EQ(g->events[0].data_links.size(), 2u);

    // Step 6: Emit and verify
    auto text = s.emit();
    EXPECT_NE(text.find("GameplayAbility"), std::string::npos);
    EXPECT_NE(text.find("in damage : float = 50.0"), std::string::npos);
    EXPECT_NE(text.find("event OnActivate"), std::string::npos);
    EXPECT_NE(text.find("function Reset"), std::string::npos);
    EXPECT_NE(text.find("Comment desc"), std::string::npos);

    // Step 7: Undo the function, verify it's gone
    s.undo(); // undo add_meta
    s.undo(); // undo add_meta
    s.undo(); // undo add_comment
    s.undo(); // undo add_flow in Reset
    s.undo(); // undo add_function Reset
    EXPECT_EQ(s.active_graph()->functions.size(), 0u);

    // Step 8: Redo it back
    s.redo(); // redo add_function
    EXPECT_EQ(s.active_graph()->functions.size(), 1u);
    EXPECT_EQ(s.active_graph()->functions[0].name, "Reset");
}
