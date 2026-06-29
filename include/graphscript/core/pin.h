#pragma once

#include <string>
#include <vector>

#include "graphscript/core/annotation.h"
#include "graphscript/parse/token.h"

namespace gs {

/// Pin 种类：执行流或数据。
enum class PinKind : uint8_t {
    Exec,   ///< 执行流 Pin，用于控制执行顺序
    Data    ///< 数据 Pin，用于传递数据
};

/// Pin 方向：输入或输出。
enum class PinDirection : uint8_t {
    Input,  ///< 输入 Pin
    Output  ///< 输出 Pin
};

/// Pin 定义，描述节点上一个 Pin 的静态属性。
struct PinDefinition {
    std::string  name;       ///< Pin 名称
    PinKind      kind      = PinKind::Data;       ///< Pin 种类（Exec/Data）
    PinDirection direction = PinDirection::Input; ///< Pin 方向（Input/Output）
    std::string  type_name; ///< 数据类型名称，仅对 Data 类 Pin 有意义
    std::vector<Annotation> annotations; ///< C# 风格标注
    SourceRange  source_range; ///< Pin declaration source span when loaded from a declaration file.
    SourceRange  type_name_range; ///< Data type reference source span when loaded from a declaration file.
    SourceRange  name_range; ///< Pin name token source span when loaded from a declaration file.
    std::string  source_file; ///< Source file containing this declaration when loaded from disk.
};

/// Pin 地址，用于在图中唯一标识一个节点实例上的 Pin。
struct PinAddress {
    std::string node_instance;  ///< 节点实例名称
    std::string pin_name;      ///< Pin 名称
};

} // namespace gs
