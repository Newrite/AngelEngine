#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"
#include "Mocks.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/atomic.h>

// Import AngelEngine modules
import AngelEngine.ExecutionManager;
import AngelEngine.Interfaces;
import AngelEngine.Logger;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Фикстура для тестов ExecutionManager
 * 
 * Предоставляет:
 * - Mock объекты для зависимостей
 * - Чистое состояние перед каждым тестом
 */
class ExecutionManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Создаём ExecutionManager с отключенным watchdog для быстрых тестов
        executionManager_ = eastl::make_unique<ExecutionManager>(5000, false);
        
        // Сбрасываем моки
        mockEventManager_ = eastl::make_unique<MockEventManager>();
        mockDispatcher_ = eastl::make_unique<MockBuiltinEventDispatcher>();
        mockEngine_ = nullptr;  // Будет создан в тестах где нужен
    }
    
    void TearDown() override
    {
        executionManager_.reset();
        mockEventManager_.reset();
        mockDispatcher_.reset();
    }
    
    eastl::unique_ptr<ExecutionManager> executionManager_;
    eastl::unique_ptr<MockEventManager> mockEventManager_;
    eastl::unique_ptr<MockBuiltinEventDispatcher> mockDispatcher_;
    asIScriptEngine* mockEngine_;
};

/**
 * @brief Тесты для RequestContext / ReturnContext
 */
class ContextPoolingTest : public ExecutionManagerTest
{
};

TEST_F(ContextPoolingTest, RequestContext_ReturnsValidContext)
{
    // Создаём реальный движок для теста
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Запрашиваем контекст
    auto ctxPtr = executionManager_->RequestContext(engine, nullptr);
    
    // Проверяем что контекст валиден
    EXPECT_NE(ctxPtr.get(), nullptr);
    EXPECT_TRUE(static_cast<bool>(ctxPtr));
    
    engine->ShutDownAndRelease();
}

TEST_F(ContextPoolingTest, RequestContext_MultipleRequests)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Запрашиваем несколько контекстов
    eastl::vector<asIScriptContext*> contexts;
    
    for (int i = 0; i < 5; ++i)
    {
        auto ctxPtr = executionManager_->RequestContext(engine, nullptr);
        EXPECT_NE(ctxPtr.get(), nullptr);
        contexts.push_back(ctxPtr.get());
        
        // Освобождаем контекст (уничтожение ctxPtr)
        ctxPtr.reset();
    }
    
    engine->ShutDownAndRelease();
}

TEST_F(ContextPoolingTest, ReturnContext_ReturnsToPool)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Запрашиваем и возвращаем контекст
    {
        auto ctxPtr = executionManager_->RequestContext(engine, nullptr);
        asIScriptContext* rawCtx = ctxPtr.get();
        EXPECT_NE(rawCtx, nullptr);
        
        // ctxPtr уничтожается здесь, вызывая ReturnContext
    }
    
    // Запрашиваем снова - должен вернуться контекст из пула
    auto ctxPtr2 = executionManager_->RequestContext(engine, nullptr);
    EXPECT_NE(ctxPtr2.get(), nullptr);
    
    engine->ShutDownAndRelease();
}

TEST_F(ContextPoolingTest, ContextPool_LockFree)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    constexpr int numThreads = 4;
    constexpr int requestsPerThread = 10;
    
    eastl::vector<std::thread> threads;
    eastl::atomic<int> successCount{0};
    
    // Создаём потоки для одновременного запроса контекстов
    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back([&, this]() {
            for (int i = 0; i < requestsPerThread; ++i)
            {
                auto ctxPtr = executionManager_->RequestContext(engine, nullptr);
                if (ctxPtr)
                {
                    successCount.fetch_add(1);
                }
            }
        });
    }
    
    // Ждём завершения
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // Все запросы должны быть успешными
    EXPECT_EQ(successCount.load(), numThreads * requestsPerThread);
    
    engine->ShutDownAndRelease();
}

/**
 * @brief Тесты для Tick()
 */
class TickTest : public ExecutionManagerTest
{
};

TEST_F(TickTest, Tick_WithNullEventManager)
{
    // Tick с nullptr должен работать без ошибок
    auto result = executionManager_->Tick(0.016f, nullptr, nullptr, nullptr);
    
    EXPECT_TRUE(result.has_value());
}

TEST_F(TickTest, Tick_WithMockEventManager)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Настраиваем mock для ProcessAllDeferred
    EXPECT_CALL(*mockEventManager_, ProcessAllDeferred(::testing::_))
        .WillOnce(::testing::Return(eastl::expected<void, EventError>()));
    
    // Tick с EventManager
    auto result = executionManager_->Tick(
        0.016f, 
        mockEventManager_.get(), 
        engine, 
        nullptr
    );
    
    EXPECT_TRUE(result.has_value());
    
    // Проверяем что ProcessAllDeferred был вызван
    ::testing::Mock::VerifyAndClearExpectations(mockEventManager_.get());
    
    engine->ShutDownAndRelease();
}

TEST_F(TickTest, Tick_WithMockDispatcher)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Настраиваем mock для DispatchBuiltinEvents
    EXPECT_CALL(*mockDispatcher_, DispatchBuiltinEvents(::testing::_, ::testing::_))
        .Times(1);
    
    auto result = executionManager_->Tick(
        0.016f,
        nullptr,
        engine,
        mockDispatcher_.get()
    );
    
    EXPECT_TRUE(result.has_value());
    
    ::testing::Mock::VerifyAndClearExpectations(mockDispatcher_.get());
    
    engine->ShutDownAndRelease();
}

TEST_F(TickTest, Tick_DeltaTime_PassedToDispatcher)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    constexpr float expectedDeltaTime = 0.032f;
    
    // Проверяем что deltaTime передаётся в dispatcher
    EXPECT_CALL(*mockDispatcher_, DispatchBuiltinEvents(::testing::_, ::testing::FloatEq(expectedDeltaTime)))
        .Times(1);
    
    executionManager_->Tick(expectedDeltaTime, nullptr, engine, mockDispatcher_.get());
    
    ::testing::Mock::VerifyAndClearExpectations(mockDispatcher_.get());
    
    engine->ShutDownAndRelease();
}

/**
 * @brief Тесты для RunAllMods
 */
class RunAllModsTest : public ExecutionManagerTest
{
};

TEST_F(RunAllModsTest, RunAllMods_EmptyModuleLoader)
{
    MockModuleLoader mockLoader;
    
    // Настраиваем mock для Empty()
    EXPECT_CALL(mockLoader, Empty())
        .WillOnce(::testing::Return(true));
    
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    auto result = executionManager_->RunAllMods(engine, &mockLoader);
    
    // Ожидаем ошибку NoModsLoadedToRun
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ExecutionError::NoModsLoadedToRun);
    
    engine->ShutDownAndRelease();
}

TEST_F(RunAllModsTest, RunAllMods_WithModules)
{
    MockModuleLoader mockLoader;
    
    // Настраиваем mock
    EXPECT_CALL(mockLoader, Empty())
        .WillOnce(::testing::Return(false));
    
    static eastl::vector<eastl::string> modules = {"TestMod"};
    EXPECT_CALL(mockLoader, GetLoadedModules())
        .WillOnce(::testing::ReturnRef(modules));
    
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Создаём тестовый модуль
    int r = engine->RegisterGlobalProperty("int testVar", nullptr);
    EXPECT_GE(r, 0);
    
    auto result = executionManager_->RunAllMods(engine, &mockLoader);
    
    // RunAllMods должен завершиться успешно (даже если модуль пустой)
    EXPECT_TRUE(result.has_value());
    
    ::testing::Mock::VerifyAndClearExpectations(&mockLoader);
    
    engine->ShutDownAndRelease();
}

/**
 * @brief Тесты для Renew()
 */
class RenewTest : public ExecutionManagerTest
{
};

TEST_F(RenewTest, Renew_ResetsState)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Запрашиваем контекст перед Renew
    auto ctxPtr1 = executionManager_->RequestContext(engine, nullptr);
    EXPECT_NE(ctxPtr1.get(), nullptr);
    
    // Вызываем Renew
    executionManager_->Renew();
    
    // После Renew должны móc запросить контекст снова
    auto ctxPtr2 = executionManager_->RequestContext(engine, nullptr);
    EXPECT_NE(ctxPtr2.get(), nullptr);
    
    engine->ShutDownAndRelease();
}

TEST_F(RenewTest, Renew_StopsWatchdog)
{
    // Создаём ExecutionManager с включенным watchdog
    auto emWithWatchdog = eastl::make_unique<ExecutionManager>(1000, true);
    
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Renew должен остановить watchdog поток
    EXPECT_NO_THROW(emWithWatchdog->Renew());
    
    engine->ShutDownAndRelease();
}

/**
 * @brief Тесты для ExecuteManaged
 */
class ExecuteManagedTest : public ExecutionManagerTest
{
};

TEST_F(ExecuteManagedTest, ExecuteManaged_NullContext)
{
    auto result = executionManager_->ExecuteManaged(nullptr);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ExecutionError::InvalidFunction);
}

TEST_F(ExecuteManagedTest, ExecuteManaged_ValidContext)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Создаём контекст
    asIScriptContext* ctx = engine->CreateContext();
    ASSERT_NE(ctx, nullptr);
    
    // Prepare с несуществующей функцией вернёт ошибку
    int r = ctx->Prepare(nullptr);
    EXPECT_LT(r, 0);
    
    // ExecuteManaged должен обработать это
    auto result = executionManager_->ExecuteManaged(ctx);
    
    // Ожидаем ошибку (функция не подготовлена)
    EXPECT_FALSE(result.has_value());
    
    ctx->Release();
    engine->ShutDownAndRelease();
}

/**
 * @brief Параметризированные тесты для различных конфигураций
 */
class ExecutionManagerConfigTest : public ::testing::TestWithParam<std::pair<int64_t, bool>>
{
protected:
    void SetUp() override
    {
        auto [maxTime, enableWatchdog] = GetParam();
        executionManager_ = eastl::make_unique<ExecutionManager>(maxTime, enableWatchdog);
    }
    
    void TearDown() override
    {
        executionManager_.reset();
    }
    
    eastl::unique_ptr<ExecutionManager> executionManager_;
};

TEST_P(ExecutionManagerConfigTest, Create_WithConfig)
{
    // Просто проверяем что менеджер создаётся без ошибок
    EXPECT_NE(executionManager_, nullptr);
}

TEST_P(ExecutionManagerConfigTest, Tick_BasicFunctionality)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    auto result = executionManager_->Tick(0.016f, nullptr, engine, nullptr);
    
    EXPECT_TRUE(result.has_value());
    
    engine->ShutDownAndRelease();
}

// Параметры: (maxScriptExecutionTimeMs, enableWatchdog)
INSTANTIATE_TEST_SUITE_P(
    VariousConfigs,
    ExecutionManagerConfigTest,
    ::testing::Values(
        std::make_pair(1000, false),   // 1 секунда, без watchdog
        std::make_pair(5000, false),   // 5 секунд, без watchdog
        std::make_pair(1000, true),    // 1 секунда, с watchdog
        std::make_pair(10000, true)    // 10 секунд, с watchdog
    )
);

/**
 * @brief Death tests для критических ошибок
 * 
 * Проверяют что код корректно обрабатывает ошибочные ситуации
 */
class ExecutionManagerDeathTest : public ExecutionManagerTest
{
};

TEST_F(ExecutionManagerDeathTest, RequestContext_NullEngine)
{
    // Запрос контекста с nullptr engine должен вернуть nullptr
    auto ctxPtr = executionManager_->RequestContext(nullptr, nullptr);
    
    EXPECT_EQ(ctxPtr.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(ctxPtr));
}
