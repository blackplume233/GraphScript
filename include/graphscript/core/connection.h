#pragma once

#include <string>

#include "graphscript/core/pin.h"

namespace gs {

/// 执行流连接，表示从一个 exec 输出 Pin 到另一个 exec 输入 Pin 的连线。
struct FlowConnection {
    PinAddress from;  ///< 源：exec 输出 Pin 地址
    PinAddress to;    ///< 目标：exec 输入 Pin 地址
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
};

} // namespace gs
