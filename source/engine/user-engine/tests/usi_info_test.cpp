#include <gtest/gtest.h>

#include <string>

#include "../score.hpp"
#include "../usi_info.hpp"

using komori::Score;
using komori::UsiInfo;
using komori::UsiInfoKey;

namespace {
class UsiInfoTest : public ::testing::Test {
 public:
  void ExecTest(std::stringstream& ss, std::unordered_map<std::string, std::string> expected, int line) {
    std::string input;
    ss >> input;
    EXPECT_EQ(input, "info") << line;
    while (ss >> input) {
      std::string value;
      if (!(ss >> value)) {
        break;
      }

      ASSERT_NE(expected.find(input), expected.end()) << line;
      EXPECT_EQ(value, expected[input]) << line;
      expected.erase(input);
    }
    EXPECT_TRUE(expected.empty()) << line;
  }
};
}  // namespace

TEST_F(UsiInfoTest, Set) {
  UsiInfo info;
  info.Set(UsiInfoKey::kSelDepth, 3340);
  info.Set(UsiInfoKey::kTime, 264);
  info.Set(UsiInfoKey::kNodes, 2640);
  info.Set(UsiInfoKey::kNps, 33400);
  info.Set(UsiInfoKey::kHashfull, 445);
  info.Set(UsiInfoKey::kCurrMove, "resign");

  std::unordered_map<std::string, std::string> expected{
      {"depth", "0"},   {"seldepth", "3340"}, {"time", "264"},       {"nodes", "2640"},
      {"nps", "33400"}, {"hashfull", "445"},  {"currmove", "resign"}};

  std::stringstream ss;
  ss << info;
  std::cout << ss.str() << std::endl;
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, NoPV) {
  UsiInfo info;

  std::unordered_map<std::string, std::string> expected{{"string", "hoge"}};

  std::stringstream ss;
  ss << info << "hoge";
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, OnePV) {
  UsiInfo info;
  info.PushPVFront(33, "4", "hoge");

  std::unordered_map<std::string, std::string> expected{{"depth", "33"}, {"score", "4"}, {"pv", "hoge"}};

  std::stringstream ss;
  ss << info;
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, TwoPV) {
  UsiInfo info;
  info.PushPVFront(33, "4", "hoge");
  info.PushPVFront(334, "334", "fuga");

  std::stringstream ss;
  ss << info;

  const auto ret = ss.str();
  EXPECT_EQ(ret, "info score 334 depth 334 multipv 1 pv fuga\ninfo score 4 depth 33 multipv 2 pv hoge");
}
