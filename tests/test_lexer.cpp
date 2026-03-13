#include <gtest/gtest.h>
#include "graphscript/parse/lexer.h"

using namespace gs;

static std::vector<Token> lex(std::string_view src) {
    return Lexer(src).tokenize();
}

TEST(Lexer, EmptyInput) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::EndOfFile);
}

TEST(Lexer, WhitespaceOnly) {
    auto tokens = lex("   \n\t  ");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::EndOfFile);
}

TEST(Lexer, Keywords) {
    auto tokens = lex("import let declare type Node Schema Graph event function generate in out var link Comment constructible exec data");
    // 18 keywords + EOF
    ASSERT_EQ(tokens.size(), 19u);
    EXPECT_EQ(tokens[0].type,  TokenType::KW_import);
    EXPECT_EQ(tokens[1].type,  TokenType::KW_let);
    EXPECT_EQ(tokens[2].type,  TokenType::KW_declare);
    EXPECT_EQ(tokens[3].type,  TokenType::KW_type);
    EXPECT_EQ(tokens[4].type,  TokenType::KW_Node);
    EXPECT_EQ(tokens[5].type,  TokenType::KW_Schema);
    EXPECT_EQ(tokens[6].type,  TokenType::KW_Graph);
    EXPECT_EQ(tokens[7].type,  TokenType::KW_event);
    EXPECT_EQ(tokens[8].type,  TokenType::KW_function);
    EXPECT_EQ(tokens[9].type,  TokenType::KW_generate);
    EXPECT_EQ(tokens[10].type, TokenType::KW_in);
    EXPECT_EQ(tokens[11].type, TokenType::KW_out);
    EXPECT_EQ(tokens[12].type, TokenType::KW_var);
    EXPECT_EQ(tokens[13].type, TokenType::KW_link);
    EXPECT_EQ(tokens[14].type, TokenType::KW_Comment);
    EXPECT_EQ(tokens[15].type, TokenType::KW_constructible);
    EXPECT_EQ(tokens[16].type, TokenType::KW_exec);
    EXPECT_EQ(tokens[17].type, TokenType::KW_data);
}

TEST(Lexer, BoolLiterals) {
    auto tokens = lex("true false");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::BoolLiteral);
    EXPECT_EQ(tokens[0].text, "true");
    EXPECT_EQ(tokens[1].type, TokenType::BoolLiteral);
    EXPECT_EQ(tokens[1].text, "false");
}

TEST(Lexer, IntLiteral) {
    auto tokens = lex("42 0 -5");
    EXPECT_EQ(tokens[0].type, TokenType::IntLiteral);
    EXPECT_EQ(tokens[0].text, "42");
    EXPECT_EQ(tokens[1].type, TokenType::IntLiteral);
    EXPECT_EQ(tokens[1].text, "0");
    EXPECT_EQ(tokens[2].type, TokenType::IntLiteral);
    EXPECT_EQ(tokens[2].text, "-5");
}

TEST(Lexer, FloatLiteral) {
    auto tokens = lex("3.14 0.5");
    EXPECT_EQ(tokens[0].type, TokenType::FloatLiteral);
    EXPECT_EQ(tokens[0].text, "3.14");
    EXPECT_EQ(tokens[1].type, TokenType::FloatLiteral);
    EXPECT_EQ(tokens[1].text, "0.5");
}

TEST(Lexer, StringLiteral) {
    auto tokens = lex(R"("hello world" "with \"escape\"")");
    EXPECT_EQ(tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(tokens[0].text, "hello world");
    EXPECT_EQ(tokens[1].type, TokenType::StringLiteral);
    EXPECT_EQ(tokens[1].text, "with \"escape\"");
}

TEST(Lexer, Identifier) {
    auto tokens = lex("myVar _private some_name_123");
    EXPECT_EQ(tokens[0].type, TokenType::Identifier);
    EXPECT_EQ(tokens[0].text, "myVar");
    EXPECT_EQ(tokens[1].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].text, "_private");
    EXPECT_EQ(tokens[2].type, TokenType::Identifier);
    EXPECT_EQ(tokens[2].text, "some_name_123");
}

TEST(Lexer, Symbols) {
    auto tokens = lex("{}()[]<>,.;:=");
    EXPECT_EQ(tokens[0].type,  TokenType::LeftBrace);
    EXPECT_EQ(tokens[1].type,  TokenType::RightBrace);
    EXPECT_EQ(tokens[2].type,  TokenType::LeftParen);
    EXPECT_EQ(tokens[3].type,  TokenType::RightParen);
    EXPECT_EQ(tokens[4].type,  TokenType::LeftBracket);
    EXPECT_EQ(tokens[5].type,  TokenType::RightBracket);
    EXPECT_EQ(tokens[6].type,  TokenType::LeftAngle);
    EXPECT_EQ(tokens[7].type,  TokenType::RightAngle);
    EXPECT_EQ(tokens[8].type,  TokenType::Comma);
    EXPECT_EQ(tokens[9].type,  TokenType::Dot);
    EXPECT_EQ(tokens[10].type, TokenType::Semicolon);
    EXPECT_EQ(tokens[11].type, TokenType::Colon);
    EXPECT_EQ(tokens[12].type, TokenType::Assign);
}

TEST(Lexer, LineComment) {
    auto tokens = lex("// this is a comment\nlet x");
    EXPECT_EQ(tokens[0].type, TokenType::LineComment);
    EXPECT_EQ(tokens[0].text, " this is a comment");
    EXPECT_EQ(tokens[1].type, TokenType::KW_let);
}

TEST(Lexer, BlockComment) {
    auto tokens = lex("/* block\ncomment */let");
    EXPECT_EQ(tokens[0].type, TokenType::BlockComment);
    EXPECT_EQ(tokens[0].text, " block\ncomment ");
    EXPECT_EQ(tokens[1].type, TokenType::KW_let);
}

TEST(Lexer, SourceLocation) {
    auto tokens = lex("Graph\n  MyGraph");
    EXPECT_EQ(tokens[0].location.line, 1u);
    EXPECT_EQ(tokens[0].location.column, 1u);
    EXPECT_EQ(tokens[1].location.line, 2u);
    EXPECT_EQ(tokens[1].location.column, 3u);
}

TEST(Lexer, DeclareTypeStatement) {
    auto tokens = lex("declare type FVector : constructible;");
    EXPECT_EQ(tokens[0].type, TokenType::KW_declare);
    EXPECT_EQ(tokens[1].type, TokenType::KW_type);
    EXPECT_EQ(tokens[2].type, TokenType::Identifier);
    EXPECT_EQ(tokens[2].text, "FVector");
    EXPECT_EQ(tokens[3].type, TokenType::Colon);
    EXPECT_EQ(tokens[4].type, TokenType::KW_constructible);
    EXPECT_EQ(tokens[5].type, TokenType::Semicolon);
}

TEST(Lexer, DeclareNodeBlock) {
    auto tokens = lex(R"(declare Node PrintString {
    exec in enter;
    exec out exit;
    data in message : FString;
})");
    EXPECT_EQ(tokens[0].type, TokenType::KW_declare);
    EXPECT_EQ(tokens[1].type, TokenType::KW_Node);
    EXPECT_EQ(tokens[2].type, TokenType::Identifier);
    EXPECT_EQ(tokens[2].text, "PrintString");
    EXPECT_EQ(tokens[3].type, TokenType::LeftBrace);
    EXPECT_EQ(tokens[4].type, TokenType::KW_exec);
    EXPECT_EQ(tokens[5].type, TokenType::KW_in);
    EXPECT_EQ(tokens[6].type, TokenType::Identifier);
    EXPECT_EQ(tokens[6].text, "enter");
}

TEST(Lexer, ImportStatement) {
    auto tokens = lex(R"(import "ue_core.d.gs";)");
    EXPECT_EQ(tokens[0].type, TokenType::KW_import);
    EXPECT_EQ(tokens[1].type, TokenType::StringLiteral);
    EXPECT_EQ(tokens[1].text, "ue_core.d.gs");
    EXPECT_EQ(tokens[2].type, TokenType::Semicolon);
}

TEST(Lexer, GraphWithBaseType) {
    auto tokens = lex("Graph MyHTN : HTNGraph {");
    EXPECT_EQ(tokens[0].type, TokenType::KW_Graph);
    EXPECT_EQ(tokens[1].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].text, "MyHTN");
    EXPECT_EQ(tokens[2].type, TokenType::Colon);
    EXPECT_EQ(tokens[3].type, TokenType::Identifier);
    EXPECT_EQ(tokens[3].text, "HTNGraph");
    EXPECT_EQ(tokens[4].type, TokenType::LeftBrace);
}

TEST(Lexer, TokenTypeName) {
    EXPECT_STREQ(token_type_name(TokenType::KW_import), "import");
    EXPECT_STREQ(token_type_name(TokenType::KW_declare), "declare");
    EXPECT_STREQ(token_type_name(TokenType::EndOfFile), "EOF");
    EXPECT_STREQ(token_type_name(TokenType::Error), "Error");
}

TEST(Lexer, UnterminatedBlockComment) {
    auto tokens = lex("/* unterminated");
    bool has_error = false;
    for (auto& t : tokens) {
        if (t.type == TokenType::Error) has_error = true;
    }
    EXPECT_TRUE(has_error);
}

TEST(Lexer, NegativeNumberVsMinusIdentifier) {
    auto tokens = lex("-1");
    EXPECT_EQ(tokens[0].type, TokenType::IntLiteral);
    EXPECT_EQ(tokens[0].text, "-1");
}

TEST(Lexer, DeclareSchemaBlock) {
    auto tokens = lex(R"(declare Schema HTNGraph {
    max_exec_fan_out: unlimited;
})");
    EXPECT_EQ(tokens[0].type, TokenType::KW_declare);
    EXPECT_EQ(tokens[1].type, TokenType::KW_Schema);
    EXPECT_EQ(tokens[2].type, TokenType::Identifier);
    EXPECT_EQ(tokens[2].text, "HTNGraph");
}
