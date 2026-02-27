#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(SaveLoad, SaveDataAndHotReload)
{
    EngineFixture fixture(false, {"TestMod"});

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

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "Run failed.");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    ASSERT_TRUE(mod != nullptr, "Module __Megamodule__ not found");

    int varIdx = mod->GetGlobalVarIndexByName("TestMod::globalCounter");
    int* counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    int actorIdx = mod->GetGlobalVarIndexByName("TestMod::savedActor");
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

    mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    varIdx = mod->GetGlobalVarIndexByName("TestMod::globalCounter");
    counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    actorIdx = mod->GetGlobalVarIndexByName("TestMod::savedActor");
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

TEST_CASE(SaveLoad, VersionMismatch)
{
    EngineFixture fixture(false, {"TestMod"});
    fixture.WriteAndCompile("TestMod", "[Save] int globalCounter = 0;");
    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "Run failed.");

    auto saveLoadManager = fixture.engine->GetSaveLoadManager();
    auto saveRes = saveLoadManager->GetSaveData(fixture.engine->GetEngine(), fixture.engine->GetModuleLoader());
    ASSERT_TRUE(saveRes.has_value(), "Save failed");
    eastl::vector<uint8_t> saveBlob = saveRes.value();

    // Corrupt the magic header
    saveBlob[0] = 0x00;

    auto loadRes = saveLoadManager->LoadFromData(fixture.engine->GetEngine(), saveBlob);
    ASSERT_TRUE(!loadRes.has_value(), "Load should fail due to bad magic number");
    EXPECT_EQ((int)loadRes.error(), (int)AngelEngine::SerializationError::VersionMismatch,
              "Should return VersionMismatch");
}

TEST_CASE(SaveLoad, SkipMissingVariable)
{
    EngineFixture fixture(false, {"TestMod"});
    fixture.WriteAndCompile("TestMod", R"(
        [Save] int oldVar = 100;
        [Save] int newVar = 200;
    )");
    ASSERT_TRUE(fixture.engine->RunAllMods().has_value(), "Run failed.");

    auto saveLoadManager = fixture.engine->GetSaveLoadManager();
    auto saveRes = saveLoadManager->GetSaveData(fixture.engine->GetEngine(), fixture.engine->GetModuleLoader());
    eastl::vector<uint8_t> saveBlob = saveRes.value();

    // Recompile WITHOUT oldVar
    fixture.WriteAndCompile("TestMod", "[Save] int newVar = 0;");
    ASSERT_TRUE(fixture.engine->HotReload().has_value(), "HotReload failed.");

    // Load old data (contains oldVar)
    auto loadRes = saveLoadManager->LoadFromData(fixture.engine->GetEngine(), saveBlob);
    ASSERT_TRUE(loadRes.has_value(), "Load should succeed and skip oldVar");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    int varIdx = mod->GetGlobalVarIndexByName("TestMod::newVar");
    int* newVarPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);

    EXPECT_EQ(*newVarPtr, 200, "newVar should be correctly loaded after skipping oldVar");
}

TEST_CASE(SaveLoad, TypeMismatch)
{
    EngineFixture fixture(false, {"TestMod"});
    fixture.WriteAndCompile("TestMod", "[Save] int changeMyType = 42;");
    ASSERT_TRUE(fixture.engine->RunAllMods().has_value(), "Run failed.");

    auto saveLoadManager = fixture.engine->GetSaveLoadManager();
    auto saveRes = saveLoadManager->GetSaveData(fixture.engine->GetEngine(), fixture.engine->GetModuleLoader());
    eastl::vector<uint8_t> saveBlob = saveRes.value();

    // Recompile WITH DIFFERENT TYPE (float instead of int)
    fixture.WriteAndCompile("TestMod", "[Save] float changeMyType = 3.14f;");
    ASSERT_TRUE(fixture.engine->HotReload().has_value(), "HotReload failed.");

    auto loadRes = saveLoadManager->LoadFromData(fixture.engine->GetEngine(), saveBlob);
    ASSERT_TRUE(!loadRes.has_value(), "Load should fail due to type mismatch");
    EXPECT_EQ((int)loadRes.error(), (int)AngelEngine::SerializationError::TypeMismatch, "Should return TypeMismatch");
}
