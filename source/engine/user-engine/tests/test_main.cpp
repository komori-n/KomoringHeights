#include <gtest/gtest.h>

#include "../../misc.h"
#include "../../search.h"
#include "../../thread.h"
#include "../../tt.h"
#include "../../usi.h"

namespace {
class Environment : public ::testing::Environment {
 public:
  void SetUp() override {
    USI::init(Options);
    Bitboards::init();
    Position::init();
    Search::init();
    Threads.set(1);
  }

  void TearDown() override { Threads.set(0); }
};
}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  ::testing::AddGlobalTestEnvironment(new Environment);
  return RUN_ALL_TESTS();
}