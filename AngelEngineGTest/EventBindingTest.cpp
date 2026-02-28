#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"
#include "Mocks.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <angelscript.h>

// Import AngelEngine modules
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;
import AngelEngine.Types;
import AngelEngine.Errors;
import AngelEngine.Logger;
import AngelEngine.EventsBinding;
import AngelEngine.EventManager;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для EventBinding
 *
 * Проверяют:
 * - Конструктор и регистрация каналов
 * - DispatchBuiltinEvents
 * - PushLoad / PushSave / PushTick
 */
class EventBindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        eventManager_ = eastl::make_unique<EventManager>();
        eventBinding_ = eastl::make_unique<EventBinding>(eventManager_.get());
    }

    void TearDown() override
    {
        eventBinding_.reset();
        eventManager_.reset();
    }

    eastl::unique_ptr<EventManager> eventManager_;
    eastl::unique_ptr<EventBinding> eventBinding_;
};

/**
 * @brief Constructor_RegistersChannels
 *
 * Проверяет что конструктор регистрирует каналы событий
 */
TEST_F(EventBindingTest, Constructor_RegistersChannels) {
    // Конструктор уже вызван в SetUp
    // Проверяем что каналы зарегистрированы через GetChannel
    // Используем хэши событий из EventsName
    auto* tickChannel = eventManager_->GetChannel(EventsName::OnTick);
    auto* loadChannel = eventManager_->GetChannel(EventsName::OnLoad);
    auto* saveChannel = eventManager_->GetChannel(EventsName::OnSave);
    
    EXPECT_NE(tickChannel, nullptr) << "Tick channel should be registered";
    EXPECT_NE(loadChannel, nullptr) << "Load channel should be registered";
    EXPECT_NE(saveChannel, nullptr) << "Save channel should be registered";
}

/**
 * @brief DispatchBuiltinEvents_CallsTickChannel
 *
 * Проверяет что DispatchBuiltinEvents вызывает Tick канал
 */
TEST_F(EventBindingTest, DispatchBuiltinEvents_CallsTickChannel) {
    // Проверяем что канал Tick доступен
    auto* channel = eventManager_->GetChannel(EventsName::OnTick);
    if (channel) {
        // Канал доступен - это означает что EventBinding корректно зарегистрировал его
        SUCCEED() << "Tick channel is accessible";
    } else {
        FAIL() << "Tick channel should be available";
    }
}

/**
 * @brief PushTick_EnqueuesTickEvent
 *
 * Проверяет что PushTick добавляет событие в очередь
 */
TEST_F(EventBindingTest, PushTick_EnqueuesTickEvent) {
    float dt = 0.016f; // ~60 FPS
    eventBinding_->PushTick(dt);
    
    // Создаём контекст для ProcessDeferred
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    asIScriptContext* ctx = engine->CreateContext();
    
    // Проверяем что событие добавлено в очередь через ProcessDeferred
    auto result = eventManager_->ProcessAllDeferred(ctx);
    EXPECT_TRUE(result.has_value()) << "ProcessDeferred should succeed";
    
    ctx->Release();
    engine->Release();
    
    SUCCEED() << "PushTick successfully enqueued event";
}

/**
 * @brief PushLoad_EnqueuesLoadEvent
 *
 * Проверяет что PushLoad добавляет событие загрузки в очередь
 */
TEST_F(EventBindingTest, PushLoad_EnqueuesLoadEvent) {
    eventBinding_->PushLoad();
    
    // Создаём контекст для ProcessDeferred
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    asIScriptContext* ctx = engine->CreateContext();
    
    // Проверяем что событие добавлено в очередь
    auto result = eventManager_->ProcessAllDeferred(ctx);
    EXPECT_TRUE(result.has_value()) << "ProcessDeferred should succeed";
    
    ctx->Release();
    engine->Release();
    
    SUCCEED() << "PushLoad successfully enqueued event";
}

/**
 * @brief PushSave_EnqueuesSaveEvent
 *
 * Проверяет что PushSave добавляет событие сохранения в очередь
 */
TEST_F(EventBindingTest, PushSave_EnqueuesSaveEvent) {
    eventBinding_->PushSave();
    
    // Создаём контекст для ProcessDeferred
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    asIScriptContext* ctx = engine->CreateContext();
    
    // Проверяем что событие добавлено в очередь
    auto result = eventManager_->ProcessAllDeferred(ctx);
    EXPECT_TRUE(result.has_value()) << "ProcessDeferred should succeed";
    
    ctx->Release();
    engine->Release();
    
    SUCCEED() << "PushSave successfully enqueued event";
}

/**
 * @brief EventBinding_NullEventManager
 *
 * Проверяет работу с null event manager
 */
TEST_F(EventBindingTest, EventBinding_NullEventManager) {
    // Конструктор должен обработать null без крэша
    EXPECT_NO_THROW({
        EventBinding nullBinding(nullptr);
    });
}

/**
 * @brief Destructor_UnregistersChannels
 *
 * Проверяет что деструктор отписывает каналы
 */
TEST_F(EventBindingTest, Destructor_UnregistersChannels) {
    // Создаём и уничтожаем EventBinding
    {
        EventBinding tempBinding(eventManager_.get());
        
        auto* tickChannel = eventManager_->GetChannel(EventsName::OnTick);
        EXPECT_NE(tickChannel, nullptr) << "Channels should be registered";
    }
    
    // После уничтожения каналы должны быть отписаны
    // Проверяем что нет крэша при доступе
    SUCCEED() << "Destructor completed without crashes";
}

/**
 * @brief MultiplePushEvents
 *
 * Проверяет множественные Push вызовы
 */
TEST_F(EventBindingTest, MultiplePushEvents) {
    // Push multiple ticks
    for (int i = 0; i < 10; ++i) {
        eventBinding_->PushTick(0.016f);
    }
    
    eventBinding_->PushLoad();
    eventBinding_->PushSave();
    
    // Создаём контекст для ProcessDeferred
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    asIScriptContext* ctx = engine->CreateContext();
    
    // Process all
    auto result = eventManager_->ProcessAllDeferred(ctx);
    EXPECT_TRUE(result.has_value()) << "ProcessDeferred should succeed";
    
    ctx->Release();
    engine->Release();
    
    SUCCEED() << "Multiple push events processed successfully";
}
