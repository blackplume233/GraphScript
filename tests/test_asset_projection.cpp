#include <gtest/gtest.h>

#include "graphscript/asset/language.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/registry/environment.h"

using namespace gs;

static asset::ParseResult parse_asset_compiler_source(const std::string& source,
                                                      const std::string& name = "compiler_asset_test.gs") {
    asset::Parser parser(source, name);
    return parser.parse();
}

static void load_core_for_compiler(EditSession& session) {
    const std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    auto loaded = session.load_import(path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
}

TEST(AssetProjection, ImportPopulatesEnvironmentTypesAndNodes) {
    Environment env;
    EditSession session(env);
    load_core_for_compiler(session);

    auto* vector_type = env.types().find("FVector");
    ASSERT_NE(vector_type, nullptr);
    EXPECT_TRUE(vector_type->constructible);
    auto* fname = env.types().find("FName");
    ASSERT_NE(fname, nullptr);
    EXPECT_FALSE(fname->constructible);
    EXPECT_NE(env.types().find("FString"), nullptr);
    EXPECT_NE(env.types().find("SoftObjectPath"), nullptr);

    auto* print = env.nodes().find("PrintString");
    ASSERT_NE(print, nullptr);
    EXPECT_TRUE(print->is_native);
    EXPECT_EQ(print->pins.size(), 3u);
    EXPECT_NE(print->find_pin("enter"), nullptr);
    EXPECT_NE(print->find_pin("exit"), nullptr);
    EXPECT_NE(print->find_pin("message"), nullptr);
    ASSERT_FALSE(print->fields.empty());
    EXPECT_EQ(print->fields[0].name, "message");
    EXPECT_EQ(print->fields[0].type_name, "FString");

    auto* delay = env.nodes().find("Delay");
    ASSERT_NE(delay, nullptr);
    EXPECT_EQ(delay->pins.size(), 3u);
    ASSERT_FALSE(delay->fields.empty());
    EXPECT_EQ(delay->fields[0].name, "duration");
    EXPECT_EQ(delay->fields[0].type_name, "float");
}

TEST(AssetProjection, ImportPopulatesSchemas) {
    Environment env;
    EditSession session(env);
    load_core_for_compiler(session);

    const std::string htn_path = std::string(GS_PRESETS_DIR) + "/htn_nodes.d.gs";
    auto loaded = session.load_import(htn_path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto* schema = env.schemas().find("HTNGraph");
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->connection_policy.max_exec_fan_out, -1);
    EXPECT_FALSE(schema->connection_policy.allow_exec_fan_in);
    EXPECT_TRUE(schema->connection_policy.strict_type_match);
    ASSERT_FALSE(schema->fields.empty());
    EXPECT_EQ(schema->fields[0].name, "max_exec_fan_out");
    EXPECT_EQ(schema->fields[0].value, "unlimited");
}

TEST(AssetProjection, FlowGraphProjectionCapturesSchemaParamsNodesAndEdges) {
    auto parsed = parse_asset_compiler_source(R"(graph Execute {
    schema TraceGraph;
    @graph.input
    param target: AActor;
    @graph.output
    param result: bool;
    node apply {
        type ApplyDamage;
        amount: 50;
    }
    node log {
        type PrintString;
    }
    event Start {
        connect(context.start, apply.enter);
        connect(apply.exit, log.enter);
        bind(target, log.message);
    }
}
)");

    ASSERT_TRUE(parsed.diagnostics.empty());
    auto projected = asset::FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    const auto& graph = projected.value();
    EXPECT_EQ(graph.name, "Execute");
    EXPECT_EQ(graph.schema, "TraceGraph");
    ASSERT_EQ(graph.parameters.size(), 2u);
    EXPECT_EQ(graph.parameters[0].name, "target");
    EXPECT_EQ(graph.parameters[0].direction, "in");
    EXPECT_EQ(graph.parameters[1].direction, "out");
    ASSERT_EQ(graph.nodes.size(), 2u);
    EXPECT_EQ(graph.nodes[0].type, "ApplyDamage");
    ASSERT_FALSE(graph.nodes[0].properties.empty());
    EXPECT_EQ(graph.nodes[0].properties[0].path, "amount");
    ASSERT_EQ(graph.edges.size(), 2u);
    ASSERT_EQ(graph.data_edges.size(), 1u);
}

TEST(AssetProjection, EditSessionDerivesGraphAsNode) {
    Environment env;
    EditSession session(env);
    load_core_for_compiler(session);

    const std::string source = R"(graph Child {
    @graph.input
    param msg: FString;
    @graph.output
    param result: bool;
    @graph.var
    param scratch: float;
    event Run {
    }
}
graph Parent {
    node child {
        type Child;
    }
    event Start {
        connect(context.start, child.Run);
    }
}
)";

    auto loaded = session.load_source(source, "graph_as_node_asset.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto* child = env.nodes().find("Child");
    ASSERT_NE(child, nullptr);
    EXPECT_FALSE(child->is_native);
    EXPECT_EQ(child->source_graph, "Child");
    ASSERT_EQ(child->pins.size(), 3u);
    auto* msg = child->find_pin("msg");
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->kind, PinKind::Data);
    EXPECT_EQ(msg->direction, PinDirection::Input);
    auto* result = child->find_pin("result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->kind, PinKind::Data);
    EXPECT_EQ(result->direction, PinDirection::Output);
    auto* run = child->find_pin("Run");
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run->kind, PinKind::Exec);
    EXPECT_EQ(run->direction, PinDirection::Input);
    EXPECT_EQ(child->find_pin("scratch"), nullptr);
}

TEST(AssetProjection, EditSessionReportsUnknownEventReference) {
    Environment env;
    EditSession session(env);

    const std::string source = R"(graph Test {
    event Run {
        connect(missing.enter, context.done);
    }
}
)";

    auto loaded = session.load_source(source, "bad_event_ref.gs");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Unknown source node"), std::string::npos);
}

TEST(AssetProjection, EditSessionReportsFunctionNodeReference) {
    Environment env;
    EditSession session(env);

    const std::string source = R"(graph Test {
    node printer {
        type PrintString;
    }
    function Compute {
        connect(printer.exit, context.done);
    }
}
)";

    auto loaded = session.load_source(source, "bad_function_ref.gs");
    ASSERT_TRUE(loaded.is_err());
    EXPECT_NE(loaded.error().find("Unknown source node"), std::string::npos);
}
