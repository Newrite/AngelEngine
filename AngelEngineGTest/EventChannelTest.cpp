#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"
#include "Mocks.h"

#include <EASTL/unique_ptr.h>

// Forward declare test function
static void TestDispatcher();

// Import AngelEngine modules
import AngelEngine.EventChannel;
import AngelEngine.EventManager;
import AngelEngine.Interfaces;
import AngelEngine.Errors;
import AngelEngine.Types;

using namespace AngelEngine;
using namespace AngelEngine::Test;

// Test function definition
static void TestDispatcher() {}

/**
 * @brief Фикстура для тестов EventChannel
 */
class EventChannelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        eventManager_ = eastl::make_unique<EventManager>();
    }
    
    void TearDown() override
    {
        eventManager_.reset();
    }
    
    eastl::unique_ptr<EventManager> eventManager_;
};

/**
 * @brief Тесты для RegisterChannel
 */
class RegisterChannelTest : public EventChannelTest
{
};

TEST_F(RegisterChannelTest, RegisterChannel_Success)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    auto result = eventManager_->RegisterChannel(1, &channel);
    
    EXPECT_TRUE(result.has_value());
}

TEST_F(RegisterChannelTest, RegisterChannel_Duplicate)
{
    EventChannel channel1(ChannelDescriptor{.eventName = "Test1", .funcdefDecl = "void Test1()", .callbackType = "TestCallback1", .argDecl = "", .dispatchArgs = ""});
    EventChannel channel2(ChannelDescriptor{.eventName = "Test2", .funcdefDecl = "void Test2()", .callbackType = "TestCallback2", .argDecl = "", .dispatchArgs = ""});
    
    auto result1 = eventManager_->RegisterChannel(1, &channel1);
    EXPECT_TRUE(result1.has_value());
    
    auto result2 = eventManager_->RegisterChannel(1, &channel2);
    
    // Ожидаем ошибку ChannelAlreadyRegistered
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), EventError::ChannelAlreadyRegistered);
}

TEST_F(RegisterChannelTest, RegisterChannel_MultipleChannels)
{
    EventChannel channel1(ChannelDescriptor{.eventName = "Test1", .funcdefDecl = "void Test1()", .callbackType = "TestCallback1", .argDecl = "", .dispatchArgs = ""});
    EventChannel channel2(ChannelDescriptor{.eventName = "Test2", .funcdefDecl = "void Test2()", .callbackType = "TestCallback2", .argDecl = "", .dispatchArgs = ""});
    EventChannel channel3(ChannelDescriptor{.eventName = "Test3", .funcdefDecl = "void Test3()", .callbackType = "TestCallback3", .argDecl = "", .dispatchArgs = ""});
    
    EXPECT_TRUE(eventManager_->RegisterChannel(1, &channel1).has_value());
    EXPECT_TRUE(eventManager_->RegisterChannel(2, &channel2).has_value());
    EXPECT_TRUE(eventManager_->RegisterChannel(3, &channel3).has_value());
}

/**
 * @brief Тесты для GetChannel
 */
class GetChannelTest : public EventChannelTest
{
};

TEST_F(GetChannelTest, GetChannel_Existing)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    eventManager_->RegisterChannel(42, &channel);
    
    IEventChannel* retrieved = eventManager_->GetChannel(42);
    
    EXPECT_EQ(retrieved, &channel);
}

TEST_F(GetChannelTest, GetChannel_NonExisting)
{
    IEventChannel* retrieved = eventManager_->GetChannel(999);
    
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(GetChannelTest, GetChannel_AfterUnregister)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    eventManager_->RegisterChannel(42, &channel);
    eventManager_->UnregisterChannel(42);
    
    IEventChannel* retrieved = eventManager_->GetChannel(42);
    
    EXPECT_EQ(retrieved, nullptr);
}

/**
 * @brief Тесты для UnregisterChannel
 */
class UnregisterChannelTest : public EventChannelTest
{
};

TEST_F(UnregisterChannelTest, UnregisterChannel_Existing)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    eventManager_->RegisterChannel(42, &channel);
    
    EXPECT_NO_THROW(eventManager_->UnregisterChannel(42));
    
    IEventChannel* retrieved = eventManager_->GetChannel(42);
    EXPECT_EQ(retrieved, nullptr);
}

TEST_F(UnregisterChannelTest, UnregisterChannel_NonExisting)
{
    // Не должно вызывать ошибку
    EXPECT_NO_THROW(eventManager_->UnregisterChannel(999));
}

/**
 * @brief Тесты для ProcessAllDeferred
 */
class ProcessAllDeferredTest : public EventChannelTest
{
};

TEST_F(ProcessAllDeferredTest, ProcessAllDeferred_NoChannels)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    asIScriptContext* ctx = engine->CreateContext();
    
    auto result = eventManager_->ProcessAllDeferred(ctx);
    
    EXPECT_TRUE(result.has_value());
    
    ctx->Release();
    engine->ShutDownAndRelease();
}

TEST_F(ProcessAllDeferredTest, ProcessAllDeferred_NullContext)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    eventManager_->RegisterChannel(1, &channel);

    auto result = eventManager_->ProcessAllDeferred(nullptr);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), EventError::ContextPreparationFailed);
}

/**
 * @brief Тесты для ClearAll
 */
class ClearAllTest : public EventChannelTest
{
};

TEST_F(ClearAllTest, ClearAll_EmptyManager)
{
    EXPECT_NO_THROW(eventManager_->ClearAll());
}

TEST_F(ClearAllTest, ClearAll_WithChannels)
{
    EventChannel channel1(ChannelDescriptor{.eventName = "Test1", .funcdefDecl = "void Test1()", .callbackType = "TestCallback1", .argDecl = "", .dispatchArgs = ""});
    EventChannel channel2(ChannelDescriptor{.eventName = "Test2", .funcdefDecl = "void Test2()", .callbackType = "TestCallback2", .argDecl = "", .dispatchArgs = ""});
    
    eventManager_->RegisterChannel(1, &channel1);
    eventManager_->RegisterChannel(2, &channel2);
    
    EXPECT_NO_THROW(eventManager_->ClearAll());
    
    // ClearAll очищает содержимое каналов, но не unregister
    // Каналы всё ещё зарегистрированы
    EXPECT_NE(eventManager_->GetChannel(1), nullptr);
    EXPECT_NE(eventManager_->GetChannel(2), nullptr);
}

/**
 * @brief Тесты для GetAllDescriptors
 */
class GetAllDescriptorsTest : public EventChannelTest
{
};

TEST_F(GetAllDescriptorsTest, GetAllDescriptors_Empty)
{
    auto descriptors = eventManager_->GetAllDescriptors();
    
    EXPECT_TRUE(descriptors.empty());
}

TEST_F(GetAllDescriptorsTest, GetAllDescriptors_WithChannels)
{
    // Создаём канал с дескриптором
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    channel.SetDispatcherFn(nullptr, nullptr, nullptr);
    
    eventManager_->RegisterChannel(1, &channel);
    
    auto descriptors = eventManager_->GetAllDescriptors();
    
    EXPECT_EQ(descriptors.size(), 1u);
}

/**
 * @brief Тесты для EventChannel
 */
TEST_F(EventChannelTest, GetDescriptor)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    auto desc = channel.GetDescriptor();

    EXPECT_EQ(desc.eventName, "Test");
}

TEST_F(EventChannelTest, Clear_EmptyChannel)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    EXPECT_NO_THROW(channel.Clear());
}

TEST_F(EventChannelTest, WarmupJIT_NoFunction)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    // WarmupJIT без установленной функции не должен вызывать ошибку
    EXPECT_NO_THROW(channel.WarmupJIT());
}

TEST_F(EventChannelTest, SetDispatcherFn)
{
    asIScriptEngine* engine = asCreateScriptEngine();
    ASSERT_NE(engine, nullptr);
    
    // Создаём простой модуль с функцией
    asIScriptModule* mod = engine->GetModule("test", asGM_ALWAYS_CREATE);
    ASSERT_NE(mod, nullptr);
    
    // Регистрируем функцию
    int r = engine->RegisterGlobalFunction("void TestDispatcher()", asFUNCTION(TestDispatcher), asCALL_CDECL);
    EXPECT_GE(r, 0);
    
    asIScriptFunction* fn = mod->GetFunctionByDecl("void TestDispatcher()");
    
    class MockPool : public IContextPooling
    {
    public:
        MOCK_METHOD(ContextPtr, RequestContext, (asIScriptEngine*, void*), (override));
        MOCK_METHOD(void, ReturnContext, (asIScriptEngine*, asIScriptContext*, void*), (override));
    };
    
    MockPool pool;
    
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    EXPECT_NO_THROW(channel.SetDispatcherFn(engine, &pool, fn));
    
    engine->ShutDownAndRelease();
}

/**
 * @brief Тесты для ProcessDeferred
 */
class ProcessDeferredTest : public EventChannelTest
{
};

TEST_F(ProcessDeferredTest, ProcessDeferred_EmptyChannel)
{
    EventChannel channel(ChannelDescriptor{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""});
    auto result = channel.ProcessDeferred();
    
    EXPECT_TRUE(result.has_value());
}

/**
 * @brief Параметризированные тесты для различных EventID
 */
class EventIdTest : public ::testing::TestWithParam<uint32_t>
{
protected:
    void SetUp() override
    {
        eventManager_ = eastl::make_unique<EventManager>();
    }
    
    void TearDown() override
    {
        eventManager_.reset();
    }
    
    eastl::unique_ptr<EventManager> eventManager_;
};

TEST_P(EventIdTest, RegisterAndGetChannel)
{
    uint32_t eventId = GetParam();
    ChannelDescriptor desc{.eventName = "Test", .funcdefDecl = "void Test()", .callbackType = "TestCallback", .argDecl = "", .dispatchArgs = ""};
    EventChannel channel(desc);
    
    auto result = eventManager_->RegisterChannel(eventId, &channel);
    EXPECT_TRUE(result.has_value());
    
    IEventChannel* retrieved = eventManager_->GetChannel(eventId);
    EXPECT_EQ(retrieved, &channel);
}

INSTANTIATE_TEST_SUITE_P(
    VariousEventIds,
    EventIdTest,
    ::testing::Values(0, 1, 42, 100, 1000, 65535, 0xFFFFFFFF)
);
