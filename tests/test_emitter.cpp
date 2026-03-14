#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "graphscript/parse/lexer.h"
#include "graphscript/parse/parser.h"
#include "graphscript/compile/compiler.h"
#include "graphscript/emit/emitter.h"

using namespace gs;

static std::string read_file(const std::string& dir, const std::string& filename) {
    std::string path = dir + "/" + filename;
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string read_fixture(const std::string& filename) {
    return read_file(GS_TEST_FIXTURES_DIR, filename);
}

static std::string read_preset(const std::string& filename) {
    return read_file(GS_PRESETS_DIR, filename);
}

static Module compile_file(const std::string& src, Environment& env) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    if (ast.is_err()) return {};
    Compiler compiler(env);
    auto result = compiler.compile(*ast.value());
    if (result.is_err()) return {};
    return std::move(result).value();
}

TEST(Emitter, EmitMinimal) {
    Environment env;
    auto core = read_preset("ue_core.d.gs");
    compile_file(core, env);

    auto src = read_fixture("minimal.gs");
    auto mod = compile_file(src, env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("import \"ue_core.d.gs\""), std::string::npos);
    EXPECT_NE(output.find("Graph HelloWorld"), std::string::npos);
    EXPECT_NE(output.find("in message : FString"), std::string::npos);
    EXPECT_NE(output.find("PrintString printer{}"), std::string::npos);
    EXPECT_NE(output.find("event OnStart"), std::string::npos);
}

TEST(Emitter, EmitWithBaseType) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);
    compile_file(read_preset("htn_nodes.d.gs"), env);

    auto mod = compile_file(read_fixture("htn_basic.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("Graph SimpleHTN : HTNGraph"), std::string::npos);
}

TEST(Emitter, EmitLetDecl) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("let actor_1 = SoftObjectPath(\"actor_path_1\")"), std::string::npos);
}

TEST(Emitter, EmitGenerate) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("[Comment("), std::string::npos);
    EXPECT_NE(output.find("[Position("), std::string::npos);
}

TEST(Emitter, EmitFunction) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("function Calculate"), std::string::npos);
}

TEST(Emitter, EmitVarParam) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("var temp : float"), std::string::npos);
}

TEST(Emitter, RoundTripReparse) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod1 = compile_file(read_fixture("minimal.gs"), env);

    Emitter emitter;
    auto emitted = emitter.emit(mod1);

    // Re-parse the emitted output
    Environment env2;
    compile_file(read_preset("ue_core.d.gs"), env2);
    auto mod2 = compile_file(emitted, env2);

    ASSERT_EQ(mod2.graphs.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].name, "HelloWorld");
    EXPECT_EQ(mod2.graphs[0].parameters.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].node_instances.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].events.size(), 1u);
}

TEST(Emitter, EmitLinkBareParam) {
    Environment env;
    compile_file(read_preset("ue_core.d.gs"), env);

    auto mod = compile_file(read_fixture("minimal.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("link printer.message = message"), std::string::npos);
}
