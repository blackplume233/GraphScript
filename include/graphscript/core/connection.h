#pragma once

#include <string>
#include <vector>

#include "graphscript/core/annotation.h"
#include "graphscript/core/pin.h"
#include "graphscript/core/source_range.h"

namespace gs {

/// 执行流连接，表示从一个 exec 输出 Pin 到另一个 exec 输入 Pin 的连线。
struct FlowConnection {
    PinAddress from;  ///< 源：exec 输出 Pin 地址
    PinAddress to;    ///< 目标：exec 输入 Pin 地址
    SourceRange source_range;             ///< Source span of this flow statement.
    SourceRange from_endpoint_range;      ///< Source span of the source endpoint expression.
    SourceRange to_endpoint_range;        ///< Source span of the target endpoint expression.
    SourceRange from_node_range;          ///< Source span of the source node reference.
    SourceRange from_pin_range;           ///< Source span of the source pin reference.
    SourceRange to_node_range;            ///< Source span of the target node reference.
    SourceRange to_pin_range;             ///< Source span of the target pin reference.
    std::vector<Annotation> annotations;  ///< C# 风格标注
};

/// 数据源，表示数据来自哪个节点实例的哪个 Pin（或参数）。
struct DataSource {
    std::string node_instance;  ///< 节点实例名称
    std::string pin_name;      ///< Pin 或参数名称
};

/// 数据连接，表示将数据源连接到目标数据输入 Pin。
struct DataLink {
    PinAddress  target;  ///< 目标：数据输入 Pin 地址
    DataSource  source;  ///< 源：数据输出 Pin 或参数
    SourceRange source_range;             ///< Source span of this link statement.
    SourceRange target_endpoint_range;    ///< Source span of the target endpoint expression.
    SourceRange source_endpoint_range;    ///< Source span of the source endpoint expression.
    SourceRange target_node_range;        ///< Source span of the target node reference.
    SourceRange target_pin_range;         ///< Source span of the target pin reference.
    SourceRange source_node_range;        ///< Source span of the source node or bare parameter reference.
    SourceRange source_pin_range;         ///< Source span of the source pin reference when present.
    std::vector<Annotation> annotations;  ///< C# 风格标注
};

} // namespace gs
