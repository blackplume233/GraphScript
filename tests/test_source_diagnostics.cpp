#include <gtest/gtest.h>

#include "source_diagnostics.h"

#include <filesystem>
#include <fstream>

using namespace gs;

namespace {

std::filesystem::path make_temp_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / ("graphscript_" + name);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST(SourceDiagnostics, ResolveImportsLoadsDeclarationInDryRunEnvironment) {
    auto dir = make_temp_dir("source_diag_loads");
    write_text(dir / "custom.d.gs",
               "declare type FString;\n"
               "declare Node CustomPrint {\n"
               "    exec in enter;\n"
               "    exec out exit;\n"
               "    data in message : FString;\n"
               "}\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"custom.d.gs\";\n"
        "Graph Demo {\n"
        "    CustomPrint printer{};\n"
        "}\n";

    auto json = source_diagnostics_to_json(source, env, options);

    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(json.find("\"mode\":\"resolved\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"loaded\""), std::string::npos);
    EXPECT_NE(json.find("\"path\":\"custom.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"command\":\"import "), std::string::npos);
    EXPECT_NE(json.find("\"node_type_count\":1"), std::string::npos);

    EXPECT_EQ(env.nodes().all().size(), 0u);
    EXPECT_EQ(env.types().all().size(), 0u);
}

TEST(SourceDiagnostics, ResolveImportsLoadsNestedDeclarations) {
    auto dir = make_temp_dir("source_diag_nested");
    write_text(dir / "types.d.gs",
               "declare type FString;\n");
    write_text(dir / "nodes.d.gs",
               "import \"types.d.gs\";\n"
               "declare Node CustomPrint {\n"
               "    exec in enter;\n"
               "    data in message : FString;\n"
               "}\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"nodes.d.gs\";\n"
        "Graph Demo {\n"
        "    CustomPrint printer{};\n"
        "}\n";

    auto json = source_diagnostics_to_json(source, env, options);

    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(json.find("\"path\":\"types.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"path\":\"nodes.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"parent_path\":\"nodes.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"parent_normalized_path\":"), std::string::npos);
    EXPECT_NE(json.find("\"import_chain\":\"source -> nodes.d.gs -> types.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"import_chain\":\"source -> nodes.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"depth\":2"), std::string::npos);
    EXPECT_NE(json.find("\"depth\":1"), std::string::npos);
    EXPECT_NE(json.find("\"type_count\":1"), std::string::npos);
    EXPECT_NE(json.find("\"node_type_count\":1"), std::string::npos);
    EXPECT_EQ(env.nodes().all().size(), 0u);
    EXPECT_EQ(env.types().all().size(), 0u);
}

TEST(SourceDiagnostics, ResolveImportsDetectsCycles) {
    auto dir = make_temp_dir("source_diag_cycle");
    write_text(dir / "a.d.gs",
               "import \"b.d.gs\";\n"
               "declare type A;\n");
    write_text(dir / "b.d.gs",
               "import \"a.d.gs\";\n"
               "declare type B;\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"a.d.gs\";\n"
        "Graph Demo {}\n";

    auto json = source_diagnostics_to_json(source, env, options);
    auto hash = source_diagnostics_environment_hash(source, env, options);

    EXPECT_NE(json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"GS_IMPORT_CYCLE\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"cycle\""), std::string::npos);
    EXPECT_NE(json.find("\"import_chain\":\"source -> a.d.gs -> b.d.gs -> a.d.gs\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"dependency_error\""), std::string::npos);
    EXPECT_TRUE(hash.is_err());
    EXPECT_NE(hash.error().find("cycle"), std::string::npos);
    EXPECT_EQ(env.types().all().size(), 0u);
}

TEST(SourceDiagnostics, ResolveImportsEnforcesMaxDepth) {
    auto dir = make_temp_dir("source_diag_depth");
    write_text(dir / "a.d.gs",
               "import \"b.d.gs\";\n"
               "declare type A;\n");
    write_text(dir / "b.d.gs",
               "declare type B;\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();
    options.max_import_depth = 1;

    std::string source =
        "import \"a.d.gs\";\n"
        "Graph Demo {}\n";

    auto json = source_diagnostics_to_json(source, env, options);

    EXPECT_NE(json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"GS_IMPORT_DEPTH_EXCEEDED\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"too_deep\""), std::string::npos);
    EXPECT_NE(json.find("\"max_import_depth\":1"), std::string::npos);
    EXPECT_EQ(env.types().all().size(), 0u);
}

TEST(SourceDiagnostics, ResolveImportsRejectsPathEscape) {
    auto root = make_temp_dir("source_diag_root");
    auto outside = root.parent_path() / "outside_graphscript_decl.d.gs";
    write_text(outside, "declare type FString;\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = root.string();

    std::string source =
        "import \"../outside_graphscript_decl.d.gs\";\n"
        "Graph Demo {}\n";

    auto json = source_diagnostics_to_json(source, env, options);

    EXPECT_NE(json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(json.find("\"stage\":\"resolver\""), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"GS_IMPORT_PATH_BLOCKED\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"blocked\""), std::string::npos);
    EXPECT_EQ(env.types().all().size(), 0u);
}

TEST(SourceDiagnostics, ResolveImportsRejectsNonDeclarationImport) {
    auto dir = make_temp_dir("source_diag_unsupported");
    write_text(dir / "other.gs", "Graph Other {}\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"other.gs\";\n"
        "Graph Demo {}\n";

    auto json = source_diagnostics_to_json(source, env, options);

    EXPECT_NE(json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(json.find("\"id\":\"diagnostic:GS_IMPORT_UNSUPPORTED_EXTENSION"), std::string::npos);
    EXPECT_NE(json.find("\"code\":\"GS_IMPORT_UNSUPPORTED_EXTENSION\""), std::string::npos);
    EXPECT_NE(json.find("\"status\":\"unsupported\""), std::string::npos);
    EXPECT_EQ(env.nodes().all().size(), 0u);
}

TEST(SourceDiagnostics, DefaultDiagnosticsDoNotIncludeResolverMetadata) {
    Environment env;
    std::string source = "Graph Demo {}\n";

    auto json = source_diagnostics_to_json(source, env);

    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_EQ(json.find("\"environment\""), std::string::npos);
}

TEST(SourceDiagnostics, EnvironmentHashChangesWhenResolvedDeclarationChanges) {
    auto dir = make_temp_dir("source_diag_hash");
    auto declaration = dir / "custom.d.gs";
    write_text(declaration,
               "declare type FString;\n"
               "declare Node CustomPrint {\n"
               "    exec in enter;\n"
               "}\n");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"custom.d.gs\";\n"
        "Graph Demo {\n"
        "    CustomPrint printer{};\n"
        "}\n";

    auto first = source_diagnostics_environment_hash(source, env, options);
    ASSERT_TRUE(first.is_ok());

    write_text(declaration,
               "declare type FString;\n"
               "declare Node CustomPrint {\n"
               "    exec in enter;\n"
               "    exec out exit;\n"
               "}\n");
    auto second = source_diagnostics_environment_hash(source, env, options);
    ASSERT_TRUE(second.is_ok());

    EXPECT_NE(first.value(), second.value());
}

TEST(SourceDiagnostics, EnvironmentHashFailsWhenResolverHasDiagnostics) {
    auto dir = make_temp_dir("source_diag_hash_blocked");

    Environment env;
    SourceDiagnosticsOptions options;
    options.resolve_imports = true;
    options.base_dir = dir.string();

    std::string source =
        "import \"../blocked.d.gs\";\n"
        "Graph Demo {}\n";

    auto hash = source_diagnostics_environment_hash(source, env, options);

    EXPECT_TRUE(hash.is_err());
    EXPECT_NE(hash.error().find("Blocked source import"), std::string::npos);
}
