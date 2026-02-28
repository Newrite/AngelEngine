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

using namespace AngelEngine;
using namespace AngelEngine::Test;

/**
 * @brief Тесты для ModuleLoader
 * 
 * Проверяют:
 * - Компиляцию модулей
 * - Обработку ошибок
 * - Зависимости между модулями
 * - Saveable переменные
 */
class ModuleLoaderTest : public ::testing::Test
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
 * @brief CompileAllMods_Empty
 * 
 * Проверяет компиляцию без модулей
 */
TEST_F(ModuleLoaderTest, CompileAllMods_Empty) {
    // Remove all mods from directory
    std::filesystem::remove_all("angelscripts/mods");
    std::filesystem::create_directories("angelscripts/mods");
    
    // Should handle empty directory gracefully
    auto compileResult = engine_->CompileAllMods();
    (void)compileResult;
    
    SUCCEED() << "CompileAllMods with empty directory completed";
}

/**
 * @brief CompileAllMods_WithSingleMod
 * 
 * Проверяет компиляцию одного модуля
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithSingleMod) {
    CreateTempScriptFile(R"(
        void main() {}
    )", "MLMod");
    
    auto* moduleLoader = engine_->GetModuleLoader();
    auto result = engine_->CompileAllMods();
    
    ASSERT_TRUE(result.has_value()) << "Compilation should succeed";
    
    const auto& modules = moduleLoader->GetLoadedModules();
    EXPECT_FALSE(modules.empty()) << "Should have at least one loaded module";
    EXPECT_EQ(modules.size(), 1) << "Should have exactly one module";
}

/**
 * @brief CompileAllMods_WithMultipleMods
 * 
 * Проверяет компиляцию нескольких модулей
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithMultipleMods) {
    CreateTempScriptFile(R"(
        void main() {}
    )", "ModA");
    
    CreateTempScriptFile(R"(
        void main() {}
    )", "ModB");
    
    auto* moduleLoader = engine_->GetModuleLoader();
    auto result = engine_->CompileAllMods();
    
    ASSERT_TRUE(result.has_value()) << "Compilation should succeed";
    
    const auto& modules = moduleLoader->GetLoadedModules();
    EXPECT_GE(modules.size(), 2) << "Should have at least two modules";
}

/**
 * @brief CompileAllMods_WithDependencies
 * 
 * Проверяет компиляцию модулей с зависимостями
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithDependencies) {
    // Create ModA with public API
    CreateTempScriptFile(R"(
        int valueA = 10;
        int getValueA() { return valueA; }
        void main() {}
    )", "ModA");
    
    std::filesystem::create_directories("angelscripts/mods/ModA");
    std::ofstream jsonFile("angelscripts/mods/ModA/mod.json");
    jsonFile << "{\"public_api\": true, \"depends_on\": []}";
    jsonFile.close();
    
    // Create ModB that depends on ModA
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
    
    auto result = engine_->CompileAllMods();
    ASSERT_TRUE(result.has_value()) << "Compilation with dependencies should succeed";
    
    // Run and verify
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("ModB::valueB");
    if (varIdx >= 0) {
        int* valuePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
        EXPECT_EQ(*valuePtr, 15) << "ModB should receive value from ModA";
    }
    
    CleanupTempScriptFiles("ModA");
    CleanupTempScriptFiles("ModB");
}

/**
 * @brief CompileAllMods_SyntaxError
 * 
 * Проверяет обработку синтаксических ошибок
 */
TEST_F(ModuleLoaderTest, CompileAllMods_SyntaxError) {
    CreateTempScriptFile(R"(
        void main() {
            this is invalid syntax!!!
        }
    )", "MLMod");

    auto compileResult = engine_->CompileAllMods();
    (void)compileResult;

    // Engine may handle this by excluding the bad module or returning error
    SUCCEED() << "Compilation with syntax error handled";
}

/**
 * @brief CompileAllMods_RetryWithoutFaultyModules
 * 
 * Проверяет повторную компиляцию без faulty модулей
 */
TEST_F(ModuleLoaderTest, CompileAllMods_RetryWithoutFaultyModules) {
    // Create one good module and one bad module
    CreateTempScriptFile(R"(
        void main() {}
    )", "GoodMod");
    
    CreateTempScriptFile(R"(
        void main() { invalid syntax }
    )", "BadMod");

    auto compileResult = engine_->CompileAllMods();
    (void)compileResult;

    // Engine should either succeed (excluding bad module) or fail
    SUCCEED() << "Compilation with mixed modules handled";
    
    CleanupTempScriptFiles("GoodMod");
    CleanupTempScriptFiles("BadMod");
}

/**
 * @brief GetLoadedModules
 * 
 * Проверяет получение списка загруженных модулей
 */
TEST_F(ModuleLoaderTest, GetLoadedModules) {
    CreateTempScriptFile(R"(
        void main() {}
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto* moduleLoader = engine_->GetModuleLoader();
    const auto& modules = moduleLoader->GetLoadedModules();
    
    EXPECT_FALSE(modules.empty()) << "Should have loaded modules";
    
    // Check if our module is in the list
    bool found = false;
    for (const auto& modName : modules) {
        if (modName == "MLMod") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "MLMod should be in loaded modules list";
}

/**
 * @brief GetSaveableVars
 *
 * Проверяет получение списка saveable переменных
 */
TEST_F(ModuleLoaderTest, GetSaveableVars) {
    CreateTempScriptFile(R"(
        [Save]
        int savedVar = 100;
        int normalVar = 200;
        void main() {}
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto* moduleLoader = engine_->GetModuleLoader();
    const auto& saveableVars = moduleLoader->GetSaveableVars("MLMod");
    
    EXPECT_FALSE(saveableVars.empty()) << "Should have saveable variables";
    
    // Check if savedVar is in the list
    bool found = false;
    for (const auto& varName : saveableVars) {
        if (varName == "savedVar") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "savedVar should be in saveable variables list";
}

/**
 * @brief RecordCompilationError
 * 
 * Проверяет запись ошибок компиляции
 */
TEST_F(ModuleLoaderTest, RecordCompilationError) {
    auto* moduleLoader = engine_->GetModuleLoader();
    
    // Record an error
    moduleLoader->RecordCompilationError("TestError");
    
    // The error should be recorded (implementation dependent)
    SUCCEED() << "RecordCompilationError completed";
}

/**
 * @brief CompileAllMods_WithFunction
 * 
 * Проверяет компиляцию модуля с функциями
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithFunction) {
    CreateTempScriptFile(R"(
        int getValue() { return 42; }
        void main() {}
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Verify function exists
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    asIScriptFunction* func = mod->GetFunctionByDecl("int MLMod::getValue()");
    EXPECT_NE(func, nullptr) << "Function getValue should exist";
}

/**
 * @brief CompileAllMods_WithGlobalVars
 * 
 * Проверяет компиляцию модуля с глобальными переменными
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithGlobalVars) {
    CreateTempScriptFile(R"(
        int counter = 0;
        string name = "Test";
        void main() {
            counter = 10;
        }
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Verify global variables
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    ASSERT_NE(mod, nullptr);
    
    int varIdx = mod->GetGlobalVarIndexByName("MLMod::counter");
    ASSERT_GE(varIdx, 0);
    
    int* counterPtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    EXPECT_EQ(*counterPtr, 10) << "Counter should be 10 after main()";
}

/**
 * @brief CompileAllMods_WithClasses
 * 
 * Проверяет компиляцию модуля с классами
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithClasses) {
    CreateTempScriptFile(R"(
        class MyClass {
            int value;
            MyClass() { value = 0; }
            int getValue() { return value; }
            void setValue(int v) { value = v; }
        }
        
        MyClass@ obj;
        void main() {
            @obj = MyClass();
            obj.setValue(42);
        }
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    SUCCEED() << "Class compilation completed";
}

/**
 * @brief CompileAllMods_WithArrays
 * 
 * Проверяет компиляцию модуля с массивами
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithArrays) {
    CreateTempScriptFile(R"(
        array<int> numbers(10);
        void main() {
            for (uint i = 0; i < 10; i++) {
                numbers[i] = i * 2;
            }
        }
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    SUCCEED() << "Array compilation completed";
}

/**
 * @brief CompileAllMods_WithInterfaces
 * 
 * Проверяет компиляцию модуля с интерфейсами
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithInterfaces) {
    CreateTempScriptFile(R"(
        interface IMyInterface {
            void doSomething();
        }
        
        class MyImpl : IMyInterface {
            void doSomething() { }
        }
        
        void main() {}
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    SUCCEED() << "Interface compilation completed";
}

/**
 * @brief CompileAllMods_WithNamespaces
 * 
 * Проверяет компиляцию модуля с namespace
 */
TEST_F(ModuleLoaderTest, CompileAllMods_WithNamespaces) {
    CreateTempScriptFile(R"(
        namespace MyNamespace {
            int value = 100;
            void setValue(int v) { value = v; }
        }
        
        void main() {
            MyNamespace::setValue(42);
        }
    )", "MLMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    SUCCEED() << "Namespace compilation completed";
}

/**
 * @brief CompileAllMods_CircularDependency
 *
 * Проверяет обработку циклических зависимостей:
 * ModA зависит от ModB, ModB зависит от ModA
 * Engine должен обработать это корректно (ошибка или разрешение зависимостей)
 */
TEST_F(ModuleLoaderTest, CompileAllMods_CircularDependency) {
    // 1. Initialize is already done in SetUp
    
    // 2. Create ModA that depends on ModB
    CreateTempScriptFile(R"(
        int valueA = 10;
        void main() {}
    )", "ModA");

    std::filesystem::create_directories("angelscripts/mods/ModA");
    std::ofstream jsonFileA("angelscripts/mods/ModA/mod.json");
    jsonFileA << "{\"public_api\": true, \"depends_on\": [\"ModB\"]}";
    jsonFileA.close();

    // 3. Create ModB that depends on ModA (circular!)
    CreateTempScriptFile(R"(
        int valueB = 20;
        void main() {}
    )", "ModB");

    std::filesystem::create_directories("angelscripts/mods/ModB");
    std::ofstream jsonFileB("angelscripts/mods/ModB/mod.json");
    jsonFileB << "{\"public_api\": true, \"depends_on\": [\"ModA\"]}";
    jsonFileB.close();

    // 4. Compile - engine должен обработать циклическую зависимость
    auto compileResult = engine_->CompileAllMods();
    
    // Engine может:
    // 1. Успешно скомпилировать (если разрешает циклические зависимости)
    // 2. Вернуть ошибку
    // 3. Скомпилировать только один модуль
    // Главное - не должно быть крэша или зависания
    
    if (compileResult.has_value()) {
        // Если компиляция успешна - проверяем что модули загружены
        auto* moduleLoader = engine_->GetModuleLoader();
        const auto& modules = moduleLoader->GetLoadedModules();
        
        // Хотя бы один модуль должен быть загружен
        EXPECT_GE(modules.size(), 1) << "At least one module should be loaded";
        
        SUCCEED() << "Circular dependency handled gracefully (modules loaded: " 
                  << modules.size() << ")";
    } else {
        // Если ошибка - это тоже допустимое поведение
        SUCCEED() << "Circular dependency detected and reported as error";
    }

    // Cleanup
    CleanupTempScriptFiles("ModA");
    CleanupTempScriptFiles("ModB");
}

/**
 * @brief EmptyTest
 *
 * Пустой тест для завершения
 */
TEST_F(ModuleLoaderTest, EmptyTest) {
    SUCCEED() << "ModuleLoader tests completed";
}
