#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "editor.h"
#include "source_diagnostics.h"

using namespace gs;

static void load_core_for_cli(EditSession& s) {
    std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    auto r = s.load_import(path);
    ASSERT_TRUE(r.is_ok()) << r.error();
}

static std::string b64_for_cli_test(const std::string& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
    }
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

static std::filesystem::path make_cli_temp_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / ("graphscript_cli_" + name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

static void write_cli_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static std::string quoted_path(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

TEST(CLIEditor, ExplicitWebAliasesExecuteSuccessfully) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph CliAlias"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "CliAlias");

    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->node_instances.size(), 1u);

    EXPECT_TRUE(editor.execute("create_graph Other"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("switch_graph CliAlias"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "CliAlias");

    EXPECT_TRUE(editor.execute("remove_node logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(session.active_graph()->node_instances.empty());

    EXPECT_TRUE(editor.execute("switch_graph Other"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("delete_graph CliAlias"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.module().graphs.size(), 1u);
}

TEST(CLIEditor, DiagramCommandUsesAssetProjection) {
    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    const std::string source = R"(graph DiagramCli {
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
    auto loaded = session.load_source(source, "diagram_cli.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();

    testing::internal::CaptureStdout();
    EXPECT_TRUE(editor.execute("diagram"));
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(output.find("Warning: asset projection unavailable"), std::string::npos);
    EXPECT_NE(output.find("## DiagramCli : TraceGraph"), std::string::npos);
    EXPECT_NE(output.find("in message : FString"), std::string::npos);
    EXPECT_NE(output.find("context ==>|\"context.start"), std::string::npos);
    EXPECT_NE(output.find("message -.->|\"message"), std::string::npos);
}

TEST(CLIEditor, AnnotatesTopLevelImportAndLet) {
    Environment env;
    EditSession session(env);
    session.add_import("custom.d.gs");
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("let cached SoftObjectPath seed"));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("annotate import custom.d.gs Id import-cli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate let cached PersistentId let-cli"));
    EXPECT_TRUE(editor.last_command_succeeded());

    auto text = session.emit();
    EXPECT_EQ(text.find("@Id(\"import-cli\")"), std::string::npos);
    EXPECT_NE(text.find("@PersistentId(\"let-cli\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("unannotate import custom.d.gs Id"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("unannotate let cached PersistentId"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@Id(\"import-cli\")"), std::string::npos);
    EXPECT_EQ(session.emit().find("@PersistentId(\"let-cli\")"), std::string::npos);
}

TEST(CLIEditor, RenameGraphCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph RenameGraphCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_graph RenameGraphCli RenamedGraphCli"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_EQ(session.module().graphs.size(), 1u);
    EXPECT_EQ(session.module().graphs[0].name, "RenamedGraphCli");

    EXPECT_TRUE(editor.execute("switch_graph RenamedGraphCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_graph RenamedGraphCli RenamedGraphCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_graph Missing Other"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, RenameNodeCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph RenameCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_node logger writer"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->node_instances.size(), 1u);
    EXPECT_EQ(session.active_graph()->node_instances[0].instance_name, "writer");

    EXPECT_TRUE(editor.execute("rename_node writer writer"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_node missing other"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, SetInitializerFieldCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph InitCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger \"message = old\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init logger message updated"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init logger asset PreviewValue(seed)"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init_ctor logger asset SoftObjectPath /Game/Asset"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init_ctor_arg logger asset /Game/OtherAsset"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init_ctor_type logger asset AssetRef"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->node_instances.size(), 1u);
    const auto& node = session.active_graph()->node_instances[0];
    EXPECT_EQ(node.initializer, "message = updated, asset = AssetRef(/Game/OtherAsset)");
    ASSERT_EQ(node.initializer_fields.size(), 2u);
    EXPECT_EQ(node.initializer_fields[0].name, "message");
    EXPECT_EQ(node.initializer_fields[1].name, "asset");
    const std::string emitted = session.emit();
    EXPECT_NE(emitted.find("asset: AssetRef(\"/Game/OtherAsset\");"), std::string::npos);
    Environment reparse_env;
    EditSession reparsed(reparse_env);
    load_core_for_cli(reparsed);
    auto reloaded = reparsed.load_source(emitted, "init_cli_roundtrip.gs");
    EXPECT_TRUE(reloaded.is_ok()) << reloaded.error();
    EXPECT_TRUE(editor.execute("set_init_ctor_arg logger message no"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init_ctor_type logger message AssetRef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_init logger message text"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(node.initializer, "text = updated, asset = AssetRef(/Game/OtherAsset)");
    ASSERT_EQ(node.initializer_fields.size(), 2u);
    EXPECT_EQ(node.initializer_fields[0].name, "text");
    EXPECT_TRUE(editor.execute("rename_init logger text asset"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_init logger missing other"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("unset_init logger text"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(node.initializer, "asset = AssetRef(/Game/OtherAsset)");
    ASSERT_EQ(node.initializer_fields.size(), 1u);
    EXPECT_EQ(node.initializer_fields[0].name, "asset");
    EXPECT_TRUE(editor.execute("unset_init logger asset"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(node.initializer.empty());
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_TRUE(editor.execute("unset_init logger missing"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("set_init_expr logger Factory(seed)"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(node.initializer, "Factory(seed)");
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_TRUE(editor.execute("set_init_expr logger \"message = restored\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(node.initializer, "message = restored");
    ASSERT_EQ(node.initializer_fields.size(), 1u);
    EXPECT_EQ(node.initializer_fields[0].name, "message");
    EXPECT_TRUE(editor.execute("set_init_expr logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(node.initializer.empty());
    EXPECT_TRUE(node.initializer_fields.empty());
    EXPECT_TRUE(editor.execute("set_init_expr missing Factory(seed)"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("set_init missing message no"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, SetInitializerFieldPreservesEscapedQuotedValues) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph InitQuotedCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init logger message \"\\\"quoted value\\\"\""));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->node_instances.size(), 1u);
    const auto& node = session.active_graph()->node_instances[0];
    EXPECT_EQ(node.initializer, "message = \"quoted value\"");
    ASSERT_EQ(node.initializer_fields.size(), 1u);
    EXPECT_EQ(node.initializer_fields[0].name, "message");
    EXPECT_EQ(node.initializer_fields[0].value, "\"quoted value\"");
}

TEST(CLIEditor, RenameParamCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph RenameParamCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("param in msg FString"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_param msg text"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(session.active_graph()->parameters[0].name, "text");

    EXPECT_TRUE(editor.execute("rename_param text text"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_param missing other"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, SetParamDefaultCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph ParamDefaultCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("param in speed float"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_default speed 2.5"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(session.active_graph()->parameters[0].default_value, "2.5");
    EXPECT_NE(session.emit_active().find("param speed: float = 2.5;"), std::string::npos);

    EXPECT_TRUE(editor.execute("set_param_default_ctor_arg speed 3.5"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("set_param_default_ctor speed SoftFloat 3.5"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->parameters[0].default_value, "SoftFloat(3.5)");
    EXPECT_NE(session.emit_active().find("param speed: float = SoftFloat(3.5);"), std::string::npos);

    EXPECT_TRUE(editor.execute("set_param_default_ctor_arg speed 4.5"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->parameters[0].default_value, "SoftFloat(4.5)");

    EXPECT_TRUE(editor.execute("set_param_default_ctor_type speed PreciseFloat"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->parameters[0].default_value, "PreciseFloat(4.5)");
    EXPECT_NE(session.emit_active().find("param speed: float = PreciseFloat(4.5);"), std::string::npos);

    EXPECT_TRUE(editor.execute("set_param_default speed"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(session.active_graph()->parameters[0].default_value.empty());

    EXPECT_TRUE(editor.execute("set_param_default_ctor speed 123Bad 1.0"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_default_ctor_type speed 123Bad"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_default missing 1.0"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, SetParamTypeCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph ParamTypeCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("param in speed float"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_type speed double"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->parameters.size(), 1u);
    EXPECT_EQ(session.active_graph()->parameters[0].type_name, "double");
    EXPECT_NE(session.emit_active().find("param speed: double;"), std::string::npos);

    ASSERT_NE(session.env().nodes().find("ParamTypeCli"), nullptr);
    ASSERT_NE(session.env().nodes().find("ParamTypeCli")->find_pin("speed"), nullptr);
    EXPECT_EQ(session.env().nodes().find("ParamTypeCli")->find_pin("speed")->type_name, "double");

    EXPECT_TRUE(editor.execute("set_param_type speed double"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_type speed 123Bad"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_param_type missing float"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, RenameLogicBlockCommandsAreReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph RenameBlocksCli"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node Delay wait"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event OnStart"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_event OnStart Begin"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("flow logger.exit wait.enter"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->events.size(), 1u);
    EXPECT_EQ(session.active_graph()->events[0].name, "Begin");
    ASSERT_EQ(session.active_graph()->events[0].flow_connections.size(), 1u);

    EXPECT_TRUE(editor.execute("fn Compute"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_function Compute Evaluate"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("flow context.start context.done"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_EQ(session.active_graph()->functions.size(), 1u);
    EXPECT_EQ(session.active_graph()->functions[0].name, "Evaluate");
    ASSERT_EQ(session.active_graph()->functions[0].flow_connections.size(), 1u);

    EXPECT_TRUE(editor.execute("rename_event Begin Begin"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_event Missing Other"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_function Evaluate Evaluate"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_function Missing Other"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, RenameGraphDerivedPinCommandsAreReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Child {\n"
        "    @graph.input\n"
        "    param msg: FString;\n"
        "    event Run {\n"
        "    }\n"
        "}\n"
        "graph Parent {\n"
        "    @graph.input\n"
        "    param incoming: FString;\n"
        "    node child {\n"
        "        type Child;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, child.Run);\n"
        "        bind(incoming, child.msg);\n"
        "    }\n"
        "}\n";

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "Child");

    EXPECT_TRUE(editor.execute("rename_param msg text"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_event Run Execute"));
    EXPECT_TRUE(editor.last_command_succeeded());

    ASSERT_EQ(session.module().graphs.size(), 2u);
    EXPECT_EQ(session.module().graphs[0].parameters[0].name, "text");
    EXPECT_EQ(session.module().graphs[0].events[0].name, "Execute");
    EXPECT_EQ(session.module().graphs[1].events[0].flow_connections[0].to.pin_name, "Execute");
    EXPECT_EQ(session.module().graphs[1].events[0].data_links[0].target.pin_name, "text");
    ASSERT_NE(session.env().nodes().find("Child"), nullptr);
    EXPECT_NE(session.env().nodes().find("Child")->find_pin("text"), nullptr);
    EXPECT_NE(session.env().nodes().find("Child")->find_pin("Execute"), nullptr);

    const std::string emitted = session.emit();
    EXPECT_NE(emitted.find("connect(context.start, child.Execute);"), std::string::npos);
    EXPECT_NE(emitted.find("bind(incoming, child.text);"), std::string::npos);
    EXPECT_EQ(emitted.find("child.Run"), std::string::npos);
    EXPECT_EQ(emitted.find("child.msg"), std::string::npos);
}

TEST(CLIEditor, GraphInterfaceCommandsRefreshGraphDerivedNodeType) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph Child"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.env().nodes().find("Child"), nullptr);

    EXPECT_TRUE(editor.execute("param in msg FString"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event Run"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.env().nodes().find("Child"), nullptr);
    EXPECT_NE(session.env().nodes().find("Child")->find_pin("msg"), nullptr);
    EXPECT_NE(session.env().nodes().find("Child")->find_pin("Run"), nullptr);

    EXPECT_TRUE(editor.execute("create_graph Parent"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node Child child"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    ASSERT_EQ(session.active_graph()->node_instances.size(), 1u);
    EXPECT_EQ(session.active_graph()->node_instances[0].type_name, "Child");

    EXPECT_TRUE(editor.execute("switch_graph Child"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("param rm msg"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event rm Run"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.env().nodes().find("Child"), nullptr);
    EXPECT_EQ(session.env().nodes().find("Child")->find_pin("msg"), nullptr);
    EXPECT_EQ(session.env().nodes().find("Child")->find_pin("Run"), nullptr);

    EXPECT_TRUE(editor.execute("undo"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.env().nodes().find("Child"), nullptr);
    EXPECT_NE(session.env().nodes().find("Child")->find_pin("Run"), nullptr);
}

TEST(CLIEditor, FailedCommandUpdatesMachineReadableStatus) {
    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    std::ostringstream capture;
    auto* old_buf = std::cout.rdbuf(capture.rdbuf());
    EXPECT_TRUE(editor.execute("unknown_command"));
    std::cout.rdbuf(old_buf);

    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(capture.str().find("[ERROR] Unknown command"), std::string::npos);
}

TEST(CLIEditor, RepeatedImportReportsAlreadyLoaded) {
    Environment env;
    EditSession session(env);
    CLIEditor editor(session);
    std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";

    std::ostringstream capture;
    auto* old_buf = std::cout.rdbuf(capture.rdbuf());
    EXPECT_TRUE(editor.execute("import " + path));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("import " + path));
    EXPECT_TRUE(editor.last_command_succeeded());
    std::cout.rdbuf(old_buf);

    ASSERT_EQ(session.module().imports.size(), 1u);
    EXPECT_TRUE(session.module().imports[0].loaded);
    EXPECT_NE(capture.str().find("Loaded: " + path), std::string::npos);
    EXPECT_NE(capture.str().find("Already loaded: " + path), std::string::npos);
}

TEST(CLIEditor, AnnotationCommandsAreReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph Annotated"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("param in name FString"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node Delay wait"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event OnStart"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("flow logger.exit wait.enter"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("link logger.message name"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("fn Compute"));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("annotate graph Comment title \"Graph note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate param name Tooltip \"Player name\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate node logger Position X=100 Y=200"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate event OnStart Id \"event-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate function Compute PersistentId \"function-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate flow event OnStart logger.exit wait.enter Id \"flow-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate link event OnStart logger.message name PersistentId \"link-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());

    auto text = session.emit();
    EXPECT_NE(text.find("@Comment(\"title\", \"Graph note\")"), std::string::npos);
    EXPECT_NE(text.find("@Tooltip(\"Player name\")"), std::string::npos);
    EXPECT_NE(text.find("@Position(X = 100, Y = 200)"), std::string::npos);
    EXPECT_NE(text.find("@Id(\"event-cli\")"), std::string::npos);
    EXPECT_NE(text.find("@PersistentId(\"function-cli\")"), std::string::npos);
    EXPECT_NE(text.find("@Id(\"flow-cli\")"), std::string::npos);
    EXPECT_NE(text.find("@PersistentId(\"link-cli\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("unannotate node logger Position"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@Position("), std::string::npos);
    EXPECT_TRUE(editor.execute("unannotate event OnStart Id"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@Id(\"event-cli\")"), std::string::npos);
    EXPECT_TRUE(editor.execute("unannotate flow event OnStart logger.exit wait.enter Id"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@Id(\"flow-cli\")"), std::string::npos);
    EXPECT_TRUE(editor.execute("unannotate link event OnStart logger.message name PersistentId"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@PersistentId(\"link-cli\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("unannotate param name Missing"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("unannotate function Compute Missing"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("unannotate flow event OnStart logger.exit wait.enter Missing"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, GenerateAnnotationCommandsAreReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph GenerateAnnotated"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString logger"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("comment logger \"legacy note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("meta position:logger.x 100"));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("annotate generate-comment logger \"legacy note\" Id \"gen-comment-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate generate-meta position:logger.x 100 PersistentId \"gen-meta-cli\""));
    EXPECT_TRUE(editor.last_command_succeeded());

    auto text = session.emit();
    EXPECT_NE(text.find("@Id(\"gen-comment-cli\")"), std::string::npos);
    EXPECT_NE(text.find("@PersistentId(\"gen-meta-cli\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("comment logger \"legacy note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate generate-comment logger \"legacy note\" Tag duplicate"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate generate-comment logger \"legacy note\" #2 Id \"gen-comment-cli-2\""));
    EXPECT_TRUE(editor.last_command_succeeded());

    text = session.emit();
    EXPECT_NE(text.find("@Id(\"gen-comment-cli-2\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("move_comment logger \"legacy note\" #2 up"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_EQ(session.active_graph()->generate->comments.size(), 2u);
    EXPECT_EQ(session.active_graph()->generate->comments[0].annotations[0].args[0].value, "gen-comment-cli-2");

    EXPECT_TRUE(editor.execute("comment move logger \"legacy note\" #1 down"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->comments[1].annotations[0].args[0].value, "gen-comment-cli-2");

    EXPECT_TRUE(editor.execute("rename_comment logger \"legacy note\" #2 \"renamed note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->comments[1].text, "renamed note");
    EXPECT_EQ(session.active_graph()->generate->comments[1].annotations[0].args[0].value, "gen-comment-cli-2");

    EXPECT_TRUE(editor.execute("comment rename logger \"renamed note\" \"legacy note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->comments[1].text, "legacy note");

    EXPECT_TRUE(editor.execute("meta position:logger.y 200"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("meta move position:logger.x 100 down"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_EQ(session.active_graph()->generate->metadata.size(), 2u);
    EXPECT_EQ(session.active_graph()->generate->metadata[0].property, "y");

    EXPECT_TRUE(editor.execute("rename_meta position:logger.x 100 150"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->metadata[1].value, "150");

    EXPECT_TRUE(editor.execute("meta rename position:logger.x 150 100"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->metadata[1].value, "100");

    EXPECT_TRUE(editor.execute("rename_meta_ref position:logger.x 100 layout:logger.y"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->metadata[1].scope, "layout");
    EXPECT_EQ(session.active_graph()->generate->metadata[1].property, "y");

    EXPECT_TRUE(editor.execute("meta rename_ref layout:logger.y 100 position:logger.x"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->metadata[1].scope, "position");
    EXPECT_EQ(session.active_graph()->generate->metadata[1].property, "x");

    EXPECT_TRUE(editor.execute("unannotate generate-meta position:logger.x 100 PersistentId"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.emit().find("@PersistentId(\"gen-meta-cli\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("remove_comment logger \"legacy note\""));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("remove_comment logger \"legacy note\" #2"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_EQ(session.active_graph()->generate->comments.size(), 1u);

    EXPECT_TRUE(editor.execute("comment rm logger \"legacy note\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("meta rm position:logger.x 100"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("remove_meta position:logger.y 200"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_FALSE(session.active_graph()->generate.has_value());
}

TEST(CLIEditor, LogicBlockDeleteCommandsAreReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("create_graph DeleteBlocks"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event OnStart"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("fn Compute"));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->events.size(), 1u);
    EXPECT_EQ(session.active_graph()->functions.size(), 1u);

    EXPECT_TRUE(editor.execute("delete_event OnStart"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("delete_function Compute"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(session.active_graph()->events.empty());
    EXPECT_TRUE(session.active_graph()->functions.empty());

    EXPECT_TRUE(editor.execute("event Temporary"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event rm Temporary"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("fn Helper"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("fn rm Helper"));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("delete_event Missing"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceBase64CommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Replayed {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "}\n";
    const std::string source_b64 = b64_for_cli_test(source);
    EXPECT_TRUE(editor.execute("apply_source_b64 " + source_b64));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "Replayed");
    EXPECT_NE(session.emit().find("param speed: float"), std::string::npos);
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 not_base64!"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourcePatchCommandIsReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Patched {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "}\n";
    const std::string source_b64 = b64_for_cli_test(source);
    EXPECT_TRUE(editor.execute("apply_source_b64 " + source_b64));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_patch 4 10 4 16 d3JpdGVy"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param speed: float"), std::string::npos);
    EXPECT_NE(session.emit().find("node writer"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + source_b64));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_patch 4 10 4 16 d3JpdGVy deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);
    EXPECT_EQ(session.emit().find("node writer"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_patch 2 14 2 14 not_base64!"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_patch 99 1 99 1 OiA="));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, SourceBackedGraphCommandsPatchTextWithoutReemit) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph SourceBacked {\n"
        "    // keep this comment\n"
        "\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "        message: \"old\";\n"
        "    }\n"
        "    node wait {\n"
        "        type Delay;\n"
        "    }\n"
        "\n"
        "    event OnStart {\n"
        "        connect(context.start, logger.enter);\n"
        "    }\n"
        "}\n";

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("add_node PrintString extra"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("annotate node extra Position X=10 Y=20"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init extra message inserted"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("event OnStart"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("flow logger.exit wait.enter"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("unflow context.start logger.enter"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("set_init logger message \"new value\""));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("rename_node logger writer"));
    EXPECT_TRUE(editor.last_command_succeeded());

    const std::string emitted = session.emit();
    EXPECT_NE(emitted.find("    // keep this comment\n\n    node writer"), std::string::npos);
    EXPECT_NE(emitted.find("message: \"new value\";"), std::string::npos);
    EXPECT_NE(emitted.find("node extra {\n        type PrintString;\n        message: inserted;"), std::string::npos);
    EXPECT_NE(emitted.find("connect(writer.exit, wait.enter);"), std::string::npos);
    EXPECT_EQ(emitted.find("connect(context.start, logger.enter);"), std::string::npos);
    EXPECT_NE(emitted.find("    @Position(X = 10, Y = 20)\n    node extra"), std::string::npos);
}

TEST(CLIEditor, ApplySourcePatchesBase64CommandIsAtomicAndReplayable) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Patched {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "}\n";
    const std::string source_b64 = b64_for_cli_test(source);
    EXPECT_TRUE(editor.execute("apply_source_b64 " + source_b64));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param speed: float"), std::string::npos);
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);

    const std::string patch_lines =
        "3 11 3 16 " + b64_for_cli_test("velocity") + "\n" +
        "4 10 4 16 " + b64_for_cli_test("writer") + "\n";
    EXPECT_TRUE(editor.execute("apply_source_patches_b64 " + b64_for_cli_test(patch_lines)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param velocity: float"), std::string::npos);
    EXPECT_NE(session.emit().find("node writer"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + source_b64));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_patches_b64 " + b64_for_cli_test(patch_lines) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param speed: float"), std::string::npos);
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);

    const std::string overlapping =
        "4 10 4 16 " + b64_for_cli_test("writer") + "\n" +
        "4 12 4 16 " + b64_for_cli_test("bad") + "\n";
    EXPECT_TRUE(editor.execute("apply_source_patches_b64 " + b64_for_cli_test(overlapping)));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);
    EXPECT_EQ(session.emit().find("node writer"), std::string::npos);
}

TEST(CLIEditor, ApplySourceIdentifierRenameUsesTokenPatches) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"speed\")\n"
        "graph RenameSource {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    event OnStart {\n"
        "        bind(speed, logger.message);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_identifier_rename speed velocity"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("param velocity: float"), std::string::npos);
    EXPECT_NE(renamed.find("bind(velocity, logger.message)"), std::string::npos);
    EXPECT_EQ(renamed.find("param speed: float"), std::string::npos);
    EXPECT_EQ(renamed.find("bind(speed, logger.message)"), std::string::npos);
    EXPECT_NE(renamed.find("@Comment(\"speed\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_identifier_rename speed velocity deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param speed: float"), std::string::npos);
    EXPECT_EQ(session.emit().find("param velocity: float"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_identifier_rename missing velocity"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_identifier_rename speed Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceParamRenameUsesActiveGraphRanges) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph RenameSource {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    event OnStart {\n"
        "        bind(speed, logger.message);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Other {\n"
        "    @graph.input\n"
        "    param speed: float;\n"
        "    node otherLogger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    event OnStart {\n"
        "        bind(speed, otherLogger.message);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Parent {\n"
        "    @graph.input\n"
        "    param parentSpeed: float;\n"
        "    node child {\n"
        "        type RenameSource;\n"
        "    }\n"
        "    node parentLogger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    event OnStart {\n"
        "        bind(parentSpeed, child.speed);\n"
        "        bind(child.speed, parentLogger.message);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_param_rename speed velocity"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph RenameSource"), std::string::npos);
    EXPECT_NE(renamed.find("param velocity: float"), std::string::npos);
    EXPECT_NE(renamed.find("bind(velocity, logger.message)"), std::string::npos);
    EXPECT_NE(renamed.find("graph Other"), std::string::npos);
    EXPECT_NE(renamed.find("param speed: float"), std::string::npos);
    EXPECT_NE(renamed.find("bind(speed, otherLogger.message)"), std::string::npos);
    EXPECT_EQ(renamed.find("bind(velocity, otherLogger.message)"), std::string::npos);
    EXPECT_NE(renamed.find("graph Parent"), std::string::npos);
    EXPECT_NE(renamed.find("bind(parentSpeed, child.velocity)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(child.velocity, parentLogger.message)"), std::string::npos);
    EXPECT_EQ(renamed.find("bind(parentSpeed, child.speed)"), std::string::npos);
    EXPECT_EQ(renamed.find("bind(child.speed, parentLogger.message)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_param_rename speed velocity deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("bind(speed, logger.message)"), std::string::npos);
    EXPECT_EQ(session.emit().find("bind(velocity, logger.message)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_param_rename missing velocity"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_param_rename speed Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceEventRenameUsesActiveGraphRanges) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Child {\n"
        "    event Run {\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Other {\n"
        "    event Run {\n"
        "        connect(context.start, context.done);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Parent {\n"
        "    node child {\n"
        "        type Child;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, child.Run);\n"
        "        connect(child.Run, context.done);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_event_rename Run Execute"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph Child"), std::string::npos);
    EXPECT_NE(renamed.find("event Execute"), std::string::npos);
    EXPECT_NE(renamed.find("connect(context.start, child.Execute)"), std::string::npos);
    EXPECT_NE(renamed.find("connect(child.Execute, context.done)"), std::string::npos);
    EXPECT_NE(renamed.find("graph Other"), std::string::npos);
    EXPECT_NE(renamed.find("event Run"), std::string::npos);
    EXPECT_EQ(renamed.find("connect(context.start, child.Run)"), std::string::npos);
    EXPECT_EQ(renamed.find("connect(child.Run, context.done)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_event_rename Run Execute deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("event Run"), std::string::npos);
    EXPECT_EQ(session.emit().find("event Execute"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_event_rename Missing Execute"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_event_rename Run Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceFunctionRenameUsesActiveGraphRanges) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Worker {\n"
        "    @graph.input\n"
        "    param msg: FString;\n"
        "    function Compute {\n"
        "        connect(context.start, context.done);\n"
        "        bind(msg, context.result);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Other {\n"
        "    function Compute {\n"
        "        connect(context.start, context.done);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Parent {\n"
        "    node worker {\n"
        "        type Worker;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, worker.Compute);\n"
        "        connect(worker.Compute, context.done);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_function_rename Compute Evaluate"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph Worker"), std::string::npos);
    EXPECT_NE(renamed.find("function Evaluate"), std::string::npos);
    EXPECT_NE(renamed.find("bind(msg, context.result)"), std::string::npos);
    EXPECT_NE(renamed.find("connect(context.start, worker.Evaluate)"), std::string::npos);
    EXPECT_NE(renamed.find("connect(worker.Evaluate, context.done)"), std::string::npos);
    EXPECT_NE(renamed.find("graph Other"), std::string::npos);
    EXPECT_NE(renamed.find("function Compute"), std::string::npos);
    EXPECT_EQ(renamed.find("connect(context.start, worker.Compute)"), std::string::npos);
    EXPECT_EQ(renamed.find("connect(worker.Compute, context.done)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_function_rename Compute Evaluate deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("function Compute"), std::string::npos);
    EXPECT_EQ(session.emit().find("function Evaluate"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_function_rename Missing Evaluate"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_function_rename Compute Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceNodeRenameUsesActiveGraphRanges) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph RenameNodeSource {\n"
        "    @graph.input\n"
        "    param msg: FString;\n"
        "    node logger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    node wait {\n"
        "        type Delay;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(logger.exit, wait.enter);\n"
        "        bind(msg, logger.message);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Other {\n"
        "    @graph.input\n"
        "    param otherMsg: FString;\n"
        "    node otherLogger {\n"
        "        type PrintString;\n"
        "    }\n"
        "    event OnStart {\n"
        "        bind(otherMsg, otherLogger.message);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_rename logger writer"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph RenameNodeSource"), std::string::npos);
    EXPECT_NE(renamed.find("node writer"), std::string::npos);
    EXPECT_NE(renamed.find("connect(writer.exit, wait.enter)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(msg, writer.message)"), std::string::npos);
    EXPECT_NE(renamed.find("graph Other"), std::string::npos);
    EXPECT_NE(renamed.find("node otherLogger"), std::string::npos);
    EXPECT_NE(renamed.find("bind(otherMsg, otherLogger.message)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_node_rename logger writer deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node logger"), std::string::npos);
    EXPECT_EQ(session.emit().find("node writer"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_node_rename missing writer"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_rename logger Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceGraphRenameMigratesGraphNodeTypes) {
    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    const std::string source =
        "graph Child {\n"
        "    event Run {\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Parent {\n"
        "    node child {\n"
        "        type Child;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, child.Run);\n"
        "    }\n"
        "}\n"
        "\n"
        "graph Other {\n"
        "    node otherChild {\n"
        "        type Child;\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_graph_rename Child Leaf"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph Leaf"), std::string::npos);
    EXPECT_NE(renamed.find("type Leaf"), std::string::npos);
    EXPECT_NE(renamed.find("connect(context.start, child.Run)"), std::string::npos);
    EXPECT_EQ(renamed.find("graph Child"), std::string::npos);
    EXPECT_EQ(renamed.find("type Child"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_graph_rename Child Leaf deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("graph Child"), std::string::npos);
    EXPECT_EQ(session.emit().find("graph Leaf"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_graph_rename Missing Leaf"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_graph_rename Child Parent"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_graph_rename Child Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyFilesGraphRenameMigratesDiskGraphDeclarationsAndNodeTypesAtomically) {
    auto dir = make_cli_temp_dir("files_graph_rename");
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto conflict_file = dir / "conflict.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(first_file,
                   "@Comment(\"Child stays string\")\n"
                   "graph Child {\n"
                   "    event Run {\n"
                   "    }\n"
                   "}\n"
                   "graph Parent {\n"
                   "    node child {\n"
                   "        type Child;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, child.Run);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph UsesExternal {\n"
                   "    node external {\n"
                   "        type Child;\n"
                   "    }\n"
                   "}\n");
    write_cli_text(conflict_file,
                   "graph Child {\n"
                   "}\n"
                   "graph Leaf {\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_graph_rename Child Leaf " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("graph Child"), std::string::npos);
        EXPECT_NE(first_contents.str().find("type Child;"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("graph Leaf"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("type Child;"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_rename Child Leaf " +
                               quoted_path(first_file) + " " + quoted_path(conflict_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("graph Child"), std::string::npos);
        EXPECT_NE(first_contents.str().find("type Child;"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_rename Child Leaf " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("graph Leaf"), std::string::npos);
        EXPECT_NE(first_contents.str().find("type Leaf;"), std::string::npos);
        EXPECT_NE(first_contents.str().find("connect(context.start, child.Run)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Child stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("graph Child"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("type Child;"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("type Leaf;"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("type Child;"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("Leaf"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_rename Child Leaf " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_graph_rename Child Graph " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyFilesGraphParamRenameMigratesDiskGraphInterfaceDataPinsAtomically) {
    auto dir = make_cli_temp_dir("files_graph_param_rename");
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto conflict_file = dir / "conflict.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(first_file,
                   "@Comment(\"speed stays string\")\n"
                   "graph Child {\n"
                   "    @graph.input\n"
                   "    param speed: FString;\n"
                   "    node logger {\n"
                   "        type PrintString;\n"
                   "    }\n"
                   "    event Run {\n"
                   "        bind(speed, logger.message);\n"
                   "    }\n"
                   "}\n"
                   "graph Parent {\n"
                   "    @graph.input\n"
                   "    param parentMsg: FString;\n"
                   "    node child {\n"
                   "        type Child;\n"
                   "    }\n"
                   "    node parentLogger {\n"
                   "        type PrintString;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        bind(parentMsg, child.speed);\n"
                   "        bind(child.speed, parentLogger.message);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph UsesExternal {\n"
                   "    @graph.input\n"
                   "    param parentMsg: FString;\n"
                   "    node external {\n"
                   "        type Child;\n"
                   "    }\n"
                   "    node logger {\n"
                   "        type PrintString;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        bind(parentMsg, external.speed);\n"
                   "        bind(external.speed, logger.message);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(conflict_file,
                   "graph Child {\n"
                   "    @graph.input\n"
                   "    param speed: FString;\n"
                   "    @graph.input\n"
                   "    param velocity: FString;\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_graph_param_rename Child speed velocity " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("param speed: FString"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(parentMsg, child.speed)"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("param velocity: FString"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("bind(parentMsg, external.speed)"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_param_rename Child speed velocity " +
                               quoted_path(first_file) + " " + quoted_path(conflict_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("param speed: FString"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(parentMsg, child.speed)"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_param_rename Child speed velocity " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("param velocity: FString"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(velocity, logger.message)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(parentMsg, child.velocity)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(child.velocity, parentLogger.message)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("speed stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("param speed: FString"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("bind(speed, logger.message)"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("child.speed"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("bind(parentMsg, external.velocity)"), std::string::npos);
        EXPECT_NE(second_contents.str().find("bind(external.velocity, logger.message)"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("external.speed"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("velocity"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_param_rename Child speed velocity " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_graph_param_rename Child speed Graph " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyFilesGraphEventRenameMigratesDiskGraphInterfaceExecPinsAtomically) {
    auto dir = make_cli_temp_dir("files_graph_event_rename");
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto conflict_file = dir / "conflict.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(first_file,
                   "@Comment(\"Run stays string\")\n"
                   "graph Child {\n"
                   "    event Run {\n"
                   "    }\n"
                   "}\n"
                   "graph Parent {\n"
                   "    node child {\n"
                   "        type Child;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, child.Run);\n"
                   "        connect(child.Run, context.done);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph UsesExternal {\n"
                   "    node external {\n"
                   "        type Child;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, external.Run);\n"
                   "        connect(external.Run, context.done);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(conflict_file,
                   "graph Child {\n"
                   "    event Run {\n"
                   "    }\n"
                   "    event Execute {\n"
                   "    }\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_graph_event_rename Child Run Execute " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("event Run"), std::string::npos);
        EXPECT_NE(first_contents.str().find("child.Run"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("event Execute"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("external.Run"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_event_rename Child Run Execute " +
                               quoted_path(first_file) + " " + quoted_path(conflict_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("event Run"), std::string::npos);
        EXPECT_NE(first_contents.str().find("child.Run"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_event_rename Child Run Execute " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("event Execute"), std::string::npos);
        EXPECT_NE(first_contents.str().find("connect(context.start, child.Execute)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("connect(child.Execute, context.done)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Run stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("event Run"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("child.Run"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("connect(context.start, external.Execute)"), std::string::npos);
        EXPECT_NE(second_contents.str().find("connect(external.Execute, context.done)"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("external.Run"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("Execute"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_event_rename Child Run Execute " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_graph_event_rename Child Run Graph " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyFilesGraphFunctionRenameMigratesDiskGraphFunctionInterfaceExecPinsAtomically) {
    auto dir = make_cli_temp_dir("files_graph_function_rename");
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto conflict_file = dir / "conflict.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(first_file,
                   "@Comment(\"Compute stays string\")\n"
                   "graph Worker {\n"
                   "    @graph.input\n"
                   "    param msg: FString;\n"
                   "    function Compute {\n"
                   "        connect(context.start, context.done);\n"
                   "        bind(msg, context.result);\n"
                   "    }\n"
                   "}\n"
                   "graph Parent {\n"
                   "    node worker {\n"
                   "        type Worker;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, worker.Compute);\n"
                   "        connect(worker.Compute, context.done);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph UsesExternal {\n"
                   "    node external {\n"
                   "        type Worker;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, external.Compute);\n"
                   "        connect(external.Compute, context.done);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(conflict_file,
                   "graph Worker {\n"
                   "    function Compute {\n"
                   "    }\n"
                   "    function Evaluate {\n"
                   "    }\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    load_core_for_cli(session);
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_graph_function_rename Worker Compute Evaluate " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("function Compute"), std::string::npos);
        EXPECT_NE(first_contents.str().find("worker.Compute"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("function Evaluate"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("external.Compute"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_function_rename Worker Compute Evaluate " +
                               quoted_path(first_file) + " " + quoted_path(conflict_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("function Compute"), std::string::npos);
        EXPECT_NE(first_contents.str().find("worker.Compute"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("function Evaluate"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_function_rename Worker Compute Evaluate " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("function Evaluate"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(msg, context.result)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("connect(context.start, worker.Evaluate)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("connect(worker.Evaluate, context.done)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Compute stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("function Compute"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("worker.Compute"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("graph UsesExternal"), std::string::npos);
        EXPECT_NE(second_contents.str().find("connect(context.start, external.Evaluate)"), std::string::npos);
        EXPECT_NE(second_contents.str().find("connect(external.Evaluate, context.done)"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("external.Compute"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("Evaluate"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_graph_function_rename Worker Compute Evaluate " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_graph_function_rename Worker Compute Graph " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceNodeTypeRenameMigratesDeclaredNodeReferences) {
    auto dir = make_cli_temp_dir("node_type_rename");
    auto declarations = dir / "declared_nodes.d.gs";
    write_cli_text(declarations,
                   "export declare type FString;\n"
                   "export declare object OldNode {\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n"
                   "export declare object NewNode {\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"OldNode stays string\")\n"
        "graph UsesDeclaredNodes {\n"
        "    node first {\n"
        "        type OldNode;\n"
        "    }\n"
        "    node second {\n"
        "        type OldNode;\n"
        "    }\n"
        "}\n"
        "\n"
        "graph AlreadyNew {\n"
        "    node existing {\n"
        "        type NewNode;\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_type_rename OldNode NewNode"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("node first"), std::string::npos);
    EXPECT_NE(renamed.find("node second"), std::string::npos);
    EXPECT_NE(renamed.find("type NewNode;"), std::string::npos);
    EXPECT_NE(renamed.find("OldNode stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("type OldNode;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_node_type_rename OldNode NewNode deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("type OldNode;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_node_type_rename Missing NewNode"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_type_rename OldNode Missing"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_type_rename OldNode Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceTypeRenameMigratesActiveModuleTypeReferences) {
    auto dir = make_cli_temp_dir("source_type_rename");
    auto declarations = dir / "declared_types.d.gs";
    write_cli_text(declarations,
                   "export declare type OldType: constructible;\n"
                   "export declare type NewType: constructible;\n"
                   "export declare object Holder {\n"
                   "    @flow.input\n"
                   "    value: OldType;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Bind(Type = OldType(\"let annotation OldType\"))\n"
        "const cached = new OldType {\n"
        "}\n"
        "@Comment(\"OldType stays string\")\n"
        "@Bind(Type = OldType(\"graph annotation OldType\"))\n"
        "graph UsesTypes {\n"
        "    @Bind(Type = OldType(\"param annotation OldType\"))\n"
        "    @graph.input\n"
        "    param input: OldType = OldType(\"default OldType\");\n"
        "    @Bind(Type = OldType(\"node annotation OldType\"))\n"
        "    node fieldInit {\n"
        "        type Holder;\n"
        "        value: OldType(\"field OldType\");\n"
        "    }\n"
        "    @Bind(Type = OldType(\"event annotation OldType\"))\n"
        "    event OnStart {\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_type_rename OldType NewType deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param input: OldType = OldType(\"default OldType\")"), std::string::npos);
    EXPECT_EQ(session.emit().find("param input: NewType = NewType(\"default OldType\")"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_type_rename OldType NewType"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("@Bind(Type = NewType(\"let annotation OldType\"))"), std::string::npos);
    EXPECT_NE(renamed.find("const cached = new NewType"), std::string::npos);
    EXPECT_NE(renamed.find("Bind(Type = NewType(\"graph annotation OldType\"))"), std::string::npos);
    EXPECT_NE(renamed.find("Bind(Type = NewType(\"param annotation OldType\"))"), std::string::npos);
    EXPECT_NE(renamed.find("Bind(Type = NewType(\"node annotation OldType\"))"), std::string::npos);
    EXPECT_NE(renamed.find("Bind(Type = NewType(\"event annotation OldType\"))"), std::string::npos);
    EXPECT_NE(renamed.find("param input: NewType = NewType(\"default OldType\")"), std::string::npos);
    EXPECT_NE(renamed.find("value: NewType(\"field OldType\")"), std::string::npos);
    EXPECT_NE(renamed.find("OldType stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("Bind(Type = OldType"), std::string::npos);
    EXPECT_EQ(renamed.find("new OldType"), std::string::npos);
    EXPECT_EQ(renamed.find("param input: OldType"), std::string::npos);
    EXPECT_EQ(renamed.find("value: OldType"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_type_rename OldType NewType"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_type_rename NewType MissingType"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_type_rename NewType Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyImportNodeRenamePatchesDeclarationAndCurrentModuleReferences) {
    auto dir = make_cli_temp_dir("import_node_rename");
    auto declarations = dir / "declared_nodes.d.gs";
    write_cli_text(declarations,
                   "export declare type FString;\n"
                   "export declare object OldNode {\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"OldNode stays string\")\n"
        "graph UsesDeclaredNodes {\n"
        "    node first {\n"
        "        type OldNode;\n"
        "    }\n"
        "    node second {\n"
        "        type OldNode;\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_node_rename " + quoted_path(declarations) + " OldNode NewNode deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("type OldNode;"), std::string::npos);
    {
        std::ifstream in(declarations, std::ios::binary);
        std::stringstream contents;
        contents << in.rdbuf();
        EXPECT_NE(contents.str().find("object OldNode"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_import_node_rename " + quoted_path(declarations) + " OldNode Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("type OldNode;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_node_rename " + quoted_path(declarations) + " OldNode NewNode"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("type NewNode;"), std::string::npos);
    EXPECT_NE(renamed.find("OldNode stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("type OldNode;"), std::string::npos);

    std::ifstream in(declarations, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    EXPECT_NE(contents.str().find("object NewNode"), std::string::npos);
    EXPECT_EQ(contents.str().find("object OldNode"), std::string::npos);
    EXPECT_EQ(env.nodes().find("OldNode"), nullptr);
    EXPECT_NE(env.nodes().find("NewNode"), nullptr);
}

TEST(CLIEditor, ApplyFilesNodeTypeRenameMigratesDiskGraphFilesAtomically) {
    auto dir = make_cli_temp_dir("files_node_type_rename");
    auto declarations = dir / "declared_nodes.d.gs";
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(declarations,
                   "export declare type FString;\n"
                   "export declare object OldNode {\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n"
                   "export declare object NewNode {\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n");
    write_cli_text(first_file,
                   "@Comment(\"OldNode stays string\")\n"
                   "graph First {\n"
                   "    node first {\n"
                   "        type OldNode;\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph Second {\n"
                   "    node second {\n"
                   "        type OldNode;\n"
                   "    }\n"
                   "    node existing {\n"
                   "        type NewNode;\n"
                   "    }\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_node_type_rename OldNode NewNode " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("type OldNode;"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("type OldNode;"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_node_type_rename OldNode NewNode " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("type NewNode;"), std::string::npos);
        EXPECT_NE(first_contents.str().find("OldNode stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("type OldNode;"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("type NewNode;"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("type OldNode;"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("NewNode"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_node_type_rename OldNode NewNode " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_node_type_rename OldNode Missing " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceNodePinRenameMigratesActiveModulePinReferences) {
    auto dir = make_cli_temp_dir("source_node_pin_rename");
    auto declarations = dir / "declared_nodes.d.gs";
    write_cli_text(declarations,
                   "export declare type Exec;\n"
                   "export declare type FString;\n"
                   "export declare object Worker {\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    enter: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    begin: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"out\")\n"
                   "    exit: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"out\")\n"
                   "    done: Exec;\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "    @flow.input\n"
                   "    body: FString;\n"
                   "    @flow.output\n"
                   "    result: FString;\n"
                   "    @flow.output\n"
                   "    output: FString;\n"
                   "}\n"
                   "export declare object SinkNode {\n"
                   "    @flow.input\n"
                   "    value: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"enter exit message result stay string\")\n"
        "graph UsesDeclaredPins {\n"
        "    @graph.input\n"
        "    param text: FString;\n"
        "    node worker {\n"
        "        type Worker;\n"
        "    }\n"
        "    node sink {\n"
        "        type SinkNode;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, worker.enter);\n"
        "        connect(worker.exit, context.done);\n"
        "        bind(text, worker.message);\n"
        "        bind(worker.result, sink.value);\n"
        "    }\n"
        "    event OnTick {\n"
        "        connect(context.start, worker.enter);\n"
        "        connect(worker.exit, context.done);\n"
        "        bind(text, worker.message);\n"
        "        bind(worker.result, sink.value);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker enter begin deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("worker.enter"), std::string::npos);
    EXPECT_EQ(session.emit().find("worker.begin"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker enter begin"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker exit done"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker message body"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker result output"));
    EXPECT_TRUE(editor.last_command_succeeded());

    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("connect(context.start, worker.begin)"), std::string::npos);
    EXPECT_NE(renamed.find("connect(worker.done, context.done)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(text, worker.body)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(worker.output, sink.value)"), std::string::npos);
    EXPECT_NE(renamed.find("enter exit message result stay string"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.enter"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.exit"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.message"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.result"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker enter begin"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename MissingWorker begin enter"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker missing begin"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker begin missing"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_node_pin_rename Worker begin Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyImportNodePinRenamePatchesDeclarationAndCurrentModuleReferences) {
    auto dir = make_cli_temp_dir("import_node_pin_rename");
    auto declarations = dir / "declared_nodes.d.gs";
    write_cli_text(declarations,
                   "export declare type Exec;\n"
                   "export declare type FString;\n"
                   "export declare object OldNode {\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    enter: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"out\")\n"
                   "    exit: Exec;\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "    @flow.output\n"
                   "    result: FString;\n"
                   "}\n"
                   "export declare object SinkNode {\n"
                   "    @flow.input\n"
                   "    value: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"enter exit message result stay string\")\n"
        "graph UsesDeclaredPins {\n"
        "    @graph.input\n"
        "    param text: FString;\n"
        "    node worker {\n"
        "        type OldNode;\n"
        "    }\n"
        "    node sink {\n"
        "        type SinkNode;\n"
        "    }\n"
        "    event OnStart {\n"
        "        connect(context.start, worker.enter);\n"
        "        connect(worker.exit, context.done);\n"
        "        bind(text, worker.message);\n"
        "        bind(worker.result, sink.value);\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode enter begin deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("worker.enter"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode message result"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("bind(text, worker.message)"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode enter begin"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode exit done"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode message body"));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_import_node_pin_rename " + quoted_path(declarations) + " OldNode result output"));
    EXPECT_TRUE(editor.last_command_succeeded());

    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("connect(context.start, worker.begin)"), std::string::npos);
    EXPECT_NE(renamed.find("connect(worker.done, context.done)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(text, worker.body)"), std::string::npos);
    EXPECT_NE(renamed.find("bind(worker.output, sink.value)"), std::string::npos);
    EXPECT_NE(renamed.find("enter exit message result stay string"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.enter"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.exit"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.message"), std::string::npos);
    EXPECT_EQ(renamed.find("worker.result"), std::string::npos);

    std::ifstream in(declarations, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    EXPECT_NE(contents.str().find("begin: Exec;"), std::string::npos);
    EXPECT_NE(contents.str().find("done: Exec;"), std::string::npos);
    EXPECT_NE(contents.str().find("body: FString;"), std::string::npos);
    EXPECT_NE(contents.str().find("output: FString;"), std::string::npos);
    EXPECT_EQ(contents.str().find("enter: Exec;"), std::string::npos);
    EXPECT_EQ(contents.str().find("exit: Exec;"), std::string::npos);
    EXPECT_EQ(contents.str().find("message: FString"), std::string::npos);
    EXPECT_EQ(contents.str().find("result: FString"), std::string::npos);

    const auto* node = env.nodes().find("OldNode");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->find_pin("enter"), nullptr);
    EXPECT_EQ(node->find_pin("exit"), nullptr);
    EXPECT_EQ(node->find_pin("message"), nullptr);
    EXPECT_EQ(node->find_pin("result"), nullptr);
    EXPECT_NE(node->find_pin("begin"), nullptr);
    EXPECT_NE(node->find_pin("done"), nullptr);
    EXPECT_NE(node->find_pin("body"), nullptr);
    EXPECT_NE(node->find_pin("output"), nullptr);
}

TEST(CLIEditor, ApplyFilesNodePinRenameMigratesDiskGraphPinReferencesAtomically) {
    auto dir = make_cli_temp_dir("files_node_pin_rename");
    auto declarations = dir / "declared_node_pins.d.gs";
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(declarations,
                   "export declare type Exec;\n"
                   "export declare type FString;\n"
                   "export declare object Worker {\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    enter: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    begin: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"out\")\n"
                   "    exit: Exec;\n"
                   "    @flow.pin(kind = \"exec\", direction = \"out\")\n"
                   "    done: Exec;\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "    @flow.input\n"
                   "    body: FString;\n"
                   "    @flow.output\n"
                   "    result: FString;\n"
                   "    @flow.output\n"
                   "    output: FString;\n"
                   "}\n"
                   "export declare object SinkNode {\n"
                   "    @flow.input\n"
                   "    value: FString;\n"
                   "}\n");
    write_cli_text(first_file,
                   "@Comment(\"enter message result stay string\")\n"
                   "graph First {\n"
                   "    @graph.input\n"
                   "    param text: FString;\n"
                   "    node worker {\n"
                   "        type Worker;\n"
                   "    }\n"
                   "    node sink {\n"
                   "        type SinkNode;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(context.start, worker.enter);\n"
                   "        bind(text, worker.message);\n"
                   "        bind(worker.result, sink.value);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph Second {\n"
                   "    @graph.input\n"
                   "    param text: FString;\n"
                   "    node worker {\n"
                   "        type Worker;\n"
                   "    }\n"
                   "    event OnStart {\n"
                   "        connect(worker.exit, context.done);\n"
                   "        bind(text, worker.message);\n"
                   "    }\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker message body " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("bind(text, worker.message)"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("bind(text, worker.body)"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("bind(text, worker.message)"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker enter begin " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker message body " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker result output " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker exit done " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("connect(context.start, worker.begin)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(text, worker.body)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("bind(worker.output, sink.value)"), std::string::npos);
        EXPECT_NE(first_contents.str().find("enter message result stay string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("worker.enter"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("worker.message"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("worker.result"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("connect(worker.done, context.done)"), std::string::npos);
        EXPECT_NE(second_contents.str().find("bind(text, worker.body)"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("worker.exit"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("worker.message"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("worker."), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker message body " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_node_pin_rename Worker missing body " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplySourceSchemaRenameMigratesActiveModuleGraphBaseTypes) {
    auto dir = make_cli_temp_dir("source_schema_rename");
    auto declarations = dir / "declared_schemas.d.gs";
    write_cli_text(declarations,
                   "export declare schema OldSchema: FlowGraphSchema {\n"
                   "    strict_type_match: true;\n"
                   "}\n"
                   "export declare schema NewSchema: FlowGraphSchema {\n"
                   "    strict_type_match: false;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"OldSchema stays string\")\n"
        "graph UsesSchema {\n"
        "    schema OldSchema;\n"
        "}\n"
        "graph AlsoUsesSchema {\n"
        "    schema OldSchema;\n"
        "}\n"
        "graph AlreadyNew {\n"
        "    schema NewSchema;\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_schema_rename OldSchema NewSchema deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("schema OldSchema;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_schema_rename OldSchema NewSchema"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("graph UsesSchema"), std::string::npos);
    EXPECT_NE(renamed.find("schema NewSchema;"), std::string::npos);
    EXPECT_NE(renamed.find("OldSchema stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("schema OldSchema;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_schema_rename OldSchema NewSchema"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_schema_rename NewSchema MissingSchema"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_source_schema_rename NewSchema Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyImportSchemaRenamePatchesDeclarationAndCurrentGraphBaseTypes) {
    auto dir = make_cli_temp_dir("import_schema_rename");
    auto declarations = dir / "declared_schemas.d.gs";
    write_cli_text(declarations,
                   "export declare schema OldSchema: FlowGraphSchema {\n"
                   "    strict_type_match: true;\n"
                   "}\n"
                   "export declare schema ExistingSchema: FlowGraphSchema {\n"
                   "    strict_type_match: false;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "@Comment(\"OldSchema stays string\")\n"
        "graph UsesSchema {\n"
        "    schema OldSchema;\n"
        "}\n"
        "graph Plain {\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_schema_rename " + quoted_path(declarations) + " OldSchema NewSchema deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("schema OldSchema;"), std::string::npos);
    {
        std::ifstream in(declarations, std::ios::binary);
        std::stringstream contents;
        contents << in.rdbuf();
        EXPECT_NE(contents.str().find("schema OldSchema"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_import_schema_rename " + quoted_path(declarations) + " OldSchema ExistingSchema"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("schema OldSchema;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_schema_rename " + quoted_path(declarations) + " OldSchema Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("schema OldSchema;"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_schema_rename " + quoted_path(declarations) + " OldSchema NewSchema"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("schema NewSchema;"), std::string::npos);
    EXPECT_NE(renamed.find("OldSchema stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("schema OldSchema;"), std::string::npos);

    std::ifstream in(declarations, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    EXPECT_NE(contents.str().find("schema NewSchema"), std::string::npos);
    EXPECT_EQ(contents.str().find("schema OldSchema"), std::string::npos);
    EXPECT_EQ(env.schemas().find("OldSchema"), nullptr);
    EXPECT_NE(env.schemas().find("NewSchema"), nullptr);
    EXPECT_NE(env.schemas().find("ExistingSchema"), nullptr);
}

TEST(CLIEditor, ApplyImportSchemaFieldRenamePatchesDeclarationFieldName) {
    auto dir = make_cli_temp_dir("import_schema_field_rename");
    auto declarations = dir / "declared_schema_fields.d.gs";
    write_cli_text(declarations,
                   "export declare type PayloadType: constructible;\n"
                   "export declare schema PreviewSchema: FlowGraphSchema {\n"
                   "    max_exec_fan_out: 1;\n"
                   "    default_payload: PayloadType(\"seed\");\n"
                   "    existing_field: true;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const auto* schema = env.schemas().find("PreviewSchema");
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->fields.size(), 3u);
    EXPECT_EQ(schema->fields[1].name, "default_payload");

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " PreviewSchema default_payload renamed_payload deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream in(declarations, std::ios::binary);
        std::stringstream contents;
        contents << in.rdbuf();
        EXPECT_NE(contents.str().find("default_payload: PayloadType(\"seed\");"), std::string::npos);
        EXPECT_EQ(contents.str().find("renamed_payload: PayloadType(\"seed\");"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " PreviewSchema default_payload existing_field"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " MissingSchema default_payload renamed_payload"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " PreviewSchema missing renamed_payload"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " PreviewSchema default_payload Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_schema_field_rename " + quoted_path(declarations) + " PreviewSchema default_payload renamed_payload"));
    EXPECT_TRUE(editor.last_command_succeeded());

    std::ifstream in(declarations, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    EXPECT_NE(contents.str().find("renamed_payload: PayloadType(\"seed\");"), std::string::npos);
    EXPECT_NE(contents.str().find("existing_field: true;"), std::string::npos);
    EXPECT_EQ(contents.str().find("default_payload: PayloadType(\"seed\");"), std::string::npos);

    schema = env.schemas().find("PreviewSchema");
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->fields.size(), 3u);
    EXPECT_EQ(schema->fields[0].name, "max_exec_fan_out");
    EXPECT_EQ(schema->fields[1].name, "renamed_payload");
    EXPECT_EQ(schema->fields[1].value, "PayloadType(\"seed\")");
    EXPECT_EQ(schema->fields[2].name, "existing_field");
}

TEST(CLIEditor, ApplyFilesSchemaRenameMigratesDiskGraphBaseTypesAtomically) {
    auto dir = make_cli_temp_dir("files_schema_rename");
    auto declarations = dir / "declared_schemas.d.gs";
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(declarations,
                   "export declare schema OldSchema: FlowGraphSchema {\n"
                   "    strict_type_match: true;\n"
                   "}\n"
                   "export declare schema NewSchema: FlowGraphSchema {\n"
                   "    strict_type_match: false;\n"
                   "}\n");
    write_cli_text(first_file,
                   "@Comment(\"OldSchema stays string\")\n"
                   "graph First {\n"
                   "    schema OldSchema;\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph Second {\n"
                   "    schema OldSchema;\n"
                   "}\n"
                   "graph AlreadyNew {\n"
                   "    schema NewSchema;\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_schema_rename OldSchema NewSchema " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("schema OldSchema;"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("schema NewSchema;"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("schema OldSchema;"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_schema_rename OldSchema NewSchema " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("schema NewSchema;"), std::string::npos);
        EXPECT_NE(first_contents.str().find("OldSchema stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("schema OldSchema;"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("schema NewSchema;"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("schema OldSchema;"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("NewSchema"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_schema_rename OldSchema NewSchema " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_schema_rename OldSchema MissingSchema " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyFilesTypeRenameMigratesDiskGraphTypeReferencesAtomically) {
    auto dir = make_cli_temp_dir("files_type_rename");
    auto declarations = dir / "declared_types.d.gs";
    auto first_file = dir / "first.gs";
    auto second_file = dir / "second.gs";
    auto untouched_file = dir / "untouched.gs";
    write_cli_text(declarations,
                   "export declare type OldType: constructible;\n"
                   "export declare type NewType: constructible;\n"
                   "export declare object Holder {\n"
                   "    @flow.input\n"
                   "    value: OldType;\n"
                   "}\n");
    write_cli_text(first_file,
                   "@Bind(Type = OldType(\"let annotation OldType\"))\n"
                   "const cached = new OldType {\n"
                   "}\n"
                   "@Comment(\"OldType stays string\")\n"
                   "@Bind(Type = OldType(\"graph annotation OldType\"))\n"
                   "graph First {\n"
                   "    @Bind(Type = OldType(\"param annotation OldType\"))\n"
                   "    @graph.input\n"
                   "    param input: OldType = OldType(\"default OldType\");\n"
                   "    @Bind(Type = OldType(\"node annotation OldType\"))\n"
                   "    node fieldInit {\n"
                   "        type Holder;\n"
                   "        value: OldType(\"field OldType\");\n"
                   "    }\n"
                   "    @Bind(Type = OldType(\"event annotation OldType\"))\n"
                   "    event OnStart {\n"
                   "    }\n"
                   "}\n");
    write_cli_text(second_file,
                   "graph Second {\n"
                   "    @graph.input\n"
                   "    param input: OldType;\n"
                   "    @graph.output\n"
                   "    param output: OldType;\n"
                   "    @graph.input\n"
                   "    param existing: NewType;\n"
                   "}\n");
    write_cli_text(untouched_file,
                   "graph Plain {\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    EXPECT_TRUE(editor.execute("apply_files_type_rename OldType NewType " +
                               quoted_path(first_file) + " " + quoted_path(second_file) +
                               " --hash " + quoted_path(first_file) + " deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("@Bind(Type = OldType(\"let annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = OldType(\"graph annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = OldType(\"param annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = OldType(\"node annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = OldType(\"event annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("const cached = new OldType"), std::string::npos);
        EXPECT_NE(first_contents.str().find("param input: OldType = OldType(\"default OldType\")"), std::string::npos);
        EXPECT_NE(first_contents.str().find("value: OldType(\"field OldType\")"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("Bind(Type = NewType"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("const cached = new NewType"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("param input: NewType = NewType(\"default OldType\")"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("param input: OldType"), std::string::npos);
        EXPECT_NE(second_contents.str().find("param output: OldType"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_type_rename OldType NewType " +
                               quoted_path(first_file) + " " + quoted_path(second_file) + " " +
                               quoted_path(untouched_file)));
    EXPECT_TRUE(editor.last_command_succeeded());
    {
        std::ifstream first_in(first_file, std::ios::binary);
        std::stringstream first_contents;
        first_contents << first_in.rdbuf();
        EXPECT_NE(first_contents.str().find("@Bind(Type = NewType(\"let annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = NewType(\"graph annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = NewType(\"param annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = NewType(\"node annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("Bind(Type = NewType(\"event annotation OldType\"))"), std::string::npos);
        EXPECT_NE(first_contents.str().find("const cached = new NewType"), std::string::npos);
        EXPECT_NE(first_contents.str().find("param input: NewType = NewType(\"default OldType\")"), std::string::npos);
        EXPECT_NE(first_contents.str().find("value: NewType(\"field OldType\")"), std::string::npos);
        EXPECT_NE(first_contents.str().find("OldType stays string"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("Bind(Type = OldType"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("const cached = new OldType"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("param input: OldType"), std::string::npos);
        EXPECT_EQ(first_contents.str().find("value: OldType(\"field OldType\")"), std::string::npos);

        std::ifstream second_in(second_file, std::ios::binary);
        std::stringstream second_contents;
        second_contents << second_in.rdbuf();
        EXPECT_NE(second_contents.str().find("param input: NewType"), std::string::npos);
        EXPECT_NE(second_contents.str().find("param output: NewType"), std::string::npos);
        EXPECT_NE(second_contents.str().find("param existing: NewType"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("param input: OldType"), std::string::npos);
        EXPECT_EQ(second_contents.str().find("param output: OldType"), std::string::npos);

        std::ifstream untouched_in(untouched_file, std::ios::binary);
        std::stringstream untouched_contents;
        untouched_contents << untouched_in.rdbuf();
        EXPECT_NE(untouched_contents.str().find("graph Plain"), std::string::npos);
        EXPECT_EQ(untouched_contents.str().find("NewType"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_files_type_rename OldType NewType " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_files_type_rename OldType MissingType " + quoted_path(untouched_file)));
    EXPECT_FALSE(editor.last_command_succeeded());
}

TEST(CLIEditor, ApplyImportTypeRenamePatchesDeclarationAndCurrentModuleReferences) {
    auto dir = make_cli_temp_dir("import_type_rename");
    auto declarations = dir / "declared_types.d.gs";
    write_cli_text(declarations,
                   "export declare @Bind(Type = OldType(\"decl type annotation OldType\")) type OldType: constructible;\n"
                   "export declare type ExistingType;\n"
                   "export declare @Bind(Type = OldType(\"node annotation OldType\")) object Passthrough {\n"
                   "    @Bind(Type = OldType(\"pin annotation OldType\"))\n"
                   "    @flow.input\n"
                   "    input: OldType;\n"
                   "    @flow.output\n"
                   "    output: OldType;\n"
                   "}\n"
                   "export declare @Bind(Type = OldType(\"schema annotation OldType\")) schema PreviewSchema: FlowGraphSchema {\n"
                   "    @Bind(Type = OldType(\"schema field annotation OldType\"))\n"
                   "    default_payload: OldType(\"schema field OldType\");\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    auto imported = session.load_import(declarations.string());
    ASSERT_TRUE(imported.is_ok()) << imported.error();
    CLIEditor editor(session);

    const std::string source =
        "const cached = new OldType {\n"
        "}\n"
        "@Comment(\"OldType stays string\")\n"
        "graph UsesTypes {\n"
        "    @graph.input\n"
        "    param input: OldType;\n"
        "    @graph.output\n"
        "    param output: OldType;\n"
        "    node worker {\n"
        "        type Passthrough;\n"
        "    }\n"
        "}\n";
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source)));
    EXPECT_TRUE(editor.last_command_succeeded());

    EXPECT_TRUE(editor.execute("apply_import_type_rename " + quoted_path(declarations) + " OldType NewType deadbeefdeadbeef"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("const cached = new OldType"), std::string::npos);
    {
        std::ifstream in(declarations, std::ios::binary);
        std::stringstream contents;
        contents << in.rdbuf();
        EXPECT_NE(contents.str().find("Bind(Type = OldType(\"decl type annotation OldType\"))"), std::string::npos);
        EXPECT_NE(contents.str().find("default_payload: OldType(\"schema field OldType\")"), std::string::npos);
        EXPECT_NE(contents.str().find("type OldType"), std::string::npos);
        EXPECT_EQ(contents.str().find("Bind(Type = NewType"), std::string::npos);
    }

    EXPECT_TRUE(editor.execute("apply_import_type_rename " + quoted_path(declarations) + " OldType ExistingType"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param input: OldType"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_type_rename " + quoted_path(declarations) + " OldType Graph"));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("param output: OldType"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_import_type_rename " + quoted_path(declarations) + " OldType NewType"));
    EXPECT_TRUE(editor.last_command_succeeded());
    const std::string renamed = session.emit();
    EXPECT_NE(renamed.find("const cached = new NewType"), std::string::npos);
    EXPECT_NE(renamed.find("param input: NewType"), std::string::npos);
    EXPECT_NE(renamed.find("param output: NewType"), std::string::npos);
    EXPECT_NE(renamed.find("OldType stays string"), std::string::npos);
    EXPECT_EQ(renamed.find("const cached = new OldType"), std::string::npos);
    EXPECT_EQ(renamed.find("param input: OldType"), std::string::npos);
    EXPECT_EQ(renamed.find("param output: OldType"), std::string::npos);

    std::ifstream in(declarations, std::ios::binary);
    std::stringstream contents;
    contents << in.rdbuf();
    EXPECT_NE(contents.str().find("Bind(Type = NewType(\"decl type annotation OldType\"))"), std::string::npos);
    EXPECT_NE(contents.str().find("Bind(Type = NewType(\"node annotation OldType\"))"), std::string::npos);
    EXPECT_NE(contents.str().find("Bind(Type = NewType(\"pin annotation OldType\"))"), std::string::npos);
    EXPECT_NE(contents.str().find("Bind(Type = NewType(\"schema annotation OldType\"))"), std::string::npos);
    EXPECT_NE(contents.str().find("Bind(Type = NewType(\"schema field annotation OldType\"))"), std::string::npos);
    EXPECT_NE(contents.str().find("type NewType: constructible;"), std::string::npos);
    EXPECT_NE(contents.str().find("input: NewType;"), std::string::npos);
    EXPECT_NE(contents.str().find("output: NewType;"), std::string::npos);
    EXPECT_NE(contents.str().find("default_payload: NewType(\"schema field OldType\");"), std::string::npos);
    EXPECT_EQ(contents.str().find("Bind(Type = OldType"), std::string::npos);
    EXPECT_EQ(contents.str().find("type OldType"), std::string::npos);
    EXPECT_EQ(contents.str().find(": OldType"), std::string::npos);
    EXPECT_EQ(contents.str().find("default_payload: OldType"), std::string::npos);

    EXPECT_EQ(env.types().find("OldType"), nullptr);
    const auto* new_type = env.types().find("NewType");
    ASSERT_NE(new_type, nullptr);
    EXPECT_TRUE(new_type->constructible);
    EXPECT_NE(env.types().find("ExistingType"), nullptr);

    const auto* node = env.nodes().find("Passthrough");
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->find_pin("input"), nullptr);
    ASSERT_NE(node->find_pin("output"), nullptr);
    EXPECT_EQ(node->find_pin("input")->type_name, "NewType");
    EXPECT_EQ(node->find_pin("output")->type_name, "NewType");
}

TEST(CLIEditor, ApplySourceCommandsSupportEnvironmentGuard) {
    auto dir = make_cli_temp_dir("asset_guard");
    auto declarations = dir / "custom.d.gs";
    write_cli_text(declarations,
                   "export declare type Exec;\n"
                   "export declare type FString;\n"
                   "export declare object CustomPrint {\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    enter: Exec;\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    const std::string source =
        "import \"custom.d.gs\";\n"
        "graph Guarded {\n"
        "    node logger {\n"
        "        type CustomPrint;\n"
        "    }\n"
        "}\n";
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();
    auto hash = source_diagnostics_environment_hash(source, session.env(), options);
    ASSERT_TRUE(hash.is_ok()) << hash.error();

    const std::string guarded_args =
        " --env-hash " + hash.value() +
        " --resolve-imports --base-dir " + quoted_path(dir);
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source) + guarded_args));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "Guarded");

    std::string patched_source = session.emit();
    auto logger = patched_source.find("logger");
    ASSERT_NE(logger, std::string::npos);
    patched_source.replace(logger, std::string("logger").size(), "writer");
    auto patched_hash = source_diagnostics_environment_hash(patched_source, session.env(), options);
    ASSERT_TRUE(patched_hash.is_ok()) << patched_hash.error();
    const std::string patched_guard_args =
        " --env-hash " + patched_hash.value() +
        " --resolve-imports --base-dir " + quoted_path(dir);

    EXPECT_TRUE(editor.execute("apply_source_patch 3 10 3 16 d3JpdGVy" + patched_guard_args));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node writer"), std::string::npos);

    EXPECT_TRUE(editor.execute("apply_source_patch 3 10 3 16 bG9nZ2Vy --env-hash definitely-wrong --resolve-imports --base-dir " + quoted_path(dir)));
    EXPECT_FALSE(editor.last_command_succeeded());
    EXPECT_NE(session.emit().find("node writer"), std::string::npos);
    EXPECT_EQ(session.emit().find("node logger"), std::string::npos);
}

TEST(CLIEditor, NestedResolvedImportReplayLoadsDependenciesBeforeGuardedSourceApply) {
    auto dir = make_cli_temp_dir("nested_replay");
    auto types = dir / "types.d.gs";
    auto nodes = dir / "nodes.d.gs";
    write_cli_text(types,
                   "export declare type Exec;\n"
                   "export declare type FString;\n");
    write_cli_text(nodes,
                   "import \"types.d.gs\";\n"
                   "export declare object CustomPrint {\n"
                   "    @flow.pin(kind = \"exec\", direction = \"in\")\n"
                   "    enter: Exec;\n"
                   "    @flow.input\n"
                   "    message: FString;\n"
                   "}\n");

    Environment env;
    EditSession session(env);
    CLIEditor editor(session);

    const std::string source =
        "import \"nodes.d.gs\";\n"
        "graph Replay {\n"
        "    node printer {\n"
        "        type CustomPrint;\n"
        "    }\n"
        "}\n";
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    auto preview_hash = source_diagnostics_environment_hash(source, session.env(), options);
    ASSERT_TRUE(preview_hash.is_ok()) << preview_hash.error();
    EXPECT_EQ(session.env().nodes().find("CustomPrint"), nullptr);

    EXPECT_TRUE(editor.execute("import " + quoted_path(types)));
    EXPECT_TRUE(editor.last_command_succeeded());
    EXPECT_TRUE(editor.execute("import " + quoted_path(nodes)));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.env().nodes().find("CustomPrint"), nullptr);

    auto hash = source_diagnostics_environment_hash(source, session.env(), options);
    ASSERT_TRUE(hash.is_ok()) << hash.error();
    const std::string guarded_args =
        " --env-hash " + hash.value() +
        " --resolve-imports --base-dir " + quoted_path(dir);
    EXPECT_TRUE(editor.execute("apply_source_b64 " + b64_for_cli_test(source) + guarded_args));
    EXPECT_TRUE(editor.last_command_succeeded());
    ASSERT_NE(session.active_graph(), nullptr);
    EXPECT_EQ(session.active_graph()->name, "Replay");
    ASSERT_EQ(session.active_graph()->node_instances.size(), 1u);
    EXPECT_EQ(session.active_graph()->node_instances[0].type_name, "CustomPrint");
    ASSERT_GE(session.module().imports.size(), 1u);
    EXPECT_TRUE(session.module().imports[0].loaded);
}
