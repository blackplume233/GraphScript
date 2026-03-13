#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>

#include "graphscript/core/pin.h"

namespace gs {

/// 节点定义，描述一类节点的类型、Pin 配置及元数据。
struct NodeDefinition {
    std::string                type_name;     ///< 节点类型名称
    std::vector<PinDefinition> pins;          ///< 所有 Pin 定义
    bool                       is_native = true;  ///< 是否为原生（C++）实现
    std::vector<std::string>   tags;          ///< 标签，用于分类与搜索
    std::string                source_graph;  ///< 若为子图节点，则为其来源图名

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
    std::string instance_name;  ///< 实例唯一名称
    std::string initializer;    ///< 初始化表达式或参数
};

/// 节点注册表，管理所有已注册的节点定义。
class NodeRegistry {
public:
    /// 注册普通节点定义。
    /// @param def 节点定义
    void register_node(NodeDefinition def);

    /// 注册子图节点定义（由子图生成的节点类型）。
    /// @param def 节点定义
    void register_graph_node(NodeDefinition def);

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
