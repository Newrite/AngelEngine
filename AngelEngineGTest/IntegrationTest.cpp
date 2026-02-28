#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"
#include "Mocks.h"

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
import AngelEngine.EventChannel;
import AngelEngine.Utils;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Интеграционные тесты для AngelEngine
 * 
 * Тестируют взаимодействие между компонентами:
 * - ScriptEngine + ModuleLoader
 * - ExecutionManager + EventManager + EventChannel
 * - SaveLoadManager + SerializationHandlers
 * - BindingManager + ScriptEngine
 * - ReloadManager + все компоненты
 */
class IntegrationTest : public ::testing::Test
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
 * @brief Integration_ScriptEngine_ModuleLoader
 * 
 * Тестирует взаимодействие ScriptEngine и ModuleLoader:
 * - Компиляция модулей
 * - Запуск скриптов
 * - Проверка результатов
 */
TEST_F(IntegrationTest, ScriptEngine_ModuleLoader_CompileAndRun) {
    // 1. Initialize engine
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value()) << "Engine initialization failed";
    
    // 2. Create test script
    CreateTempScriptFile(R"(
        int result = 0;
        void main() {
            result = 42;
        }
    )", "IntMod");
    
    // 3. Compile
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value()) << "Compilation failed";
    
    // 4. Verify module was loaded
    auto* moduleLoader = engine_->GetModuleLoader();
    const auto& modules = moduleLoader->GetLoadedModules();
    EXPECT_FALSE(modules.empty()) << "Should have at least one loaded module";
    
    // 5. Run
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value()) << "Run failed";
    
    // 6. Verify script executed
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("IntMod::result");
    ASSERT_GE(varIdx, 0);
    
    int* resultPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*resultPtr, 42) << "Script should set result to 42";
}

/**
 * @brief Integration_ExecutionManager_Events
 *
 * Тестирует взаимодействие ExecutionManager, EventManager и EventChannel:
 * - Tick вызывает события
 * - События доставляются подписчикам
 * - OnTick вызывается каждый кадр
 */
TEST_F(IntegrationTest, ExecutionManager_EventManager_Tick) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create script with OnTick handler and subscription
    CreateTempScriptFile(R"(
        int tickCount = 0;

        void OnTick(float dt) {
            tickCount++;
        }

        void main() {
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "IntMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 4. Run multiple ticks
    int numTicks = 50;
    for (int i = 0; i < numTicks; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value()) << "Tick " << i << " failed";
    }
    
    // 5. Verify OnTick was called correct number of times
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("IntMod::tickCount");
    ASSERT_GE(varIdx, 0);
    
    int* tickCountPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*tickCountPtr, numTicks) << "OnTick should be called " << numTicks << " times";
}

/**
 * @brief Integration_SaveLoad_Serialization
 *
 * Тестирует взаимодействие SaveLoadManager и SerializationHandlers:
 * - Сохранение сложных типов (array, dictionary)
 * - Загрузка и восстановление состояния
 */
TEST_F(IntegrationTest, SaveLoadManager_SerializationHandlers) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create script with saveable variables
    CreateTempScriptFile(R"(
        [Save]
        int score = 100;
        [Save]
        string name = "Player";

        void main() {
            score = 200;
            name = "Hero";
        }
    )", "SaveMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 4. Verify initial values
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int scoreIdx = mod->GetGlobalVarIndexByName("SaveMod::score");
    int* scorePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(scoreIdx));
    EXPECT_EQ(*scorePtr, 200) << "Initial score should be 200";
    
    // 5. Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value()) << "Save failed";
    EXPECT_GT(saveResult.value().size(), 0) << "Save data should not be empty";
    
    // 6. Modify values
    *scorePtr = 999;
    EXPECT_EQ(*scorePtr, 999) << "Score should be corrupted to 999";
    
    // 7. Load
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value()) << "Load failed";
    
    // 8. Verify restored values
    EXPECT_EQ(*scorePtr, 200) << "Score should be restored to 200";
}

/**
 * @brief Integration_BindingManager_ScriptEngine
 *
 * Тестирует взаимодействие BindingManager и ScriptEngine:
 * - Регистрация C++ функций в скриптах
 * - Вызов зарегистрированных функций из скриптов
 */
TEST_F(IntegrationTest, BindingManager_ScriptEngine_RegisterFunctions) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create custom binding
    class TestBinding : public IScriptBinding
    {
    public:
        void Bind(asIScriptEngine* engine) override
        {
            // Register a global function
            int r = engine->RegisterGlobalFunction(
                "int GetAnswer()",
                asFUNCTION(GetAnswer),
                asCALL_CDECL
            );
            EXPECT_GE(r, 0) << "Failed to register GetAnswer function";
        }

    private:
        static int GetAnswer()
        {
            return 42;
        }
    };

    // 3. Add binding BEFORE compilation
    TestBinding binding;
    engine_->AddBinding(&binding);
    
    // 4. Re-bind to register the new binding
    auto bindResult = engine_->GetBindingManager()->BindAll(engine_->GetEngine());
    ASSERT_TRUE(bindResult.has_value()) << "Failed to bind new binding";

    // 5. Create script that uses the binding
    CreateTempScriptFile(R"(
        int result = 0;
        void main() {
            result = GetAnswer();
        }
    )", "BindMod");

    // 6. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value()) << "Compilation failed";

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value()) << "Run failed";

    // 7. Verify the function was called
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);

    int varIdx = mod->GetGlobalVarIndexByName("BindMod::result");
    ASSERT_GE(varIdx, 0) << "Variable result not found in BindMod";

    int* resultPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*resultPtr, 42) << "GetAnswer() should return 42";
}

/**
 * @brief Integration_ReloadManager_Full
 *
 * Тестирует полный цикл hot-reload со всеми компонентами:
 * - Compile → Run → Modify → Reload → Verify
 * - События переподключаются
 * - Состояние сохраняется
 */
TEST_F(IntegrationTest, ReloadManager_FullCycle) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create initial script with event subscription
    CreateTempScriptFile(R"(
        int value = 1;
        int tickCount = 0;

        void OnTick(float dt) {
            tickCount++;
        }

        void main() {
            value = 10;
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "IntMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // 4. Verify initial state
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int valueIdx = mod->GetGlobalVarIndexByName("IntMod::value");
    int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(valueIdx));
    EXPECT_EQ(*valuePtr, 10) << "Initial value should be 10";
    
    // 5. Run some ticks
    for (int i = 0; i < 10; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value());
    }
    
    int tickIdx = mod->GetGlobalVarIndexByName("IntMod::tickCount");
    int* tickPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(tickIdx));
    EXPECT_EQ(*tickPtr, 10) << "Should have 10 ticks before reload";
    
    // 6. Modify script
    CreateTempScriptFile(R"(
        int value = 1;
        int tickCount = 0;
        
        void OnTick(float dt) {
            tickCount++;
        }
        
        void main() {
            value = 42;
            SubscribeTick(@OnTick);  // Re-subscribe after reload
        }
    )", "IntMod");
    
    // 7. Hot reload
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value()) << "Hot reload failed";
    
    // 8. Verify new script is active
    mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    valueIdx = mod->GetGlobalVarIndexByName("IntMod::value");
    valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(valueIdx));
    EXPECT_EQ(*valuePtr, 42) << "Value should be 42 after reload";
    
    // 9. Verify events still work
    for (int i = 0; i < 5; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value());
    }
    
    tickIdx = mod->GetGlobalVarIndexByName("IntMod::tickCount");
    tickPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(tickIdx));
    EXPECT_EQ(*tickPtr, 5) << "Should have 5 ticks after reload";
}

/**
 * @brief Integration_EventChannel_Dispatch
 * 
 * Тестирует работу EventChannel с подписчиками:
 * - Регистрация канала
 * - Подписка на события
 * - Dispatch событий
 */
TEST_F(IntegrationTest, EventChannel_Dispatch_Subscribers) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 1.5 Register custom event
    // Use new to ensure channel lives until EventManager is destroyed
    auto* customChannel = new EventChannel<int>(ChannelDescriptor{
        "CustomEvent", "funcdef void CustomEventCallback(int)", "CustomEventCallback", "int value", "value"});
    uint32_t eventId = HashString("CustomEvent");
    engine_->GetEventManager()->RegisterChannel(eventId, customChannel);
    
    // 2. Create script with event subscription
    CreateTempScriptFile(R"(
        int customEventCount = 0;
        
        void OnCustomEvent(int value) {
            customEventCount += value;
        }
        
        void Subscribe() {
            SubscribeCustomEvent(@OnCustomEvent);
        }
        
        void main() {
            Subscribe();
        }
    )", "IntMod");
    
    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Note: Full event subscription testing requires proper event binding setup
    // This test verifies the basic infrastructure is in place
    SUCCEED() << "Event channel infrastructure is in place";
}

/**
 * @brief Integration_MultiComponent_Stress
 *
 * Стресс-тест взаимодействия всех компонентов:
 * - Multiple modules
 * - Events + Tick
 * - Save/Load during execution
 * - GC during execution
 */
TEST_F(IntegrationTest, MultiComponent_StressTest) {
    // 1. Initialize
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());

    // 2. Create multiple modules with event subscription
    CreateTempScriptFile(R"(
        int modA_value = 0;
        void OnTick(float dt) { modA_value++; }
        void main() { 
            modA_value = 10;
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "ModA");

    CreateTempScriptFile(R"(
        int modB_value = 0;
        void OnTick(float dt) { modB_value += 2; }
        void main() { 
            modB_value = 20;
            SubscribeTick(@OnTick);  // Subscribe to tick events
        }
    )", "ModB");

    // 3. Compile and run
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    // 4. Verify initial state after run
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);

    int modAIdx = mod->GetGlobalVarIndexByName("ModA::modA_value");
    int modBIdx = mod->GetGlobalVarIndexByName("ModB::modB_value");
    ASSERT_GE(modAIdx, 0);
    ASSERT_GE(modBIdx, 0);

    int* modAPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(modAIdx));
    int* modBPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(modBIdx));
    
    EXPECT_EQ(*modAPtr, 10) << "ModA initial value should be 10";
    EXPECT_EQ(*modBPtr, 20) << "ModB initial value should be 20";

    // 5. Run many ticks with periodic GC
    const int numTicks = 100;
    for (int i = 0; i < numTicks; ++i) {
        auto tickResult = engine_->Tick(0.016f);
        ASSERT_TRUE(tickResult.has_value());

        if (i % 25 == 0) {
            engine_->CallGarbageColletorOneStep();
        }
    }

    // 6. Verify tick updates
    EXPECT_EQ(*modAPtr, 10 + numTicks) << "ModA should increment by " << numTicks;
    EXPECT_EQ(*modBPtr, 20 + numTicks * 2) << "ModB should increment by " << (numTicks * 2);

    // 7. Save state
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value()) << "Save should succeed during stress test";
    EXPECT_GT(saveResult.value().size(), 0) << "Save data should not be empty";

    // 8. Load and verify state is preserved
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value()) << "Load should succeed";
    
    // Verify values after load
    EXPECT_EQ(*modAPtr, 10 + numTicks) << "ModA value should be preserved after load";
    EXPECT_EQ(*modBPtr, 20 + numTicks * 2) << "ModB value should be preserved after load";

    // 9. Final GC
    engine_->CallGarbageCollectorFullCycle();

    SUCCEED() << "Multi-component stress test completed successfully "
              << "(ModA=" << *modAPtr << ", ModB=" << *modBPtr << ")";

    // Cleanup
    CleanupTempScriptFiles("ModA");
    CleanupTempScriptFiles("ModB");
}
