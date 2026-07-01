#pragma once

#include <string>
#include <vector>

#include "graphscript/core/source_range.h"

namespace gs {

/// 标注参数（C# Attribute 风格），支持位置参数和命名参数。
/// 位置参数：name 为空，value 为字面值。
/// 命名参数：name = "X"，value = "100" → [Position(X = 100)]。
struct AnnotationArg {
    std::string name;   ///< 命名参数名称；位置参数时为空
    std::string value;  ///< 参数值的字面文本
    SourceRange source_range;  ///< Source span of this annotation argument.
    SourceRange name_range;    ///< Source span of the named argument key, if present.
    SourceRange value_range;   ///< Source span of the annotation argument value.
    SourceRange value_constructor_range;      ///< Source span of a constructor-style value, if present.
    SourceRange value_constructor_type_range; ///< Source span of the constructor type token, if present.
    SourceRange value_constructor_arg_range;  ///< Source span of the constructor argument expression, if present.
};

/// 标注（C# Attribute 风格），如 [Position(X = 100, Y = 200)]。
/// 附加在 Graph / NodeInstance / GraphParameter 等元素上，
/// 用于携带位置、注释、UI 提示等元数据。
struct Annotation {
    std::string name;                 ///< 标注名称，如 "Position"、"Comment"
    std::vector<AnnotationArg> args;  ///< 参数列表（位置参数 + 命名参数）
    SourceRange source_range;         ///< Source span of this annotation.
    SourceRange name_range;           ///< Source span of the annotation name token.
};

} // namespace gs
