#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <cassert>

namespace gs {

/// Opaque handle to an element in a SlotMap. Invalid if generation is 0.
struct Handle {
    uint32_t index      = 0;
    uint32_t generation = 0;

    bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Handle& o) const { return !(*this == o); }
    /// Returns true if this handle refers to a live slot (generation != 0).
    bool valid() const { return generation != 0; }
};

/// Slot map: stable handles survive insert/remove; handles invalidate when the slot is reused.
template<typename T>
class SlotMap {
public:
    /// Inserts value and returns a handle. Reuses freed slots when possible.
    Handle insert(T value);
    /// Removes the element. Returns false if handle is invalid or stale.
    bool   remove(Handle h);
    /// Returns pointer to element, or nullptr if handle is invalid or stale.
    T*     get(Handle h);
    /// Returns pointer to element, or nullptr if handle is invalid or stale.
    const T* get(Handle h) const;
    /// Returns true if handle refers to a live element.
    bool   contains(Handle h) const;
    size_t size() const { return count_; }
    size_t capacity() const { return slots_.size(); }

    /// Invokes fn(Handle, const T&) for each occupied slot. Order is unspecified.
    template<typename Fn>
    void for_each(Fn&& fn) const;

    /// Invokes fn(Handle, T&) for each occupied slot. Order is unspecified.
    template<typename Fn>
    void for_each(Fn&& fn);

private:
    struct Slot {
        uint32_t        generation = 0;
        bool            occupied   = false;
        std::optional<T> data;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> free_list_;
    size_t count_ = 0;
};

// ─── Implementation ────────────────────────────────────────────────

template<typename T>
Handle SlotMap<T>::insert(T value) {
    uint32_t idx;
    if (!free_list_.empty()) {
        idx = free_list_.back();
        free_list_.pop_back();
    } else {
        idx = static_cast<uint32_t>(slots_.size());
        slots_.push_back({});
    }
    auto& slot = slots_[idx];
    slot.generation++;
    slot.occupied = true;
    slot.data = std::move(value);
    count_++;
    return {idx, slot.generation};
}

template<typename T>
bool SlotMap<T>::remove(Handle h) {
    if (h.index >= slots_.size()) return false;
    auto& slot = slots_[h.index];
    if (!slot.occupied || slot.generation != h.generation) return false;
    slot.occupied = false;
    slot.data.reset();
    free_list_.push_back(h.index);
    count_--;
    return true;
}

template<typename T>
T* SlotMap<T>::get(Handle h) {
    if (h.index >= slots_.size()) return nullptr;
    auto& slot = slots_[h.index];
    if (!slot.occupied || slot.generation != h.generation) return nullptr;
    return &(*slot.data);
}

template<typename T>
const T* SlotMap<T>::get(Handle h) const {
    if (h.index >= slots_.size()) return nullptr;
    auto& slot = slots_[h.index];
    if (!slot.occupied || slot.generation != h.generation) return nullptr;
    return &(*slot.data);
}

template<typename T>
bool SlotMap<T>::contains(Handle h) const {
    if (h.index >= slots_.size()) return false;
    auto& slot = slots_[h.index];
    return slot.occupied && slot.generation == h.generation;
}

template<typename T>
template<typename Fn>
void SlotMap<T>::for_each(Fn&& fn) const {
    for (uint32_t i = 0; i < slots_.size(); i++) {
        if (slots_[i].occupied) {
            fn(Handle{i, slots_[i].generation}, *slots_[i].data);
        }
    }
}

template<typename T>
template<typename Fn>
void SlotMap<T>::for_each(Fn&& fn) {
    for (uint32_t i = 0; i < slots_.size(); i++) {
        if (slots_[i].occupied) {
            fn(Handle{i, slots_[i].generation}, *slots_[i].data);
        }
    }
}

} // namespace gs
