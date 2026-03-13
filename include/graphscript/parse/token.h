#pragma once

#include <string>
#include <cstdint>

namespace gs {

/// 1-based line and column in source text.
struct SourceLocation {
    uint32_t line   = 1;
    uint32_t column = 1;
};

/// Span from start to end location (inclusive).
struct SourceRange {
    SourceLocation start;
    SourceLocation end;
};

/// Lexer token type (keywords, literals, symbols, control).
enum class TokenType : uint8_t {
    // Keywords
    KW_import,
    KW_let,
    KW_declare,
    KW_type,
    KW_Node,
    KW_Schema,
    KW_Graph,
    KW_event,
    KW_function,
    KW_generate,
    KW_in,
    KW_out,
    KW_var,
    KW_link,
    KW_Comment,
    KW_constructible,
    KW_exec,
    KW_data,

    // Literals
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,

    // Identifier
    Identifier,

    // Symbols
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftAngle,
    RightAngle,
    LeftBracket,
    RightBracket,
    Comma,
    Semicolon,
    Colon,
    Dot,
    Assign,

    // Comments
    LineComment,
    BlockComment,

    // Control
    EndOfFile,
    Error
};

/// Single token with type, lexeme text, and source location.
struct Token {
    TokenType      type;
    std::string    text;
    SourceLocation location;
};

/// Returns human-readable name for token type (for diagnostics).
const char* token_type_name(TokenType type);

} // namespace gs
