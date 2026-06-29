#include <gtest/gtest.h>
#include "graphscript/core/types.h"

using namespace gs;

TEST(TypeRegistry, RegisterAndFind) {
    TypeRegistry reg;
    TypeHandle h = reg.register_type({"FVector", true});
    EXPECT_NE(h, InvalidType);

    auto* info = reg.find("FVector");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "FVector");
    EXPECT_TRUE(info->constructible);
}

TEST(TypeRegistry, FindByHandle) {
    TypeRegistry reg;
    TypeHandle h = reg.register_type({"FName", false});

    auto* info = reg.find(h);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "FName");
    EXPECT_FALSE(info->constructible);
}

TEST(TypeRegistry, HandleOf) {
    TypeRegistry reg;
    TypeHandle h = reg.register_type({"int", false});
    EXPECT_EQ(reg.handle_of("int"), h);
    EXPECT_EQ(reg.handle_of("nonexistent"), InvalidType);
}

TEST(TypeRegistry, DuplicateReturnsSameHandle) {
    TypeRegistry reg;
    TypeHandle h1 = reg.register_type({"float", true});
    TypeHandle h2 = reg.register_type({"float", true});
    EXPECT_EQ(h1, h2);
}

TEST(TypeRegistry, FindNonexistentReturnsNull) {
    TypeRegistry reg;
    EXPECT_EQ(reg.find("Nope"), nullptr);
    EXPECT_EQ(reg.find(InvalidType), nullptr);
    EXPECT_EQ(reg.find(999), nullptr);
}

TEST(TypeRegistry, UnregisterTypeRemovesNameAndPreservesOtherHandles) {
    TypeRegistry reg;
    TypeHandle old_handle = reg.register_type({"OldType", false});
    TypeHandle other_handle = reg.register_type({"OtherType", true});

    EXPECT_TRUE(reg.unregister_type("OldType"));
    EXPECT_EQ(reg.find("OldType"), nullptr);
    EXPECT_EQ(reg.handle_of("OldType"), InvalidType);
    EXPECT_EQ(reg.find(old_handle), nullptr);
    ASSERT_NE(reg.find(other_handle), nullptr);
    EXPECT_EQ(reg.find(other_handle)->name, "OtherType");
    EXPECT_FALSE(reg.unregister_type("OldType"));

    auto all = reg.all();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0]->name, "OtherType");
}

TEST(TypeRegistry, AllReturnsRegisteredTypes) {
    TypeRegistry reg;
    reg.register_type({"A", false});
    reg.register_type({"B", true});
    reg.register_type({"C", false});

    auto all = reg.all();
    EXPECT_EQ(all.size(), 3u);
}

TEST(TypeRegistry, InvalidHandleIsZero) {
    EXPECT_EQ(InvalidType, 0u);
}
