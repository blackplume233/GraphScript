#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"

using namespace gs;

static std::string read_fixture(const std::string& filename) {
    std::string path = std::string(GS_TEST_FIXTURES_DIR) + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static Result<std::unique_ptr<ModuleNode>, std::string> parse_source(std::string_view src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parse();
}

// ─── .d.gs declaration files ───────────────────────────────────────

TEST(Parser, DeclareType) {
    auto result = parse_source("declare type FVector : constructible;");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->declare_types.size(), 1u);
    EXPECT_EQ(mod->declare_types[0]->name, "FVector");
    EXPECT_TRUE(mod->declare_types[0]->constructible);
}

TEST(Parser, DeclareTypeNonConstructible) {
    auto result = parse_source("declare type FName;");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->declare_types.size(), 1u);
    EXPECT_EQ(mod->declare_types[0]->name, "FName");
    EXPECT_FALSE(mod->declare_types[0]->constructible);
}

TEST(Parser, DeclareNode) {
    auto result = parse_source(R"(
declare Node PrintString {
    exec in enter;
    exec out exit;
    data in message : FString;
}
)");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->declare_nodes.size(), 1u);
    auto& node = mod->declare_nodes[0];
    EXPECT_EQ(node->name, "PrintString");
    ASSERT_EQ(node->pins.size(), 3u);
    EXPECT_EQ(node->pins[0]->kind, PinKind::Exec);
    EXPECT_EQ(node->pins[0]->direction, PinDirection::Input);
    EXPECT_EQ(node->pins[0]->name, "enter");
    EXPECT_EQ(node->pins[2]->kind, PinKind::Data);
    EXPECT_EQ(node->pins[2]->type_name, "FString");
}

TEST(Parser, DeclareSchema) {
    auto result = parse_source(R"(
declare Schema HTNGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: true;
    allowed_node_tags: ["htn_task", "common"];
}
)");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->declare_schemas.size(), 1u);
    auto& schema = mod->declare_schemas[0];
    EXPECT_EQ(schema->name, "HTNGraph");
    ASSERT_EQ(schema->fields.size(), 4u);
    EXPECT_EQ(schema->fields[0].first, "max_exec_fan_out");
    EXPECT_EQ(schema->fields[0].second, "unlimited");
}

TEST(Parser, UeCoreFixture) {
    auto src = read_fixture("ue_core.d.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& mod = result.value();
    EXPECT_GE(mod->declare_types.size(), 5u);
    EXPECT_GE(mod->declare_nodes.size(), 3u);
}

TEST(Parser, HtnNodesFixture) {
    auto src = read_fixture("htn_nodes.d.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& mod = result.value();
    EXPECT_GE(mod->declare_nodes.size(), 2u);
    EXPECT_EQ(mod->declare_schemas.size(), 1u);
}

TEST(Parser, TaskNodesFixture) {
    auto src = read_fixture("task_nodes.d.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
}

TEST(Parser, LevelScriptNodesFixture) {
    auto src = read_fixture("levelscript_nodes.d.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
}

// ─── .gs script files ──────────────────────────────────────────────

TEST(Parser, Import) {
    auto result = parse_source(R"(import "ue_core.d.gs";)");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->imports.size(), 1u);
    EXPECT_EQ(mod->imports[0]->path, "ue_core.d.gs");
}

TEST(Parser, LetDecl) {
    auto result = parse_source(R"(let actor_1 = SoftObjectPath("actor_path_1");)");
    ASSERT_TRUE(result.is_ok());
    auto& mod = result.value();
    ASSERT_EQ(mod->let_decls.size(), 1u);
    EXPECT_EQ(mod->let_decls[0]->name, "actor_1");
    EXPECT_EQ(mod->let_decls[0]->type_name, "SoftObjectPath");
    EXPECT_EQ(mod->let_decls[0]->constructor_arg, "actor_path_1");
}

TEST(Parser, MinimalGraph) {
    auto src = read_fixture("minimal.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& mod = result.value();
    ASSERT_EQ(mod->graphs.size(), 1u);
    auto& g = mod->graphs[0];
    EXPECT_EQ(g->name, "HelloWorld");
    EXPECT_FALSE(g->base_type.has_value());
    EXPECT_EQ(g->params.size(), 1u);
    EXPECT_EQ(g->node_instances.size(), 1u);
    EXPECT_EQ(g->events.size(), 1u);
}

TEST(Parser, GraphWithBaseType) {
    auto src = read_fixture("htn_basic.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& g = result.value()->graphs[0];
    EXPECT_EQ(g->name, "SimpleHTN");
    ASSERT_TRUE(g->base_type.has_value());
    EXPECT_EQ(*g->base_type, "HTNGraph");
}

TEST(Parser, TaskBasicFixture) {
    auto src = read_fixture("task_basic.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& g = result.value()->graphs[0];
    EXPECT_EQ(g->name, "SimpleTask");
    ASSERT_TRUE(g->base_type.has_value());
    EXPECT_EQ(*g->base_type, "TaskGraph");
    EXPECT_EQ(g->params.size(), 2u);
    EXPECT_EQ(g->node_instances.size(), 3u);
}

TEST(Parser, LevelScriptBasicFixture) {
    auto src = read_fixture("levelscript_basic.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
}

TEST(Parser, GraphAsNodeFixture) {
    auto src = read_fixture("graph_as_node.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& mod = result.value();
    ASSERT_EQ(mod->graphs.size(), 2u);
    EXPECT_EQ(mod->graphs[0]->name, "SubRoutine");
    EXPECT_EQ(mod->graphs[1]->name, "MainGraph");
}

TEST(Parser, InvalidConnectionFixture) {
    auto src = read_fixture("invalid_connection.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
}

TEST(Parser, RoundTripFixture) {
    auto src = read_fixture("round_trip.gs");
    ASSERT_FALSE(src.empty());
    auto result = parse_source(src);
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& mod = result.value();
    ASSERT_EQ(mod->let_decls.size(), 1u);
    ASSERT_EQ(mod->graphs.size(), 1u);
    auto& g = mod->graphs[0];
    EXPECT_EQ(g->params.size(), 3u);
    EXPECT_EQ(g->node_instances.size(), 2u);
    EXPECT_EQ(g->events.size(), 1u);
    EXPECT_EQ(g->functions.size(), 1u);
    // Graph-level annotations (was Comment in generate)
    EXPECT_EQ(g->annotations.size(), 1u);
    EXPECT_EQ(g->annotations[0].name, "Comment");
    EXPECT_EQ(g->annotations[0].args.size(), 2u);
    // Node instance annotations (was position in generate)
    EXPECT_EQ(g->node_instances[0]->annotations.size(), 1u);
    EXPECT_EQ(g->node_instances[0]->annotations[0].name, "Position");
    EXPECT_EQ(g->node_instances[1]->annotations.size(), 1u);
    EXPECT_EQ(g->node_instances[1]->annotations[0].name, "Position");
}

TEST(Parser, FlowAndLinkStatements) {
    auto result = parse_source(R"(
Graph Test {
    PrintString p{};
    Delay d{};

    event OnStart {
        context.start(p.enter);
        p.exit(d.enter);
        link p.message = hp;
    }
}
)");
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& ev = result.value()->graphs[0]->events[0];
    ASSERT_EQ(ev->flow_stmts.size(), 2u);
    EXPECT_EQ(ev->flow_stmts[0]->from_node, "context");
    EXPECT_EQ(ev->flow_stmts[0]->from_pin, "start");
    EXPECT_EQ(ev->flow_stmts[0]->to_node, "p");
    EXPECT_EQ(ev->flow_stmts[0]->to_pin, "enter");
    ASSERT_EQ(ev->link_stmts.size(), 1u);
    EXPECT_EQ(ev->link_stmts[0]->target_node, "p");
    EXPECT_EQ(ev->link_stmts[0]->target_pin, "message");
    EXPECT_EQ(ev->link_stmts[0]->source_node, "hp");
    EXPECT_TRUE(ev->link_stmts[0]->source_pin.empty());
}

TEST(Parser, GenerateBlock) {
    auto result = parse_source(R"(
[Comment("c1", "hello")]
Graph Test {
    [Position(X = 100)]
    PrintString p{};
}
)");
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& g = result.value()->graphs[0];
    EXPECT_EQ(g->annotations.size(), 1u);
    EXPECT_EQ(g->annotations[0].name, "Comment");
    EXPECT_EQ(g->annotations[0].args.size(), 2u);
    EXPECT_EQ(g->annotations[0].args[0].value, "c1");
    EXPECT_EQ(g->annotations[0].args[1].value, "hello");
    EXPECT_EQ(g->node_instances[0]->annotations.size(), 1u);
    EXPECT_EQ(g->node_instances[0]->annotations[0].name, "Position");
    EXPECT_EQ(g->node_instances[0]->annotations[0].args.size(), 1u);
    EXPECT_EQ(g->node_instances[0]->annotations[0].args[0].name, "X");
    EXPECT_EQ(g->node_instances[0]->annotations[0].args[0].value, "100");
}

TEST(Parser, ErrorOnBadInput) {
    auto result = parse_source("badtoken @#$");
    EXPECT_TRUE(result.is_err());
}

TEST(Parser, ParamWithDefault) {
    auto result = parse_source(R"(
Graph Test {
    in speed : float = 1.5;
}
)");
    ASSERT_TRUE(result.is_ok()) << result.error();
    auto& p = result.value()->graphs[0]->params[0];
    EXPECT_EQ(p->name, "speed");
    EXPECT_EQ(p->type_name, "float");
    EXPECT_EQ(p->default_value, "1.5");
}
