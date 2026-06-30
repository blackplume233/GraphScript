#include <gtest/gtest.h>

#include <string>

#include "graphscript/asset/language.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/graph/runtime_ir.h"

using namespace gs;

static void load_blueprint_presets(EditSession& session) {
    for (const char* preset : {"ue_core.d.gs", "ue_blueprint.d.gs"}) {
        const std::string path = std::string(GS_PRESETS_DIR) + "/" + preset;
        auto loaded = session.load_import(path);
        ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    }
}

static EditSession load_blueprint_source(Environment& env, const std::string& source) {
    EditSession session(env);
    load_blueprint_presets(session);
    auto loaded = session.load_source(source, "blueprint_asset.gs");
    EXPECT_TRUE(loaded.is_ok()) << loaded.error();
    return session;
}

TEST(Blueprint, AssetGameplayEventRoundTrip) {
    Environment env;
    const std::string source = R"(graph GameplayEvent {
    @graph.input
    param target: AActor;
    node location {
        type GetActorLocation;
    }
    node logger {
        type PrintString;
    }
    event BeginPlay {
        connect(context.start, logger.enter);
        bind(target, location.target);
        bind(location.location, logger.message);
    }
}
)";
    auto session = load_blueprint_source(env, source);

    ASSERT_EQ(session.module().graphs.size(), 1u);
    EXPECT_EQ(session.module().graphs[0].events[0].data_links.size(), 2u);
    const std::string emitted = session.emit();

    Environment env2;
    auto reloaded = load_blueprint_source(env2, emitted);
    ASSERT_EQ(reloaded.module().graphs.size(), session.module().graphs.size());
    EXPECT_EQ(reloaded.module().graphs[0].events[0].flow_connections.size(),
              session.module().graphs[0].events[0].flow_connections.size());
    EXPECT_EQ(reloaded.module().graphs[0].events[0].data_links.size(),
              session.module().graphs[0].events[0].data_links.size());
    EXPECT_EQ(reloaded.module().graphs[0].node_instances.size(), 2u);
}

TEST(Blueprint, AssetMathBranchProjection) {
    asset::Parser parser(R"(graph DamageMath {
    @graph.input
    param base_damage: float;
    node scale {
        type Multiply_Float;
        A: base_damage;
        B: 2.0;
    }
    node clamp {
        type Clamp_Float;
        Value: scale.Result;
        Min: 0.0;
        Max: 100.0;
    }
    event Compute {
        bind(base_damage, scale.A);
        bind(scale.Result, clamp.Value);
    }
}
)", "blueprint_math.gs");
    auto parsed = parser.parse();
    ASSERT_TRUE(parsed.diagnostics.empty());

    auto projected = asset::FlowGraphProjector::project(parsed.module, "DamageMath");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    EXPECT_EQ(projected.value().nodes.size(), 2u);
    EXPECT_EQ(projected.value().data_edges.size(), 2u);
}

TEST(Blueprint, AssetRuntimeBakeUsesBlueprintPresets) {
    Environment env;
    const std::string source = R"(graph RuntimeBlueprint {
    node logger {
        type PrintString;
    }
    node delay {
        type Delay;
    }
    event BeginPlay {
        connect(context.start, logger.enter);
        connect(logger.exit, delay.enter);
    }
}
)";
    auto session = load_blueprint_source(env, source);

    asset::Parser parser(session.emit(), "runtime_blueprint_asset.gs");
    auto parsed = parser.parse();
    ASSERT_TRUE(parsed.diagnostics.empty());
    auto projected = asset::FlowGraphProjector::project(parsed.module, "RuntimeBlueprint");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    auto runtime = GraphRuntimeIR::bake(projected.value());
    EXPECT_EQ(runtime.node_count(), 2u);
    EXPECT_EQ(runtime.flow_edge_count(), 1u);
}

TEST(Blueprint, AssetMultiGraphBlueprintRoundTrip) {
    Environment env;
    const std::string source = R"(graph DamageSubgraph {
    @graph.input
    param value: float;
    @graph.output
    param result: float;
    node clamp {
        type Clamp_Float;
        Value: value;
        Min: 0.0;
        Max: 100.0;
    }
    event Execute {
        bind(value, clamp.Value);
    }
}
graph PlayerAbility {
    @graph.input
    param target: AActor;
    node damage {
        type DamageSubgraph;
    }
    node logger {
        type PrintString;
    }
    event Activate {
        connect(context.start, damage.Execute);
        bind(target, logger.message);
    }
}
)";
    auto session = load_blueprint_source(env, source);
    EXPECT_NE(env.nodes().find("DamageSubgraph"), nullptr);
    EXPECT_NE(env.nodes().find("PlayerAbility"), nullptr);

    const std::string emitted = session.emit();
    Environment env2;
    auto reloaded = load_blueprint_source(env2, emitted);
    ASSERT_EQ(reloaded.module().graphs.size(), 2u);
    EXPECT_EQ(reloaded.module().graphs[1].node_instances.size(), 2u);
    EXPECT_EQ(reloaded.module().graphs[1].events[0].flow_connections.size(), 1u);
    EXPECT_EQ(reloaded.module().graphs[1].events[0].data_links.size(), 1u);
}
