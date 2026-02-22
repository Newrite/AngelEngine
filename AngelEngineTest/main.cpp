#include "TestFramework.hpp"

// We no longer need the 600-line monolithic logic!
// Tests are auto-registered via TEST_CASE macros in their respective files.

int main()
{
    // The registry automatically gathers all TEST_CASE definitions across the executable.
    return AngelEngineTest::TestRegistry::Get().RunAll();
}
