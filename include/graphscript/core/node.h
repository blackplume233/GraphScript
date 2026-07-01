#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

#include "graphscript/core/pin.h"
#include "graphscript/core/annotation.h"
#include "graphscript/core/initializer_field.h"
#include "graphscript/core/source_range.h"

namespace gs {

/// Node-local field definition. Fields are intrinsic node properties, not pins,
/// so they do not participate in graph connections.
struct NodeFieldDefinition {
    std::string name;       ///< Field name.
    std::string type_name;  ///< Field value type.
    std::string default_value; ///< Optional default value expression.
    std::vector<Annotation> annotations; ///< C# style annotations.
    SourceRange source_range; ///< Field declaration source span.
    SourceRange name_range; ///< Field name source span.
    SourceRange type_name_range; ///< Field type source span.
    SourceRange default_value_range; ///< Field default value source span.
    SourceRange default_constructor_range; ///< Default constructor call span.
    SourceRange default_constructor_type_range; ///< Default constructor type span.
    SourceRange default_constructor_arg_range; ///< Default constructor argument span.
    std::string source_file; ///< Source file containing this declaration.
};

/// 节点定义，描述一类节点的类型、Pin 配置及元数据。
struct NodeDefinition {
    std::string                type_name;     ///< 节点类型名称
    std::vector<PinDefinition> pins;          ///< 所有 Pin 定义
    std::vector<NodeFieldDefinition> fields;  ///< Intrinsic fields, separate from pins.
    bool                       is_native = true;  ///< 是否为原生（C++）实现
    std::vector<std::string>   tags;          ///< 标签，用于分类与搜索
    std::string                source_graph;  ///< 若为子图节点，则为其来源图名
    std::vector<Annotation>    annotations;   ///< C# 风格标注
    SourceRange                source_range;  ///< Node type declaration source span when loaded from a declaration file.
    SourceRange                name_range;    ///< Node type name token source span when loaded from a declaration file.
    std::string                source_file;   ///< Source file containing this declaration when loaded from disk.

    /// 按名称查找 Pin 定义。
    /// @param name Pin 名称
    /// @return 若存在则返回指针，否则返回 nullptr
    const PinDefinition* find_pin(std::string_view name) const;

    /// 返回所有执行流输入 Pin。
    std::vector<const PinDefinition*> exec_inputs() const;

    /// 返回所有执行流输出 Pin。
    std::vector<const PinDefinition*> exec_outputs() const;

    /// 返回所有数据输入 Pin。
    std::vector<const PinDefinition*> data_inputs() const;

    /// 返回所有数据输出 Pin。
    std::vector<const PinDefinition*> data_outputs() const;
};

/// 节点实例，表示图中一个具体节点的配置。
struct NodeInstance {
    std::string type_name;      ///< 节点类型名称
    SourceRange type_name_range; ///< Source span of the node type reference.
    std::string instance_name;  ///< 实例唯一名称
    std::string initializer;    ///< 初始化表达式或参数
    std::vector<InitializerField> initializer_fields; ///< Field-level source spans parsed from the initializer when available.
    SourceRange initializer_constructor_range;      ///< Source span of a raw constructor-call initializer when available.
    SourceRange initializer_constructor_type_range; ///< Source span of the raw initializer constructor type.
    SourceRange initializer_constructor_arg_range;  ///< Source span of the raw initializer constructor argument expression.
    std::vector<Annotation> annotations;  ///< C# 风格标注，如 [Position(X=100, Y=200)]
    SourceRange source_range;        ///< Source span of this node instance declaration.
    SourceRange initializer_range;   ///< Source span of the initializer expression inside braces.
    SourceRange instance_name_range; ///< Source span of the node instance name.
};

/// 节点注册表，管理所有已注册的节点定义。
class NodeRegistry {
public:
    /// 注册普通节点定义。
    /// @param def 节点定义
    void register_node(NodeDefinition def);

    /// 注册或刷新子图节点定义（由子图生成的节点类型），不会覆盖同名原生节点。
    /// @param def 节点定义
    void register_graph_node(NodeDefinition def);

    /// 注销子图节点定义；不会删除同名原生节点。
    /// @param type_name 子图节点类型名称
    void unregister_graph_node(std::string_view type_name);

    /// 注销原生节点定义；不会删除同名子图节点。
    /// @param type_name 节点类型名称
    /// @return 如果删除了原生节点定义则返回 true，否则返回 false
    bool unregister_node(std::string_view type_name);

    /// 按类型名称查找节点定义。
    /// @param type_name 节点类型名称
    /// @return 若存在则返回指针，否则返回 nullptr
    const NodeDefinition* find(std::string_view type_name) const;

    /// 返回所有已注册节点定义的指针列表。
    std::vector<const NodeDefinition*> all() const;

private:
    std::unordered_map<std::string, NodeDefinition> nodes_;
};

} // namespace gs
