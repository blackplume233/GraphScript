#include "graphscript/core/types.h"

namespace gs {

// Registers a type by name; returns existing handle if already registered.
TypeHandle TypeRegistry::register_type(TypeInfo info) {
    auto it = name_to_handle_.find(info.name);
    if (it != name_to_handle_.end()) return it->second;

    if (types_.empty()) {
        types_.push_back({"__invalid__", false});
    }
    TypeHandle h = static_cast<TypeHandle>(types_.size());
    name_to_handle_[info.name] = h;
    types_.push_back(std::move(info));
    return h;
}

// Unregisters a type name while preserving existing handle slots.
bool TypeRegistry::unregister_type(std::string_view name) {
    auto it = name_to_handle_.find(std::string(name));
    if (it == name_to_handle_.end()) return false;

    TypeHandle handle = it->second;
    name_to_handle_.erase(it);
    if (handle != InvalidType && handle < types_.size()) {
        types_[handle] = TypeInfo{};
    }
    return true;
}

// Looks up type by name; returns nullptr if not found.
const TypeInfo* TypeRegistry::find(std::string_view name) const {
    auto it = name_to_handle_.find(std::string(name));
    if (it == name_to_handle_.end()) return nullptr;
    return &types_[it->second];
}

// Looks up type by handle; returns nullptr if invalid or out of range.
const TypeInfo* TypeRegistry::find(TypeHandle handle) const {
    if (handle == InvalidType || handle >= types_.size()) return nullptr;
    if (types_[handle].name.empty()) return nullptr;
    return &types_[handle];
}

// Returns handle for type name, or InvalidType if not found.
TypeHandle TypeRegistry::handle_of(std::string_view name) const {
    auto it = name_to_handle_.find(std::string(name));
    if (it == name_to_handle_.end()) return InvalidType;
    return it->second;
}

// Returns all registered types (excludes slot 0 __invalid__ placeholder).
std::vector<const TypeInfo*> TypeRegistry::all() const {
    std::vector<const TypeInfo*> result;
    for (size_t i = 1; i < types_.size(); i++) {
        if (types_[i].name.empty()) continue;
        result.push_back(&types_[i]);
    }
    return result;
}

} // namespace gs
