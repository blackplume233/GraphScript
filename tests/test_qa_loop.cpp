#include <gtest/gtest.h>

#include <string>

#include "graphscript/edit/edit_session.h"

using namespace gs;

static void load_qa_core(EditSession& session) {
    const std::string path = std::string(GS_PRESETS_DIR) + "/ue_core.d.gs";
    auto loaded = session.load_import(path);
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
}

static EditSession make_qa_session(Environment& env, const std::string& source) {
    EditSession session(env);
    load_qa_core(session);
    auto loaded = session.load_source(source, "qa_loop_asset.gs");
    EXPECT_TRUE(loaded.is_ok()) << loaded.error();
    return session;
}

static void expect_round_trip(EditSession& session) {
    Environment env2;
    EditSession next(env2);
    load_qa_core(next);
    const std::string emitted = session.emit();
    auto loaded = next.load_source(emitted, "qa_loop_roundtrip.gs");
    ASSERT_TRUE(loaded.is_ok()) << loaded.error();
    ASSERT_EQ(next.module().graphs.size(), session.module().graphs.size());
    for (size_t i = 0; i < session.module().graphs.size(); ++i) {
        const auto& expected = session.module().graphs[i];
        const auto& actual = next.module().graphs[i];
        EXPECT_EQ(actual.name, expected.name);
        EXPECT_EQ(actual.parameters.size(), expected.parameters.size());
        EXPECT_EQ(actual.node_instances.size(), expected.node_instances.size());
        EXPECT_EQ(actual.events.size(), expected.events.size());
        EXPECT_EQ(actual.functions.size(), expected.functions.size());
        if (!expected.events.empty()) {
            ASSERT_FALSE(actual.events.empty());
            EXPECT_EQ(actual.events[0].flow_connections.size(), expected.events[0].flow_connections.size());
            EXPECT_EQ(actual.events[0].data_links.size(), expected.events[0].data_links.size());
        }
    }
}

TEST(QALoop, EditSessionMutationLoopRoundTripsAssetSource) {
    Environment env;
    auto session = make_qa_session(env, R"(graph QA {
    @graph.input
    param message: FString;
    node logger {
        type PrintString;
    }
    event Start {
        connect(context.start, logger.enter);
        bind(message, logger.message);
    }
}
)");

    ASSERT_TRUE(session.rename_node_instance("logger", "writer").is_ok());
    ASSERT_TRUE(session.rename_param("message", "text").is_ok());
    ASSERT_TRUE(session.add_comment("writer", "qa note").is_ok());
    ASSERT_TRUE(session.set_node_annotation("writer", {"Id", {{"", "writer-stable"}}}).is_ok());

    const std::string emitted = session.emit();
    EXPECT_NE(emitted.find("node writer"), std::string::npos);
    EXPECT_NE(emitted.find("param text: FString"), std::string::npos);
    EXPECT_NE(emitted.find("@Id(\"writer-stable\")"), std::string::npos);
    expect_round_trip(session);
}

TEST(QALoop, UndoRedoPreservesAssetRoundTrip) {
    Environment env;
    auto session = make_qa_session(env, R"(graph UndoGraph {
    node logger {
        type PrintString;
    }
}
)");

    ASSERT_TRUE(session.add_param(ParamDirection::In, "message", "FString").is_ok());
    ASSERT_TRUE(session.add_event("Start").is_ok());
    ASSERT_TRUE(session.add_flow("Start", "context", "start", "logger", "enter").is_ok());
    EXPECT_TRUE(session.can_undo());

    auto undone = session.undo();
    ASSERT_TRUE(undone.is_ok()) << undone.error();
    auto redone = session.redo();
    ASSERT_TRUE(redone.is_ok()) << redone.error();
    expect_round_trip(session);
}

TEST(QALoop, SourceReloadAfterRepeatedEdits) {
    Environment env;
    auto session = make_qa_session(env, R"(graph ReloadGraph {
    node logger {
        type PrintString;
    }
}
)");

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(session.set_node_initializer_field("logger", "message", "\"loop\"").is_ok());
        ASSERT_TRUE(session.set_node_annotation("logger", {"Loop", {{"Index", std::to_string(i)}}}).is_ok());
        expect_round_trip(session);
    }
}

TEST(QALoop, MultiGraphMultiBlockUndoRedoRoundTrips) {
    Environment env;
    auto session = make_qa_session(env, R"(graph Child {
    @graph.input
    param text: FString;
    event Execute {
    }
}
graph Parent {
    @graph.input
    param text: FString;
    node child {
        type Child;
    }
    node logger {
        type PrintString;
    }
    event Start {
        connect(context.start, child.Execute);
    }
    function Compute {
        bind(text, context.result);
    }
}
)");

    ASSERT_TRUE(session.set_active("Parent").is_ok());
    ASSERT_TRUE(session.rename_event("Start", "Begin").is_ok());
    ASSERT_TRUE(session.rename_function("Compute", "Evaluate").is_ok());
    ASSERT_TRUE(session.add_link("Begin", "logger", "message", "child", "text").is_ok());
    expect_round_trip(session);

    ASSERT_TRUE(session.undo().is_ok());
    ASSERT_TRUE(session.redo().is_ok());
    expect_round_trip(session);
}
