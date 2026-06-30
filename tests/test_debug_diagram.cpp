#include <gtest/gtest.h>

#include "graphscript/asset/language.h"
#include "graphscript/debug/diagram.h"
#include "graphscript/edit/edit_session.h"

using namespace gs;

TEST(DebugDiagram, EmitsMermaidFromEditSessionGraph) {
    Environment env;
    EditSession session(env);

    const std::string source = R"(graph Hello {
    @graph.input
    param message: FString;
    node printer {
        type PrintString;
    }
    event OnStart {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }
}
)";

    auto loaded = session.load_source(source, "Hello.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    auto* graph = session.active_graph();
    ASSERT_NE(graph, nullptr);

    const std::string diagram = debug::emit_mermaid_graph_diagram(*graph);
    EXPECT_NE(diagram.find("## Hello"), std::string::npos);
    EXPECT_NE(diagram.find("```mermaid"), std::string::npos);
    EXPECT_NE(diagram.find("IN message : FString"), std::string::npos);
    EXPECT_NE(diagram.find("PrintString\\nprinter"), std::string::npos);
    EXPECT_NE(diagram.find("context ==>|\"start"), std::string::npos);
    EXPECT_NE(diagram.find("message -.->|\"message"), std::string::npos);
}

TEST(DebugDiagram, EmitsMermaidFromAssetFlowGraph) {
    const std::string source = R"(graph Hello {
    schema TraceGraph;
    @graph.input
    param message: FString;
    node printer {
        type PrintString;
    }
    event OnStart {
        connect(context.start, printer.enter);
        bind(message, printer.message);
    }
}
)";

    asset::Parser parser(source, "Hello.gs");
    auto parsed = parser.parse();
    ASSERT_TRUE(parsed.diagnostics.empty());

    auto projected = asset::FlowGraphProjector::project(parsed.module, "Hello");
    ASSERT_TRUE(projected.is_ok()) << projected.error();

    const std::string diagram = debug::emit_mermaid_flow_graph_diagram(projected.value());
    EXPECT_NE(diagram.find("## Hello : TraceGraph"), std::string::npos);
    EXPECT_NE(diagram.find("```mermaid"), std::string::npos);
    EXPECT_NE(diagram.find("in message : FString"), std::string::npos);
    EXPECT_NE(diagram.find("PrintString\\nprinter"), std::string::npos);
    EXPECT_NE(diagram.find("context ==>|\"context.start"), std::string::npos);
    EXPECT_NE(diagram.find("message -.->|\"message"), std::string::npos);
}
