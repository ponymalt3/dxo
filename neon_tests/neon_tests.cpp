#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

TEST(Neon, Test_NeonImplementationCorrect)
{
  std::filesystem::current_path(std::filesystem::current_path().parent_path() / "rpi_digital_crossover" /
                                "neon_tests");
  EXPECT_EQ(std::system("bash ./build_and_run.sh"), 0);
}