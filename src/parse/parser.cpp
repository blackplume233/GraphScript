#include "graphscript/parse/parser.h"
#include <algorithm>
#include <optional>

namespace gs {

static SourceLocation token_end_location(const Token& token) {
    SourceLocation end = token.location;
    if (!token.text.empty()) {
        end.column += static_cast<uint32_t>(token.text.size() - 1);
    }
    return end;
}

static std::string expression_token_text(const Token& token) {
    if (token.type != TokenType::StringLiteral) return token.text;

    std::string escaped;
    for (char c : token.text) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    return "\"" + escaped + "\"";
}

static std::string join_expression_token_text(const std::vector<Token>& tokens, size_t begin, size_t end) {
    std::string result;
    for (size_t i = begin; i < end; ++i) {
        result += expression_token_text(tokens[i]);
    }
    return result;
}

static std::string join_initializer_token_text(const std::vector<Token>& tokens, size_t begin, size_t end) {
    std::string result;
    for (size_t i = begin; i < end; ++i) {
        const auto token_text = expression_token_text(tokens[i]);
        if (result.empty()) {
            result += token_text;
            continue;
        }

        const TokenType current_type = tokens[i].type;
        const TokenType previous_type = tokens[i - 1].type;
        if (current_type == TokenType::Comma) {
            result += ",";
        } else if (current_type == TokenType::RightParen ||
                   current_type == TokenType::RightBracket ||
                   current_type == TokenType::RightBrace ||
                   current_type == TokenType::LeftParen ||
                   current_type == TokenType::LeftBracket ||
                   current_type == TokenType::LeftBrace) {
            result += token_text;
        } else if (previous_type == TokenType::LeftParen ||
                   previous_type == TokenType::LeftBracket ||
                   previous_type == TokenType::LeftBrace) {
            result += token_text;
        } else if (current_type == TokenType::Assign) {
            result += " " + token_text;
        } else if (previous_type == TokenType::Assign || previous_type == TokenType::Comma) {
            result += " " + token_text;
        } else {
            result += " " + token_text;
        }
    }
    return result;
}

static std::vector<InitializerField> parse_initializer_fields(const std::vector<Token>& tokens) {
    std::vector<InitializerField> fields;
    size_t i = 0;
    while (i < tokens.size()) {
        while (i < tokens.size() && tokens[i].type == TokenType::Comma) ++i;
        if (i >= tokens.size()) break;

        if (tokens[i].type != TokenType::Identifier ||
            i + 1 >= tokens.size() ||
            tokens[i + 1].type != TokenType::Assign) {
            while (i < tokens.size() && tokens[i].type != TokenType::Comma) ++i;
            continue;
        }

        const Token name_token = tokens[i];
        const size_t value_begin = i + 2;
        i = value_begin;
        if (i >= tokens.size() || tokens[i].type == TokenType::Comma) continue;

        const Token value_start_token = tokens[i];
        Token value_end_token = tokens[i];
        int nested = 0;
        while (i < tokens.size()) {
            if (nested == 0 && tokens[i].type == TokenType::Comma) break;
            if (tokens[i].type == TokenType::LeftParen || tokens[i].type == TokenType::LeftBracket) {
                ++nested;
            } else if ((tokens[i].type == TokenType::RightParen || tokens[i].type == TokenType::RightBracket) &&
                       nested > 0) {
                --nested;
            }
            value_end_token = tokens[i];
            ++i;
        }

        InitializerField field;
        field.name = name_token.text;
        field.value = join_expression_token_text(tokens, value_begin, i);
        field.source_range.start = name_token.location;
        field.source_range.end = token_end_location(value_end_token);
        field.name_range.start = name_token.location;
        field.name_range.end = token_end_location(name_token);
        field.value_range.start = value_start_token.location;
        field.value_range.end = token_end_location(value_end_token);
        if (value_begin + 1 < i &&
            tokens[value_begin].type == TokenType::Identifier &&
            tokens[value_begin + 1].type == TokenType::LeftParen) {
            const Token constructor_type_token = tokens[value_begin];
            field.value_constructor_range.start = constructor_type_token.location;
            field.value_constructor_range.end = token_end_location(value_end_token);
            field.value_constructor_type_range.start = constructor_type_token.location;
            field.value_constructor_type_range.end = token_end_location(constructor_type_token);

            const size_t arg_begin = value_begin + 2;
            if (arg_begin < i && tokens[arg_begin].type != TokenType::RightParen) {
                size_t arg_end = arg_begin;
                Token arg_end_token = tokens[arg_begin];
                int arg_nested = 0;
                while (arg_end < i) {
                    if (arg_nested == 0 && tokens[arg_end].type == TokenType::RightParen) break;
                    if (tokens[arg_end].type == TokenType::LeftParen || tokens[arg_end].type == TokenType::LeftBracket) {
                        ++arg_nested;
                    } else if ((tokens[arg_end].type == TokenType::RightParen || tokens[arg_end].type == TokenType::RightBracket) &&
                               arg_nested > 0) {
                        --arg_nested;
                    }
                    arg_end_token = tokens[arg_end];
                    ++arg_end;
                }
                field.value_constructor_arg_range.start = tokens[arg_begin].location;
                field.value_constructor_arg_range.end = token_end_location(arg_end_token);
            }
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

struct ConstructorSourceRanges {
    SourceRange range;
    SourceRange type_range;
    SourceRange arg_range;
};

static std::optional<ConstructorSourceRanges> parse_constructor_source_ranges(const std::vector<Token>& tokens) {
    if (tokens.size() < 3 ||
        tokens[0].type != TokenType::Identifier ||
        tokens[1].type != TokenType::LeftParen) {
        return std::nullopt;
    }

    size_t close_paren = tokens.size();
    int nested = 0;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::LeftParen ||
            tokens[i].type == TokenType::LeftBracket ||
            tokens[i].type == TokenType::LeftBrace) {
            ++nested;
        } else if (tokens[i].type == TokenType::RightParen ||
                   tokens[i].type == TokenType::RightBracket ||
                   tokens[i].type == TokenType::RightBrace) {
            if (nested <= 0) return std::nullopt;
            --nested;
            if (nested == 0) {
                if (tokens[i].type != TokenType::RightParen) return std::nullopt;
                close_paren = i;
                break;
            }
        }
    }

    if (close_paren != tokens.size() - 1) return std::nullopt;

    ConstructorSourceRanges ranges;
    ranges.range.start = tokens[0].location;
    ranges.range.end = token_end_location(tokens[close_paren]);
    ranges.type_range.start = tokens[0].location;
    ranges.type_range.end = token_end_location(tokens[0]);
    if (close_paren > 2) {
        ranges.arg_range.start = tokens[2].location;
        ranges.arg_range.end = token_end_location(tokens[close_paren - 1]);
    }
    return ranges;
}

// Constructs parser from token stream.
Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// Parses module: imports, lets, declare type/Node/Schema, graphs.
Result<std::unique_ptr<ModuleNode>, std::string> Parser::parse() {
    errors_.clear();
    diagnostics_.clear();
    auto module = std::make_unique<ModuleNode>();

    while (!at_end()) {
        auto& tok = current();

        if (tok.type == TokenType::LineComment || tok.type == TokenType::BlockComment) {
            advance();
            continue;
        }

        if (tok.type == TokenType::KW_import) {
            auto node = parse_import();
            if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse import" : errors_.back());
            module->imports.push_back(std::move(node));
        } else if (tok.type == TokenType::KW_let) {
            auto node = parse_let();
            if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse let" : errors_.back());
            module->let_decls.push_back(std::move(node));
        } else if (tok.type == TokenType::KW_declare) {
            auto& next = peek_next();
            if (next.type == TokenType::KW_type) {
                auto node = parse_declare_type();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare type" : errors_.back());
                module->declare_types.push_back(std::move(node));
            } else if (next.type == TokenType::KW_Node) {
                auto node = parse_declare_node();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare Node" : errors_.back());
                module->declare_nodes.push_back(std::move(node));
            } else if (next.type == TokenType::KW_Schema) {
                auto node = parse_declare_schema();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare Schema" : errors_.back());
                module->declare_schemas.push_back(std::move(node));
            } else {
                record_error("Expected 'type', 'Node', or 'Schema' after 'declare'",
                             "GS_PARSE_EXPECTED_TOKEN",
                             "Use one of: declare type, declare Node, declare Schema.");
                return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.back());
            }
        } else if (tok.type == TokenType::LeftBracket) {
            auto annots = parse_annotations();
            if (current().type == TokenType::KW_import) {
                auto node = parse_import();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse import" : errors_.back());
                node->annotations = std::move(annots);
                module->imports.push_back(std::move(node));
            } else if (current().type == TokenType::KW_let) {
                auto node = parse_let();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse let" : errors_.back());
                node->annotations = std::move(annots);
                module->let_decls.push_back(std::move(node));
            } else if (current().type == TokenType::KW_declare) {
                auto& next = peek_next();
                if (next.type == TokenType::KW_type) {
                    auto node = parse_declare_type();
                    if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare type" : errors_.back());
                    node->annotations = std::move(annots);
                    module->declare_types.push_back(std::move(node));
                } else if (next.type == TokenType::KW_Node) {
                    auto node = parse_declare_node();
                    if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare Node" : errors_.back());
                    node->annotations = std::move(annots);
                    module->declare_nodes.push_back(std::move(node));
                } else if (next.type == TokenType::KW_Schema) {
                    auto node = parse_declare_schema();
                    if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse declare Schema" : errors_.back());
                    node->annotations = std::move(annots);
                    module->declare_schemas.push_back(std::move(node));
                } else {
                    record_error("Expected 'type', 'Node', or 'Schema' after 'declare'",
                                 "GS_PARSE_EXPECTED_TOKEN",
                                 "Use one of: declare type, declare Node, declare Schema.");
                    return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.back());
                }
            } else if (current().type == TokenType::KW_Graph) {
                auto node = parse_graph();
                if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse Graph" : errors_.back());
                node->annotations = std::move(annots);
                module->graphs.push_back(std::move(node));
            } else {
                record_error("Expected import, let, declare, or Graph after annotations",
                             "GS_PARSE_EXPECTED_TOKEN",
                             "Top-level annotations must appear immediately before an import, let, declare, or Graph declaration.");
                return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.back());
            }
        } else if (tok.type == TokenType::KW_Graph) {
            auto node = parse_graph();
            if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse Graph" : errors_.back());
            module->graphs.push_back(std::move(node));
        } else {
            record_error("Unexpected token '" + tok.text + "'",
                         "GS_PARSE_UNEXPECTED_TOKEN",
                         "Start a top-level item with import, let, declare, an annotation, or Graph.");
            return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.back());
        }
    }

    return Result<std::unique_ptr<ModuleNode>, std::string>::ok(std::move(module));
}

// Returns token at current position; clamps to last token when at end.
const Token& Parser::current() const {
    return tokens_[std::min(pos_, tokens_.size() - 1)];
}

// Returns next token without advancing.
const Token& Parser::peek_next() const {
    return tokens_[std::min(pos_ + 1, tokens_.size() - 1)];
}

// Advances position and returns the consumed token.
const Token& Parser::advance() {
    auto& tok = current();
    if (!at_end()) pos_++;
    return tok;
}

// Consumes and returns true if current token matches type; else false.
bool Parser::match(TokenType type) {
    if (at_end()) return false;
    if (current().type == type) { advance(); return true; }
    return false;
}

// Like match but records error message on failure.
bool Parser::expect(TokenType type, const std::string& msg) {
    if (match(type)) return true;
    record_expected_token_error(type, msg);
    return false;
}

// True when past last token or at EOF.
bool Parser::at_end() const {
    return pos_ >= tokens_.size() || tokens_[pos_].type == TokenType::EndOfFile;
}

// True when current position looks like `node.pin = ...` assignment statement.
bool Parser::starts_with_assignment_stmt() const {
    if (current().type != TokenType::Identifier) return false;
    const auto& t1 = tokens_[std::min(pos_ + 1, tokens_.size() - 1)];
    const auto& t2 = tokens_[std::min(pos_ + 2, tokens_.size() - 1)];
    const auto& t3 = tokens_[std::min(pos_ + 3, tokens_.size() - 1)];
    return t1.type == TokenType::Dot &&
           t2.type == TokenType::Identifier &&
           t3.type == TokenType::Assign;
}

static std::string token_source_text(TokenType type) {
    switch (type) {
        case TokenType::LeftBrace:    return "{";
        case TokenType::RightBrace:   return "}";
        case TokenType::LeftParen:    return "(";
        case TokenType::RightParen:   return ")";
        case TokenType::LeftBracket:  return "[";
        case TokenType::RightBracket: return "]";
        case TokenType::Comma:        return ",";
        case TokenType::Semicolon:    return ";";
        case TokenType::Colon:        return ":";
        case TokenType::Dot:          return ".";
        case TokenType::Assign:       return "=";
        default:                      return "";
    }
}

// Records an expected-token parser diagnostic with a minimal source edit action
// when the expected token has a stable textual representation.
void Parser::record_expected_token_error(TokenType type, const std::string& msg) {
    record_error(msg,
                 "GS_PARSE_EXPECTED_TOKEN",
                 std::string("Insert or replace the current token with '") + token_type_name(type) + "'.");

    std::string replacement = token_source_text(type);
    if (replacement.empty() || diagnostics_.empty()) return;

    const auto& tok = current();
    SourceRange edit_range;
    edit_range.start = tok.location;
    edit_range.end = tok.location;

    diagnostics_.back().actions.push_back({
        std::string("Insert '") + replacement + "'",
        "quickfix.source.insert",
        "",
        edit_range,
        replacement + " "
    });
}

// Records a parser error both as legacy text and as structured diagnostic data.
void Parser::record_error(const std::string& msg, const std::string& code, const std::string& hint) {
    const auto& tok = current();
    std::string legacy = msg + " at line " + std::to_string(tok.location.line);
    if (!tok.text.empty()) legacy += " (got '" + tok.text + "')";
    errors_.push_back(legacy);

    SourceRange range;
    range.start = tok.location;
    range.end = tok.location;
    if (!tok.text.empty()) {
        range.end.column += static_cast<uint32_t>(tok.text.size() - 1);
    }

    diagnostics_.push_back({
        Severity::Error,
        msg,
        tok.text,
        code,
        range,
        hint
    });
}

// Parses import "path";
std::unique_ptr<ImportNode> Parser::parse_import() {
    auto node = std::make_unique<ImportNode>();
    node->range.start = current().location;
    advance(); // consume 'import'
    if (current().type != TokenType::StringLiteral) {
        record_error("Expected string after 'import'",
                     "GS_PARSE_EXPECTED_TOKEN",
                     "Write imports as: import \"file.d.gs\";");
        return nullptr;
    }
    const Token path_token = current();
    node->path = path_token.text;
    node->path_range = {path_token.location, token_end_location(path_token)};
    advance();
    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after import path");
    return node;
}

// Parses let name = TypeName("arg");
std::unique_ptr<LetDeclNode> Parser::parse_let() {
    auto node = std::make_unique<LetDeclNode>();
    node->range.start = current().location;
    advance(); // consume 'let'
    if (current().type != TokenType::Identifier) {
        record_error("Expected identifier after 'let'",
                     "GS_PARSE_EXPECTED_TOKEN",
                     "Write let declarations as: let name = Type(\"arg\");");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();
    if (!expect(TokenType::Assign, "Expected '=' after let name")) return nullptr;
    if (current().type != TokenType::Identifier) {
        record_error("Expected type name in let initializer",
                     "GS_PARSE_EXPECTED_TOKEN",
                     "Use a constructible type name after '='.");
        return nullptr;
    }
    const Token type_token = current();
    node->type_name = type_token.text;
    node->type_name_range.start = type_token.location;
    node->type_name_range.end = token_end_location(type_token);
    node->constructor_range.start = type_token.location;
    advance();
    if (!expect(TokenType::LeftParen, "Expected '(' in constructor")) return nullptr;
    if (current().type == TokenType::StringLiteral) {
        const Token arg_token = current();
        node->constructor_arg_range.start = arg_token.location;
        node->constructor_arg = current().text;
        node->constructor_arg_range.end = token_end_location(arg_token);
        advance();
    }
    if (current().type == TokenType::RightParen) {
        node->constructor_range.end = token_end_location(current());
    }
    if (!expect(TokenType::RightParen, "Expected ')' in constructor")) return nullptr;
    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after let declaration");
    return node;
}

// Parses declare type Name [: constructible];
std::unique_ptr<DeclareTypeNode> Parser::parse_declare_type() {
    auto node = std::make_unique<DeclareTypeNode>();
    node->range.start = current().location;
    advance(); // consume 'declare'
    advance(); // consume 'type'
    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected type name after 'declare type'");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();
    if (current().type == TokenType::Colon) {
        advance();
        if (current().type == TokenType::KW_constructible) {
            node->constructible = true;
            advance();
        } else {
            errors_.push_back("Expected 'constructible' after ':'");
            return nullptr;
        }
    }
    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after declare type");
    return node;
}

// Parses declare Node Name { (pin_decl | field_decl)* }
std::unique_ptr<DeclareNodeNode> Parser::parse_declare_node() {
    auto node = std::make_unique<DeclareNodeNode>();
    node->range.start = current().location;
    advance(); // consume 'declare'
    advance(); // consume 'Node'
    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected node name after 'declare Node'");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();
    if (!expect(TokenType::LeftBrace, "Expected '{' after node name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        std::vector<Annotation> annots;
        if (current().type == TokenType::LeftBracket) {
            annots = parse_annotations();
        }
        if (current().type == TokenType::Identifier && current().text == "field") {
            auto field = std::make_unique<NodeFieldDeclNode>();
            field->range.start = current().location;
            field->annotations = std::move(annots);
            advance(); // consume 'field'

            if (current().type != TokenType::Identifier) {
                errors_.push_back("Expected field name");
                return nullptr;
            }
            const Token name_token = current();
            field->name = name_token.text;
            field->name_range = {name_token.location, token_end_location(name_token)};
            advance();

            if (!expect(TokenType::Colon, "Expected ':' after field name")) return nullptr;
            if (current().type != TokenType::Identifier) {
                errors_.push_back("Expected field type name after ':'");
                return nullptr;
            }
            const Token type_token = current();
            field->type_name = type_token.text;
            field->type_name_range = {type_token.location, token_end_location(type_token)};
            advance();

            if (current().type == TokenType::Assign) {
                advance();
                const size_t value_begin = pos_;
                if (current().type == TokenType::Semicolon || current().type == TokenType::RightBrace) {
                    errors_.push_back("Expected field default value after '='");
                    return nullptr;
                }

                Token value_end_token = current();
                int nested = 0;
                while (!at_end()) {
                    if (nested == 0 && current().type == TokenType::Semicolon) break;
                    if (current().type == TokenType::LeftParen || current().type == TokenType::LeftBracket || current().type == TokenType::LeftBrace) {
                        ++nested;
                    } else if ((current().type == TokenType::RightParen || current().type == TokenType::RightBracket || current().type == TokenType::RightBrace) &&
                               nested > 0) {
                        --nested;
                    }
                    value_end_token = current();
                    advance();
                }

                field->default_value = join_expression_token_text(tokens_, value_begin, pos_);
                field->default_value_range = {tokens_[value_begin].location, token_end_location(value_end_token)};
                if (auto ranges = parse_constructor_source_ranges(std::vector<Token>(tokens_.begin() + static_cast<std::ptrdiff_t>(value_begin),
                                                                                     tokens_.begin() + static_cast<std::ptrdiff_t>(pos_)))) {
                    field->default_constructor_range = ranges->range;
                    field->default_constructor_type_range = ranges->type_range;
                    field->default_constructor_arg_range = ranges->arg_range;
                }
            }

            if (current().type == TokenType::Semicolon) {
                field->range.end = token_end_location(current());
            }
            expect(TokenType::Semicolon, "Expected ';' after field declaration");
            node->fields.push_back(std::move(field));
            continue;
        }

        // pin_kind pin_dir name [: type] ;
        auto pin = std::make_unique<PinDeclNode>();
        pin->range.start = current().location;
        pin->annotations = std::move(annots);

        if (current().type == TokenType::KW_exec) {
            pin->kind = PinKind::Exec;
        } else if (current().type == TokenType::KW_data) {
            pin->kind = PinKind::Data;
        } else {
            errors_.push_back("Expected 'exec' or 'data' in pin declaration");
            return nullptr;
        }
        advance();

        if (current().type == TokenType::KW_in) {
            pin->direction = PinDirection::Input;
        } else if (current().type == TokenType::KW_out) {
            pin->direction = PinDirection::Output;
        } else {
            errors_.push_back("Expected 'in' or 'out' in pin declaration");
            return nullptr;
        }
        advance();

        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected pin name");
            return nullptr;
        }
        pin->name = current().text;
        pin->name_range.start = current().location;
        pin->name_range.end = token_end_location(current());
        advance();

        if (current().type == TokenType::Colon) {
            advance();
            if (current().type != TokenType::Identifier) {
                errors_.push_back("Expected type name after ':'");
                return nullptr;
            }
            pin->type_name = current().text;
            pin->type_name_range.start = current().location;
            pin->type_name_range.end = token_end_location(current());
            advance();
        }
        if (current().type == TokenType::Semicolon) {
            pin->range.end = token_end_location(current());
        }
        expect(TokenType::Semicolon, "Expected ';' after pin declaration");
        node->pins.push_back(std::move(pin));
    }
    if (current().type == TokenType::RightBrace) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::RightBrace, "Expected '}' closing declare Node");
    return node;
}

// Parses declare Schema Name { field* }
std::unique_ptr<DeclareSchemaNode> Parser::parse_declare_schema() {
    auto node = std::make_unique<DeclareSchemaNode>();
    node->range.start = current().location;
    advance(); // consume 'declare'
    advance(); // consume 'Schema'
    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected schema name after 'declare Schema'");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();
    if (!expect(TokenType::LeftBrace, "Expected '{' after schema name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        std::vector<Annotation> annots;
        if (current().type == TokenType::LeftBracket) {
            annots = parse_annotations();
        }
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected field name in schema body");
            return nullptr;
        }
        auto field = std::make_unique<SchemaFieldNode>();
        field->range.start = current().location;
        field->annotations = std::move(annots);
        field->name = current().text;
        field->name_range.start = current().location;
        field->name_range.end = token_end_location(current());
        advance();
        if (!expect(TokenType::Colon, "Expected ':' after schema field name")) return nullptr;

        // Value can be: identifier, literal, constructor call, or array [...]
        if (current().type == TokenType::LeftBracket) {
            field->value_range.start = current().location;
            field->value += "[";
            advance();
            while (!at_end() && current().type != TokenType::RightBracket) {
                if (current().type == TokenType::Comma) {
                    field->value += ", ";
                    advance();
                    continue;
                }
                if (current().type == TokenType::StringLiteral) {
                    field->value += "\"" + current().text + "\"";
                } else {
                    field->value += current().text;
                }
                advance();
            }
            field->value += "]";
            if (current().type == TokenType::RightBracket) {
                field->value_range.end = token_end_location(current());
            }
            expect(TokenType::RightBracket, "Expected ']'");
        } else if (current().type == TokenType::Identifier && peek_next().type == TokenType::LeftParen) {
            const size_t value_begin = pos_;
            const Token constructor_type_token = current();
            Token value_end_token = current();
            int nested = 0;
            while (!at_end()) {
                const Token token = current();
                if (token.type == TokenType::LeftParen || token.type == TokenType::LeftBracket) {
                    ++nested;
                } else if ((token.type == TokenType::RightParen || token.type == TokenType::RightBracket) &&
                           nested > 0) {
                    --nested;
                }
                value_end_token = token;
                advance();
                if (nested == 0 && token.type == TokenType::RightParen) break;
            }
            field->value = join_expression_token_text(tokens_, value_begin, pos_);
            field->value_range.start = constructor_type_token.location;
            field->value_range.end = token_end_location(value_end_token);
            field->value_constructor_range = field->value_range;
            field->value_constructor_type_range.start = constructor_type_token.location;
            field->value_constructor_type_range.end = token_end_location(constructor_type_token);

            const size_t arg_begin = value_begin + 2;
            const size_t close_paren = pos_ > value_begin ? pos_ - 1 : value_begin;
            if (arg_begin < close_paren) {
                field->value_constructor_arg_range.start = tokens_[arg_begin].location;
                field->value_constructor_arg_range.end = token_end_location(tokens_[close_paren - 1]);
            }
        } else {
            const Token value_token = current();
            field->value_range.start = value_token.location;
            field->value_range.end = token_end_location(value_token);
            field->value = current().text;
            advance();
        }
        if (current().type == TokenType::Semicolon) {
            field->range.end = token_end_location(current());
        }
        expect(TokenType::Semicolon, "Expected ';' after schema field value");
        node->fields.push_back(std::move(field));
    }
    if (current().type == TokenType::RightBrace) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::RightBrace, "Expected '}' closing declare Schema");
    return node;
}

// Parses Graph Name [: BaseType] { params, events, functions, node_instances, generate }
std::unique_ptr<GraphNode> Parser::parse_graph() {
    auto node = std::make_unique<GraphNode>();
    node->range.start = current().location;
    advance(); // consume 'Graph'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected graph name after 'Graph'");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();

    if (current().type == TokenType::Colon) {
        advance();
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected base type after ':'");
            return nullptr;
        }
        node->base_type = current().text;
        node->base_type_range.start = current().location;
        node->base_type_range.end = token_end_location(current());
        advance();
    }

    if (!expect(TokenType::LeftBrace, "Expected '{' after graph name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }

        auto& tok = current();
        if (tok.type == TokenType::LeftBracket) {
            auto annots = parse_annotations();
            auto& next = current();
            if (next.type == TokenType::KW_in || next.type == TokenType::KW_out || next.type == TokenType::KW_var) {
                auto param = parse_param();
                if (!param) return nullptr;
                param->annotations = std::move(annots);
                node->params.push_back(std::move(param));
            } else if (next.type == TokenType::Identifier) {
                auto inst = parse_node_instance();
                if (!inst) return nullptr;
                inst->annotations = std::move(annots);
                node->node_instances.push_back(std::move(inst));
            } else if (next.type == TokenType::KW_event) {
                auto ev = parse_event();
                if (!ev) return nullptr;
                ev->annotations = std::move(annots);
                node->events.push_back(std::move(ev));
            } else if (next.type == TokenType::KW_function) {
                auto fn = parse_function();
                if (!fn) return nullptr;
                fn->annotations = std::move(annots);
                node->functions.push_back(std::move(fn));
            } else {
                errors_.push_back("Expected parameter, node instance, event, or function after annotations at line " + std::to_string(next.location.line));
                return nullptr;
            }
        } else if (tok.type == TokenType::KW_in || tok.type == TokenType::KW_out || tok.type == TokenType::KW_var) {
            auto param = parse_param();
            if (!param) return nullptr;
            node->params.push_back(std::move(param));
        } else if (tok.type == TokenType::KW_event) {
            auto ev = parse_event();
            if (!ev) return nullptr;
            node->events.push_back(std::move(ev));
        } else if (tok.type == TokenType::KW_function) {
            auto fn = parse_function();
            if (!fn) return nullptr;
            node->functions.push_back(std::move(fn));
        } else if (tok.type == TokenType::KW_generate) {
            auto gen = parse_generate();
            if (!gen) return nullptr;
            node->generate = std::move(gen);
        } else if (tok.type == TokenType::Identifier) {
            auto inst = parse_node_instance();
            if (!inst) return nullptr;
            node->node_instances.push_back(std::move(inst));
        } else {
            errors_.push_back("Unexpected token '" + tok.text + "' in Graph body at line " + std::to_string(tok.location.line));
            return nullptr;
        }
    }
    expect(TokenType::RightBrace, "Expected '}' closing Graph");
    return node;
}

// Parses in/out/var name : Type [= default];
std::unique_ptr<ParamDeclNode> Parser::parse_param() {
    auto node = std::make_unique<ParamDeclNode>();
    node->range.start = current().location;
    node->direction = current().text;
    advance(); // consume in/out/var

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected parameter name");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();

    if (!expect(TokenType::Colon, "Expected ':' after parameter name")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected type name");
        return nullptr;
    }
    node->type_name = current().text;
    node->type_name_range.start = current().location;
    node->type_name_range.end = token_end_location(current());
    advance();

    if (current().type == TokenType::Assign) {
        advance();
        // Consume the default value (could be literal or constructor)
        node->default_value_range.start = current().location;
        if (current().type == TokenType::Identifier) {
            const Token value_token = current();
            std::string val = current().text;
            advance();
            if (current().type == TokenType::LeftParen) {
                node->default_constructor_range.start = value_token.location;
                node->default_constructor_type_range.start = value_token.location;
                node->default_constructor_type_range.end = token_end_location(value_token);
                advance();
                val += "(";
                if (current().type != TokenType::RightParen) {
                    const size_t arg_begin = pos_;
                    const Token arg_token = current();
                    Token arg_end_token = current();
                    int nested = 0;
                    while (!at_end()) {
                        if (nested == 0 && current().type == TokenType::RightParen) break;
                        if (current().type == TokenType::LeftParen || current().type == TokenType::LeftBracket) {
                            ++nested;
                        } else if ((current().type == TokenType::RightParen || current().type == TokenType::RightBracket) &&
                                   nested > 0) {
                            --nested;
                        }
                        arg_end_token = current();
                        advance();
                    }
                    node->default_constructor_arg_range.start = arg_token.location;
                    node->default_constructor_arg_range.end = token_end_location(arg_end_token);
                    val += join_expression_token_text(tokens_, arg_begin, pos_);
                }
                val += ")";
                if (current().type == TokenType::RightParen) {
                    node->default_constructor_range.end = token_end_location(current());
                    node->default_value_range.end = token_end_location(current());
                }
                expect(TokenType::RightParen, "Expected ')' in default value constructor");
            } else {
                node->default_value_range.end = token_end_location(value_token);
            }
            node->default_value = val;
        } else {
            const Token value_token = current();
            node->default_value = current().text;
            node->default_value_range.end = token_end_location(value_token);
            advance();
        }
    }

    expect(TokenType::Semicolon, "Expected ';' after parameter");
    return node;
}

// Parses TypeName instance_name{} or TypeName instance_name{initializer};
std::unique_ptr<NodeInstanceNode> Parser::parse_node_instance() {
    auto node = std::make_unique<NodeInstanceNode>();
    node->range.start = current().location;
    node->type_name = current().text;
    node->type_name_range.start = current().location;
    node->type_name_range.end = token_end_location(current());
    advance();

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected instance name after type name");
        return nullptr;
    }
    node->instance_name = current().text;
    node->instance_name_range.start = current().location;
    node->instance_name_range.end = token_end_location(current());
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' for node instance")) return nullptr;

    if (current().type != TokenType::RightBrace) {
        std::vector<Token> init_tokens;
        node->initializer_range.start = current().location;
        Token last_token = current();
        while (!at_end() && current().type != TokenType::RightBrace) {
            last_token = current();
            init_tokens.push_back(current());
            advance();
        }
        node->initializer = join_initializer_token_text(init_tokens, 0, init_tokens.size());
        node->initializer_fields = parse_initializer_fields(init_tokens);
        if (auto constructor_ranges = parse_constructor_source_ranges(init_tokens)) {
            node->initializer_constructor_range = constructor_ranges->range;
            node->initializer_constructor_type_range = constructor_ranges->type_range;
            node->initializer_constructor_arg_range = constructor_ranges->arg_range;
        }
        node->initializer_range.end = token_end_location(last_token);
    }
    expect(TokenType::RightBrace, "Expected '}' closing node instance");
    expect(TokenType::Semicolon, "Expected ';' after node instance");
    return node;
}

// Parses event Name { flow_stmt* assign_stmt* } where assign_stmt is `a.b = c[.d];`.
std::unique_ptr<EventNode> Parser::parse_event() {
    auto node = std::make_unique<EventNode>();
    node->range.start = current().location;
    advance(); // consume 'event'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected event name");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' after event name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type == TokenType::LeftBracket) {
            auto annots = parse_annotations();
            if (current().type == TokenType::KW_link || starts_with_assignment_stmt()) {
                auto link = parse_link_stmt();
                if (!link) return nullptr;
                link->annotations = std::move(annots);
                node->link_stmts.push_back(std::move(link));
            } else {
                auto flow = parse_flow_stmt();
                if (!flow) return nullptr;
                flow->annotations = std::move(annots);
                node->flow_stmts.push_back(std::move(flow));
            }
        } else if (current().type == TokenType::KW_link || starts_with_assignment_stmt()) {
            auto link = parse_link_stmt();
            if (!link) return nullptr;
            node->link_stmts.push_back(std::move(link));
        } else {
            auto flow = parse_flow_stmt();
            if (!flow) return nullptr;
            node->flow_stmts.push_back(std::move(flow));
        }
    }
    expect(TokenType::RightBrace, "Expected '}' closing event");
    return node;
}

// Parses function Name { flow_stmt* assign_stmt* } where assign_stmt is `a.b = c[.d];`.
std::unique_ptr<FunctionNode> Parser::parse_function() {
    auto node = std::make_unique<FunctionNode>();
    node->range.start = current().location;
    advance(); // consume 'function'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected function name");
        return nullptr;
    }
    node->name = current().text;
    node->name_range.start = current().location;
    node->name_range.end = token_end_location(current());
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' after function name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type == TokenType::LeftBracket) {
            auto annots = parse_annotations();
            if (current().type == TokenType::KW_link || starts_with_assignment_stmt()) {
                auto link = parse_link_stmt();
                if (!link) return nullptr;
                link->annotations = std::move(annots);
                node->link_stmts.push_back(std::move(link));
            } else {
                auto flow = parse_flow_stmt();
                if (!flow) return nullptr;
                flow->annotations = std::move(annots);
                node->flow_stmts.push_back(std::move(flow));
            }
        } else if (current().type == TokenType::KW_link || starts_with_assignment_stmt()) {
            auto link = parse_link_stmt();
            if (!link) return nullptr;
            node->link_stmts.push_back(std::move(link));
        } else {
            auto flow = parse_flow_stmt();
            if (!flow) return nullptr;
            node->flow_stmts.push_back(std::move(flow));
        }
    }
    expect(TokenType::RightBrace, "Expected '}' closing function");
    return node;
}

// Parses generate { Comment/Metadata }
std::unique_ptr<GenerateNode> Parser::parse_generate() {
    auto node = std::make_unique<GenerateNode>();
    node->range.start = current().location;
    advance(); // consume 'generate'

    if (!expect(TokenType::LeftBrace, "Expected '{' after generate")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type == TokenType::LeftBracket) {
            auto annots = parse_annotations();
            if (current().type == TokenType::KW_Comment) {
                auto c = parse_comment();
                if (!c) return nullptr;
                c->annotations = std::move(annots);
                node->comments.push_back(std::move(c));
            } else {
                auto m = parse_metadata();
                if (!m) return nullptr;
                m->annotations = std::move(annots);
                node->metadata.push_back(std::move(m));
            }
        } else if (current().type == TokenType::KW_Comment) {
            auto c = parse_comment();
            if (!c) return nullptr;
            node->comments.push_back(std::move(c));
        } else {
            auto m = parse_metadata();
            if (!m) return nullptr;
            node->metadata.push_back(std::move(m));
        }
    }
    if (current().type == TokenType::RightBrace) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::RightBrace, "Expected '}' closing generate");
    return node;
}

// Parses from_node.from_pin(to_node.to_pin) flow statement.
std::unique_ptr<FlowStmtNode> Parser::parse_flow_stmt() {
    auto node = std::make_unique<FlowStmtNode>();
    node->range.start = current().location;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected identifier in flow statement");
        return nullptr;
    }
    const Token from_node_token = current();
    node->from_expr_range.start = from_node_token.location;
    node->from_node = current().text;
    node->from_node_range = {from_node_token.location, token_end_location(from_node_token)};
    advance();

    if (!expect(TokenType::Dot, "Expected '.' after node name in flow statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected pin name");
        return nullptr;
    }
    const Token from_pin_token = current();
    node->from_pin = current().text;
    node->from_pin_range = {from_pin_token.location, token_end_location(from_pin_token)};
    node->from_expr_range.end = token_end_location(from_pin_token);
    advance();

    if (!expect(TokenType::LeftParen, "Expected '(' in flow statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target node name");
        return nullptr;
    }
    const Token to_node_token = current();
    node->to_expr_range.start = to_node_token.location;
    node->to_node = current().text;
    node->to_node_range = {to_node_token.location, token_end_location(to_node_token)};
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in flow target")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target pin name");
        return nullptr;
    }
    const Token to_pin_token = current();
    node->to_pin = current().text;
    node->to_pin_range = {to_pin_token.location, token_end_location(to_pin_token)};
    node->to_expr_range.end = token_end_location(to_pin_token);
    advance();

    if (!expect(TokenType::RightParen, "Expected ')' in flow statement")) return nullptr;
    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after flow statement");
    return node;
}

// Parses data assignment: [link] target.target_pin = source.source_pin; bare identifier = param reference.
std::unique_ptr<LinkStmtNode> Parser::parse_link_stmt() {
    auto node = std::make_unique<LinkStmtNode>();
    node->range.start = current().location;
    if (current().type == TokenType::KW_link) {
        advance(); // consume optional legacy 'link' keyword
    }

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target node in link statement");
        return nullptr;
    }
    const Token target_node_token = current();
    node->target_expr_range.start = target_node_token.location;
    node->target_node = current().text;
    node->target_node_range = {target_node_token.location, token_end_location(target_node_token)};
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in link target")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target pin name");
        return nullptr;
    }
    const Token target_pin_token = current();
    node->target_pin = current().text;
    node->target_pin_range = {target_pin_token.location, token_end_location(target_pin_token)};
    node->target_expr_range.end = token_end_location(target_pin_token);
    advance();

    if (!expect(TokenType::Assign, "Expected '=' in link statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected source in link statement");
        return nullptr;
    }
    const Token source_node_token = current();
    node->source_expr_range.start = source_node_token.location;
    Token source_end_token = current();
    node->source_node = current().text;
    node->source_node_range = {source_node_token.location, token_end_location(source_node_token)};
    advance();

    if (current().type == TokenType::Dot) {
        advance();
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected source pin name after '.'");
            return nullptr;
        }
        source_end_token = current();
        node->source_pin = current().text;
        node->source_pin_range = {current().location, token_end_location(current())};
        advance();
    }
    node->source_expr_range.end = token_end_location(source_end_token);
    // If no dot, source_pin stays empty → bare parameter reference

    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after link statement");
    return node;
}

// Parses Comment instance_name = "text";
std::unique_ptr<CommentNode> Parser::parse_comment() {
    auto node = std::make_unique<CommentNode>();
    node->range.start = current().location;
    advance(); // consume 'Comment'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected comment instance name");
        return nullptr;
    }
    const Token instance_token = current();
    node->instance_name = current().text;
    node->instance_name_range = {instance_token.location, token_end_location(instance_token)};
    advance();

    if (!expect(TokenType::Assign, "Expected '=' after comment name")) return nullptr;

    if (current().type != TokenType::StringLiteral) {
        errors_.push_back("Expected string literal for comment text");
        return nullptr;
    }
    const Token text_token = current();
    node->text = current().text;
    node->text_range = {text_token.location, token_end_location(text_token)};
    advance();

    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after comment");
    return node;
}

// Parses scope:node.property(value); metadata.
std::unique_ptr<MetadataNode> Parser::parse_metadata() {
    auto node = std::make_unique<MetadataNode>();
    node->range.start = current().location;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected scope name in metadata");
        return nullptr;
    }
    const Token scope_token = current();
    node->scope = current().text;
    node->scope_range = {scope_token.location, token_end_location(scope_token)};
    advance();

    if (!expect(TokenType::Colon, "Expected ':' after scope")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected node name in metadata");
        return nullptr;
    }
    const Token node_token = current();
    node->node = current().text;
    node->node_range = {node_token.location, token_end_location(node_token)};
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in metadata")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected property name in metadata");
        return nullptr;
    }
    const Token property_token = current();
    node->property = current().text;
    node->property_range = {property_token.location, token_end_location(property_token)};
    advance();

    if (!expect(TokenType::LeftParen, "Expected '(' in metadata")) return nullptr;

    // Value can be a string, number, identifier, constructor call, etc.
    const size_t value_begin = pos_;
    const Token value_token = current();
    if (current().type == TokenType::Identifier && peek_next().type == TokenType::LeftParen) {
        const Token constructor_type_token = current();
        Token value_end_token = current();
        int nested = 0;
        while (!at_end()) {
            const Token token = current();
            if (token.type == TokenType::LeftParen || token.type == TokenType::LeftBracket) {
                ++nested;
            } else if ((token.type == TokenType::RightParen || token.type == TokenType::RightBracket) &&
                       nested > 0) {
                --nested;
            }
            value_end_token = token;
            advance();
            if (nested == 0 && token.type == TokenType::RightParen) break;
        }
        node->value = join_expression_token_text(tokens_, value_begin, pos_);
        node->value_range = {constructor_type_token.location, token_end_location(value_end_token)};
        node->value_constructor_range = node->value_range;
        node->value_constructor_type_range = {constructor_type_token.location, token_end_location(constructor_type_token)};

        const size_t arg_begin = value_begin + 2;
        const size_t close_paren = pos_ > value_begin ? pos_ - 1 : value_begin;
        if (arg_begin < close_paren) {
            node->value_constructor_arg_range = {tokens_[arg_begin].location, token_end_location(tokens_[close_paren - 1])};
        }
    } else {
        node->value = expression_token_text(current());
        node->value_range = {value_token.location, token_end_location(value_token)};
        advance();
    }

    if (!expect(TokenType::RightParen, "Expected ')' in metadata")) return nullptr;
    if (current().type == TokenType::Semicolon) {
        node->range.end = token_end_location(current());
    }
    expect(TokenType::Semicolon, "Expected ';' after metadata");
    return node;
}

// Returns true for tokens that can serve as annotation names or named-arg keys
// (identifiers and keywords are both valid word tokens).
static bool is_word_token(TokenType t) {
    return t == TokenType::Identifier ||
           (t >= TokenType::KW_import && t <= TokenType::KW_data);
}

// Parses [Name(args), Name2(key = val, ...)] C#-style annotation list.
std::vector<Annotation> Parser::parse_annotations() {
    std::vector<Annotation> annotations;
    advance(); // consume '['

    while (!at_end() && current().type != TokenType::RightBracket) {
        Annotation annot;
        if (!is_word_token(current().type)) {
            errors_.push_back("Expected annotation name at line " + std::to_string(current().location.line));
            return {};
        }
        const SourceLocation annotation_start = current().location;
        annot.name = current().text;
        annot.name_range.start = current().location;
        annot.name_range.end = token_end_location(current());
        advance();

        if (!expect(TokenType::LeftParen, "Expected '(' after annotation name")) return {};

        while (!at_end() && current().type != TokenType::RightParen) {
            AnnotationArg arg;
            arg.source_range.start = current().location;
            if (is_word_token(current().type) && peek_next().type == TokenType::Assign) {
                arg.name = current().text;
                arg.name_range.start = current().location;
                arg.name_range.end = token_end_location(current());
                advance(); // consume name
                advance(); // consume '='
            }
            if (current().type == TokenType::Identifier && peek_next().type == TokenType::LeftParen) {
                const size_t value_begin = pos_;
                const Token constructor_type_token = current();
                advance(); // consume constructor type
                advance(); // consume '('

                const size_t constructor_arg_begin = pos_;
                Token value_end_token = current();
                int nested = 0;
                while (!at_end()) {
                    if (nested == 0 && current().type == TokenType::RightParen) {
                        value_end_token = current();
                        break;
                    }
                    if (current().type == TokenType::LeftParen || current().type == TokenType::LeftBracket) {
                        ++nested;
                    } else if ((current().type == TokenType::RightParen || current().type == TokenType::RightBracket) &&
                               nested > 0) {
                        --nested;
                    }
                    value_end_token = current();
                    advance();
                }

                const size_t value_end = at_end() ? pos_ : pos_ + 1;
                arg.value = join_expression_token_text(tokens_, value_begin, value_end);
                arg.value_range.start = constructor_type_token.location;
                arg.value_range.end = token_end_location(value_end_token);
                arg.value_constructor_range = arg.value_range;
                arg.value_constructor_type_range.start = constructor_type_token.location;
                arg.value_constructor_type_range.end = token_end_location(constructor_type_token);
                if (constructor_arg_begin < pos_) {
                    arg.value_constructor_arg_range.start = tokens_[constructor_arg_begin].location;
                    arg.value_constructor_arg_range.end = token_end_location(tokens_[pos_ - 1]);
                }
                arg.source_range.end = token_end_location(value_end_token);
                if (!at_end() && current().type == TokenType::RightParen) {
                    advance(); // consume constructor ')'
                }
            } else {
                const Token value_token = current();
                arg.value = current().text;
                arg.value_range.start = value_token.location;
                arg.value_range.end = token_end_location(value_token);
                arg.source_range.end = token_end_location(value_token);
                advance();
            }

            annot.args.push_back(std::move(arg));

            if (current().type == TokenType::Comma) {
                advance();
            }
        }
        annot.source_range.start = annotation_start;
        annot.source_range.end = token_end_location(current());
        if (!expect(TokenType::RightParen, "Expected ')' after annotation arguments")) return {};

        annotations.push_back(std::move(annot));

        if (current().type == TokenType::Comma) {
            advance();
        }
    }
    expect(TokenType::RightBracket, "Expected ']' closing annotations");
    return annotations;
}

} // namespace gs
