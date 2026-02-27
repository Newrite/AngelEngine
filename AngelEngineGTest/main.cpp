#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    
    // Enable colored output
    ::testing::GTEST_FLAG(color) = "auto";
    
    // Set output directory for XML results
    ::testing::GTEST_FLAG(output) = "xml:test-results.xml";
    
    return RUN_ALL_TESTS();
}
