#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "graphscript/parse/token.h"

namespace gs {

/// Converts source text into a stream of tokens.
class Lexer {
public:
    explicit Lexer(std::string_view source);
    /// Produces all tokens from source; ends with EndOfFile (or Error on failure).
    std::vector<Token> tokenize();

private:
    Token next_token();
    char  peek() const;
    char  advance();
    bool  at_end() const;
    void  skip_whitespace();
    Token read_string();
    Token read_number();
    Token read_identifier_or_keyword();
    Token read_line_comment();
    Token read_block_comment();
    Token make_token(TokenType type, const std::string& text, SourceLocation loc);

    std::string_view source_;
    size_t           pos_    = 0;
    uint32_t         line_   = 1;
    uint32_t         column_ = 1;
};

} // namespace gs
