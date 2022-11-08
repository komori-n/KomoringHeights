#include <gtest/gtest.h>

#include "../ranges.hpp"

TEST(WithIndexTest, LvalueReferenceRange) {
  std::vector<int> vec{3, 3, 4};

  std::size_t expected = 0;
  for (auto&& [i, x] : komori::WithIndex(vec)) {
    EXPECT_EQ(i, expected) << i;
    EXPECT_EQ(x, vec[i]) << i;
    x = 334;
    expected++;
  }
  EXPECT_EQ(vec[0], 334);
  EXPECT_EQ(vec[1], 334);
  EXPECT_EQ(vec[2], 334);
}

TEST(WithIndexTest, ConstLvalueReferenceRange) {
  const std::vector<int> vec{3, 3, 4};
  const auto with_index = komori::WithIndex(vec);

  std::size_t expected = 0;
  for (const auto& [i, x] : with_index) {
    EXPECT_EQ(i, expected) << i;
    EXPECT_EQ(x, vec[i]) << i;
    expected++;
  }
}

TEST(WithIndexTest, RvalueRange) {
  const std::vector<int> vec{3, 3, 4};

  std::size_t expected = 0;
  for (const auto& [i, x] : komori::WithIndex(std::vector<int>{3, 3, 4})) {
    EXPECT_EQ(i, expected) << i;
    EXPECT_EQ(x, vec[i]) << i;
    expected++;
  }
}

TEST(WithIndexTest, RvalueRange_AsConst) {
  const std::vector<int> vec{3, 3, 4};
  const auto with_index = komori::WithIndex(std::vector<int>{3, 3, 4});

  std::size_t expected = 0;
  for (const auto& [i, x] : with_index) {
    EXPECT_EQ(i, expected) << i;
    EXPECT_EQ(x, vec[i]) << i;
    expected++;
  }
}

TEST(WithIndexTest, Array) {
  int vec[]{3, 3, 4};

  std::size_t expected = 0;
  for (auto&& [i, x] : komori::WithIndex(vec)) {
    EXPECT_EQ(i, expected) << i;
    EXPECT_EQ(x, vec[i]) << i;
    expected++;
  }
}

namespace {
struct FreeFunctionRange {
  struct Iterator {
    int i;

    Iterator& operator++() noexcept {
      ++i;
      return *this;
    }

    int operator*() noexcept { return i; }

    friend bool operator!=(Iterator itr, std::nullptr_t) noexcept { return itr.i != 3; }
  };

  // begin を空定義（end が定義されていないなら free 関数版が呼ばれるはず）
  Iterator begin(const FreeFunctionRange&) noexcept;
  friend Iterator begin(const FreeFunctionRange&) noexcept { return Iterator{3}; }
  friend std::nullptr_t end(const FreeFunctionRange&) noexcept { return nullptr; }
};
}  // namespace

TEST(WithIndexTest, FreeFunctionRange) {
  FreeFunctionRange obj{};
  std::size_t expected_i = 0;
  for (auto&& [i, x] : komori::WithIndex(obj)) {
    EXPECT_EQ(i, expected_i) << expected_i;
    EXPECT_EQ(x, expected_i + 3) << expected_i;
    expected_i++;
  }
}
