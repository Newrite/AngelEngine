#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <EASTL/vector.h>
#include <angelscript.h>

// Import AngelEngine modules
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;
import AngelEngine.Types;
import AngelEngine.Errors;
import AngelEngine.Logger;
import AngelEngine.BindingManager;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для BindingManager
 *
 * Проверяют:
 * - RegisterStandardAddons
 * - BindAll
 * - Bind
 * - AddBinding
 * - Обработку ошибок (null engine, null binding)
 */
class BindingManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Создаём минимальный движок для тестов
        engine_ = asCreateScriptEngine(ANGELSCRIPT_VERSION);
        ASSERT_NE(engine_, nullptr);
    }

    void TearDown() override
    {
        if (engine_)
        {
            engine_->Release();
            engine_ = nullptr;
        }
    }

    asIScriptEngine* engine_ = nullptr;
    BindingManager bindingManager_;
};

/**
 * @brief RegisterStandardAddons_Success
 *
 * Проверяет успешную регистрацию стандартных аддонов
 */
TEST_F(BindingManagerTest, RegisterStandardAddons_Success) {
    auto result = bindingManager_.RegisterStandardAddons(engine_);
    
    EXPECT_TRUE(result.has_value()) << "RegisterStandardAddons should succeed";
}

/**
 * @brief RegisterStandardAddons_NullEngine
 *
 * Проверяет обработку null engine
 */
TEST_F(BindingManagerTest, RegisterStandardAddons_NullEngine) {
    auto result = bindingManager_.RegisterStandardAddons(nullptr);
    
    EXPECT_FALSE(result.has_value()) << "Should fail with null engine";
    EXPECT_EQ(result.error(), BindingError::EngineIsNull);
}

/**
 * @brief BindAll_Success
 *
 * Проверяет успешную привязку всех bindings
 */
TEST_F(BindingManagerTest, BindAll_Success) {
    // Сначала регистрируем стандартные аддоны
    auto regResult = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(regResult.has_value());
    
    // BindAll без custom bindings должен succeed
    auto result = bindingManager_.BindAll(engine_);
    
    EXPECT_TRUE(result.has_value()) << "BindAll should succeed";
}

/**
 * @brief BindAll_NullEngine
 *
 * Проверяет обработку null engine в BindAll
 */
TEST_F(BindingManagerTest, BindAll_NullEngine) {
    auto result = bindingManager_.BindAll(nullptr);
    
    EXPECT_FALSE(result.has_value()) << "Should fail with null engine";
    EXPECT_EQ(result.error(), BindingError::EngineIsNull);
}

/**
 * @brief Bind_Success
 *
 * Проверяет успешную привязку одного binding
 */
TEST_F(BindingManagerTest, Bind_Success) {
    // Сначала регистрируем стандартные аддоны
    auto regResult = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(regResult.has_value());
    
    // Создаём тестовый binding
    class TestBinding : public IScriptBinding {
    public:
        void Bind(asIScriptEngine* engine) override {
            // Пустая реализация для теста
        }
    };
    
    TestBinding testBinding;
    auto result = bindingManager_.Bind(engine_, &testBinding);
    
    EXPECT_TRUE(result.has_value()) << "Bind should succeed";
}

/**
 * @brief Bind_NullBinding
 *
 * Проверяет обработку null binding
 */
TEST_F(BindingManagerTest, Bind_NullBinding) {
    auto result = bindingManager_.Bind(engine_, nullptr);
    
    EXPECT_FALSE(result.has_value()) << "Should fail with null binding";
    EXPECT_EQ(result.error(), BindingError::BindingIsNull);
}

/**
 * @brief Bind_NullEngine
 *
 * Проверяет обработку null engine в Bind
 */
TEST_F(BindingManagerTest, Bind_NullEngine) {
    class TestBinding : public IScriptBinding {
    public:
        void Bind(asIScriptEngine* engine) override {}
    };
    
    TestBinding testBinding;
    auto result = bindingManager_.Bind(nullptr, &testBinding);
    
    EXPECT_FALSE(result.has_value()) << "Should fail with null engine";
    EXPECT_EQ(result.error(), BindingError::EngineIsNull);
}

/**
 * @brief AddBinding_Success
 *
 * Проверяет добавление binding
 */
TEST_F(BindingManagerTest, AddBinding_Success) {
    class TestBinding : public IScriptBinding {
    public:
        void Bind(asIScriptEngine* engine) override {}
    };
    
    TestBinding testBinding;
    bindingManager_.AddBinding(&testBinding);
    
    // Добавление должно пройти успешно (метод void, нет явной проверки)
    // Проверяем что BindAll работает с добавленным binding
    auto regResult = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(regResult.has_value());
    
    auto result = bindingManager_.BindAll(engine_);
    EXPECT_TRUE(result.has_value()) << "BindAll with added binding should succeed";
}

/**
 * @brief AddBinding_Null
 *
 * Проверяет обработку null binding в AddBinding
 */
TEST_F(BindingManagerTest, AddBinding_Null) {
    // Метод просто возвращает, не должно быть крэша
    EXPECT_NO_THROW(bindingManager_.AddBinding(nullptr));
}

/**
 * @brief BindAll_WithMultipleBindings
 *
 * Проверяет привязку нескольких bindings
 */
TEST_F(BindingManagerTest, BindAll_WithMultipleBindings) {
    // Сначала регистрируем стандартные аддоны
    auto regResult = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(regResult.has_value());
    
    // Добавляем несколько test bindings
    class TestBinding1 : public IScriptBinding {
    public: void Bind(asIScriptEngine* engine) override {}
    };
    class TestBinding2 : public IScriptBinding {
    public: void Bind(asIScriptEngine* engine) override {}
    };
    class TestBinding3 : public IScriptBinding {
    public: void Bind(asIScriptEngine* engine) override {}
    };
    
    TestBinding1 binding1;
    TestBinding2 binding2;
    TestBinding3 binding3;
    
    bindingManager_.AddBinding(&binding1);
    bindingManager_.AddBinding(&binding2);
    bindingManager_.AddBinding(&binding3);
    
    auto result = bindingManager_.BindAll(engine_);
    EXPECT_TRUE(result.has_value()) << "BindAll with multiple bindings should succeed";
}

/**
 * @brief RegisterStandardAddons_VerifyArray
 *
 * Проверяет что array аддон действительно зарегистрировался
 */
TEST_F(BindingManagerTest, RegisterStandardAddons_VerifyArray) {
    auto result = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(result.has_value());
    
    // Проверяем что тип array доступен
    int typeId = engine_->GetTypeIdByDecl("array<int>");
    EXPECT_GE(typeId, 0) << "array<int> should be registered";
}

/**
 * @brief RegisterStandardAddons_VerifyString
 *
 * Проверяет что string аддон действительно зарегистрировался
 */
TEST_F(BindingManagerTest, RegisterStandardAddons_VerifyString) {
    auto result = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(result.has_value());
    
    // Проверяем что тип string доступен
    int typeId = engine_->GetTypeIdByDecl("string");
    EXPECT_GE(typeId, 0) << "string should be registered";
}

/**
 * @brief RegisterStandardAddons_VerifyDictionary
 *
 * Проверяет что dictionary аддон действительно зарегистрировался
 */
TEST_F(BindingManagerTest, RegisterStandardAddons_VerifyDictionary) {
    auto result = bindingManager_.RegisterStandardAddons(engine_);
    ASSERT_TRUE(result.has_value());
    
    // Проверяем что тип dictionary доступен
    int typeId = engine_->GetTypeIdByDecl("dictionary");
    EXPECT_GE(typeId, 0) << "dictionary should be registered";
}
