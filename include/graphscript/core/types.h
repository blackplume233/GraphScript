#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include "graphscript/core/annotation.h"
#include "graphscript/core/source_range.h"

namespace gs {

/// 类型元信息，描述注册类型的名称与可构造性。
struct TypeInfo {
    std::string name;           ///< 类型名称
    bool        constructible = false;  ///< 是否可在运行时构造
    std::vector<Annotation> annotations; ///< C# 风格标注
    SourceRange source_range;   ///< Type declaration source span when loaded from a declaration file.
    SourceRange name_range;     ///< Type name token source span when loaded from a declaration file.
    std::string source_file;     ///< Source file containing this declaration when loaded from disk.
};

/// 类型句柄，用于在 TypeRegistry 中唯一标识类型。
using TypeHandle = uint32_t;

/// 无效类型句柄，表示未找到或无效类型。
constexpr TypeHandle InvalidType = 0;

/// 类型注册表，管理所有已注册类型的元信息与句柄映射。
class TypeRegistry {
public:
    /// 注册类型并返回其句柄。
    /// @param info 类型元信息
    /// @return 新分配的类型句柄
    TypeHandle register_type(TypeInfo info);

    /// 注销类型名称；保留旧句柄槽位以避免移动其他句柄。
    /// @param name 类型名称
    /// @return 如果删除了类型名称映射则返回 true，否则返回 false
    bool unregister_type(std::string_view name);

    /// 按名称查找类型。
    /// @param name 类型名称
    /// @return 若存在则返回指向 TypeInfo 的指针，否则返回 nullptr
    const TypeInfo* find(std::string_view name) const;

    /// 按句柄查找类型。
    /// @param handle 类型句柄
    /// @return 若句柄有效则返回指向 TypeInfo 的指针，否则返回 nullptr
    const TypeInfo* find(TypeHandle handle) const;

    /// 获取指定类型名称对应的句柄。
    /// @param name 类型名称
    /// @return 若存在则返回句柄，否则返回 InvalidType
    TypeHandle handle_of(std::string_view name) const;

    /// 返回所有已注册类型的指针列表。
    std::vector<const TypeInfo*> all() const;

private:
    std::vector<TypeInfo> types_;         // index 0 reserved for invalid
    std::unordered_map<std::string, TypeHandle> name_to_handle_;
};

} // namespace gs
