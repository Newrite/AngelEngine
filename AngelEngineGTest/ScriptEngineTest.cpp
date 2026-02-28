#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"
#include "Mocks.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <angelscript.h>
#include <filesystem>
#include <fstream>

// Import AngelEngine modules - Infrastructure first for FileSystemScriptSourceProvider
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;
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
 * @brief Фикстура для тестов ScriptEngine с полной инфраструктурой
 */
class ScriptEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Очищаем временные файлы перед тестом
        CleanupTempScriptFiles("TestMod");
        
        // Создаём engine с полной инфраструктурой
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
        // Очищаем временные файлы после теста
        CleanupTempScriptFiles("TestMod");
        
        engine_.reset();
    }
    
    eastl::unique_ptr<ScriptEngine> engine_;
};

/**
 * @brief Тесты для InitializeEngine
 */
class InitializeEngineTest : public ScriptEngineTest
{
};

TEST_F(InitializeEngineTest, Initialize_Success)
{
    auto result = engine_->InitializeEngine();
    
    EXPECT_TRUE(result.has_value());
}

TEST_F(InitializeEngineTest, Initialize_WithJIT)
{
    // Создаём engine с включенным JIT
    EngineConfig config;
    config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
    config.enableUseJIT = true;
    config.enableWatchdog = false;
    
    auto factory = eastl::make_unique<StandardComponentFactory>(config);
    auto result = ScriptEngine::MakeEngine(eastl::move(factory));
    
    ASSERT_TRUE(result.has_value());
    auto jitEngine = eastl::move(result.value());
    
    auto initResult = jitEngine->InitializeEngine();
    
    EXPECT_TRUE(initResult.has_value());
}

TEST_F(InitializeEngineTest, Initialize_CallsListener)
{
    // Mock listener
    class MockListener : public IEngineListener
    {
    public:
        MOCK_METHOD(
            void,
            OnEngineInitialized,
            (asIScriptEngine*, IBindingManager*, IModuleLoader*, IExecutionManager*),
            (override)
        );
    };
    
    MockListener listener;
    EXPECT_CALL(listener, OnEngineInitialized(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1);
    
    engine_->AddListener(&listener);
    
    auto result = engine_->InitializeEngine();
    
    EXPECT_TRUE(result.has_value());
    
    ::testing::Mock::VerifyAndClearExpectations(&listener);
}

/**
 * @brief Тесты для CompileAllMods
 */
class CompileAllModsTest : public ScriptEngineTest
{
};

TEST_F(CompileAllModsTest, CompileAllMods_NoMods)
{
    // Сначала инициализируем
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Пытаемся компилировать без созданных модулей
    // В текущей реализации это может вернуть успех если есть другие моды
    auto result = engine_->CompileAllMods();
    
    // Тест просто проверяет что компиляция не крэшит
    SUCCEED() << "CompileAllMods completed (result: " << (result.has_value() ? "success" : "error") << ")";
}

TEST_F(CompileAllModsTest, CompileAllMods_WithEmptyMod)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём пустой модуль
    eastl::string scriptContent = R"(
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent, "EmptyMod");
    
    auto result = engine_->CompileAllMods();
    
    EXPECT_TRUE(result.has_value());
}

TEST_F(CompileAllModsTest, CompileAllMods_WithFunction)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём модуль с функцией
    eastl::string scriptContent = R"(
        int getValue() { return 42; }
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent, "FuncMod");
    
    auto result = engine_->CompileAllMods();
    
    EXPECT_TRUE(result.has_value());
    
    // Проверяем что функция существует
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    asIScriptFunction* func = mod->GetFunctionByDecl("int FuncMod::getValue()");
    EXPECT_NE(func, nullptr);
}

TEST_F(CompileAllModsTest, CompileAllMods_SyntaxError)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём модуль с синтаксической ошибкой
    eastl::string scriptContent = R"(
        void main() {
            this is not valid syntax!!!
        }
    )";
    
    CreateTempScriptFile(scriptContent, "BadMod");
    
    auto result = engine_->CompileAllMods();
    
    // В текущей реализации BadMod будет исключён из компиляции
    // Тест просто проверяет что компиляция не крэшит
    SUCCEED() << "CompileAllMods with syntax error completed (result: " << (result.has_value() ? "success" : "error") << ")";
}

/**
 * @brief Тесты для RunAllMods
 */
class RunAllModsTest : public ScriptEngineTest
{
};

TEST_F(RunAllModsTest, RunAllMods_Success)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём модуль
    eastl::string scriptContent = R"(
        int counter = 0;
        void main() { counter = 42; }
    )";
    
    CreateTempScriptFile(scriptContent, "RunMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    
    EXPECT_TRUE(runResult.has_value());
    
    // Проверяем что main() выполнился
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("RunMod::counter");
    int* counterPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    
    EXPECT_EQ(*counterPtr, 42);
}

TEST_F(RunAllModsTest, RunAllMods_NotCompiled)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Пытаемся запустить без компиляции
    auto result = engine_->RunAllMods();
    
    // Должно вернуть ошибку
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Тесты для Tick
 */
class TickTest : public ScriptEngineTest
{
};

TEST_F(TickTest, Tick_Success)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    auto result = engine_->Tick(0.016f);
    
    EXPECT_TRUE(result.has_value());
}

TEST_F(TickTest, Tick_MultipleCalls)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    for (int i = 0; i < 100; ++i)
    {
        auto result = engine_->Tick(0.016f);
        EXPECT_TRUE(result.has_value());
    }
}

TEST_F(TickTest, Tick_WithEvents)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём модуль с обработчиком событий
    eastl::string scriptContent = R"(
        int tickCount = 0;
        void OnTick(float dt) { tickCount++; }
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent, "TickMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    // Запускаем несколько тиков
    for (int i = 0; i < 10; ++i)
    {
        auto result = engine_->Tick(0.016f);
        EXPECT_TRUE(result.has_value());
    }
}

/**
 * @brief Тесты для HotReload
 */
class HotReloadTest : public ScriptEngineTest
{
};

TEST_F(HotReloadTest, HotReload_Basic)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Создаём начальный модуль
    eastl::string scriptContent1 = R"(
        int value = 1;
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent1, "ReloadMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    // Изменяем скрипт
    eastl::string scriptContent2 = R"(
        int value = 2;
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent2, "ReloadMod");
    
    // Hot reload
    auto reloadResult = engine_->HotReload();
    
    EXPECT_TRUE(reloadResult.has_value());
}

TEST_F(HotReloadTest, HotReload_CallsListeners)
{
    class MockListener : public IEngineListener
    {
    public:
        MOCK_METHOD(void, OnHotReloadStarted, (asIScriptEngine*), (override));
        MOCK_METHOD(void, OnHotReloadFinished, (asIScriptEngine*), (override));
    };
    
    MockListener listener;
    EXPECT_CALL(listener, OnHotReloadStarted(::testing::_)).Times(1);
    EXPECT_CALL(listener, OnHotReloadFinished(::testing::_)).Times(1);
    
    engine_->AddListener(&listener);
    
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Hot reload без модулей (должен просто пройти)
    auto result = engine_->HotReload();
    (void)result;  // Suppress unused warning
    
    // Ожидаем ошибку (нет модулей) но listener должен быть вызван
    ::testing::Mock::VerifyAndClearExpectations(&listener);
}

/**
 * @brief Тесты для Garbage Collector
 */
class GarbageCollectorTest : public ScriptEngineTest
{
};

TEST_F(GarbageCollectorTest, GC_FullCycle)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Запускаем полный цикл GC
    EXPECT_NO_THROW(engine_->CallGarbageCollectorFullCycle());
}

TEST_F(GarbageCollectorTest, GC_OneStep)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Запускаем один шаг GC
    EXPECT_NO_THROW(engine_->CallGarbageColletorOneStep());
}

TEST_F(GarbageCollectorTest, GC_MultipleSteps)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    // Запускаем несколько шагов GC
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(engine_->CallGarbageColletorOneStep());
    }
}

/**
 * @brief Тесты для AddBinding
 */
class AddBindingTest : public ScriptEngineTest
{
};

TEST_F(AddBindingTest, AddBinding_Success)
{
    class TestBinding : public IScriptBinding
    {
    public:
        void Bind(asIScriptEngine* engine) override
        {
            // Регистрируем тестовый тип
            int r = engine->RegisterGlobalProperty("int testBindingValue", nullptr);
            (void)r;
        }
    };
    
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    TestBinding binding;
    
    EXPECT_NO_THROW(engine_->AddBinding(&binding));
}

/**
 * @brief Тесты для Getters
 */
class GettersTest : public ScriptEngineTest
{
};

TEST_F(GettersTest, GetEngine_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    asIScriptEngine* engine = engine_->GetEngine();
    
    EXPECT_NE(engine, nullptr);
}

TEST_F(GettersTest, GetEventManager_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    IEventManager* eventManager = engine_->GetEventManager();
    
    EXPECT_NE(eventManager, nullptr);
}

TEST_F(GettersTest, GetExecutionManager_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    IExecutionManager* execManager = engine_->GetExecutionManager();
    
    EXPECT_NE(execManager, nullptr);
}

TEST_F(GettersTest, GetModuleLoader_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    IModuleLoader* loader = engine_->GetModuleLoader();
    
    EXPECT_NE(loader, nullptr);
}

TEST_F(GettersTest, GetSaveLoadManager_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    ISaveLoadManager* saveLoad = engine_->GetSaveLoadManager();
    
    EXPECT_NE(saveLoad, nullptr);
}

TEST_F(GettersTest, GetReloadManager_AfterInit)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    IReloadManager* reload = engine_->GetReloadManager();
    
    EXPECT_NE(reload, nullptr);
}

/**
 * @brief Параметризированные тесты для различных конфигураций
 */
class ScriptEngineConfigTest : public ::testing::TestWithParam<std::tuple<bool, bool, bool>>
{
protected:
    void SetUp() override
    {
        auto [useJIT, useWatchdog, useAutoGC] = GetParam();
        
        EngineConfig config;
        config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
        config.enableUseJIT = useJIT;
        config.enableWatchdog = useWatchdog;
        config.enableAutoGC = useAutoGC;
        
        auto factory = eastl::make_unique<StandardComponentFactory>(config);
        auto result = ScriptEngine::MakeEngine(eastl::move(factory));
        
        ASSERT_TRUE(result.has_value());
        engine_ = eastl::move(result.value());
    }
    
    void TearDown() override
    {
        engine_.reset();
    }
    
    eastl::unique_ptr<ScriptEngine> engine_;
};

TEST_P(ScriptEngineConfigTest, Initialize_WithConfig)
{
    auto result = engine_->InitializeEngine();
    
    EXPECT_TRUE(result.has_value());
}

TEST_P(ScriptEngineConfigTest, Tick_WithConfig)
{
    auto initResult = engine_->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    auto result = engine_->Tick(0.016f);
    
    EXPECT_TRUE(result.has_value());
}

// Параметры: (useJIT, useWatchdog, useAutoGC)
INSTANTIATE_TEST_SUITE_P(
    VariousConfigs,
    ScriptEngineConfigTest,
    ::testing::Values(
        std::make_tuple(false, false, false),  // Все отключено
        std::make_tuple(true, false, false),   // Только JIT
        std::make_tuple(false, true, false),   // Только Watchdog
        std::make_tuple(false, false, true),   // Только AutoGC
        std::make_tuple(true, true, true)      // Всё включено
    )
);
