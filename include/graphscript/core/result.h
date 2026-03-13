#pragma once

#include <variant>
#include <string>
#include <stdexcept>

namespace gs {

/// 表示成功或失败的 Result 类型，类似 Rust 的 Result<T, E>。
/// @tparam T 成功时持有的值类型
/// @tparam E 失败时持有的错误类型，默认为 std::string
template<typename T, typename E = std::string>
class Result {
public:
    /// 构造表示成功的 Result。
    /// @param value 成功值
    /// @return 包含 value 的 Result
    static Result ok(T value) { return Result(std::move(value)); }

    /// 构造表示失败的 Result。
    /// @param error 错误信息
    /// @return 包含 error 的 Result
    static Result err(E error) { return Result(InError{std::move(error)}); }

    /// 判断是否为成功状态。
    bool is_ok() const { return std::holds_alternative<T>(storage_); }

    /// 判断是否为失败状态。
    bool is_err() const { return !is_ok(); }

    /// 获取成功值（左值引用）。若当前为错误状态则抛出 std::logic_error。
    const T& value() const& {
        if (is_err()) throw std::logic_error("Result::value() called on error");
        return std::get<T>(storage_);
    }

    /// 获取成功值（右值引用，移动语义）。若当前为错误状态则抛出 std::logic_error。
    T value() && {
        if (is_err()) throw std::logic_error("Result::value() called on error");
        return std::get<T>(std::move(storage_));
    }

    /// 获取错误信息。若当前为成功状态则抛出 std::logic_error。
    const E& error() const& {
        if (is_ok()) throw std::logic_error("Result::error() called on ok");
        return std::get<InError>(storage_).err;
    }

private:
    struct InError { E err; };

    explicit Result(T val) : storage_(std::move(val)) {}
    explicit Result(InError e) : storage_(std::move(e)) {}

    std::variant<T, InError> storage_;
};

/// Result<void, E> 偏特化：表示无返回值的操作（仅成功或失败）。
template<typename E>
class Result<void, E> {
public:
    static Result ok(void) { return Result(false); }
    static Result err(E error) { return Result(true, std::move(error)); }

    bool is_ok() const { return !is_err_; }
    bool is_err() const { return is_err_; }

    void value() const {
        if (is_err_) throw std::logic_error("Result<void>::value() called on error");
    }

    const E& error() const& {
        if (!is_err_) throw std::logic_error("Result<void>::error() called on ok");
        return error_;
    }

private:
    explicit Result(bool is_err) : is_err_(is_err) {}
    Result(bool is_err, E error) : is_err_(is_err), error_(std::move(error)) {}

    bool is_err_;
    E    error_;
};

} // namespace gs
