#include <gtest/gtest.h>

#include "graphscript/edit/edit_session.h"

using namespace gs;

static void load_asset_source(EditSession& session, const std::string& source) {
    auto loaded = session.load_source(source, "emitter_asset_test.gs");
    EXPECT_TRUE(loaded.is_ok()) << loaded.error();
}

static void expect_reparse_ok(const std::string& emitted) {
    Environment env;
    EditSession reparsed(env);
    auto reloaded = reparsed.load_source(emitted, "emitter_asset_roundtrip.gs");
    ASSERT_TRUE(reloaded.is_ok()) << reloaded.error();
}

TEST(AssetSourceEmission, MinimalAssetGraph) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(import "ue_core.d.gs";
graph HelloWorld {
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
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("import \"ue_core.d.gs\";"), std::string::npos);
    EXPECT_NE(output.find("graph HelloWorld"), std::string::npos);
    EXPECT_NE(output.find("@graph.input\n    param message: FString;"), std::string::npos);
    EXPECT_NE(output.find("node printer"), std::string::npos);
    EXPECT_NE(output.find("type PrintString;"), std::string::npos);
    EXPECT_NE(output.find("event OnStart"), std::string::npos);
    EXPECT_NE(output.find("connect(context.start, printer.enter);"), std::string::npos);
    EXPECT_NE(output.find("bind(message, printer.message);"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, SchemaDirective) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(graph SimpleHTN {
    schema HTNGraph;
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("graph SimpleHTN"), std::string::npos);
    EXPECT_NE(output.find("schema HTNGraph;"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, TopLevelConstBody) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(@PersistentId("spawn-point")
const spawn_point = new SoftObjectPath {
    path: "/Game/Spawn";
}
graph UsesConst {
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("@PersistentId(\"spawn-point\")"), std::string::npos);
    EXPECT_NE(output.find("const spawn_point = new SoftObjectPath"), std::string::npos);
    EXPECT_NE(output.find("path: \"/Game/Spawn\";"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, GenerateBlock) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(graph GeneratedLayout {
    node logger {
        type PrintString;
    }
    generate Layout {
        comment(logger, "Legacy note");
        metadata(position, logger, x, 100);
    }
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("generate Layout"), std::string::npos);
    EXPECT_NE(output.find("comment(logger, \"Legacy note\");"), std::string::npos);
    EXPECT_NE(output.find("metadata(position, logger, x, 100);"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, AnnotationConstructorValueRaw) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(graph ConstructorAnnotation {
    @Position(Asset = SoftObjectPath("Meta"))
    node p {
        type PrintString;
    }
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("Asset = SoftObjectPath(\"Meta\")"), std::string::npos);
    EXPECT_EQ(output.find("Asset = \"SoftObjectPath"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, ConstructorStringArgumentsRoundTrip) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(graph ConstructorStrings {
    @graph.input
    param input: OldType = OldType("default OldType");
    node fieldInit {
        type Holder;
        value: OldType("field OldType");
    }
    node rawInit {
        type Holder;
        raw: OldType("raw OldType");
    }
    generate Layout {
        metadata(position, fieldInit, asset, OldType("metadata OldType"));
    }
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("param input: OldType = OldType(\"default OldType\");"), std::string::npos);
    EXPECT_NE(output.find("value: OldType(\"field OldType\");"), std::string::npos);
    EXPECT_NE(output.find("raw: OldType(\"raw OldType\");"), std::string::npos);
    EXPECT_NE(output.find("metadata(position, fieldInit, asset, OldType(\"metadata OldType\"));"), std::string::npos);
    expect_reparse_ok(output);
}

TEST(AssetSourceEmission, FunctionAndVarParam) {
    Environment env;
    EditSession session(env);
    load_asset_source(session, R"(graph WithFunction {
    @graph.var
    param temp: float;
    function Calculate {
        connect(context.start, context.done);
        bind(temp, context.result);
    }
}
)");

    const auto output = session.emit();

    EXPECT_NE(output.find("@graph.var\n    param temp: float;"), std::string::npos);
    EXPECT_NE(output.find("function Calculate"), std::string::npos);
    EXPECT_NE(output.find("connect(context.start, context.done);"), std::string::npos);
    EXPECT_NE(output.find("bind(temp, context.result);"), std::string::npos);
    expect_reparse_ok(output);
}
