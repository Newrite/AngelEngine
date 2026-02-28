#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <EASTL/vector.h>
#include <angelscript.h>
#include <filesystem>
#include <fstream>
#include <sstream>

// Import AngelEngine modules
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;
import AngelEngine.Types;
import AngelEngine.Logger;
import AngelEngine.PredefinedGenerator;
import AngelEngine.EventsInterfaces;
import AngelEngine.ScriptEngine;

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для PredefinedGenerator
 *
 * Проверяют:
 * - GenerateScriptPredefined
 * - Генерацию enums, classes, event dispatcher API
 */
class PredefinedGeneratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Создаём тестовый движок
        engine_ = asCreateScriptEngine(ANGELSCRIPT_VERSION);
        ASSERT_NE(engine_, nullptr);
        
        // Создаём временную директорию для выходных файлов
        outputDir_ = std::filesystem::current_path() / "temp_predefined";
        std::filesystem::create_directories(outputDir_);
        
        outputPath_ = outputDir_ / "as.predefined";
    }

    void TearDown() override
    {
        if (engine_) {
            engine_->Release();
            engine_ = nullptr;
        }
        
        // Очищаем временные файлы
        if (std::filesystem::exists(outputDir_)) {
            std::filesystem::remove_all(outputDir_);
        }
    }

    asIScriptEngine* engine_ = nullptr;
    std::filesystem::path outputDir_;
    std::filesystem::path outputPath_;
};

/**
 * @brief GenerateScriptPredefined_CreatesFile
 *
 * Проверяет что генерация создаёт файл
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_CreatesFile) {
    // Сначала инициализируем engine с типами через ScriptEngine
    EngineConfig config;
    config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
    config.enableUseJIT = false;
    config.enableWatchdog = false;
    config.enableAutoGC = false;
    
    auto factory = eastl::make_unique<StandardComponentFactory>(config);
    auto result = ScriptEngine::MakeEngine(eastl::move(factory));
    ASSERT_TRUE(result.has_value());
    auto scriptEngine = eastl::move(result.value());
    
    auto initResult = scriptEngine->InitializeEngine();
    ASSERT_TRUE(initResult.has_value()) << "Engine initialization failed";
    
    eastl::vector<AngelEngine::ChannelDescriptor> descriptors;
    
    AngelEngine::GenerateScriptPredefined(
        scriptEngine->GetEngine(), 
        outputPath_, 
        descriptors
    );
    
    // Проверяем что файл создан
    EXPECT_TRUE(std::filesystem::exists(outputPath_)) << "Predefined file should be created";
    
    // Проверяем что файл не пустой
    auto fileSize = std::filesystem::file_size(outputPath_);
    EXPECT_GT(fileSize, 0) << "Predefined file should not be empty";
}

/**
 * @brief GenerateScriptPredefined_EmptyDescriptors
 *
 * Проверяет генерацию с пустыми дескрипторами
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_EmptyDescriptors) {
    eastl::vector<ChannelDescriptor> descriptors;
    
    EXPECT_NO_THROW({
        GenerateScriptPredefined(engine_, outputPath_, descriptors);
    });
    
    // Файл должен быть создан
    EXPECT_TRUE(std::filesystem::exists(outputPath_)) 
        << "Predefined file should be created even with empty descriptors";
}

/**
 * @brief GenerateScriptPredefined_WithEventDescriptors
 *
 * Проверяет что генерация включает Event Dispatcher API
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_WithEventDescriptors) {
    // Создаём тестовые дескрипторы событий
    eastl::vector<ChannelDescriptor> descriptors;
    
    ChannelDescriptor tickDesc;
    tickDesc.eventName = "Tick";
    tickDesc.funcdefDecl = "funcdef void TickCallback(float dt)";
    tickDesc.callbackType = "TickCallback";
    descriptors.push_back(tickDesc);
    
    GenerateScriptPredefined(engine_, outputPath_, descriptors);
    
    // Читаем файл и проверяем наличие Event Dispatcher API
    std::ifstream ifs(outputPath_.string());
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    
    EXPECT_THAT(content, ::testing::HasSubstr("Event Dispatcher API")) 
        << "Generated file should include Event Dispatcher API section";
    EXPECT_THAT(content, ::testing::HasSubstr("SubscribeTick")) 
        << "Generated file should include SubscribeTick";
    EXPECT_THAT(content, ::testing::HasSubstr("UnsubscribeTick")) 
        << "Generated file should include UnsubscribeTick";
}

/**
 * @brief GenerateScriptPredefined_InvalidPath
 *
 * Проверяет обработку неверного пути
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_InvalidPath) {
    auto invalidPath = std::filesystem::path("Z:\\NonExistent\\Path\\as.predefined");
    eastl::vector<ChannelDescriptor> descriptors;
    
    // Не должно быть крэша, но файл не будет создан
    EXPECT_NO_THROW({
        GenerateScriptPredefined(engine_, invalidPath, descriptors);
    });
}

/**
 * @brief GenerateScriptPredefined_MultipleCalls
 *
 * Проверяет множественные вызовы генерации
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_MultipleCalls) {
    eastl::vector<ChannelDescriptor> descriptors;
    
    for (int i = 0; i < 3; ++i) {
        EXPECT_NO_THROW({
            GenerateScriptPredefined(engine_, outputPath_, descriptors);
        });
        
        EXPECT_TRUE(std::filesystem::exists(outputPath_)) 
            << "Predefined file should exist after call " << i;
    }
    
    SUCCEED() << "Multiple generation calls completed successfully";
}

/**
 * @brief GenerateScriptPredefined_MultipleEvents
 *
 * Проверяет генерацию с несколькими событиями
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_MultipleEvents) {
    eastl::vector<ChannelDescriptor> descriptors;
    
    ChannelDescriptor tickDesc;
    tickDesc.eventName = "Tick";
    tickDesc.funcdefDecl = "funcdef void TickCallback(float dt)";
    tickDesc.callbackType = "TickCallback";
    descriptors.push_back(tickDesc);
    
    ChannelDescriptor loadDesc;
    loadDesc.eventName = "Load";
    loadDesc.funcdefDecl = "funcdef void LoadCallback()";
    loadDesc.callbackType = "LoadCallback";
    descriptors.push_back(loadDesc);
    
    ChannelDescriptor saveDesc;
    saveDesc.eventName = "Save";
    saveDesc.funcdefDecl = "funcdef void SaveCallback()";
    saveDesc.callbackType = "SaveCallback";
    descriptors.push_back(saveDesc);
    
    GenerateScriptPredefined(engine_, outputPath_, descriptors);
    
    // Читаем файл и проверяем наличие всех событий
    std::ifstream ifs(outputPath_.string());
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    
    EXPECT_THAT(content, ::testing::HasSubstr("SubscribeTick")) 
        << "Should include SubscribeTick";
    EXPECT_THAT(content, ::testing::HasSubstr("SubscribeLoad")) 
        << "Should include SubscribeLoad";
    EXPECT_THAT(content, ::testing::HasSubstr("SubscribeSave")) 
        << "Should include SubscribeSave";
}

/**
 * @brief GenerateScriptPredefined_FileContentValid
 *
 * Проверяет что содержимое файла корректно
 */
TEST_F(PredefinedGeneratorTest, GenerateScriptPredefined_FileContentValid) {
    // Сначала инициализируем engine с типами
    EngineConfig config;
    config.scriptsPathMod = std::filesystem::current_path() / "angelscripts" / "mods";
    config.enableUseJIT = false;
    config.enableWatchdog = false;
    config.enableAutoGC = false;
    
    auto factory = eastl::make_unique<StandardComponentFactory>(config);
    auto result = ScriptEngine::MakeEngine(eastl::move(factory));
    ASSERT_TRUE(result.has_value());
    auto scriptEngine = eastl::move(result.value());
    
    auto initResult = scriptEngine->InitializeEngine();
    ASSERT_TRUE(initResult.has_value());
    
    eastl::vector<ChannelDescriptor> descriptors;
    
    GenerateScriptPredefined(scriptEngine->GetEngine(), outputPath_, descriptors);
    
    // Читаем файл
    std::ifstream ifs(outputPath_.string());
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();
    
    // Проверяем что файл содержит хоть какой-то контент
    EXPECT_GT(content.length(), 0) << "File should contain content";
    
    // Проверяем что нет явных ошибок в содержимом
    EXPECT_THAT(content, ::testing::Not(::testing::HasSubstr("error"))) 
        << "File should not contain error messages";
}
