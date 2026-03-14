#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/registry/environment.h"

using namespace gs;

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

static std::unique_ptr<ModuleNode> parse(std::string_view src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (result.is_err()) return nullptr;
    return std::move(result).value();
}

TEST(Compiler, CompileDeclareTypes) {
    auto ast = parse(read_preset("ue_core.d.gs"));
    ASSERT_NE(ast, nullptr);

    Environment env;
    Compiler compiler(env);
    auto result = compiler.compile(*ast, "ue_core.d.gs");
    ASSERT_TRUE(result.is_ok()) << result.error();

    EXPECT_NE(env.types().find("FVector"), nullptr);
    EXPECT_NE(env.types().find("FName"), nullptr);
    EXPECT_NE(env.types().find("SoftObjectPath"), nullptr);

    auto* fvec = env.types().find("FVector");
    ASSERT_NE(fvec, nullptr);
    EXPECT_TRUE(fvec->constructible);

    auto* fname = env.types().find("FName");
    ASSERT_NE(fname, nullptr);
    EXPECT_FALSE(fname->constructible);
}

TEST(Compiler, CompileDeclareNodes) {
    auto ast = parse(read_preset("ue_core.d.gs"));
    ASSERT_NE(ast, nullptr);

    Environment env;
    Compiler compiler(env);
    compiler.compile(*ast, "ue_core.d.gs");

    auto* ps = env.nodes().find("PrintString");
    ASSERT_NE(ps, nullptr);
    EXPECT_TRUE(ps->is_native);
    EXPECT_EQ(ps->pins.size(), 3u);
    EXPECT_NE(ps->find_pin("enter"), nullptr);
    EXPECT_NE(ps->find_pin("exit"), nullptr);
    EXPECT_NE(ps->find_pin("message"), nullptr);

    auto* delay = env.nodes().find("Delay");
    ASSERT_NE(delay, nullptr);
    EXPECT_EQ(delay->pins.size(), 3u);
}

TEST(Compiler, CompileDeclareSchemas) {
    auto ast = parse(read_preset("htn_nodes.d.gs"));
    ASSERT_NE(ast, nullptr);

    Environment env;
    Compiler compiler(env);
    compiler.compile(*ast, "htn_nodes.d.gs");

    auto* schema = env.schemas().find("HTNGraph");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->connection_policy.max_exec_fan_out, -1);
    EXPECT_FALSE(schema->connection_policy.allow_exec_fan_in);
    EXPECT_TRUE(schema->connection_policy.strict_type_match);
    EXPECT_EQ(schema->allowed_node_tags.size(), 4u);
}

TEST(Compiler, CompileMinimalGraph) {
    Environment env;
    Compiler compiler(env);

    auto core_ast = parse(read_preset("ue_core.d.gs"));
    compiler.compile(*core_ast);

    auto ast = parse(read_fixture("minimal.gs"));
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast, "minimal.gs");
    ASSERT_TRUE(result.is_ok()) << result.error();

    auto& mod = result.value();
    ASSERT_EQ(mod.graphs.size(), 1u);
    EXPECT_EQ(mod.graphs[0].name, "HelloWorld");
    EXPECT_EQ(mod.graphs[0].parameters.size(), 1u);
    EXPECT_EQ(mod.graphs[0].node_instances.size(), 1u);
    EXPECT_EQ(mod.graphs[0].events.size(), 1u);
}

TEST(Compiler, GraphAsNodeDerivation) {
    Environment env;
    Compiler compiler(env);

    auto core_ast = parse(read_preset("ue_core.d.gs"));
    compiler.compile(*core_ast);

    auto ast = parse(read_fixture("graph_as_node.gs"));
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast, "graph_as_node.gs");
    ASSERT_TRUE(result.is_ok()) << result.error();

    // SubRoutine should be registered as a node
    auto* sub = env.nodes().find("SubRoutine");
    ASSERT_NE(sub, nullptr);
    EXPECT_FALSE(sub->is_native);
    EXPECT_EQ(sub->source_graph, "SubRoutine");

    // Data pins from in/out params
    auto data_in = sub->data_inputs();
    ASSERT_EQ(data_in.size(), 1u);
    EXPECT_EQ(data_in[0]->name, "value");
    EXPECT_EQ(data_in[0]->type_name, "int");

    auto data_out = sub->data_outputs();
    ASSERT_EQ(data_out.size(), 1u);
    EXPECT_EQ(data_out[0]->name, "result");

    // Exec input from event
    auto exec_in = sub->exec_inputs();
    ASSERT_EQ(exec_in.size(), 1u);
    EXPECT_EQ(exec_in[0]->name, "Execute");

    // MainGraph should also be registered
    auto* main = env.nodes().find("MainGraph");
    ASSERT_NE(main, nullptr);
}

TEST(Compiler, GraphWithBaseType) {
    Environment env;
    Compiler compiler(env);

    auto core_ast = parse(read_preset("ue_core.d.gs"));
    compiler.compile(*core_ast);
    auto htn_ast = parse(read_preset("htn_nodes.d.gs"));
    compiler.compile(*htn_ast);

    auto ast = parse(read_fixture("htn_basic.gs"));
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast, "htn_basic.gs");
    ASSERT_TRUE(result.is_ok()) << result.error();

    auto& g = result.value().graphs[0];
    EXPECT_EQ(g.name, "SimpleHTN");
    ASSERT_TRUE(g.base_type.has_value());
    EXPECT_EQ(*g.base_type, "HTNGraph");
}

TEST(Compiler, LetDeclarations) {
    Environment env;
    Compiler compiler(env);

    auto core_ast = parse(read_preset("ue_core.d.gs"));
    compiler.compile(*core_ast);

    auto ast = parse(read_fixture("round_trip.gs"));
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast, "round_trip.gs");
    ASSERT_TRUE(result.is_ok()) << result.error();

    ASSERT_EQ(result.value().top_level_lets.size(), 1u);
    EXPECT_EQ(result.value().top_level_lets[0].name, "actor_1");
    EXPECT_EQ(result.value().top_level_lets[0].type_name, "SoftObjectPath");
    EXPECT_EQ(result.value().top_level_lets[0].constructor_arg, "actor_path_1");
}

TEST(Compiler, ImportDeclarations) {
    Environment env;
    Compiler compiler(env);

    auto ast = parse(R"(import "ue_core.d.gs"; import "other.gs";)");
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast);
    ASSERT_TRUE(result.is_ok()) << result.error();

    ASSERT_EQ(result.value().imports.size(), 2u);
    EXPECT_EQ(result.value().imports[0].path, "ue_core.d.gs");
    EXPECT_TRUE(result.value().imports[0].is_native);
    EXPECT_EQ(result.value().imports[1].path, "other.gs");
    EXPECT_FALSE(result.value().imports[1].is_native);
}

TEST(Compiler, FlowConnections) {
    Environment env;
    Compiler compiler(env);

    auto core_ast = parse(read_preset("ue_core.d.gs"));
    compiler.compile(*core_ast);

    auto ast = parse(read_fixture("minimal.gs"));
    auto result = compiler.compile(*ast);
    ASSERT_TRUE(result.is_ok()) << result.error();

    auto& ev = result.value().graphs[0].events[0];
    EXPECT_EQ(ev.name, "OnStart");
    ASSERT_GE(ev.flow_connections.size(), 1u);
    EXPECT_EQ(ev.flow_connections[0].from.node_instance, "context");
    EXPECT_EQ(ev.flow_connections[0].from.pin_name, "start");
    EXPECT_EQ(ev.flow_connections[0].to.node_instance, "printer");
    EXPECT_EQ(ev.flow_connections[0].to.pin_name, "enter");
}

TEST(Compiler, VarParamExcludedFromDerivation) {
    Environment env;
    Compiler compiler(env);

    auto ast = parse(R"(
Graph Test {
    in x : int;
    out y : int;
    var temp : float;
    event Run { }
}
)");
    ASSERT_NE(ast, nullptr);
    auto result = compiler.compile(*ast);
    ASSERT_TRUE(result.is_ok()) << result.error();

    auto* node = env.nodes().find("Test");
    ASSERT_NE(node, nullptr);
    // var should not generate a pin, so 2 data pins + 1 exec pin
    EXPECT_EQ(node->pins.size(), 3u);
    auto di = node->data_inputs();
    EXPECT_EQ(di.size(), 1u);
    EXPECT_EQ(di[0]->name, "x");
    auto do_ = node->data_outputs();
    EXPECT_EQ(do_.size(), 1u);
    EXPECT_EQ(do_[0]->name, "y");
    auto ei = node->exec_inputs();
    EXPECT_EQ(ei.size(), 1u);
    EXPECT_EQ(ei[0]->name, "Run");
}
