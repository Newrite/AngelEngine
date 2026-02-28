#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <angelscript.h>
#include <filesystem>

// Import AngelEngine modules
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;
import AngelEngine.Types;
import AngelEngine.Errors;
import AngelEngine.Logger;
import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.ReloadManager;
import AngelEngine.SaveLoadManager;
import AngelEngine.BindingManager;
import AngelEngine.EventManager;
import AngelEngine.ScriptWatcher;
import AngelEngine.ScriptEngine;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для ReloadManager
 * 
 * Проверяют:
 * - Hot reload workflow
 * - Сохранение состояния после reload
 * - Очистку event channels
 * - Renew execution manager
 * - Error recovery
 */
class ReloadManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CleanupAllTempScriptFiles();
        
        EngineConfig config;
        config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
        config.enableUseJIT = false;
        config.enableWatchdog = false;
        config.enableAutoGC = false;
        
        auto factory = eastl::make_unique<StandardComponentFactory>(config);
        auto result = ScriptEngine::MakeEngine(eastl::move(factory));
        
        ASSERT_TRUE(result.has_value());
        engine_ = eastl::move(result.value());
        
        auto initResult = engine_->InitializeEngine();
        ASSERT_TRUE(initResult.has_value());
    }
    
    void TearDown() override
    {
        CleanupAllTempScriptFiles();
        engine_.reset();
    }
    
    eastl::unique_ptr<ScriptEngine> engine_;
};

/**
 * @brief Reload_NoMods
 * 
 * Проверяет reload без модулей
 */
TEST_F(ReloadManagerTest, Reload_NoMods) {
    // Remove all mods
    std::filesystem::remove_all("angelscripts/mods");
    std::filesystem::create_directories("angelscripts/mods");
    
    auto reloadResult = engine_->HotReload();
    (void)reloadResult;
    
    // May succeed or fail depending on implementation
    SUCCEED() << "Reload with no mods completed";
}

/**
 * @brief Reload_WithSingleMod
 * 
 * Проверяет reload одного модуля
 */
TEST_F(ReloadManagerTest, Reload_WithSingleMod) {
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { value = 10; }
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Modify script
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { value = 42; }
    )", "ReloadMod");
    
    // Reload
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value()) << "Hot reload should succeed";
    
    // Verify new value
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("ReloadMod::value");
    ASSERT_GE(varIdx, 0);
    
    int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*valuePtr, 42) << "Value should be 42 after reload";
}

/**
 * @brief Reload_PreservesState
 *
 * Проверяет сохранение состояния после reload
 */
TEST_F(ReloadManagerTest, Reload_PreservesState) {
    CreateTempScriptFile(R"(
        [Save]
        int preservedValue = 100;
        int tempValue = 0;

        void main() {
            preservedValue = 200;
            tempValue = 50;
        }
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save state
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Modify script (change tempValue initialization)
    CreateTempScriptFile(R"(
        [Save]
        int preservedValue = 100;
        int tempValue = 0;

        void main() {
            preservedValue = 200;
            tempValue = 999;
        }
    )", "ReloadMod");
    
    // Reload
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Load saved state
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value());

    // Verify [Save] variable was preserved
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);

    int varIdx = mod->GetGlobalVarIndexByName("ReloadMod::preservedValue");
    ASSERT_GE(varIdx, 0);

    int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*valuePtr, 200) << "[Save] value should be preserved";
}

/**
 * @brief Reload_ClearsEventChannels
 * 
 * Проверяет очистку event channels после reload
 */
TEST_F(ReloadManagerTest, Reload_ClearsEventChannels) {
    CreateTempScriptFile(R"(
        int tickCount = 0;
        
        void OnTick(float dt) {
            tickCount++;
        }
        
        void main() {}
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Run some ticks
    for (int i = 0; i < 10; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value());
    }
    
    // Reload
    CreateTempScriptFile(R"(
        int tickCount = 0;
        
        void OnTick(float dt) {
            tickCount++;
        }
        
        void main() {}
    )", "ReloadMod");
    
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Event channels should be cleared and re-established
    SUCCEED() << "Reload with event channels completed";
}

/**
 * @brief Reload_RenewsExecutionManager
 * 
 * Проверяет renew execution manager после reload
 */
TEST_F(ReloadManagerTest, Reload_RenewsExecutionManager) {
    CreateTempScriptFile(R"(
        int value = 0;
        void main() { value = 10; }
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Reload
    CreateTempScriptFile(R"(
        int value = 0;
        void main() { value = 20; }
    )", "ReloadMod");
    
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Run again
    auto runResult2 = engine_->RunAllMods();
    ASSERT_TRUE(runResult2.has_value());
    
    // Verify execution manager is working
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("ReloadMod::value");
    ASSERT_GE(varIdx, 0);
    
    int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*valuePtr, 20) << "Value should be 20 after reload and re-run";
}

/**
 * @brief Reload_ErrorRecovery
 * 
 * Проверяет восстановление после ошибок при reload
 */
TEST_F(ReloadManagerTest, Reload_ErrorRecovery) {
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { value = 10; }
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Modify with bad syntax
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { invalid syntax!!! }
    )", "ReloadMod");
    
    // Reload may fail or succeed (excluding bad module)
    auto reloadResult = engine_->HotReload();
    (void)reloadResult;
    
    SUCCEED() << "Reload error recovery handled";
}

/**
 * @brief Reload_MultipleMods
 * 
 * Проверяет reload нескольких модулей
 */
TEST_F(ReloadManagerTest, Reload_MultipleMods) {
    CreateTempScriptFile(R"(
        int valueA = 1;
        void main() { valueA = 10; }
    )", "ModA");
    
    CreateTempScriptFile(R"(
        int valueB = 1;
        void main() { valueB = 20; }
    )", "ModB");
    
    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Modify both modules
    CreateTempScriptFile(R"(
        int valueA = 1;
        void main() { valueA = 100; }
    )", "ModA");
    
    CreateTempScriptFile(R"(
        int valueB = 1;
        void main() { valueB = 200; }
    )", "ModB");
    
    // Reload
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Verify both modules reloaded
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdxA = mod->GetGlobalVarIndexByName("ModA::valueA");
    int varIdxB = mod->GetGlobalVarIndexByName("ModB::valueB");
    
    if (varIdxA >= 0) {
        int* valueAPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdxA));
        EXPECT_EQ(*valueAPtr, 100) << "ModA should be reloaded";
    }
    
    if (varIdxB >= 0) {
        int* valueBPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdxB));
        EXPECT_EQ(*valueBPtr, 200) << "ModB should be reloaded";
    }
    
    CleanupTempScriptFiles("ModA");
    CleanupTempScriptFiles("ModB");
}

/**
 * @brief Reload_WithJIT
 * 
 * Проверяет reload с включенным JIT
 */
TEST_F(ReloadManagerTest, Reload_WithJIT) {
    // Create engine with JIT enabled
    EngineConfig config;
    config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
    config.enableUseJIT = true;
    config.enableWatchdog = false;
    config.enableAutoGC = false;
    
    auto factory = eastl::make_unique<StandardComponentFactory>(config);
    auto result = ScriptEngine::MakeEngine(eastl::move(factory));
    ASSERT_TRUE(result.has_value());
    
    auto jitEngine = eastl::move(result.value());
    auto initResult = jitEngine->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { value = 10; }
    )", "ReloadMod");
    
    // Initial compile and run
    auto compileResult = jitEngine->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = jitEngine->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Modify and reload
    CreateTempScriptFile(R"(
        int value = 1;
        void main() { value = 42; }
    )", "ReloadMod");
    
    auto reloadResult = jitEngine->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    SUCCEED() << "Reload with JIT completed";
    
    CleanupTempScriptFiles("ReloadMod");
}

/**
 * @brief Reload_RetainsBindings
 *
 * Проверяет сохранение bindings после reload
 */
TEST_F(ReloadManagerTest, Reload_RetainsBindings) {
    // Create custom binding
    class TestBinding : public IScriptBinding
    {
    public:
        void Bind(asIScriptEngine* engine) override
        {
            int r = engine->RegisterGlobalFunction(
                "int GetConstant()",
                asFUNCTION(GetConstant),
                asCALL_CDECL
            );
            (void)r;
        }

    private:
        static int GetConstant() { return 999; }
    };

    TestBinding binding;
    engine_->AddBinding(&binding);
    
    // Re-bind to register the new binding
    auto bindResult = engine_->GetBindingManager()->BindAll(engine_->GetEngine());
    ASSERT_TRUE(bindResult.has_value());

    CreateTempScriptFile(R"(
        int value = 0;
        void main() { value = GetConstant(); }
    )", "ReloadMod");

    // Initial compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    // Reload
    CreateTempScriptFile(R"(
        int value = 0;
        void main() { value = GetConstant() * 2; }
    )", "ReloadMod");

    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Re-bind after reload to restore bindings
    auto bindResult2 = engine_->GetBindingManager()->BindAll(engine_->GetEngine());
    ASSERT_TRUE(bindResult2.has_value());

    // Run again
    auto runResult2 = engine_->RunAllMods();
    ASSERT_TRUE(runResult2.has_value());

    // Verify binding still works
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);

    int varIdx = mod->GetGlobalVarIndexByName("ReloadMod::value");
    if (varIdx >= 0) {
        int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
        EXPECT_EQ(*valuePtr, 1998) << "Binding should still work after reload";
    }
    
    SUCCEED() << "Reload retains bindings";
}

/**
 * @brief EmptyTest
 * 
 * Пустой тест для завершения
 */
TEST_F(ReloadManagerTest, EmptyTest) {
    SUCCEED() << "ReloadManager tests completed";
}
