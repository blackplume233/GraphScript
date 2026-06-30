#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "graphscript/asset/language.h"

using namespace gs;

static std::string read_file(const std::string& dir, const std::string& filename) {
    std::string path = dir + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string read_preset(const std::string& filename) {
    return read_file(GS_PRESETS_DIR, filename);
}

static asset::ParseResult parse_asset_source(const std::string& source, const std::string& name = "parser_asset_test.gs") {
    asset::Parser parser(source, name);
    return parser.parse();
}

TEST(Parser, AssetDeclareTypeObjectSchemaAndCommand) {
    auto result = parse_asset_source(R"(export declare type FVector: constructible;
export declare object PrintString {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;
    @flow.input
    message: FString = "";
}
export declare schema TraceGraph: FlowGraphSchema {
    strict_type_match: true;
}
export declare command connect(from: PinRef, to: PinRef): EdgeRef;
)", "decls.d.gs");

    ASSERT_TRUE(result.diagnostics.empty());
    ASSERT_GE(result.module.symbols.size(), 1u);
    EXPECT_EQ(result.module.symbols[0].kind, "type");
    EXPECT_EQ(result.module.symbols[0].name, "FVector");
    EXPECT_EQ(result.module.symbols[0].base_type, "constructible");
    ASSERT_EQ(result.module.objects.size(), 1u);
    EXPECT_EQ(result.module.objects[0].name, "PrintString");
    ASSERT_EQ(result.module.objects[0].fields.size(), 2u);
    ASSERT_FALSE(result.module.objects[0].fields[0].attributes.empty());
    EXPECT_EQ(result.module.objects[0].fields[0].attributes[0].name, "flow.pin");
    EXPECT_TRUE(result.module.objects[0].fields[1].has_default);
    ASSERT_EQ(result.module.schemas.size(), 1u);
    EXPECT_EQ(result.module.schemas[0].name, "TraceGraph");
    ASSERT_EQ(result.module.commands.size(), 1u);
    EXPECT_EQ(result.module.commands[0].name, "connect");
}

TEST(Parser, AssetPresetsParse) {
    struct ExpectedPreset {
        const char* name;
        size_t min_symbols;
        size_t min_objects;
        size_t schemas;
    };
    const ExpectedPreset presets[] = {
        {"ue_core.d.gs", 10, 3, 0},
        {"htn_nodes.d.gs", 2, 2, 1},
        {"task_nodes.d.gs", 0, 3, 1},
        {"levelscript_nodes.d.gs", 0, 2, 1},
    };

    for (const auto& preset : presets) {
        auto source = read_preset(preset.name);
        ASSERT_FALSE(source.empty()) << preset.name;
        auto result = parse_asset_source(source, preset.name);
        EXPECT_TRUE(result.diagnostics.empty()) << preset.name;
        EXPECT_GE(result.module.symbols.size(), preset.min_symbols) << preset.name;
        EXPECT_GE(result.module.objects.size(), preset.min_objects) << preset.name;
        EXPECT_EQ(result.module.schemas.size(), preset.schemas) << preset.name;
    }
}

TEST(Parser, AssetImportsConstAndGraphBlocks) {
    auto result = parse_asset_source(R"(import "ue_core.d.gs";
const spawn_point = new SoftObjectPath {
    path: "/Game/Spawn";
}
graph Execute {
    schema TraceGraph;
    @graph.input
    param target: AActor;
}
)");

    ASSERT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.imports.size(), 1u);
    EXPECT_EQ(result.module.imports[0].path, "ue_core.d.gs");
    ASSERT_EQ(result.module.items.consts.size(), 1u);
    EXPECT_EQ(result.module.items.consts[0].alias, "spawn_point");
    EXPECT_EQ(result.module.items.consts[0].type, "SoftObjectPath");
    ASSERT_EQ(result.module.items.blocks.size(), 1u);
    EXPECT_EQ(result.module.items.blocks[0]->kind, "graph");
    EXPECT_EQ(result.module.items.blocks[0]->name, "Execute");
}

TEST(Parser, AssetProjectsFlowGraphWithBlocksAndEdges) {
    auto result = parse_asset_source(R"(graph Execute {
    schema AbilityGraph;
    @graph.input
    param target: AActor;
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

    ASSERT_TRUE(result.diagnostics.empty());
    auto projected = asset::FlowGraphProjector::project(result.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    const auto& graph = projected.value();
    EXPECT_EQ(graph.name, "Execute");
    EXPECT_EQ(graph.schema, "AbilityGraph");
    ASSERT_EQ(graph.parameters.size(), 1u);
    EXPECT_EQ(graph.parameters[0].direction, "in");
    ASSERT_EQ(graph.nodes.size(), 2u);
    EXPECT_EQ(graph.nodes[0].alias, "apply");
    ASSERT_EQ(graph.edges.size(), 2u);
    EXPECT_EQ(graph.edges[0].from, "context.start");
    EXPECT_EQ(graph.edges[0].to, "apply.enter");
    ASSERT_EQ(graph.data_edges.size(), 1u);
    EXPECT_EQ(graph.data_edges[0].source, "target");
    EXPECT_EQ(graph.data_edges[0].target, "log.message");
    ASSERT_EQ(graph.blocks.size(), 1u);
    EXPECT_EQ(graph.blocks[0].kind, "event");
}

TEST(Parser, AssetGenerateBlockSourceRanges) {
    auto result = parse_asset_source(R"(graph LayoutGraph {
    node logger {
        type PrintString;
    }
    generate Layout {
        comment(logger, "note");
        metadata(position, logger, x, 100);
    }
}
)");

    ASSERT_TRUE(result.diagnostics.empty());
    auto projected = asset::FlowGraphProjector::project(result.module, "LayoutGraph");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_TRUE(projected.value().generate.has_value());
    const auto& generate = *projected.value().generate;
    EXPECT_EQ(generate.span.range.start.line, 5u);
    EXPECT_EQ(generate.span.range.start.column, 5u);
    ASSERT_EQ(generate.comments.size(), 1u);
    ASSERT_EQ(generate.metadata.size(), 1u);
    EXPECT_EQ(generate.comments[0].instance, "logger");
    EXPECT_EQ(generate.comments[0].text, "note");
    EXPECT_EQ(generate.comments[0].span.range.start.line, 6u);
    EXPECT_EQ(generate.comments[0].span.range.start.column, 9u);
    EXPECT_EQ(generate.metadata[0].scope, "position");
    EXPECT_EQ(generate.metadata[0].value.text, "100");
    EXPECT_EQ(generate.metadata[0].span.range.start.line, 7u);
    EXPECT_EQ(generate.metadata[0].span.range.start.column, 9u);
}

TEST(Parser, AssetReportsSyntaxDiagnostics) {
    auto result = parse_asset_source(R"(graph Broken {
    node missing {
        type PrintString;
)");

    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].severity, Severity::Error);
    EXPECT_FALSE(result.diagnostics[0].message.empty());
}

TEST(Parser, AssetLinterReportsMissingGraphName) {
    auto result = parse_asset_source(R"(graph {
}
)");

    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].severity, Severity::Error);
}
