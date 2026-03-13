#pragma once

#include <string>
#include <vector>
#include <optional>

#include "graphscript/core/node.h"
#include "graphscript/core/connection.h"

namespace gs {

/// 图参数方向：输入、输出或可变（In/Out）。
enum class ParamDirection : uint8_t {
    In,   ///< 输入参数
    Out,  ///< 输出参数
    Var   ///< 可变参数（In/Out）
};

/// 图参数定义，描述图的一个输入/输出/可变参数。
struct GraphParameter {
    std::string    name;          ///< 参数名称
    std::string    type_name;     ///< 参数类型名称
    ParamDirection direction = ParamDirection::In;  ///< 参数方向
    std::string    default_value; ///< 默认值（可选）
};

/// 逻辑块基类，包含执行流连接与数据连接。
struct LogicBlock {
    std::string                 name;             ///< 逻辑块名称
    std::vector<FlowConnection> flow_connections;  ///< 执行流连接
    std::vector<DataLink>       data_links;       ///< 数据连接
};

/// 事件逻辑块，表示响应特定事件的逻辑（如 OnBeginPlay）。
struct Event : LogicBlock {};

/// 函数逻辑块，表示可被调用的函数逻辑。
struct Function : LogicBlock {};

/// 生成注释，用于代码生成时在指定节点处插入注释。
struct GenerateComment {
    std::string instance_name;  ///< 目标节点实例名称
    std::string text;          ///< 注释文本
};

/// 生成元数据，用于代码生成时的属性或元信息。
struct GenerateMetadata {
    std::string scope;    ///< 作用域
    std::string node;    ///< 节点
    std::string property;///< 属性名
    std::string value;   ///< 属性值
};

/// 生成块，包含代码生成阶段的注释与元数据。
struct GenerateBlock {
    std::vector<GenerateComment>  comments;  ///< 注释列表
    std::vector<GenerateMetadata> metadata;  ///< 元数据列表
};

/// 图定义，表示一个完整的图（子图或主图），包含参数、节点实例、事件、函数及生成配置。
struct Graph {
    std::string                    name;          ///< 图名称
    std::optional<std::string>     base_type;      ///< 基类类型（若为继承图）
    std::vector<GraphParameter>    parameters;     ///< 图参数
    std::vector<NodeInstance>      node_instances; ///< 节点实例列表
    std::vector<Event>             events;         ///< 事件逻辑块
    std::vector<Function>          functions;      ///< 函数逻辑块
    std::optional<GenerateBlock>   generate;      ///< 代码生成配置（可选）
};

} // namespace gs
