#include <gtest/gtest.h>

#include "graphscript/asset/language.h"

using namespace gs;
using namespace gs::asset;

static const char* kDecl = R"(
import "core.d.sc";

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

export declare scope graph: FlowGraphScope {
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
scope graph Execute: AbilityGraph {
    input target: Actor;
    input amount: float = 50.0;

    const apply = new ApplyDamage {
        amount: 50;
        editor.pos: [100, 100];
        target = context.target;
        metadata: { x: 1; y: ref "/Game/Ability"; };
    }

    const log = new PrintString {
        message: "done";
    }

    scope entry Start {
        context.start.connect(apply.enter);
        apply.exit.connect(log.enter);
    }
}
)";

TEST(AssetLanguage, ParsesDeclarationModuleAndObjectPins) {
    Parser parser(kDecl, "ability_core.d.sc");
    auto result = parser.parse();

    EXPECT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.imports.size(), 1u);
    EXPECT_EQ(result.module.imports[0].path, "core.d.sc");
    ASSERT_EQ(result.module.modules.size(), 1u);
    EXPECT_EQ(result.module.modules[0].id, "ability.core");
    EXPECT_EQ(result.module.enums.size(), 1u);
    EXPECT_EQ(result.module.scope_kinds.size(), 1u);
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
    Parser parser(kDecl, "ability_core.d.sc");
    auto result = parser.parse();

    ModuleGraph graph;
    auto diagnostics = Linter::lint(result.module, &graph);

    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(graph.module_id, "ability.core");
    EXPECT_FALSE(graph.module_id_inferred);
    ASSERT_EQ(graph.imports.size(), 1u);
    EXPECT_EQ(graph.imports[0], "core.d.sc");
    EXPECT_GE(graph.exports.size(), 4u);
}

TEST(AssetLanguage, ParsesGraphScopeConstObjectsAndCalls) {
    Parser parser(kGraph, "Fireball.sc");
    auto result = parser.parse();

    EXPECT_TRUE(result.diagnostics.empty());
    ASSERT_EQ(result.module.items.scopes.size(), 1u);
    const auto& graph = *result.module.items.scopes[0];
    EXPECT_EQ(graph.kind, "graph");
    EXPECT_EQ(graph.name, "Execute");
    EXPECT_EQ(graph.type, "AbilityGraph");
    ASSERT_EQ(graph.items.consts.size(), 2u);
    EXPECT_EQ(graph.items.consts[0].alias, "apply");
    ASSERT_EQ(graph.items.directives.size(), 2u);
    EXPECT_EQ(graph.items.directives[0].name, "input");
    ASSERT_EQ(graph.items.directives[0].parameters.size(), 1u);
    EXPECT_EQ(graph.items.directives[0].parameters[0].name, "target");
    ASSERT_EQ(graph.items.consts[0].properties.size(), 3u);
    EXPECT_EQ(graph.items.consts[0].properties[0].path, "amount");
    EXPECT_EQ(graph.items.consts[0].properties[0].value.text, "50");
    ASSERT_EQ(graph.items.consts[0].assignments.size(), 1u);
    EXPECT_EQ(graph.items.consts[0].assignments[0].target, "target");
    EXPECT_EQ(graph.items.consts[0].assignments[0].value.text, "context.target");
    EXPECT_EQ(graph.items.consts[0].properties[2].value.kind, ExprKind::InlineObject);
    ASSERT_EQ(graph.items.consts[0].properties[2].value.properties.size(), 2u);
    EXPECT_EQ(graph.items.consts[0].properties[2].value.properties[1].value.kind, ExprKind::AssetRef);
    EXPECT_EQ(graph.items.consts[0].properties[2].value.properties[1].value.text, "/Game/Ability");
    ASSERT_EQ(graph.items.scopes.size(), 1u);
    ASSERT_EQ(graph.items.scopes[0]->items.calls.size(), 2u);
    EXPECT_EQ(graph.items.scopes[0]->items.calls[1].args[0].text, "log.enter");
}

TEST(AssetLanguage, ProjectsFlowGraphWithPinsAndEdges) {
    Parser decl_parser(kDecl, "ability_core.d.sc");
    auto decl = decl_parser.parse();
    Parser graph_parser(kGraph, "Fireball.sc");
    auto graph_module = graph_parser.parse();

    for (auto& object : decl.module.objects) {
        graph_module.module.objects.push_back(std::move(object));
    }

    auto projected = FlowGraphProjector::project(graph_module.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    const auto& graph = projected.value();

    EXPECT_EQ(graph.name, "Execute");
    ASSERT_EQ(graph.nodes.size(), 2u);
    EXPECT_EQ(graph.nodes[0].alias, "apply");
    EXPECT_EQ(graph.nodes[0].pins.size(), 4u);
    ASSERT_EQ(graph.edges.size(), 2u);
    EXPECT_EQ(graph.edges[0].from, "context.start");
    EXPECT_EQ(graph.edges[0].to, "apply.enter");
    EXPECT_TRUE(graph.diagnostics.empty());
}

TEST(AssetLanguage, ReportsInvalidFlowEdgeButKeepsProjection) {
    const std::string source = R"(
scope graph Execute: AbilityGraph {
    const apply = new ApplyDamage {
        amount: 50;
    }
    missing.exit.connect(apply.enter);
}
)";
    Parser parser(source, "Broken.sc");
    auto parsed = parser.parse();

    auto projected = FlowGraphProjector::project(parsed.module, "Execute");
    ASSERT_TRUE(projected.is_ok()) << projected.error();
    ASSERT_EQ(projected.value().edges.size(), 1u);
    EXPECT_FALSE(projected.value().edges[0].valid);
    ASSERT_FALSE(projected.value().diagnostics.empty());
    EXPECT_EQ(projected.value().diagnostics[0].code, "GS-FLW-002");
}

TEST(AssetLanguage, PatcherAppliesImportNodeConnectAndPropertyEdits) {
    Parser parser(kGraph, "Fireball.sc");
    auto parsed = parser.parse();

    auto import_patch = Patcher::add_import(kGraph, "ability_core.d.sc");
    auto with_import = Patcher::apply(kGraph, import_patch);
    ASSERT_TRUE(with_import.is_ok()) << with_import.error();
    EXPECT_EQ(with_import.value().find("import \"ability_core.d.sc\";"), 0u);

    auto add_node_patch = Patcher::add_node(kGraph, parsed.module, "Execute", "delay", "Delay");
    ASSERT_TRUE(add_node_patch.is_ok()) << add_node_patch.error();
    auto with_node = Patcher::apply(kGraph, add_node_patch.value());
    ASSERT_TRUE(with_node.is_ok()) << with_node.error();
    EXPECT_NE(with_node.value().find("const delay = new Delay"), std::string::npos);

    Parser reparsed(with_node.value(), "Fireball.sc");
    auto reparsed_result = reparsed.parse();
    auto connect_patch = Patcher::connect(with_node.value(), reparsed_result.module, "Execute", "log.exit", "delay.enter");
    ASSERT_TRUE(connect_patch.is_ok()) << connect_patch.error();
    auto with_edge = Patcher::apply(with_node.value(), connect_patch.value());
    ASSERT_TRUE(with_edge.is_ok()) << with_edge.error();
    EXPECT_NE(with_edge.value().find("log.exit.connect(delay.enter);"), std::string::npos);

    Parser edge_parser(with_edge.value(), "Fireball.sc");
    auto edge_module = edge_parser.parse();
    auto property_patch = Patcher::set_property(with_edge.value(), edge_module.module, "apply", "amount", "75");
    ASSERT_TRUE(property_patch.is_ok()) << property_patch.error();
    auto with_property = Patcher::apply(with_edge.value(), property_patch.value());
    ASSERT_TRUE(with_property.is_ok()) << with_property.error();
    EXPECT_NE(with_property.value().find("amount: 75;"), std::string::npos);
    EXPECT_EQ(with_property.value().find("amount: 50;"), std::string::npos);

    Parser rename_parser(with_property.value(), "Fireball.sc");
    auto rename_module = rename_parser.parse();
    auto rename_patch = Patcher::rename_node(with_property.value(), rename_module.module, "Execute", "log", "logger");
    ASSERT_TRUE(rename_patch.is_ok()) << rename_patch.error();
    auto with_rename = Patcher::apply(with_property.value(), rename_patch.value());
    ASSERT_TRUE(with_rename.is_ok()) << with_rename.error();
    EXPECT_NE(with_rename.value().find("const logger = new PrintString"), std::string::npos);
    EXPECT_NE(with_rename.value().find("apply.exit.connect(logger.enter);"), std::string::npos);
    EXPECT_NE(with_rename.value().find("logger.exit.connect(delay.enter);"), std::string::npos);

    Parser disconnect_parser(with_rename.value(), "Fireball.sc");
    auto disconnect_module = disconnect_parser.parse();
    auto disconnect_patch = Patcher::disconnect(with_rename.value(), disconnect_module.module, "Execute", "apply.exit", "logger.enter");
    ASSERT_TRUE(disconnect_patch.is_ok()) << disconnect_patch.error();
    auto with_disconnect = Patcher::apply(with_rename.value(), disconnect_patch.value());
    ASSERT_TRUE(with_disconnect.is_ok()) << with_disconnect.error();
    EXPECT_EQ(with_disconnect.value().find("apply.exit.connect(logger.enter);"), std::string::npos);
    EXPECT_NE(with_disconnect.value().find("logger.exit.connect(delay.enter);"), std::string::npos);
}

TEST(AssetLanguage, PatcherAppliesScopeAttributeAndScopeRenameEdits) {
    Parser parser(kGraph, "Fireball.sc");
    auto parsed = parser.parse();

    auto add_scope_patch = Patcher::add_scope(kGraph, parsed.module, "Execute", "entry", "Finished");
    ASSERT_TRUE(add_scope_patch.is_ok()) << add_scope_patch.error();
    auto with_scope = Patcher::apply(kGraph, add_scope_patch.value());
    ASSERT_TRUE(with_scope.is_ok()) << with_scope.error();
    EXPECT_NE(with_scope.value().find("scope entry Finished"), std::string::npos);

    Parser scope_parser(with_scope.value(), "Fireball.sc");
    auto scope_module = scope_parser.parse();
    auto attr_patch = Patcher::add_attribute(with_scope.value(), scope_module.module, "apply", "@id(\"apply\")");
    ASSERT_TRUE(attr_patch.is_ok()) << attr_patch.error();
    auto with_attr = Patcher::apply(with_scope.value(), attr_patch.value());
    ASSERT_TRUE(with_attr.is_ok()) << with_attr.error();
    EXPECT_NE(with_attr.value().find("@id(\"apply\")\n    const apply"), std::string::npos);

    Parser attr_parser(with_attr.value(), "Fireball.sc");
    auto attr_module = attr_parser.parse();
    auto rename_scope_patch = Patcher::rename_scope(with_attr.value(), attr_module.module, "Finished", "Completed");
    ASSERT_TRUE(rename_scope_patch.is_ok()) << rename_scope_patch.error();
    auto with_rename = Patcher::apply(with_attr.value(), rename_scope_patch.value());
    ASSERT_TRUE(with_rename.is_ok()) << with_rename.error();
    EXPECT_NE(with_rename.value().find("scope entry Completed"), std::string::npos);
    EXPECT_EQ(with_rename.value().find("scope entry Finished"), std::string::npos);
}

TEST(AssetLanguage, RecoversBrokenTextWithDiagnosticsAndPartialAst) {
    const std::string source = R"(
scope graph Execute: AbilityGraph {
    const apply = new ApplyDamage {
        amount: 50
        target:
    }

    scope entry Start {
        context.start.connect(apply.enter
    }
}
)";
    Parser parser(source, "Broken.sc");
    auto result = parser.parse();

    EXPECT_FALSE(result.diagnostics.empty());
    ASSERT_EQ(result.module.items.scopes.size(), 1u);
    const auto& graph = *result.module.items.scopes[0];
    ASSERT_EQ(graph.items.consts.size(), 1u);
    EXPECT_EQ(graph.items.consts[0].alias, "apply");
    ASSERT_FALSE(graph.items.scopes.empty());
}
