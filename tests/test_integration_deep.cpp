#include <gtest/gtest.h>

#include <string>

#include "graphscript/asset/language.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/graph/runtime_ir.h"

using namespace gs;

static void load_core_preset(EditSession& session) {
    const std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    auto loaded = session.load_import(path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
}

static EditSession load_session_source(Environment& env, const std::string& source) {
    EditSession session(env);
    load_core_preset(session);
    auto loaded = session.load_source(source, "deep_asset.gs");
    EXPECT_TRUE(loaded.is_ok()) << loaded.error();
    return session;
}

static void expect_emit_reloads(EditSession& session) {
    Environment env2;
    EditSession reparsed(env2);
    load_core_preset(reparsed);
    const std::string emitted = session.emit();
    auto loaded = reparsed.load_source(emitted, "deep_asset_roundtrip.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(reparsed.module().graphs.size(), session.module().graphs.size());
    for (size_t i = 0; i < session.module().graphs.size(); ++i) {
        const auto& expected = session.module().graphs[i];
        const auto& actual = reparsed.module().graphs[i];
        EXPECT_EQ(actual.name, expected.name);
        EXPECT_EQ(actual.parameters.size(), expected.parameters.size());
        EXPECT_EQ(actual.node_instances.size(), expected.node_instances.size());
        EXPECT_EQ(actual.events.size(), expected.events.size());
        EXPECT_EQ(actual.functions.size(), expected.functions.size());
        EXPECT_EQ(actual.annotations.size(), expected.annotations.size());
        EXPECT_EQ(actual.generate.has_value(), expected.generate.has_value());
        if (!expected.events.empty()) {
            ASSERT_FALSE(actual.events.empty());
            EXPECT_EQ(actual.events[0].flow_connections.size(), expected.events[0].flow_connections.size());
            EXPECT_EQ(actual.events[0].data_links.size(), expected.events[0].data_links.size());
        }
    }
}

TEST(DeepCycle, AssetThreeGraphChainProjectsAndDerivesNodes) {
    Environment env;
    const std::string source = R"(graph Leaf {
    @graph.input
    param value: int;
    @graph.output
    param result: int;
    event Execute {
    }
}
graph Middle {
    node leaf {
        type Leaf;
    }
    event Execute {
        connect(context.start, leaf.Execute);
    }
}
graph Root {
    node middle {
        type Middle;
    }
    event Start {
        connect(context.start, middle.Execute);
    }
}
)";
    auto session = load_session_source(env, source);

    ASSERT_EQ(session.module().graphs.size(), 3u);
    EXPECT_NE(env.nodes().find("Leaf"), nullptr);
    EXPECT_NE(env.nodes().find("Middle"), nullptr);
    EXPECT_NE(env.nodes().find("Root"), nullptr);
    expect_emit_reloads(session);
}

TEST(DeepCycle, AssetFlowGraphProjectionKeepsFlowAndDataEdges) {
    asset::Parser parser(R"(graph Execute {
    schema AbilityGraph;
    @graph.input
    param target: AActor;
    node location {
        type GetActorLocation;
    }
    node logger {
        type PrintString;
    }
    event Start {
        connect(context.start, logger.enter);
        bind(target, location.target);
        bind(location.location, logger.message);
    }
}
)", "projection_asset.gs");
    auto parsed = parser.parse();
    ASSERT_TRUE(parsed.diagnostics.empty());

    auto projected = asset::FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    const auto& graph = projected.value();
    EXPECT_EQ(graph.name, "Execute");
    ASSERT_EQ(graph.nodes.size(), 2u);
    ASSERT_EQ(graph.edges.size(), 1u);
    EXPECT_EQ(graph.edges[0].from, "context.start");
    EXPECT_EQ(graph.edges[0].to, "logger.enter");
    ASSERT_EQ(graph.data_edges.size(), 2u);
    EXPECT_EQ(graph.data_edges[0].source, "target");
    EXPECT_EQ(graph.data_edges[0].target, "location.target");
}

TEST(DeepCycle, GraphRuntimeIRBakeFromAssetProjection) {
    Environment env;
    const std::string source = R"(graph RuntimeReady {
    node logger {
        type PrintString;
    }
    node wait {
        type Delay;
    }
    event Start {
        connect(context.start, logger.enter);
        connect(logger.exit, wait.enter);
    }
}
)";
    auto session = load_session_source(env, source);

    auto edit_graph = session.build_edit_graph();
    ASSERT_TRUE(edit_graph.has_value());
    EXPECT_EQ(edit_graph->node_count(), 2u);
    EXPECT_EQ(edit_graph->connection_count(), 1u);

    asset::Parser parser(session.emit(), "runtime_deep_asset.gs");
    auto parsed = parser.parse();
    ASSERT_TRUE(parsed.diagnostics.empty());
    auto projected = asset::FlowGraphProjector::project(parsed.module, "RuntimeReady");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    auto runtime = GraphRuntimeIR::bake(projected.value());
    EXPECT_EQ(runtime.name(), "RuntimeReady");
    EXPECT_EQ(runtime.node_count(), 2u);
    EXPECT_EQ(runtime.flow_edge_count(), 1u);
}

TEST(DeepCycle, AssetGenerateAndAnnotationsRoundTrip) {
    Environment env;
    const std::string source = R"(@Comment("title", "Deep")
graph Annotated {
    @graph.input
    param message: FString = SoftObjectPath("Default");
    @Position(X = 10, Y = 20)
    node logger {
        type PrintString;
        message: message;
    }
    generate Layout {
        comment(logger, "note");
        metadata(position, logger, x, 100);
    }
}
)";
    auto session = load_session_source(env, source);

    const auto& graph = session.module().graphs[0];
    ASSERT_EQ(graph.annotations.size(), 1u);
    ASSERT_EQ(graph.parameters.size(), 1u);
    ASSERT_EQ(graph.node_instances.size(), 1u);
    ASSERT_TRUE(graph.generate.has_value());
    EXPECT_EQ(graph.generate->comments.size(), 1u);
    expect_emit_reloads(session);
}

TEST(DeepCycle, AssetSchemaDefaultsAndMultipleBlocksRoundTrip) {
    Environment env;
    const std::string source = R"(@Comment("title", "Ability")
graph Ability {
    schema AbilityGraph;
    @graph.input
    param target: AActor;
    @graph.var
    param cooldown: float = 1.0;
    node startLog {
        type PrintString;
        message: "start";
    }
    node endLog {
        type PrintString;
        message: "end";
    }
    event Begin {
        connect(context.start, startLog.enter);
        bind(target, startLog.message);
    }
    event Finish {
        connect(startLog.exit, endLog.enter);
        bind(cooldown, endLog.message);
    }
    function Compute {
        bind(cooldown, context.result);
    }
}
)";
    auto session = load_session_source(env, source);
    const auto& graph = session.module().graphs[0];
    EXPECT_EQ(graph.base_type.value_or(""), "AbilityGraph");
    EXPECT_EQ(graph.parameters.size(), 2u);
    EXPECT_EQ(graph.events.size(), 2u);
    EXPECT_EQ(graph.functions.size(), 1u);
    expect_emit_reloads(session);
}
