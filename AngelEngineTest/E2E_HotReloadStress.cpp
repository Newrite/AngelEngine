#include <EASTL/chrono.h>
#include <atomic>
#include <thread>
#include "EngineFixture.hpp"


using namespace AngelEngineTest;

TEST_CASE(E2E, HotReloadConcurrencyStress)
{
    // Need JIT disabled for Hot Reload stress, JIT can be sensitive to aggressive file replacements mid-compilation
    // in this specific artificial 0.001ms thread looping scenario compared to real world.
    EngineFixture fixture(false, {"StressMod"});

    fixture.WriteAndCompile("StressMod", R"(
        [Save] int value = 0;
        void main() { value = 1; }
        void OnTick(float dt) { value += 1; }
    )");

    auto ms = fixture.engine->RunAllMods();
    ASSERT_TRUE(ms.has_value(), "Init run failed");

    std::atomic<bool> keepRunning = true;
    std::atomic<int> tickCount = 0;

    // Background Thread: Aggressively Ticking the Engine
    std::thread ticker(
        [&]()
        {
            while (keepRunning)
            {
                fixture.engine->Tick(0.016f);
                tickCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    // Foreground Thread: Aggressively overwriting the script file and triggering Recompilation
    for (int i = 0; i < 5; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Overwrite file
        std::string newCode = std::string(R"(
            [Save] int value = 0;
            void main() { value = )") +
            std::to_string((i + 1) * 100) + R"(; }
            void OnTick(float dt) { value += 10; }
        )";

        fixture.CreateScriptFile("StressMod", newCode);

        // Signal Watcher (Simulate OS File Event)
        fixture.engine->GetReloadManager()->ReloadScripts(
            fixture.engine->GetEngine(), fixture.engine->GetModuleLoader(), fixture.engine->GetExecutionManager(),
            fixture.engine->GetEventManager());
    }

    keepRunning = false;
    ticker.join();

    EXPECT_TRUE(tickCount > 10, "Ticker thread was permanently blocked/deadlocked by HotReloads.");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    int varIdx = mod->GetGlobalVarIndexByName("StressMod::value");
    int* valPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    // After 5 reloads, `main()` sets it to 500. Then it might have ticked a few times adding 10.
    EXPECT_TRUE(*valPtr >= 500, "Hot Reload failed to preserve/update the script execution state.");
}
