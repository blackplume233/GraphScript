#include <gtest/gtest.h>
#include "graphscript/edit/handle.h"

using namespace gs;

TEST(Handle, DefaultInvalid) {
    Handle h;
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(h.index, 0u);
    EXPECT_EQ(h.generation, 0u);
}

TEST(Handle, Equality) {
    Handle a{1, 2};
    Handle b{1, 2};
    Handle c{1, 3};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(SlotMap, InsertAndGet) {
    SlotMap<int> map;
    Handle h = map.insert(42);
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(map.size(), 1u);

    auto* val = map.get(h);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
}

TEST(SlotMap, Remove) {
    SlotMap<int> map;
    Handle h = map.insert(10);
    EXPECT_TRUE(map.remove(h));
    EXPECT_EQ(map.size(), 0u);
    EXPECT_EQ(map.get(h), nullptr);
    EXPECT_FALSE(map.contains(h));
}

TEST(SlotMap, RemoveInvalidReturnsFalse) {
    SlotMap<int> map;
    Handle h{999, 1};
    EXPECT_FALSE(map.remove(h));
}

TEST(SlotMap, GenerationInvalidatesOldHandle) {
    SlotMap<int> map;
    Handle h1 = map.insert(100);
    map.remove(h1);
    Handle h2 = map.insert(200);

    EXPECT_EQ(map.get(h1), nullptr);
    EXPECT_FALSE(map.contains(h1));

    auto* val = map.get(h2);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 200);
}

TEST(SlotMap, SlotReuse) {
    SlotMap<int> map;
    Handle h1 = map.insert(1);
    map.remove(h1);
    Handle h2 = map.insert(2);

    EXPECT_EQ(h1.index, h2.index);
    EXPECT_NE(h1.generation, h2.generation);
}

TEST(SlotMap, MultipleInserts) {
    SlotMap<std::string> map;
    Handle h1 = map.insert("hello");
    Handle h2 = map.insert("world");
    Handle h3 = map.insert("test");

    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(*map.get(h1), "hello");
    EXPECT_EQ(*map.get(h2), "world");
    EXPECT_EQ(*map.get(h3), "test");
}

TEST(SlotMap, ForEachConst) {
    SlotMap<int> map;
    map.insert(10);
    map.insert(20);
    map.insert(30);

    int sum = 0;
    const auto& cmap = map;
    cmap.for_each([&sum](Handle, const int& val) {
        sum += val;
    });
    EXPECT_EQ(sum, 60);
}

TEST(SlotMap, ForEachMutable) {
    SlotMap<int> map;
    Handle h1 = map.insert(1);
    Handle h2 = map.insert(2);

    map.for_each([](Handle, int& val) {
        val *= 10;
    });

    EXPECT_EQ(*map.get(h1), 10);
    EXPECT_EQ(*map.get(h2), 20);
}

TEST(SlotMap, ContainsValid) {
    SlotMap<int> map;
    Handle h = map.insert(5);
    EXPECT_TRUE(map.contains(h));
}

TEST(SlotMap, Capacity) {
    SlotMap<int> map;
    EXPECT_EQ(map.capacity(), 0u);
    map.insert(1);
    EXPECT_GE(map.capacity(), 1u);
}
