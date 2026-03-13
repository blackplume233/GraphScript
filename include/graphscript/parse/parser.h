#pragma once

#include <vector>
#include <memory>

#include "graphscript/parse/token.h"
#include "graphscript/parse/ast.h"
#include "graphscript/core/result.h"

namespace gs {

/// Parses token stream into AST (ModuleNode).
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    /// Parses full module. Returns error string on parse failure.
    Result<std::unique_ptr<ModuleNode>, std::string> parse();

private:
    const Token& current() const;
    const Token& peek_next() const;
    const Token& advance();
    bool         match(TokenType type);
    bool         expect(TokenType type, const std::string& msg);
    bool         at_end() const;

    std::unique_ptr<ImportNode>        parse_import();
    std::unique_ptr<LetDeclNode>       parse_let();
    std::unique_ptr<GraphNode>         parse_graph();
    std::unique_ptr<DeclareTypeNode>   parse_declare_type();
    std::unique_ptr<DeclareNodeNode>   parse_declare_node();
    std::unique_ptr<DeclareSchemaNode> parse_declare_schema();
    std::unique_ptr<ParamDeclNode>     parse_param();
    std::unique_ptr<NodeInstanceNode>  parse_node_instance();
    std::unique_ptr<EventNode>         parse_event();
    std::unique_ptr<FunctionNode>      parse_function();
    std::unique_ptr<GenerateNode>      parse_generate();
    std::unique_ptr<FlowStmtNode>      parse_flow_stmt();
    std::unique_ptr<LinkStmtNode>      parse_link_stmt();
    std::unique_ptr<CommentNode>       parse_comment();
    std::unique_ptr<MetadataNode>      parse_metadata();
    std::vector<Annotation>            parse_annotations();

    std::vector<Token> tokens_;
    size_t             pos_ = 0;
    std::vector<std::string> errors_;
};

} // namespace gs
