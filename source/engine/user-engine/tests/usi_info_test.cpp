#include <gtest/gtest.h>

#include <string>

#include "../usi_info.hpp"

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
  info.Set(UsiInfoKey::kDepth, 334);
  info.Set(UsiInfoKey::kSelDepth, 3340);
  info.Set(UsiInfoKey::kTime, 264);
  info.Set(UsiInfoKey::kNodes, 2640);
  info.Set(UsiInfoKey::kNps, 33400);
  info.Set(UsiInfoKey::kHashfull, 445);
  info.Set(UsiInfoKey::kCurrMove, "resign");
  info.Set(UsiInfoKey::kPv, "hoge");

  std::unordered_map<std::string, std::string> expected{
      {"depth", "334"}, {"seldepth", "3340"}, {"time", "264"},        {"nodes", "2640"},
      {"nps", "33400"}, {"hashfull", "445"},  {"currmove", "resign"}, {"pv", "hoge"},
  };

  std::stringstream ss;
  ss << info;
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, Default) {
  UsiInfo info;

  std::unordered_map<std::string, std::string> expected{{"string", "hoge"}};

  std::stringstream ss;
  ss << info << "hoge";
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, String) {
  UsiInfo info;
  info.Set(UsiInfoKey::kString, "hoge");

  std::unordered_map<std::string, std::string> expected{{"string", "hoge"}};

  std::stringstream ss;
  ss << info;
  ExecTest(ss, std::move(expected), __LINE__);
}

TEST_F(UsiInfoTest, SelDepth) {
  UsiInfo info;
  info.Set(UsiInfoKey::kSelDepth, "334");

  std::unordered_map<std::string, std::string> expected{{"seldepth", "334"}, {"depth", "0"}, {"string", "264"}};

  std::stringstream ss;
  ss << info << 264;
  ExecTest(ss, std::move(expected), __LINE__);
}
