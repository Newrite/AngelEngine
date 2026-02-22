#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(SaveLoad, SaveDataAndHotReload)
{
    EngineFixture fixture;

    fixture.WriteAndCompile("TestMod", R"(
        [Save] int globalCounter = 0;
        [Save] MockActor@ savedActor;

        void main() {
            globalCounter = 42;
            @savedActor = GetActor(100);
            if (savedActor !is null) {
                savedActor.health = 80;
            }
        }

        void OnTick(float dt) {
            globalCounter++;
            if (savedActor !is null) {
                savedActor.health += 1;
            }
        }
    )");

    MockActor actor100(100,
                       50); // EngineFixture will cleanly destruct this internally since it resets MockActor::registry

    auto runRes = fixture.engine->RunMod("TestMod");
    ASSERT_TRUE(runRes.has_value(), "Run failed.");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule("TestMod");
    ASSERT_TRUE(mod != nullptr, "Module TestMod not found");

    int varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    int* counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    int actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    MockActor** actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    fixture.engine->Tick(
        1.0f / 60.0f); // Fire an engine tick (might not hit OnTick unless subscribed, but simulates execution phase)

    EXPECT_EQ(*counterPtr, 42, "Initial globalCounter should be 42");
    EXPECT_EQ(*actorPtr, &actor100, "Initial savedActor should point to actor100");
    EXPECT_EQ(actor100.health, 80, "Actor health should be 80");

    // Save Data
    auto saveLoadManager = fixture.engine->GetSaveLoadManager();
    auto saveRes = saveLoadManager->GetSaveData(fixture.engine->GetEngine(), fixture.engine->GetModuleLoader());
    ASSERT_TRUE(saveRes.has_value(), "Save failed");
    eastl::vector<uint8_t> saveBlob = saveRes.value();

    // Simulate Hot Reload
    auto reloadRes = fixture.engine->HotReload();
    ASSERT_TRUE(reloadRes.has_value(), "HotReload failed");

    mod = fixture.engine->GetEngine()->GetModule("TestMod");
    varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    EXPECT_EQ(*counterPtr, 42, "globalCounter should re-initialize to 42 post-reload");

    // Destruct memory deliberately to see if save structure rebuilds it.
    *counterPtr = 999;
    *actorPtr = nullptr;
    actor100.health = 10;

    // Load Data
    auto loadRes = saveLoadManager->LoadFromData(fixture.engine->GetEngine(), saveBlob);
    ASSERT_TRUE(loadRes.has_value(), "Load failed");

    EXPECT_EQ(*counterPtr, 42, "globalCounter should be restored to 42");
    EXPECT_EQ(*actorPtr, &actor100, "savedActor should be restored to actor100");
    EXPECT_EQ(actor100.health, 80, "Actor health should be restored to 80");
}
