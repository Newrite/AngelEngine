#include "EngineFixture.hpp"

using namespace AngelEngineTest;

// ---------------------------------------------------------------------------
// Megamodule: Check Dependency Loading Order
// ---------------------------------------------------------------------------
TEST_CASE(Megamodule, Dependencies)
{
    EngineFixture fixture;

    // We create three mods: A, B, C
    // C depends on B. B depends on A.
    // Order of execution of global initialization should be A -> B -> C.
    // We will append to a global array in the game state (or just use stdout/logger to verify).

    // ModA
    fixture.CreateScriptFile("ModA", R"(
        int valueA = 10;
        void main() { print("ModA init"); }
    )",
                             true); // Public API to easily be visible by B

    // ModB
    fixture.CreateScriptFile("ModB", R"(
        int valueB = ModA::valueA + 20; // Tests access to ModA's scope
        void main() { print("ModB init"); }
    )",
                             true, {"ModA"});

    // ModC
    fixture.CreateScriptFile("ModC", R"(
        void main() { 
            print("ModC init. B = " + ModB::valueB); 
        }
    )",
                             false, {"ModB"});

    // Initial compile
    auto res = fixture.engine->CompileAllMods();
    ASSERT_TRUE(res.has_value(), "Dependencies failed to compile");

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "Dependencies failed to run");

    // We can't strictly guarantee print order across different modules' global initialization
    // because AngelScript initializes globals across the ENTIRE megamodule module in a single pass
    // but the builder *adds* sections in the specified order. Topological sort ensures B's
    // source code is parsed AFTER A.
    // So Mod B sees Mod A's `valueA`.

    // Wait, let's verify ModC's output
    EXPECT_TRUE(fixture.OutputContains("ModC init. B = 30"), "ModC did not correctly resolve ModB's dependency value");
}

// ---------------------------------------------------------------------------
// Megamodule: Check Faulty Mod Isolation
// ---------------------------------------------------------------------------
TEST_CASE(Megamodule, ErrorIsolation)
{
    EngineFixture fixture;

    // ModGood
    fixture.CreateScriptFile("ModGood", R"(
        void main() { print("ModGood is running"); }
    )");

    // ModBad (Syntax error)
    fixture.CreateScriptFile("ModBad", R"(
        void main() {
            this is a syntax error;
        }
    )");

    // Compile. It should catch the error in ModBad, exclude it, and successfully compile Megamodule with ModGood.
    auto res = fixture.engine->CompileAllMods();
    ASSERT_TRUE(res.has_value(), "Compile should succeed after retrying without ModBad");

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "Run should succeed for remaining mods");

    EXPECT_TRUE(fixture.OutputContains("ModGood is running"), "ModGood should have run");
    EXPECT_TRUE(!fixture.OutputContains("this is a syntax error"), "ModBad should have been completely isolated");

    // Check loaded modules
    auto loadedModules = fixture.engine->GetModuleLoader()->GetLoadedModules();
    bool goodFound = false;
    bool badFound = false;
    for (const auto& m : loadedModules)
    {
        if (m == "ModGood")
            goodFound = true;
        if (m == "ModBad")
            badFound = true;
    }

    EXPECT_TRUE(goodFound, "ModGood should be in loaded modules list");
    EXPECT_TRUE(!badFound, "ModBad should NOT be in loaded modules list");
}
