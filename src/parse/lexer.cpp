#include "graphscript/parse/lexer.h"
#include <unordered_map>

namespace gs {

// Static map of reserved keywords to token types.
static const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> kw = {
        {"import",        TokenType::KW_import},
        {"let",           TokenType::KW_let},
        {"declare",       TokenType::KW_declare},
        {"type",          TokenType::KW_type},
        {"Node",          TokenType::KW_Node},
        {"Schema",        TokenType::KW_Schema},
        {"Graph",         TokenType::KW_Graph},
        {"event",         TokenType::KW_event},
        {"function",      TokenType::KW_function},
        {"generate",      TokenType::KW_generate},
        {"in",            TokenType::KW_in},
        {"out",           TokenType::KW_out},
        {"var",           TokenType::KW_var},
        {"link",          TokenType::KW_link},
        {"Comment",       TokenType::KW_Comment},
        {"constructible", TokenType::KW_constructible},
        {"exec",          TokenType::KW_exec},
        {"data",          TokenType::KW_data},
        {"true",          TokenType::BoolLiteral},
        {"false",         TokenType::BoolLiteral},
    };
    return kw;
}

// Constructs lexer over source text.
Lexer::Lexer(std::string_view source) : source_(source) {}

// Tokenizes entire source; stops on first Error token.
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!at_end()) {
        skip_whitespace();
        if (at_end()) break;
        tokens.push_back(next_token());
        if (tokens.back().type == TokenType::Error) break;
    }
    tokens.push_back({TokenType::EndOfFile, "", {line_, column_}});
    return tokens;
}

// Advances and returns next token; handles comments, strings, numbers, identifiers, symbols.
Token Lexer::next_token() {
    char c = peek();

    if (c == '/' && pos_ + 1 < source_.size()) {
        char next = source_[pos_ + 1];
        if (next == '/') return read_line_comment();
        if (next == '*') return read_block_comment();
    }

    if (c == '"') return read_string();
    if (std::isdigit(c) || (c == '-' && pos_ + 1 < source_.size() && std::isdigit(source_[pos_ + 1])))
        return read_number();
    if (std::isalpha(c) || c == '_') return read_identifier_or_keyword();

    SourceLocation loc{line_, column_};
    advance();
    switch (c) {
        case '{': return make_token(TokenType::LeftBrace,    "{", loc);
        case '}': return make_token(TokenType::RightBrace,   "}", loc);
        case '(': return make_token(TokenType::LeftParen,    "(", loc);
        case ')': return make_token(TokenType::RightParen,   ")", loc);
        case '<': return make_token(TokenType::LeftAngle,    "<", loc);
        case '>': return make_token(TokenType::RightAngle,   ">", loc);
        case '[': return make_token(TokenType::LeftBracket,  "[", loc);
        case ']': return make_token(TokenType::RightBracket, "]", loc);
        case ',': return make_token(TokenType::Comma,        ",", loc);
        case ';': return make_token(TokenType::Semicolon,    ";", loc);
        case ':': return make_token(TokenType::Colon,        ":", loc);
        case '.': return make_token(TokenType::Dot,          ".", loc);
        case '=': return make_token(TokenType::Assign,       "=", loc);
        default:
            return make_token(TokenType::Error, std::string(1, c), loc);
    }
}

// Returns current character without advancing; '\0' at end.
char Lexer::peek() const {
    return at_end() ? '\0' : source_[pos_];
}

// Advances position, updates line/column; returns consumed character.
char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

// True when position is past end of source.
bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

// Skips spaces, tabs, newlines.
void Lexer::skip_whitespace() {
    while (!at_end() && std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
    }
}

// Reads double-quoted string with escape sequences (\\, \n, \t, \").
Token Lexer::read_string() {
    SourceLocation loc{line_, column_};
    advance(); // consume opening "
    std::string text;
    while (!at_end() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            if (at_end()) break;
            char esc = advance();
            switch (esc) {
                case 'n':  text += '\n'; break;
                case 't':  text += '\t'; break;
                case '\\': text += '\\'; break;
                case '"':  text += '"';  break;
                default:   text += '\\'; text += esc; break;
            }
        } else {
            text += advance();
        }
    }
    if (!at_end()) advance(); // consume closing "
    return make_token(TokenType::StringLiteral, text, loc);
}

// Reads integer or float literal; supports leading minus.
Token Lexer::read_number() {
    SourceLocation loc{line_, column_};
    std::string text;
    if (peek() == '-') text += advance();
    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }
    if (!at_end() && peek() == '.' && pos_ + 1 < source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[pos_ + 1]))) {
        text += advance(); // '.'
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
        return make_token(TokenType::FloatLiteral, text, loc);
    }
    return make_token(TokenType::IntLiteral, text, loc);
}

// Reads identifier or keyword; resolves via keywords() map.
Token Lexer::read_identifier_or_keyword() {
    SourceLocation loc{line_, column_};
    std::string text;
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        text += advance();
    }
    auto& kw = keywords();
    auto it = kw.find(text);
    if (it != kw.end()) {
        return make_token(it->second, text, loc);
    }
    return make_token(TokenType::Identifier, text, loc);
}

// Reads // ... line comment; returns LineComment token.
Token Lexer::read_line_comment() {
    SourceLocation loc{line_, column_};
    advance(); advance(); // consume //
    std::string text;
    while (!at_end() && peek() != '\n') {
        text += advance();
    }
    return make_token(TokenType::LineComment, text, loc);
}

// Reads /* ... */ block comment; returns Error if unterminated.
Token Lexer::read_block_comment() {
    SourceLocation loc{line_, column_};
    advance(); advance(); // consume /*
    std::string text;
    while (!at_end()) {
        if (peek() == '*' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            advance(); advance(); // consume */
            return make_token(TokenType::BlockComment, text, loc);
        }
        text += advance();
    }
    return make_token(TokenType::Error, "Unterminated block comment", loc);
}

// Helper to construct a Token.
Token Lexer::make_token(TokenType type, const std::string& text, SourceLocation loc) {
    return {type, text, loc};
}

// Returns human-readable name for token type (debugging).
const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::KW_import:        return "import";
        case TokenType::KW_let:           return "let";
        case TokenType::KW_declare:       return "declare";
        case TokenType::KW_type:          return "type";
        case TokenType::KW_Node:          return "Node";
        case TokenType::KW_Schema:        return "Schema";
        case TokenType::KW_Graph:         return "Graph";
        case TokenType::KW_event:         return "event";
        case TokenType::KW_function:      return "function";
        case TokenType::KW_generate:      return "generate";
        case TokenType::KW_in:            return "in";
        case TokenType::KW_out:           return "out";
        case TokenType::KW_var:           return "var";
        case TokenType::KW_link:          return "link";
        case TokenType::KW_Comment:       return "Comment";
        case TokenType::KW_constructible: return "constructible";
        case TokenType::KW_exec:          return "exec";
        case TokenType::KW_data:          return "data";
        case TokenType::IntLiteral:       return "IntLiteral";
        case TokenType::FloatLiteral:     return "FloatLiteral";
        case TokenType::StringLiteral:    return "StringLiteral";
        case TokenType::BoolLiteral:      return "BoolLiteral";
        case TokenType::Identifier:       return "Identifier";
        case TokenType::LeftBrace:        return "{";
        case TokenType::RightBrace:       return "}";
        case TokenType::LeftParen:        return "(";
        case TokenType::RightParen:       return ")";
        case TokenType::LeftAngle:        return "<";
        case TokenType::RightAngle:       return ">";
        case TokenType::LeftBracket:      return "[";
        case TokenType::RightBracket:     return "]";
        case TokenType::Comma:            return ",";
        case TokenType::Semicolon:        return ";";
        case TokenType::Colon:            return ":";
        case TokenType::Dot:              return ".";
        case TokenType::Assign:           return "=";
        case TokenType::LineComment:      return "LineComment";
        case TokenType::BlockComment:     return "BlockComment";
        case TokenType::EndOfFile:        return "EOF";
        case TokenType::Error:            return "Error";
    }
    return "Unknown";
}

} // namespace gs
