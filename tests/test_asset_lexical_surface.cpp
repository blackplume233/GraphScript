#include <gtest/gtest.h>

#include "graphscript/asset/language.h"

using namespace gs;

static asset::ParseResult parse_asset_lexer_source(const std::string& source) {
    asset::Parser parser(source, "lexer_asset_test.gs");
    return parser.parse();
}

TEST(AssetLexicalSurface, EmptyAndWhitespaceOnlyInput) {
    auto empty = parse_asset_lexer_source("");
    EXPECT_TRUE(empty.diagnostics.empty());
    EXPECT_TRUE(empty.module.imports.empty());
    EXPECT_TRUE(empty.module.items.blocks.empty());

    auto whitespace = parse_asset_lexer_source("   \n\t  // comment only\n");
    EXPECT_TRUE(whitespace.diagnostics.empty());
    EXPECT_TRUE(whitespace.module.items.blocks.empty());
}

TEST(AssetLexicalSurface, ImportsStringsAndEscapes) {
    auto result = parse_asset_lexer_source(R"(import "ue_core.d.gs";
const text = new LocalizedText {
    value: "with \"escape\"";
}
)");

    ASSERT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.imports.size(), 1u);
    EXPECT_EQ(result.module.imports[0].path, "ue_core.d.gs");
    ASSERT_EQ(result.module.items.consts.size(), 1u);
    ASSERT_EQ(result.module.items.consts[0].properties.size(), 1u);
    EXPECT_EQ(result.module.items.consts[0].properties[0].value.kind, asset::ExprKind::String);
    EXPECT_EQ(result.module.items.consts[0].properties[0].value.text, "with \\\"escape\\\"");
}

TEST(AssetLexicalSurface, NumericBoolNullAssetRefAndCallExpressions) {
    auto result = parse_asset_lexer_source(R"(graph Literals {
    node sample {
        type LiteralNode;
        int_value: -5;
        float_value: 3.14;
        bool_value: true;
        none_value: null;
        asset_value: ref "/Game/Path/Thing";
        call_value: SoftObjectPath("/Game/Other");
    }
}
)");

    ASSERT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.items.blocks.size(), 1u);
    const auto& node = *result.module.items.blocks[0]->items.blocks[0];
    ASSERT_EQ(node.items.directives.size(), 1u);
    ASSERT_EQ(node.items.properties.size(), 6u);
    EXPECT_EQ(node.items.properties[0].value.kind, asset::ExprKind::Int);
    EXPECT_EQ(node.items.properties[0].value.text, "-5");
    EXPECT_EQ(node.items.properties[1].value.kind, asset::ExprKind::Float);
    EXPECT_EQ(node.items.properties[2].value.kind, asset::ExprKind::Bool);
    EXPECT_EQ(node.items.properties[3].value.kind, asset::ExprKind::Null);
    EXPECT_EQ(node.items.properties[4].value.kind, asset::ExprKind::AssetRef);
    EXPECT_EQ(node.items.properties[5].value.kind, asset::ExprKind::Call);
}

TEST(AssetLexicalSurface, SymbolsAttributesArraysAndInlineObjects) {
    auto result = parse_asset_lexer_source(R"(@Comment("title", "Graph")
graph Rich {
    @graph.input
    param target: AActor;
    node apply {
        type ApplyDamage;
        tags: ["combat", "fire"];
        config: { amount: 50; enabled: true; };
    }
}
)");

    ASSERT_TRUE(result.diagnostics.empty());
    const auto& graph = *result.module.items.blocks[0];
    ASSERT_EQ(graph.attributes.size(), 1u);
    EXPECT_EQ(graph.attributes[0].name, "Comment");
    ASSERT_EQ(graph.items.directives.size(), 1u);
    ASSERT_EQ(graph.items.directives[0].attributes.size(), 1u);
    EXPECT_EQ(graph.items.directives[0].attributes[0].name, "graph.input");
    const auto& node = *graph.items.blocks[0];
    ASSERT_EQ(node.items.directives.size(), 1u);
    ASSERT_EQ(node.items.properties.size(), 2u);
    EXPECT_EQ(node.items.properties[0].value.kind, asset::ExprKind::Array);
    EXPECT_EQ(node.items.properties[1].value.kind, asset::ExprKind::InlineObject);
}

TEST(AssetLexicalSurface, ReportsSyntaxErrorsWithRanges) {
    auto result = parse_asset_lexer_source(R"(graph Broken {
    node missing {
        type PrintString
)");

    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics[0].severity, Severity::Error);
    EXPECT_FALSE(result.diagnostics[0].message.empty());
    EXPECT_GT(result.diagnostics[0].range.start.line, 0u);
}
