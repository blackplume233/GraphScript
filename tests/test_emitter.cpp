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

static constexpr const char* kLegacyCoreDeclarations = R"(// Legacy declarations for emitter-layer tests only.
declare type FName;
declare type FString;
declare type FVector : constructible;
declare type FRotator : constructible;
declare type SoftObjectPath : constructible;
declare type AActor;
declare type UObject;
declare type float;
declare type int;
declare type bool;

declare Node PrintString {
    exec in enter;
    exec out exit;
    field message : FString = "";
    data in message : FString;
}

declare Node Delay {
    exec in enter;
    exec out completed;
    field duration : float = 0.2;
    data in duration : float;
}

declare Node GetActorLocation {
    data in target : AActor;
    data out location : FVector;
}
)";

static constexpr const char* kLegacyHtnDeclarations = R"(// HTN domain node declarations
declare type HTNTask;
declare type HTNCondition;

declare Node HTN_MoveToTarget {
    exec in enter;
    exec out success;
    exec out fail;
    data in target : AActor;
    data in speed : float;
}

declare Node HTN_CheckDistance {
    exec in enter;
    exec out inRange;
    exec out outOfRange;
    data in target : AActor;
    data in threshold : float;
}

declare Schema HTNGraph {
    max_exec_fan_out: unlimited;
    allow_exec_fan_in: false;
    strict_type_match: true;
    allowed_node_tags: ["htn_task", "htn_decorator", "htn_service", "common"];
}
)";

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
    compile_file(kLegacyCoreDeclarations, env);

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
    compile_file(kLegacyCoreDeclarations, env);
    compile_file(kLegacyHtnDeclarations, env);

    auto mod = compile_file(read_fixture("htn_basic.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("Graph SimpleHTN : HTNGraph"), std::string::npos);
}

TEST(Emitter, EmitLetDecl) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("let actor_1 = SoftObjectPath(\"actor_path_1\")"), std::string::npos);
}

TEST(Emitter, EmitGenerate) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("[Comment("), std::string::npos);
    EXPECT_NE(output.find("[Position("), std::string::npos);
}

TEST(Emitter, EmitAnnotationConstructorValueRaw) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(R"(Graph ConstructorAnnotation {
    [Position(Asset = SoftObjectPath("Meta"))]
    PrintString p{};
}
)", env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("Asset = SoftObjectPath(\"Meta\")"), std::string::npos);
    EXPECT_EQ(output.find("Asset = \"SoftObjectPath"), std::string::npos);

    Environment env2;
    compile_file(kLegacyCoreDeclarations, env2);
    auto reparsed = compile_file(output, env2);

    ASSERT_EQ(reparsed.graphs.size(), 1u);
    ASSERT_EQ(reparsed.graphs[0].node_instances.size(), 1u);
    ASSERT_EQ(reparsed.graphs[0].node_instances[0].annotations.size(), 1u);
    ASSERT_EQ(reparsed.graphs[0].node_instances[0].annotations[0].args.size(), 1u);
    EXPECT_EQ(reparsed.graphs[0].node_instances[0].annotations[0].args[0].value, "SoftObjectPath(\"Meta\")");
}

TEST(Emitter, EmitConstructorStringArgumentsRoundTrip) {
    Environment env;
    compile_file(R"(
declare type OldType : constructible;
declare Node Holder {
    data in value : OldType;
}
)", env);

    auto mod = compile_file(R"(
Graph ConstructorStrings {
    in input : OldType = OldType("default OldType");
    Holder fieldInit{value = OldType("field OldType")};
    Holder rawInit{OldType("raw OldType")};
    generate {
        position:fieldInit.asset(OldType("metadata OldType"));
    }
}
)", env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("in input : OldType = OldType(\"default OldType\")"), std::string::npos);
    EXPECT_NE(output.find("Holder fieldInit{value = OldType(\"field OldType\")}"), std::string::npos);
    EXPECT_NE(output.find("Holder rawInit{OldType(\"raw OldType\")}"), std::string::npos);
    EXPECT_NE(output.find("position:fieldInit.asset(OldType(\"metadata OldType\"))"), std::string::npos);

    Environment env2;
    compile_file(R"(
declare type OldType : constructible;
declare Node Holder {
    data in value : OldType;
}
)", env2);
    auto reparsed = compile_file(output, env2);

    ASSERT_EQ(reparsed.graphs.size(), 1u);
    ASSERT_EQ(reparsed.graphs[0].parameters.size(), 1u);
    EXPECT_EQ(reparsed.graphs[0].parameters[0].default_value, "OldType(\"default OldType\")");
    ASSERT_EQ(reparsed.graphs[0].node_instances.size(), 2u);
    EXPECT_EQ(reparsed.graphs[0].node_instances[0].initializer, "value = OldType(\"field OldType\")");
    ASSERT_EQ(reparsed.graphs[0].node_instances[0].initializer_fields.size(), 1u);
    EXPECT_EQ(reparsed.graphs[0].node_instances[0].initializer_fields[0].value, "OldType(\"field OldType\")");
    EXPECT_EQ(reparsed.graphs[0].node_instances[1].initializer, "OldType(\"raw OldType\")");
}

TEST(Emitter, EmitFunction) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("function Calculate"), std::string::npos);
}

TEST(Emitter, EmitVarParam) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(read_fixture("round_trip.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("var temp : float"), std::string::npos);
}

TEST(Emitter, RoundTripReparse) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod1 = compile_file(read_fixture("minimal.gs"), env);

    Emitter emitter;
    auto emitted = emitter.emit(mod1);

    // Re-parse the emitted output
    Environment env2;
    compile_file(kLegacyCoreDeclarations, env2);
    auto mod2 = compile_file(emitted, env2);

    ASSERT_EQ(mod2.graphs.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].name, "HelloWorld");
    EXPECT_EQ(mod2.graphs[0].parameters.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].node_instances.size(), 1u);
    EXPECT_EQ(mod2.graphs[0].events.size(), 1u);
}

TEST(Emitter, EmitLinkBareParam) {
    Environment env;
    compile_file(kLegacyCoreDeclarations, env);

    auto mod = compile_file(read_fixture("minimal.gs"), env);

    Emitter emitter;
    auto output = emitter.emit(mod);

    EXPECT_NE(output.find("printer.message = message"), std::string::npos);
}
