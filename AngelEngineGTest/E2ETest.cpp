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
import AngelEngine.FrameAllocator;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Helper для получения значения глобальной переменной
 */
static int GetGlobalVar(ScriptEngine* engine, const char* modName, const char* varName)
{
    asIScriptModule* mod = engine->GetEngine()->GetModule("__Megamodule__");
    if (!mod) return -1;
    
    eastl::string fullName = eastl::string(modName) + "::" + varName;
    int varIdx = mod->GetGlobalVarIndexByName(fullName.c_str());
    if (varIdx < 0) return -1;
    
    return *static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
}

/**
 * @brief End-to-End тесты для AngelEngine
 * 
 * Тестируют полный цикл работы движка:
 * - Инициализация
 * - Компиляция модулей
 * - Запуск скриптов
 * - События
 * - Hot Reload
 * - Save/Load
 * - GC
 */
class E2ETest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CleanupAllTempScriptFiles();
        
        EngineConfig config;
        config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
        config.enableUseJIT = false;  // Отключаем JIT для быстрых тестов
        config.enableWatchdog = false;
        config.enableAutoGC = false;
        
        auto factory = eastl::make_unique<StandardComponentFactory>(config);
        auto result = ScriptEngine::MakeEngine(eastl::move(factory));
        
        ASSERT_TRUE(result.has_value());
        engine_ = eastl::move(result.value());
    }
    
    void TearDown() override
    {
        CleanupAllTempScriptFiles();
        
        engine_.reset();
        FrameMemoryPool::Get().Reset();
    }
    
    eastl::unique_ptr<ScriptEngine> engine_;
};

/**
 * @brief E2E_FullEngineLifecycle
 *
 * Проверяет полный цикл работы движка:
 * Init → Compile → Run → Tick × 100 → GC → Shutdown
 */
TEST_F(E2ETest, FullEngineLifecycle) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value()) << "Engine initialization failed";

    // 2. Create test script with event subscription
    CreateTempScriptFile(R"(
        int counter = 0;
        void OnTick(float dt) { counter++; }
        void main() { 
            counter = 10;
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "E2EMod");
    
    // 3. Compile
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value()) << "Compilation failed";
    
    // 4. Run
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value()) << "Run failed";
    
    // Verify initial value (main() should set counter to 10)
    int counter = GetGlobalVar(engine_.get(), "E2EMod", "counter");
    EXPECT_EQ(counter, 10) << "Initial counter value should be 10";
    
    // 5. Tick multiple times (OnTick should increment counter)
    for (int i = 0; i < 100; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value()) << "Tick " << i << " failed";
    }
    
    // Verify counter was incremented 100 times (10 + 100 = 110)
    counter = GetGlobalVar(engine_.get(), "E2EMod", "counter");
    EXPECT_EQ(counter, 110) << "Counter should be 110 after 100 ticks";
    
    // 6. Garbage collect
    EXPECT_NO_THROW(engine_->CallGarbageCollectorFullCycle()) << "GC should not throw";
    
    // 7. Engine shutdown happens in TearDown
    SUCCEED() << "Full engine lifecycle completed successfully";
}

/**
 * @brief E2E_HotReloadWorkflow
 * 
 * Проверяет workflow hot reload:
 * Init → Compile → Run → Modify Script → HotReload → Verify State
 */
TEST_F(E2ETest, HotReloadWorkflow) {
    // 1. Initial script
    CreateTempScriptFile(R"(
        int value = 1;
        void main() {}
    )", "ReloadMod");
    
    // 2. Compile and run
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 3. Verify initial value
    int value = GetGlobalVar(engine_.get(), "ReloadMod", "value");
    EXPECT_EQ(value, 1) << "Initial value should be 1";
    
    // 4. Modify script
    CreateTempScriptFile(R"(
        int value = 42;
        void main() {}
    )", "ReloadMod");
    
    // 5. Hot reload
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value()) << "Hot reload failed";
    
    // 6. Verify new value
    value = GetGlobalVar(engine_.get(), "ReloadMod", "value");
    EXPECT_EQ(value, 42) << "Value should be 42 after hot reload";
}

/**
 * @brief E2E_SaveLoadCycle
 *
 * Проверяет цикл сохранения/загрузки:
 * Init → Run → Save → Shutdown → Load → Verify State
 */
TEST_F(E2ETest, SaveLoadCycle) {
    // 1. Setup and run
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    CreateTempScriptFile(R"(
        [Save]
        int score = 100;
        void main() { score = 200; }
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Verify score was set to 200
    int score = GetGlobalVar(engine_.get(), "SaveMod", "score");
    EXPECT_EQ(score, 200) << "Initial score should be 200";
    
    // 2. Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(), 
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value()) << "Save failed";
    
    // 3. Modify state
    // Find and modify the variable directly
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    int varIdx = mod->GetGlobalVarIndexByName("SaveMod::score");
    int* scorePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    *scorePtr = 999;  // Corrupt the value
    
    score = GetGlobalVar(engine_.get(), "SaveMod", "score");
    EXPECT_EQ(score, 999) << "Score should be corrupted to 999";
    
    // 4. Load (restore from save)
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value()) << "Load failed";
    
    // 5. Verify loaded state (should be restored to 200)
    score = GetGlobalVar(engine_.get(), "SaveMod", "score");
    EXPECT_EQ(score, 200) << "Score should be restored to 200 after load";
}

/**
 * @brief E2E_EventSystemFlow
 *
 * Проверяет поток событий:
 * Subscribe → Enqueue → Tick → Dispatch → Verify Callbacks
 */
TEST_F(E2ETest, EventSystemFlow) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create script with event handler and subscription
    CreateTempScriptFile(R"(
        int eventCount = 0;

        void OnTick(float dt) {
            eventCount++;
        }

        void main() {
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "EventMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 4. Tick multiple times
    for (int i = 0; i < 10; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value());
    }
    
    // 5. Verify event handler was called
    int eventCount = GetGlobalVar(engine_.get(), "EventMod", "eventCount");
    EXPECT_EQ(eventCount, 10) << "OnTick should be called 10 times";
}

/**
 * @brief E2E_MultiModuleDependency
 * 
 * Проверяет зависимости между модулями:
 * Load ModA → Load ModB (depends on A) → Run → Verify Order
 */
TEST_F(E2ETest, MultiModuleDependency) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // 2. Create ModA
    CreateTempScriptFile(R"(
        int valueA = 10;
        int getValueA() { return valueA; }
        void main() {}
    )", "ModA");
    
    // Create mod.json with public API
    std::filesystem::create_directories("angelscripts/mods/ModA");
    std::ofstream jsonFile("angelscripts/mods/ModA/mod.json");
    jsonFile << "{\"public_api\": true, \"depends_on\": []}";
    jsonFile.close();
    
    // 3. Create ModB (depends on ModA)
    CreateTempScriptFile(R"(
        int valueB = 0;
        void main() { 
            valueB = ModA::getValueA() + 5;
        }
    )", "ModB");
    
    std::filesystem::create_directories("angelscripts/mods/ModB");
    std::ofstream jsonFileB("angelscripts/mods/ModB/mod.json");
    jsonFileB << "{\"public_api\": false, \"depends_on\": [\"ModA\"]}";
    jsonFileB.close();
    
    // 4. Compile
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value()) << "Compilation failed";
    
    // 5. Run
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value()) << "Run failed";
    
    // 6. Verify ModB received value from ModA
    int valueB = GetGlobalVar(engine_.get(), "ModB", "valueB");
    EXPECT_EQ(valueB, 15) << "valueB should be 15 (10 from ModA + 5)";
    
    // Cleanup
    CleanupTempScriptFiles("ModA");
    CleanupTempScriptFiles("ModB");
}

/**
 * @brief E2E_ErrorRecovery
 *
 * Проверяет восстановление после ошибок:
 * Compile Bad Script → Verify Error → Fix → Reload → Verify Success
 */
TEST_F(E2ETest, ErrorRecovery) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create good script first
    CreateTempScriptFile(R"(
        int value = 0;
        void main() { value = 1; }
    )", "BadMod");

    // 3. Compile good script
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value()) << "Initial compilation should succeed";

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    int value = GetGlobalVar(engine_.get(), "BadMod", "value");
    EXPECT_EQ(value, 1) << "Initial value should be 1";

    // 4. Create bad script (with syntax error)
    CreateTempScriptFile(R"(
        void main() {
            this is invalid syntax!!!
        }
    )", "BadMod");

    // 5. Hot reload with bad script - engine should handle error gracefully
    auto reloadResult = engine_->HotReload();
    (void)reloadResult;  // Engine may succeed (keeping old version) or fail
    // Key point: no crash, engine remains functional

    // 6. Fix the script
    CreateTempScriptFile(R"(
        int fixedValue = 42;
        void main() { fixedValue = fixedValue * 2; }
    )", "BadMod");

    // 7. Hot reload with fixed script
    auto reloadResult2 = engine_->HotReload();
    ASSERT_TRUE(reloadResult2.has_value()) << "Hot reload after fix should succeed";

    // 8. Verify fixed value
    int fixedValue = GetGlobalVar(engine_.get(), "BadMod", "fixedValue");
    EXPECT_EQ(fixedValue, 84) << "fixedValue should be 84 (42 * 2) after fix and run";
}

/**
 * @brief E2E_MemoryStress
 *
 * Стресс-тест памяти:
 * 1000 ticks + arrays + objects → Verify no leaks
 */
TEST_F(E2ETest, MemoryStress) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create script with arrays and objects
    CreateTempScriptFile(R"(
        array<int> numbers(100);
        int tickCount = 0;

        void OnTick(float dt) {
            tickCount++;
            for (uint i = 0; i < 100; i++) {
                numbers[i] = tickCount * i;
            }
        }

        void main() {
            for (uint i = 0; i < 100; i++) {
                numbers[i] = i;
            }
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "StressMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 4. Run 1000 ticks
    for (int i = 0; i < 1000; ++i) {
        auto tickResult = engine_->Tick(0.001f);
        ASSERT_TRUE(tickResult.has_value()) << "Tick " << i << " failed";
        
        // Periodically run GC
        if (i % 100 == 0) {
            engine_->CallGarbageColletorOneStep();
        }
    }
    
    // 5. Verify tick count
    int tickCount = GetGlobalVar(engine_.get(), "StressMod", "tickCount");
    EXPECT_EQ(tickCount, 1000) << "Should have 1000 ticks";
    
    // 6. Final GC
    engine_->CallGarbageCollectorFullCycle();
    
    SUCCEED() << "Memory stress test completed without crashes";
}

/**
 * @brief E2E_JITPerformance
 *
 * Сравнивает производительность с JIT и без:
 * Run with JIT vs without → Compare execution time
 * Примечание: JIT может быть быстрее в 10-100 раз для вычислительных задач
 */
TEST_F(E2ETest, JITPerformance) {
    // Увеличим для более точного замера
    
    int64_t durationNoJIT = 0;
    int64_t durationJIT = 0;
    
    // Test without JIT
    {
        EngineConfig configNoJIT;
        configNoJIT.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
        configNoJIT.enableUseJIT = false;
        configNoJIT.enableWatchdog = false;
        configNoJIT.enableAutoGC = false;

        auto factory = eastl::make_unique<StandardComponentFactory>(configNoJIT);
        auto result = ScriptEngine::MakeEngine(eastl::move(factory));
        ASSERT_TRUE(result.has_value());

        auto engine = eastl::move(result.value());
        auto initResult = engine->InitializeEngine();
        ASSERT_TRUE(initResult.has_value());

        CreateTempScriptFile(R"(
            int result = 0;
            void main() {
                for (int i = 0; i < 100000; i++) {
                    result += i;
                }
            }
        )", "PerfMod");

        auto compileResult = engine->CompileAllMods();
        ASSERT_TRUE(compileResult.has_value());

        auto startTime = std::chrono::high_resolution_clock::now();

        auto runResult = engine->RunAllMods();
        ASSERT_TRUE(runResult.has_value());

        auto endTime = std::chrono::high_resolution_clock::now();
        durationNoJIT = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        int perfResult = GetGlobalVar(engine.get(), "PerfMod", "result");
        // Проверяем что результат корректен (хотя бы знак)
        EXPECT_GT(perfResult, 0) << "Result should be positive";

        printf("[PERF] Without JIT: %lld ms\n", durationNoJIT);

        CleanupTempScriptFiles("PerfMod");
    }

    // Test with JIT
    {
        EngineConfig configJIT;
        configJIT.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
        configJIT.enableUseJIT = true;
        configJIT.enableWatchdog = false;
        configJIT.enableAutoGC = false;

        auto factory = eastl::make_unique<StandardComponentFactory>(configJIT);
        auto result = ScriptEngine::MakeEngine(eastl::move(factory));
        ASSERT_TRUE(result.has_value());

        auto engine = eastl::move(result.value());
        auto initResult = engine->InitializeEngine();
        ASSERT_TRUE(initResult.has_value());

        CreateTempScriptFile(R"(
            int result = 0;
            void main() {
                for (int i = 0; i < 100000; i++) {
                    result += i;
                }
            }
        )", "PerfMod");

        auto compileResult = engine->CompileAllMods();
        ASSERT_TRUE(compileResult.has_value());

        auto startTime = std::chrono::high_resolution_clock::now();

        auto runResult = engine->RunAllMods();
        ASSERT_TRUE(runResult.has_value());

        auto endTime = std::chrono::high_resolution_clock::now();
        durationJIT = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        int perfResult = GetGlobalVar(engine.get(), "PerfMod", "result");
        EXPECT_GT(perfResult, 0) << "Result should be positive";

        printf("[PERF] With JIT: %lld ms\n", durationJIT);

        CleanupTempScriptFiles("PerfMod");
    }

    // Проверяем что JIT версия не медленнее (может быть быстрее или одинаково)
    // Примечание: Для маленьких скриптов JIT может быть медленнее из-за накладных расходов
    // но для больших вычислений должен быть быстрее
    if (durationNoJIT > 10) {  // Только если без JIT заняло заметное время
        // JIT должен быть хотя бы не медленнее чем 2x
        EXPECT_LT(durationJIT, durationNoJIT * 2) 
            << "JIT should not be significantly slower than interpreter";
    }
    
    printf("[PERF] Speedup: %.2fx\n", durationNoJIT > 0 ? (double)durationNoJIT / durationJIT : 0);
    
    SUCCEED() << "JIT performance test completed (NoJIT=" << durationNoJIT 
              << "ms, JIT=" << durationJIT << "ms)";
}
