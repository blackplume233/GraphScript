#include <gtest/gtest.h>

#include "graphscript/asset/language.h"

using namespace gs;
using namespace gs::asset;

static const char* kDecl = R"(
import "core.d.gs";

declare module "ability.core" {
    package: "Game.Ability";
    version: "1.0.0";
}

export declare type Actor;
export declare type Exec;
export declare type float;
export declare type FString;

export declare enum DamageType {
    Fire;
    Ice = "ice";
}

export declare kind block graph;
export declare kind block node;
export declare kind block event;

export declare block graph {
    allows object ApplyDamage;
    allows command connect;
}

export declare command connect(from: PinRef, to: PinRef): EdgeRef;

export declare schema AbilityGraph: FlowGraphSchema {
    max_exec_fan_out: 1;
    allow_exec_fan_in: false;
}

export declare lint AbilityGraphLint for AbilityGraph;

export declare object ApplyDamage {
    @flow.input
    target: Actor;

    @flow.input
    amount: float;

    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.pin(kind = "exec", direction = "out")
    exit: Exec;
}

export declare object PrintString {
    @flow.pin(kind = "exec", direction = "in")
    enter: Exec;

    @flow.input
    message: FString;
}
)";

static const char* kGraph = R"(
graph Execute {
    schema AbilityGraph;
    @graph.input
    param target: Actor;
    @graph.input
    param amount: float = 50.0;

    node apply {
        type ApplyDamage;
        amount: 50;
        editor.pos: [100, 100];
        target = context.target;
        metadata: { x: 1; y: ref "/Game/Ability"; };
    }

    node log {
        type PrintString;
        message: "done";
    }

    event Start {
        connect(context.start, apply.enter);
        connect(apply.exit, log.enter);
        bind(target, log.message);
    }
}
)";

TEST(AssetLanguage, ParsesDeclarationModuleAndObjectPins) {
    Parser parser(kDecl, "ability_core.d.gs");
    auto result = parser.parse();

    EXPECT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.imports.size(), 1u);
    EXPECT_EQ(result.module.imports[0].path, "core.d.gs");
    ASSERT_EQ(result.module.modules.size(), 1u);
    EXPECT_EQ(result.module.modules[0].id, "ability.core");
    EXPECT_EQ(result.module.enums.size(), 1u);
    EXPECT_EQ(result.module.block_kinds.size(), 1u);
    EXPECT_EQ(result.module.commands.size(), 1u);
    ASSERT_EQ(result.module.commands[0].parameters.size(), 2u);
    EXPECT_EQ(result.module.schemas.size(), 1u);
    EXPECT_EQ(result.module.lints.size(), 1u);
    ASSERT_EQ(result.module.objects.size(), 2u);
    EXPECT_EQ(result.module.objects[0].name, "ApplyDamage");
    ASSERT_EQ(result.module.objects[0].fields.size(), 4u);
    EXPECT_EQ(result.module.objects[0].fields[2].attributes[0].name, "flow.pin");
}

TEST(AssetLanguage, BuildsModuleGraphAndExports) {
    Parser parser(kDecl, "ability_core.d.gs");
    auto result = parser.parse();

    ModuleGraph graph;
    auto diagnostics = Linter::lint(result.module, &graph);

    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(graph.module_id, "ability.core");
    EXPECT_FALSE(graph.module_id_inferred);
    ASSERT_EQ(graph.imports.size(), 1u);
    EXPECT_EQ(graph.imports[0], "core.d.gs");
    EXPECT_GE(graph.exports.size(), 4u);
}

TEST(AssetLanguage, ParsesTypeBaseAndAllowsFieldPinNamePair) {
    const std::string source = R"(
export declare type FVector: constructible;

export declare object Delay {
    duration: float = 0.2;

    @flow.input
    duration: float;
}
)";

    Parser parser(source, "preset.d.gs");
    auto result = parser.parse();
    EXPECT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.symbols.size(), 2u);
    EXPECT_EQ(result.module.symbols[0].kind, "type");
    EXPECT_EQ(result.module.symbols[0].name, "FVector");
    EXPECT_EQ(result.module.symbols[0].base_type, "constructible");
    EXPECT_EQ(result.module.symbols[0].name_span.range.start.line, 2u);
    EXPECT_EQ(result.module.symbols[0].name_span.range.start.column, 21u);

    auto diagnostics = Linter::lint(result.module);
    EXPECT_TRUE(diagnostics.empty());
}

TEST(AssetLanguage, RejectsDuplicateFieldsWithSameRole) {
    const std::string source = R"(
export declare object Broken {
    @flow.input
    value: float;

    @flow.output
    value: float;
}
)";

    Parser parser(source, "broken.d.gs");
    auto result = parser.parse();
    EXPECT_TRUE(result.diagnostics.empty());

    auto diagnostics = Linter::lint(result.module);
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].code, "GS-LINT-002");
}

TEST(AssetLanguage, ParsesGraphBlockNodeObjectsAndCalls) {
    Parser parser(kGraph, "Fireball.gs");
    auto result = parser.parse();

    EXPECT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.items.blocks.size(), 1u);
    const auto& graph = *result.module.items.blocks[0];
    EXPECT_EQ(graph.kind, "graph");
    EXPECT_EQ(graph.name, "Execute");
    ASSERT_EQ(graph.items.directives.size(), 3u);
    EXPECT_EQ(graph.items.directives[0].name, "schema");
    EXPECT_EQ(graph.items.directives[0].args[0].text, "AbilityGraph");
    EXPECT_EQ(graph.items.directives[1].name, "param");
    ASSERT_EQ(graph.items.directives[1].attributes.size(), 1u);
    EXPECT_EQ(graph.items.directives[1].attributes[0].name, "graph.input");
    ASSERT_EQ(graph.items.directives[1].parameters.size(), 1u);
    EXPECT_EQ(graph.items.directives[1].parameters[0].name, "target");
    ASSERT_EQ(graph.items.blocks.size(), 3u);
    const auto& apply = *graph.items.blocks[0];
    EXPECT_EQ(apply.kind, "node");
    EXPECT_EQ(apply.name, "apply");
    ASSERT_EQ(apply.items.directives.size(), 1u);
    EXPECT_EQ(apply.items.directives[0].name, "type");
    EXPECT_EQ(apply.items.directives[0].args[0].text, "ApplyDamage");
    ASSERT_EQ(apply.items.properties.size(), 3u);
    EXPECT_EQ(apply.items.properties[0].path, "amount");
    EXPECT_EQ(apply.items.properties[0].value.text, "50");
    ASSERT_EQ(apply.items.assignments.size(), 1u);
    EXPECT_EQ(apply.items.assignments[0].target, "target");
    EXPECT_EQ(apply.items.assignments[0].value.text, "context.target");
    EXPECT_EQ(apply.items.properties[2].value.kind, ExprKind::InlineObject);
    ASSERT_EQ(apply.items.properties[2].value.properties.size(), 2u);
    EXPECT_EQ(apply.items.properties[2].value.properties[1].value.kind, ExprKind::AssetRef);
    EXPECT_EQ(apply.items.properties[2].value.properties[1].value.text, "/Game/Ability");
    const auto& start = *graph.items.blocks[2];
    EXPECT_EQ(start.kind, "event");
    ASSERT_EQ(start.items.calls.size(), 3u);
    EXPECT_EQ(start.items.calls[1].args[1].text, "log.enter");
    EXPECT_EQ(start.items.calls[2].callee_parts[0], "bind");
}

TEST(AssetLanguage, ProjectsFlowGraphWithPinsAndEdges) {
    Parser decl_parser(kDecl, "ability_core.d.gs");
    auto decl = decl_parser.parse();
    Parser graph_parser(kGraph, "Fireball.gs");
    auto graph_module = graph_parser.parse();

    for (auto& object : decl.module.objects) {
        graph_module.module.objects.push_back(std::move(object));
    }

    auto projected = FlowGraphProjector::project(graph_module.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    const auto& graph = projected.value();

    EXPECT_EQ(graph.name, "Execute");
    ASSERT_EQ(graph.parameters.size(), 2u);
    EXPECT_EQ(graph.parameters[0].name, "target");
    EXPECT_EQ(graph.parameters[0].direction, "in");
    EXPECT_TRUE(graph.parameters[0].span.range.end.line > graph.parameters[0].span.range.start.line ||
                graph.parameters[0].span.range.end.column > graph.parameters[0].span.range.start.column);
    EXPECT_EQ(graph.parameters[1].name, "amount");
    EXPECT_TRUE(graph.parameters[1].has_default);
    ASSERT_EQ(graph.nodes.size(), 2u);
    EXPECT_EQ(graph.nodes[0].alias, "apply");
    EXPECT_EQ(graph.nodes[0].pins.size(), 4u);
    ASSERT_EQ(graph.edges.size(), 2u);
    EXPECT_EQ(graph.edges[0].from, "context.start");
    EXPECT_EQ(graph.edges[0].to, "apply.enter");
    ASSERT_EQ(graph.data_edges.size(), 1u);
    EXPECT_EQ(graph.data_edges[0].source, "target");
    EXPECT_EQ(graph.data_edges[0].target, "log.message");
    EXPECT_TRUE(graph.data_edges[0].span.range.end.line > graph.data_edges[0].span.range.start.line ||
                graph.data_edges[0].span.range.end.column > graph.data_edges[0].span.range.start.column);
    ASSERT_EQ(graph.blocks.size(), 1u);
    EXPECT_EQ(graph.blocks[0].kind, "event");
    EXPECT_EQ(graph.blocks[0].name, "Start");
    EXPECT_TRUE(graph.blocks[0].span.range.end.line > graph.blocks[0].span.range.start.line ||
                graph.blocks[0].span.range.end.column > graph.blocks[0].span.range.start.column);
    EXPECT_EQ(graph.blocks[0].edges.size(), 2u);
    EXPECT_EQ(graph.blocks[0].data_edges.size(), 1u);
    EXPECT_EQ(graph.edges.size(), graph.blocks[0].edges.size());
    EXPECT_EQ(graph.data_edges.size(), graph.blocks[0].data_edges.size());
    EXPECT_TRUE(graph.diagnostics.empty());
}

TEST(AssetLanguage, ReportsInvalidDataEdgeButKeepsProjection) {
    const std::string source = R"(
graph Execute {
    node apply {
        type ApplyDamage;
    }
    bind(missing.value, apply.amount);
}
)";
    Parser parser(source, "BrokenData.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().data_edges.size(), 1u);
    EXPECT_FALSE(projected.value().data_edges[0].valid);
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-004");
}

TEST(AssetLanguage, ReportsInvalidDataTargetButKeepsProjection) {
    const std::string source = R"(
graph Execute {
    node apply {
        type ApplyDamage;
    }
    bind(apply.amount, missing.value);
}
)";
    Parser parser(source, "BrokenDataTarget.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().data_edges.size(), 1u);
    EXPECT_FALSE(projected.value().data_edges[0].valid);
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-005");
}

TEST(AssetLanguage, ReportsInvalidFlowEdgeButKeepsProjection) {
    const std::string source = R"(
graph Execute {
    node apply {
        type ApplyDamage;
        amount: 50;
    }
    connect(missing.exit, apply.enter);
}
)";
    Parser parser(source, "Broken.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().edges.size(), 1u);
    EXPECT_FALSE(projected.value().edges[0].valid);
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-002");
}

TEST(AssetLanguage, FunctionBlockCannotReferenceGraphNodeAlias) {
    const std::string source = R"(
graph Execute {
    @graph.input
    param target: Actor;

    node apply {
        type ApplyDamage;
    }

    function Compute {
        connect(apply.exit, context.done);
        bind(apply.amount, target);
    }
}
)";
    Parser parser(source, "FunctionScope.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().blocks.size(), 1u);
    EXPECT_EQ(projected.value().blocks[0].kind, "function");
    ASSERT_EQ(projected.value().edges.size(), 1u);
    ASSERT_EQ(projected.value().data_edges.size(), 1u);
    EXPECT_FALSE(projected.value().edges[0].valid);
    EXPECT_FALSE(projected.value().data_edges[0].valid);
    ASSERT_GE(projected.value().diagnostics.size(), 2u);
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-002");
    EXPECT_EQ(projected.value().diagnostics[1].code, "GS-FLW-004");
}

TEST(AssetLanguage, ReportsConflictingGraphParameterDirectionAttributes) {
    const std::string source = R"(
graph Execute {
    @graph.input
    @graph.output
    param target: Actor;
}
)";
    Parser parser(source, "ConflictingParamDirection.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().parameters.size(), 1u);
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-006");
}

TEST(AssetLanguage, LinterReportsDuplicateNodeBlockAlias) {
    const std::string duplicate_nodes = R"(
graph Execute {
    node apply {
        type ApplyDamage;
    }
    node apply {
        type PrintString;
    }
}
)";
    Parser node_parser(duplicate_nodes, "DuplicateNodes.gs");
    auto node_module = node_parser.parse();
    auto node_diagnostics = Linter::lint(node_module.module);

    ASSERT_FALSE(node_diagnostics.empty());
    EXPECT_EQ(node_diagnostics[0].code, "GS-LINT-003");
    EXPECT_EQ(node_diagnostics[0].context, "apply");
    EXPECT_FALSE(node_diagnostics[0].hint.empty());
    EXPECT_GT(node_diagnostics[0].range.start.line, 1);

    const std::string duplicate_const_and_node = R"(
graph Execute {
    const apply = new ApplyDamage {}
    node apply {
        type PrintString;
    }
}
)";
    Parser mixed_parser(duplicate_const_and_node, "DuplicateMixed.gs");
    auto mixed_module = mixed_parser.parse();
    auto mixed_diagnostics = Linter::lint(mixed_module.module);

    ASSERT_FALSE(mixed_diagnostics.empty());
    EXPECT_EQ(mixed_diagnostics[0].code, "GS-LINT-003");
    EXPECT_EQ(mixed_diagnostics[0].context, "apply");
    EXPECT_FALSE(mixed_diagnostics[0].hint.empty());
    EXPECT_GT(mixed_diagnostics[0].range.start.line, 1);
}

TEST(AssetLanguage, ProjectorDoesNotTreatMemberConnectAsCanonicalEdge) {
    const std::string source = R"(
graph Execute {
    node apply {
        type ApplyDamage;
    }
    apply.exit.connect(context.done);
}
)";
    Parser parser(source, "LegacyConnect.gs");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    EXPECT_TRUE(projected.value().edges.empty());
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-001");
}

TEST(AssetLanguage, ParsesQualifiedSchemaAndNodeTypeCommands) {
    const std::string source = R"(
graph Execute {
    schema Game.Ability.FlowGraph;
    node apply {
        type Game.Ability.ApplyDamage<Actor>;
    }
}
)";
    Parser parser(source, "QualifiedTypes.gs");
    auto parsed = parser.parse();

    EXPECT_TRUE(parsed.diagnostics.empty());
    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    EXPECT_EQ(projected.value().schema, "Game.Ability.FlowGraph");
    ASSERT_EQ(projected.value().nodes.size(), 1u);
    EXPECT_EQ(projected.value().nodes[0].type, "Game.Ability.ApplyDamage<Actor>");
}

TEST(AssetLanguage, PatcherAppliesImportNodeConnectAndPropertyEdits) {
    Parser parser(kGraph, "Fireball.gs");
    auto parsed = parser.parse();

    auto import_patch = Patcher::add_import(kGraph, "ability_core.d.gs");
    auto with_import = Patcher::apply(kGraph, import_patch);
    ASSERT_TRUE(with_import.is_ok()) << with_import.error();
    EXPECT_EQ(with_import.value().find("import \"ability_core.d.gs\";"), 0u);

    auto add_node_patch = Patcher::add_node(kGraph, parsed.module, "Execute", "delay", "Delay");
    ASSERT_TRUE(add_node_patch.is_ok()) << add_node_patch.error();
    auto with_node = Patcher::apply(kGraph, add_node_patch.value());
    ASSERT_TRUE(with_node.is_ok()) << with_node.error();
    EXPECT_NE(with_node.value().find("node delay"), std::string::npos);
    EXPECT_NE(with_node.value().find("type Delay;"), std::string::npos);

    Parser reparsed(with_node.value(), "Fireball.gs");
    auto reparsed_result = reparsed.parse();
    auto connect_patch = Patcher::connect(with_node.value(), reparsed_result.module, "Execute", "log.exit", "delay.enter");
    ASSERT_TRUE(connect_patch.is_ok()) << connect_patch.error();
    auto with_edge = Patcher::apply(with_node.value(), connect_patch.value());
    ASSERT_TRUE(with_edge.is_ok()) << with_edge.error();
    EXPECT_NE(with_edge.value().find("connect(log.exit, delay.enter);"), std::string::npos);

    Parser edge_parser(with_edge.value(), "Fireball.gs");
    auto edge_module = edge_parser.parse();
    auto property_patch = Patcher::set_property(with_edge.value(), edge_module.module, "apply", "amount", "75");
    ASSERT_TRUE(property_patch.is_ok()) << property_patch.error();
    auto with_property = Patcher::apply(with_edge.value(), property_patch.value());
    ASSERT_TRUE(with_property.is_ok()) << with_property.error();
    EXPECT_NE(with_property.value().find("amount: 75;"), std::string::npos);
    EXPECT_EQ(with_property.value().find("amount: 50;"), std::string::npos);

    Parser rename_parser(with_property.value(), "Fireball.gs");
    auto rename_module = rename_parser.parse();
    auto rename_patch = Patcher::rename_node(with_property.value(), rename_module.module, "Execute", "log", "logger");
    ASSERT_TRUE(rename_patch.is_ok()) << rename_patch.error();
    auto with_rename = Patcher::apply(with_property.value(), rename_patch.value());
    ASSERT_TRUE(with_rename.is_ok()) << with_rename.error();
    EXPECT_NE(with_rename.value().find("node logger"), std::string::npos);
    EXPECT_NE(with_rename.value().find("connect(apply.exit, logger.enter);"), std::string::npos);
    EXPECT_NE(with_rename.value().find("connect(logger.exit, delay.enter);"), std::string::npos);

    Parser disconnect_parser(with_rename.value(), "Fireball.gs");
    auto disconnect_module = disconnect_parser.parse();
    auto disconnect_patch = Patcher::disconnect(with_rename.value(), disconnect_module.module, "Execute", "apply.exit", "logger.enter");
    ASSERT_TRUE(disconnect_patch.is_ok()) << disconnect_patch.error();
    auto with_disconnect = Patcher::apply(with_rename.value(), disconnect_patch.value());
    ASSERT_TRUE(with_disconnect.is_ok()) << with_disconnect.error();
    EXPECT_EQ(with_disconnect.value().find("connect(apply.exit, logger.enter);"), std::string::npos);
    EXPECT_NE(with_disconnect.value().find("connect(logger.exit, delay.enter);"), std::string::npos);
}

TEST(AssetLanguage, PatcherAppliesBlockAttributeAndBlockRenameEdits) {
    Parser parser(kGraph, "Fireball.gs");
    auto parsed = parser.parse();

    auto add_block_patch = Patcher::add_block(kGraph, parsed.module, "Execute", "entry", "Finished");
    ASSERT_TRUE(add_block_patch.is_ok()) << add_block_patch.error();
    auto with_scope = Patcher::apply(kGraph, add_block_patch.value());
    ASSERT_TRUE(with_scope.is_ok()) << with_scope.error();
    EXPECT_NE(with_scope.value().find("entry Finished"), std::string::npos);

    Parser block_parser(with_scope.value(), "Fireball.gs");
    auto block_module = block_parser.parse();
    auto attr_patch = Patcher::add_attribute(with_scope.value(), block_module.module, "apply", "@id(\"apply\")");
    ASSERT_TRUE(attr_patch.is_ok()) << attr_patch.error();
    auto with_attr = Patcher::apply(with_scope.value(), attr_patch.value());
    ASSERT_TRUE(with_attr.is_ok()) << with_attr.error();
    EXPECT_NE(with_attr.value().find("@id(\"apply\")\n    node apply"), std::string::npos);

    Parser attr_parser(with_attr.value(), "Fireball.gs");
    auto attr_module = attr_parser.parse();
    auto rename_block_patch = Patcher::rename_block(with_attr.value(), attr_module.module, "Finished", "Completed");
    ASSERT_TRUE(rename_block_patch.is_ok()) << rename_block_patch.error();
    auto with_rename = Patcher::apply(with_attr.value(), rename_block_patch.value());
    ASSERT_TRUE(with_rename.is_ok()) << with_rename.error();
    EXPECT_NE(with_rename.value().find("entry Completed"), std::string::npos);
    EXPECT_EQ(with_rename.value().find("entry Finished"), std::string::npos);
}

TEST(AssetLanguage, RecoversBrokenTextWithDiagnosticsAndPartialAst) {
    const std::string source = R"(
graph Execute {
    node apply {
        type ApplyDamage;
        amount: 50
        target:
    }

    event Start {
        connect(context.start, apply.enter
    }
}
)";
    Parser parser(source, "Broken.gs");
    auto result = parser.parse();

    EXPECT_FALSE(result.diagnostics.empty());
    ASSERT_EQ(result.module.items.blocks.size(), 1u);
    const auto& graph = *result.module.items.blocks[0];
    ASSERT_FALSE(graph.items.blocks.empty());
    EXPECT_EQ(graph.items.blocks[0]->name, "apply");
}
