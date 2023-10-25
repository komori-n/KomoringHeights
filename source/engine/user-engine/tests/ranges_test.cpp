#include <gtest/gtest.h>

#include <unordered_map>
#include <unordered_set>
#include <utility>

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

TEST(AsRange, unordered_multimap) {
  std::unordered_multimap<std::int32_t, std::int32_t> map{{10, 1}, {10, 0}, {10, 1}, {3, 2}};

  std::unordered_multiset<std::int32_t> ans{1, 0, 1};
  for (const auto& [key, value] : komori::AsRange(map.equal_range(10))) {
    ASSERT_NE(ans.find(value), ans.end());
    ans.erase(ans.find(value));
  }
}

TEST(Skip, MutableVector) {
  std::vector<int> vec{10, 1, 10, 0, 10, 1, 3, 2};

  for (auto&& x : komori::Skip<6>(vec)) {
    x = 10;
  }

  EXPECT_EQ(vec[5], 1);
  EXPECT_EQ(vec[6], 10);
  EXPECT_EQ(vec[7], 10);
}

TEST(Skip, SkipStepIsGreaterThanLength) {
  std::vector<int> vec{10, 1, 10, 0, 10, 1, 3, 2};

  for (auto&& x : komori::Skip<334>(vec)) {
    EXPECT_EQ(334, x);
  }

  EXPECT_EQ(vec[7], 2);
}

TEST(Skip, ConstRange) {
  const std::vector<int> vec{10, 1, 10, 0, 10, 1, 3, 2};
  const auto range = komori::Skip<7>(vec);

  for (const auto& x : range) {
    EXPECT_EQ(x, 2);
  }
}

TEST(Zip, ZipTest) {
  std::vector<int> a{3, 3, 4, 3, 3, 4};
  std::vector<std::string> b{"hoge", "fuga", "piyo"};

  std::vector<std::pair<int, std::string>> ans{{3, "hoge"}, {3, "fuga"}, {4, "piyo"}};
  std::size_t idx = 0;

  for (const auto& [x, y] : komori::Zip(a, std::move(b))) {
    EXPECT_EQ(x, ans[idx].first) << idx;
    EXPECT_EQ(y, ans[idx].second) << idx;
    idx++;
  }
  EXPECT_EQ(idx, 3);
}
