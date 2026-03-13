#include <gtest/gtest.h>
#include "graphscript/core/result.h"

using namespace gs;

TEST(Result, OkValue) {
    auto r = Result<int>::ok(42);
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_EQ(r.value(), 42);
}

TEST(Result, ErrValue) {
    auto r = Result<int>::err("something went wrong");
    EXPECT_FALSE(r.is_ok());
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "something went wrong");
}

TEST(Result, ValueOnErrThrows) {
    auto r = Result<int>::err("bad");
    EXPECT_THROW(r.value(), std::logic_error);
}

TEST(Result, ErrorOnOkThrows) {
    auto r = Result<int>::ok(1);
    EXPECT_THROW(r.error(), std::logic_error);
}

TEST(Result, MoveValue) {
    auto r = Result<std::string>::ok("hello");
    std::string val = std::move(r).value();
    EXPECT_EQ(val, "hello");
}

TEST(Result, CustomErrorType) {
    struct MyError { int code; std::string msg; };
    auto r = Result<int, MyError>::err({404, "not found"});
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.error().code, 404);
    EXPECT_EQ(r.error().msg, "not found");
}
