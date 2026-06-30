#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <sstream>
#include <string>

#include "graphscript/asset/language.h"
#include "graphscript/edit/edit_session.h"
#include "graphscript/runtime/runtime_graph.h"

using namespace gs;

static long long elapsed_ms(const std::function<void()>& fn) {
    auto start = std::chrono::steady_clock::now();
    fn();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

static std::string make_large_asset_graph(int node_count) {
    std::ostringstream os;
    os << "graph Thousand {\n";
    os << "    @graph.input\n";
    os << "    param message: FString;\n";
    for (int i = 0; i < node_count; ++i) {
        os << "    node logger" << i << " {\n";
        os << "        type PrintString;\n";
        os << "        message: message;\n";
        os << "    }\n";
    }
    os << "    event Start {\n";
    if (node_count > 0) {
        os << "        connect(context.start, logger0.enter);\n";
        os << "        bind(message, logger0.message);\n";
    }
    for (int i = 1; i < node_count; ++i) {
        os << "        connect(logger" << (i - 1) << ".exit, logger" << i << ".enter);\n";
        os << "        bind(message, logger" << i << ".message);\n";
    }
    os << "    }\n";
    os << "}\n";
    return os.str();
}

static void load_stress_core(EditSession& session) {
    const std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    auto loaded = session.load_import(path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
}

TEST(QAStress, AssetParserProjectsLargeGraphWithinBudget) {
    const std::string source = make_large_asset_graph(350);
    asset::ParseResult parsed;
    long long parse_ms = elapsed_ms([&] {
        asset::Parser parser(source, "large_asset.gs");
        parsed = parser.parse();
    });
    ASSERT_TRUE(parsed.diagnostics.empty());

    asset::FlowGraph graph;
    long long project_ms = elapsed_ms([&] {
        auto projected = asset::FlowGraphProjector::project(parsed.module, "Thousand");
        ASSERT_TRUE(projected.is_ok()) << projected.error();
        graph = std::move(projected).value();
    });

    EXPECT_EQ(graph.nodes.size(), 350u);
    EXPECT_EQ(graph.edges.size(), 350u);
    EXPECT_EQ(graph.data_edges.size(), 350u);
    EXPECT_LT(parse_ms, 3000);
    EXPECT_LT(project_ms, 3000);
}

TEST(QAStress, EditSessionLoadsEmitsAndReloadsLargeAssetGraph) {
    Environment env;
    EditSession session(env);
    load_stress_core(session);
    const std::string source = make_large_asset_graph(120);

    long long load_ms = elapsed_ms([&] {
        auto loaded = session.load_source(source, "large_session_asset.gs");
        ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    });
    EXPECT_LT(load_ms, 5000);
    ASSERT_EQ(session.module().graphs.size(), 1u);
    EXPECT_EQ(session.module().graphs[0].node_instances.size(), 120u);

    const std::string emitted = session.emit();
    Environment env2;
    EditSession reparsed(env2);
    load_stress_core(reparsed);
    auto loaded = reparsed.load_source(emitted, "large_session_roundtrip.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    EXPECT_EQ(reparsed.module().graphs[0].node_instances.size(), 120u);
}

TEST(QAStress, RuntimeBakeLargeAssetSession) {
    Environment env;
    EditSession session(env);
    load_stress_core(session);
    auto loaded = session.load_source(make_large_asset_graph(80), "large_runtime_asset.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto edit_graph = session.build_edit_graph();
    ASSERT_TRUE(edit_graph.has_value());
    auto runtime = RuntimeGraph::bake(*edit_graph);
    EXPECT_EQ(runtime.node_count(), 80u);
    EXPECT_EQ(runtime.flow_edge_count(), 79u);
    // Current RuntimeGraph bake only materializes node-to-node data links; bare
    // parameter links remain covered by asset projection and Module round-trip.
    EXPECT_EQ(runtime.data_edge_count(), 0u);
}
