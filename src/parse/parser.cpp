#include "graphscript/parse/parser.h"
#include <algorithm>

namespace gs {

// Constructs parser from token stream.
Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// Parses module: imports, lets, declare type/Node/Schema, graphs.
Result<std::unique_ptr<ModuleNode>, std::string> Parser::parse() {
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
                errors_.push_back("Expected 'type', 'Node', or 'Schema' after 'declare' at line " + std::to_string(tok.location.line));
                return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.back());
            }
        } else if (tok.type == TokenType::KW_Graph) {
            auto node = parse_graph();
            if (!node) return Result<std::unique_ptr<ModuleNode>, std::string>::err(errors_.empty() ? "Failed to parse Graph" : errors_.back());
            module->graphs.push_back(std::move(node));
        } else {
            errors_.push_back("Unexpected token '" + tok.text + "' at line " + std::to_string(tok.location.line));
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
    errors_.push_back(msg + " at line " + std::to_string(current().location.line) + " (got '" + current().text + "')");
    return false;
}

// True when past last token or at EOF.
bool Parser::at_end() const {
    return pos_ >= tokens_.size() || tokens_[pos_].type == TokenType::EndOfFile;
}

// Parses import "path";
std::unique_ptr<ImportNode> Parser::parse_import() {
    auto node = std::make_unique<ImportNode>();
    node->range.start = current().location;
    advance(); // consume 'import'
    if (current().type != TokenType::StringLiteral) {
        errors_.push_back("Expected string after 'import'");
        return nullptr;
    }
    node->path = current().text;
    advance();
    expect(TokenType::Semicolon, "Expected ';' after import path");
    return node;
}

// Parses let name = TypeName("arg");
std::unique_ptr<LetDeclNode> Parser::parse_let() {
    auto node = std::make_unique<LetDeclNode>();
    node->range.start = current().location;
    advance(); // consume 'let'
    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected identifier after 'let'");
        return nullptr;
    }
    node->name = current().text;
    advance();
    if (!expect(TokenType::Assign, "Expected '=' after let name")) return nullptr;
    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected type name in let initializer");
        return nullptr;
    }
    node->type_name = current().text;
    advance();
    if (!expect(TokenType::LeftParen, "Expected '(' in constructor")) return nullptr;
    if (current().type == TokenType::StringLiteral) {
        node->constructor_arg = current().text;
        advance();
    }
    if (!expect(TokenType::RightParen, "Expected ')' in constructor")) return nullptr;
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
    expect(TokenType::Semicolon, "Expected ';' after declare type");
    return node;
}

// Parses declare Node Name { pin_decl* }
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
    advance();
    if (!expect(TokenType::LeftBrace, "Expected '{' after node name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        // pin_kind pin_dir name [: type] ;
        auto pin = std::make_unique<PinDeclNode>();
        pin->range.start = current().location;

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
        advance();

        if (current().type == TokenType::Colon) {
            advance();
            if (current().type != TokenType::Identifier) {
                errors_.push_back("Expected type name after ':'");
                return nullptr;
            }
            pin->type_name = current().text;
            advance();
        }
        expect(TokenType::Semicolon, "Expected ';' after pin declaration");
        node->pins.push_back(std::move(pin));
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
    advance();
    if (!expect(TokenType::LeftBrace, "Expected '{' after schema name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected field name in schema body");
            return nullptr;
        }
        std::string key = current().text;
        advance();
        if (!expect(TokenType::Colon, "Expected ':' after schema field name")) return nullptr;

        // Value can be: identifier, literal, or array [...]
        std::string value;
        if (current().type == TokenType::LeftBracket) {
            value += "[";
            advance();
            while (!at_end() && current().type != TokenType::RightBracket) {
                if (current().type == TokenType::Comma) {
                    value += ", ";
                    advance();
                    continue;
                }
                if (current().type == TokenType::StringLiteral) {
                    value += "\"" + current().text + "\"";
                } else {
                    value += current().text;
                }
                advance();
            }
            value += "]";
            expect(TokenType::RightBracket, "Expected ']'");
        } else {
            value = current().text;
            advance();
        }
        expect(TokenType::Semicolon, "Expected ';' after schema field value");
        node->fields.emplace_back(std::move(key), std::move(value));
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
    advance();

    if (current().type == TokenType::Colon) {
        advance();
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected base type after ':'");
            return nullptr;
        }
        node->base_type = current().text;
        advance();
    }

    if (!expect(TokenType::LeftBrace, "Expected '{' after graph name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }

        auto& tok = current();
        if (tok.type == TokenType::KW_in || tok.type == TokenType::KW_out || tok.type == TokenType::KW_var) {
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
    advance();

    if (!expect(TokenType::Colon, "Expected ':' after parameter name")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected type name");
        return nullptr;
    }
    node->type_name = current().text;
    advance();

    if (current().type == TokenType::Assign) {
        advance();
        // Consume the default value (could be literal or constructor)
        if (current().type == TokenType::Identifier) {
            std::string val = current().text;
            advance();
            if (current().type == TokenType::LeftParen) {
                advance();
                val += "(";
                if (current().type != TokenType::RightParen) {
                    val += current().text;
                    advance();
                }
                val += ")";
                expect(TokenType::RightParen, "Expected ')' in default value constructor");
            }
            node->default_value = val;
        } else {
            node->default_value = current().text;
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
    advance();

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected instance name after type name");
        return nullptr;
    }
    node->instance_name = current().text;
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' for node instance")) return nullptr;

    if (current().type != TokenType::RightBrace) {
        std::string init;
        while (!at_end() && current().type != TokenType::RightBrace) {
            if (!init.empty()) init += " ";
            init += current().text;
            advance();
        }
        node->initializer = init;
    }
    expect(TokenType::RightBrace, "Expected '}' closing node instance");
    expect(TokenType::Semicolon, "Expected ';' after node instance");
    return node;
}

// Parses event Name { flow_stmt* link_stmt* }
std::unique_ptr<EventNode> Parser::parse_event() {
    auto node = std::make_unique<EventNode>();
    node->range.start = current().location;
    advance(); // consume 'event'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected event name");
        return nullptr;
    }
    node->name = current().text;
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' after event name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type == TokenType::KW_link) {
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

// Parses function Name { flow_stmt* link_stmt* }
std::unique_ptr<FunctionNode> Parser::parse_function() {
    auto node = std::make_unique<FunctionNode>();
    node->range.start = current().location;
    advance(); // consume 'function'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected function name");
        return nullptr;
    }
    node->name = current().text;
    advance();

    if (!expect(TokenType::LeftBrace, "Expected '{' after function name")) return nullptr;

    while (!at_end() && current().type != TokenType::RightBrace) {
        if (current().type == TokenType::LineComment || current().type == TokenType::BlockComment) {
            advance();
            continue;
        }
        if (current().type == TokenType::KW_link) {
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
        if (current().type == TokenType::KW_Comment) {
            auto c = parse_comment();
            if (!c) return nullptr;
            node->comments.push_back(std::move(c));
        } else {
            auto m = parse_metadata();
            if (!m) return nullptr;
            node->metadata.push_back(std::move(m));
        }
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
    node->from_node = current().text;
    advance();

    if (!expect(TokenType::Dot, "Expected '.' after node name in flow statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected pin name");
        return nullptr;
    }
    node->from_pin = current().text;
    advance();

    if (!expect(TokenType::LeftParen, "Expected '(' in flow statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target node name");
        return nullptr;
    }
    node->to_node = current().text;
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in flow target")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target pin name");
        return nullptr;
    }
    node->to_pin = current().text;
    advance();

    if (!expect(TokenType::RightParen, "Expected ')' in flow statement")) return nullptr;
    expect(TokenType::Semicolon, "Expected ';' after flow statement");
    return node;
}

// Parses link target.target_pin = source.source_pin; bare identifier = param reference.
std::unique_ptr<LinkStmtNode> Parser::parse_link_stmt() {
    auto node = std::make_unique<LinkStmtNode>();
    node->range.start = current().location;
    advance(); // consume 'link'

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target node in link statement");
        return nullptr;
    }
    node->target_node = current().text;
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in link target")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected target pin name");
        return nullptr;
    }
    node->target_pin = current().text;
    advance();

    if (!expect(TokenType::Assign, "Expected '=' in link statement")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected source in link statement");
        return nullptr;
    }
    node->source_node = current().text;
    advance();

    if (current().type == TokenType::Dot) {
        advance();
        if (current().type != TokenType::Identifier) {
            errors_.push_back("Expected source pin name after '.'");
            return nullptr;
        }
        node->source_pin = current().text;
        advance();
    }
    // If no dot, source_pin stays empty → bare parameter reference

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
    node->instance_name = current().text;
    advance();

    if (!expect(TokenType::Assign, "Expected '=' after comment name")) return nullptr;

    if (current().type != TokenType::StringLiteral) {
        errors_.push_back("Expected string literal for comment text");
        return nullptr;
    }
    node->text = current().text;
    advance();

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
    node->scope = current().text;
    advance();

    if (!expect(TokenType::Colon, "Expected ':' after scope")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected node name in metadata");
        return nullptr;
    }
    node->node = current().text;
    advance();

    if (!expect(TokenType::Dot, "Expected '.' in metadata")) return nullptr;

    if (current().type != TokenType::Identifier) {
        errors_.push_back("Expected property name in metadata");
        return nullptr;
    }
    node->property = current().text;
    advance();

    if (!expect(TokenType::LeftParen, "Expected '(' in metadata")) return nullptr;

    // Value can be a string, number, identifier, etc.
    node->value = current().text;
    advance();

    if (!expect(TokenType::RightParen, "Expected ')' in metadata")) return nullptr;
    expect(TokenType::Semicolon, "Expected ';' after metadata");
    return node;
}

} // namespace gs
