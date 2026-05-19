// Placeholder GoogleTest. Exists to verify the test harness is wired
// correctly before any real distributed-systems logic shows up.

#include <gtest/gtest.h>

TEST(Smoke, BuildSystemWorks) {
  EXPECT_EQ(2 + 2, 4);
}
