#include <gtest/gtest.h>

#include "graphscript/asset/language.h"
#include "graphscript/debug/dump.h"
#include "graphscript/graph/runtime_ir.h"

using namespace gs;

static const char* kRuntimeDecls = R"(export declare object PrintString {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
    @flow.input
    message: FString;
}
export declare object Delay {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.pin(kind = "exec", direction = "out")
    completed: Exec;
    @flow.input
    duration: float;
}
export declare object DataSource {
    @flow.output
    value: FString;
}
export declare schema TaskGraph: FlowGraphSchema {
}
)";

static Result<GraphRuntimeIR, std::string> bake_runtime_ir(const std::string& source,
                                                           const std::string& graph_name = "Test") {
    asset::Parser parser(std::string(kRuntimeDecls) + source, "runtime_ir_asset.gs");
    auto parsed = parser.parse();
    if (!parsed.diagnostics.empty()) return Result<GraphRuntimeIR, std::string>::err(parsed.diagnostics.front().message);

    auto projected = asset::FlowGraphProjector::project(parsed.module, graph_name);
    if (projected.is_err()) return Result<GraphRuntimeIR, std::string>::err(projected.error());

    return Result<GraphRuntimeIR, std::string>::ok(GraphRuntimeIR::bake(projected.value()));
}

TEST(GraphRuntimeIR, BakeEmpty) {
    auto rt = bake_runtime_ir(R"(graph Empty {
}
)", "Empty");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_EQ(rt.value().name(), "Empty");
    EXPECT_EQ(rt.value().node_count(), 0u);
    EXPECT_EQ(rt.value().flow_edge_count(), 0u);
    EXPECT_EQ(rt.value().data_edge_count(), 0u);
}

TEST(GraphRuntimeIR, BakeWithNodes) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node a {
        type PrintString;
    }
    node b {
        type Delay;
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_EQ(rt.value().node_count(), 2u);
    EXPECT_EQ(rt.value().pins().size(), 6u);
}

TEST(GraphRuntimeIR, BakePreservesNodeData) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node printer {
        type PrintString;
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    auto* rn = rt.value().find_node("printer");
    ASSERT_NE(rn, nullptr);
    EXPECT_EQ(rn->type_name, "PrintString");
    EXPECT_EQ(rn->instance_name, "printer");
    EXPECT_EQ(rn->pin_count, 3u);
}

TEST(GraphRuntimeIR, BakeFlowEdges) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node a {
        type PrintString;
    }
    node b {
        type Delay;
    }
    event Start {
        connect(a.exit, b.enter);
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_EQ(rt.value().flow_edge_count(), 1u);
    auto& fe = rt.value().flow_edges()[0];
    EXPECT_EQ(rt.value().nodes()[fe.from_node].instance_name, "a");
    EXPECT_EQ(rt.value().nodes()[fe.to_node].instance_name, "b");
}

TEST(GraphRuntimeIR, BakeDataEdges) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node src {
        type DataSource;
    }
    node printer {
        type PrintString;
    }
    event Start {
        bind(src.value, printer.message);
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_EQ(rt.value().data_edge_count(), 1u);
    auto& de = rt.value().data_edges()[0];
    EXPECT_EQ(rt.value().nodes()[de.source_node].instance_name, "src");
    EXPECT_EQ(rt.value().nodes()[de.target_node].instance_name, "printer");
}

TEST(GraphRuntimeIR, BakeWithSchema) {
    auto rt = bake_runtime_ir(R"(graph TaskTest {
    schema TaskGraph;
    node a {
        type PrintString;
    }
}
)", "TaskTest");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_EQ(rt.value().domain_name(), "TaskGraph");
}

TEST(GraphRuntimeIR, FindNode) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node p {
        type PrintString;
    }
    node d {
        type Delay;
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    EXPECT_NE(rt.value().find_node("p"), nullptr);
    EXPECT_NE(rt.value().find_node("d"), nullptr);
    EXPECT_EQ(rt.value().find_node("nonexistent"), nullptr);
}

TEST(GraphRuntimeIR, FindPinIndex) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node p {
        type PrintString;
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    auto* rn = rt.value().find_node("p");
    ASSERT_NE(rn, nullptr);
    uint32_t idx = static_cast<uint32_t>(rn - &rt.value().nodes()[0]);

    auto enter_idx = rt.value().find_pin_index(idx, "enter");
    EXPECT_NE(enter_idx, 0xFF);

    auto bad_idx = rt.value().find_pin_index(idx, "nonexistent");
    EXPECT_EQ(bad_idx, 0xFF);
}

TEST(GraphRuntimeIR, PinData) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node p {
        type PrintString;
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    auto& pins = rt.value().pins();
    ASSERT_EQ(pins.size(), 3u);
    EXPECT_EQ(pins[0].name, "enter");
    EXPECT_EQ(pins[0].kind, 0);
    EXPECT_EQ(pins[0].direction, 0);
    EXPECT_EQ(pins[1].name, "exit");
    EXPECT_EQ(pins[1].direction, 1);
    EXPECT_EQ(pins[2].name, "message");
    EXPECT_EQ(pins[2].kind, 1);
}

TEST(GraphRuntimeIR, DebugDumpUsesRuntimeIRTerminology) {
    auto rt = bake_runtime_ir(R"(graph Test {
    node src {
        type DataSource;
    }
    node printer {
        type PrintString;
    }
    event Start {
        bind(src.value, printer.message);
    }
}
)");
    ASSERT_TRUE(rt.is_ok()) << rt.error();
    const std::string dump = debug::dump_graph_runtime_ir(rt.value());
    EXPECT_NE(dump.find("GraphRuntimeIR \"Test\""), std::string::npos);
    EXPECT_NE(dump.find("nodes (2)"), std::string::npos);
    EXPECT_NE(dump.find("data_edges (1)"), std::string::npos);
}
