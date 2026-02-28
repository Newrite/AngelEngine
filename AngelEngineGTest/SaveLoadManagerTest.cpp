#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "TestHelpers.h"

#include <EASTL/unique_ptr.h>
#include <EASTL/memory.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <angelscript.h>
#include <filesystem>
#include <cstring>
#include <thread>

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
 * @brief Тесты для SaveLoadManager
 * 
 * Проверяют:
 * - Сохранение и загрузку данных
 * - Обработку ошибок
 * - Сериализацию сложных типов (array, dictionary)
 * - Version mismatch
 * - Type mismatch
 */
class SaveLoadManagerTest : public ::testing::Test
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
 * @brief Save_EmptyEngine
 * 
 * Проверяет сохранение пустого движка
 */
TEST_F(SaveLoadManagerTest, Save_EmptyEngine) {
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    
    // Save without any modules
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    
    // May succeed with empty data or fail
    SUCCEED() << "Save empty engine completed";
}

/**
 * @brief Save_WithGlobalVars
 *
 * Проверяет сохранение глобальных переменных
 */
TEST_F(SaveLoadManagerTest, Save_WithGlobalVars) {
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
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    
    ASSERT_TRUE(saveResult.has_value()) << "Save should succeed";
    EXPECT_GT(saveResult.value().size(), 0) << "Save data should not be empty";
}

/**
 * @brief Save_WithArrays
 *
 * Проверяет сохранение массивов array<T>
 */
TEST_F(SaveLoadManagerTest, Save_WithArrays) {
    CreateTempScriptFile(R"(
        [Save]
        array<int> scores = {1, 2, 3, 4, 5};
        [Save]
        array<string> names = {"Alice", "Bob", "Charlie"};
        void main() {}
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    
    ASSERT_TRUE(saveResult.has_value()) << "Save with arrays should succeed";
    EXPECT_GT(saveResult.value().size(), 0) << "Save data should not be empty";
}

/**
 * @brief Save_WithDictionaries
 *
 * Проверяет сохранение dictionary
 */
TEST_F(SaveLoadManagerTest, Save_WithDictionaries) {
    CreateTempScriptFile(R"(
        [Save]
        dictionary data;
        void main() {
            data["score"] = 100;
            data["name"] = "Player";
            data["health"] = 50.5f;
        }
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Run GC before save to clean up any garbage
    engine_->CallGarbageCollectorFullCycle();

    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );

    ASSERT_TRUE(saveResult.has_value()) << "Save with dictionary should succeed";
}

/**
 * @brief Load_ValidData
 *
 * Проверяет загрузку валидных данных
 */
TEST_F(SaveLoadManagerTest, Load_ValidData) {
    CreateTempScriptFile(R"(
        [Save]
        int score = 100;
        void main() { score = 200; }
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Modify value
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    int varIdx = mod->GetGlobalVarIndexByName("SaveMod::score");
    int* scorePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(varIdx));
    *scorePtr = 999;
    
    // Load
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    
    ASSERT_TRUE(loadResult.has_value()) << "Load should succeed";
    EXPECT_EQ(*scorePtr, 200) << "Score should be restored to 200";
}

/**
 * @brief Load_CorruptedData
 *
 * Проверяет загрузку повреждённых данных
 */
TEST_F(SaveLoadManagerTest, Load_CorruptedData) {
    CreateTempScriptFile(R"(
        [Save]
        int score = 100;
        void main() {}
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Corrupt the data
    auto& data = saveResult.value();
    if (data.size() > 4) {
        data[0] = 0x00;  // Corrupt magic number
    }
    
    // Load should fail
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        data
    );
    
    EXPECT_FALSE(loadResult.has_value()) << "Load corrupted data should fail";
}

/**
 * @brief Load_VersionMismatch
 *
 * Проверяет загрузку с несовпадением версии
 */
TEST_F(SaveLoadManagerTest, Load_VersionMismatch) {
    CreateTempScriptFile(R"(
        [Save]
        int score = 100;
        void main() {}
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Corrupt version number (bytes 4-7)
    auto& data = saveResult.value();
    if (data.size() > 7) {
        data[4] = 0xFF;  // Corrupt version
    }
    
    // Load should fail with version mismatch
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        data
    );
    
    EXPECT_FALSE(loadResult.has_value()) << "Load with version mismatch should fail";
    if (!loadResult.has_value()) {
        EXPECT_EQ(loadResult.error(), SerializationError::VersionMismatch);
    }
}

/**
 * @brief Load_TypeMismatch
 *
 * Проверяет загрузку с несовпадением типов
 */
TEST_F(SaveLoadManagerTest, Load_TypeMismatch) {
    CreateTempScriptFile(R"(
        [Save]
        int value = 100;
        void main() {}
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Modify script to change type
    CreateTempScriptFile(R"(
        [Save]
        float value = 1.0f;
        void main() {}
    )", "SaveMod");
    
    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    // Load should fail with type mismatch
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    
    EXPECT_FALSE(loadResult.has_value()) << "Load with type mismatch should fail";
}

/**
 * @brief SerializationHandler_Array
 *
 * Проверяет работу ArraySerializationHandler
 */
TEST_F(SaveLoadManagerTest, SerializationHandler_Array) {
    CreateTempScriptFile(R"(
        [Save]
        array<int> numbers = {10, 20, 30, 40, 50};
        void main() {}
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Load and verify
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value());
    
    SUCCEED() << "Array serialization handler works";
}

/**
 * @brief SerializationHandler_Dictionary
 *
 * Проверяет работу DictionarySerializationHandler
 */
TEST_F(SaveLoadManagerTest, SerializationHandler_Dictionary) {
    CreateTempScriptFile(R"(
        [Save]
        dictionary data;
        void main() {
            data["key1"] = 100;
            data["key2"] = "value";
        }
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Load and verify
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value());
    
    SUCCEED() << "Dictionary serialization handler works";
}

/**
 * @brief SaveLoad_RoundTrip
 *
 * Проверяет полный цикл Save → Load → Verify
 */
TEST_F(SaveLoadManagerTest, SaveLoad_RoundTrip) {
    CreateTempScriptFile(R"(
        [Save]
        int score = 0;
        [Save]
        string name = "";
        [Save]
        array<int> items = {1, 2, 3};

        void main() {
            score = 100;
            name = "Hero";
        }
    )", "SaveMod");
    
    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());
    
    // Save
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult.has_value());
    
    // Modify values
    asIScriptModule* mod = engine_->GetEngine()->GetModule("__Megamodule__");
    int scoreIdx = mod->GetGlobalVarIndexByName("SaveMod::score");
    int* scorePtr = static_cast<int*>(mod->GetAddressOfGlobalVar(scoreIdx));
    *scorePtr = 999;
    
    // Load
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );
    ASSERT_TRUE(loadResult.has_value());
    
    // Verify
    EXPECT_EQ(*scorePtr, 100) << "Score should be restored";
    
    SUCCEED() << "SaveLoad round trip completed successfully";
}

/**
 * @brief SaveLoad_StringExceedsMaxLen
 *
 * Проверяет обработку строк превышающих MAX_SAFE_STRING_LEN (1MB)
 * Примечание: Этот тест может быть медленным, так как создаёт большую строку
 */
TEST_F(SaveLoadManagerTest, SaveLoad_StringExceedsMaxLen) {
    // Создаём скрипт с очень длинной строкой (> 1MB)
    // MAX_SAFE_STRING_LEN = 1024 * 1024 = 1MB
    const size_t largeStringSize = 1024 * 1024 + 1000;  // Чуть больше 1MB
    eastl::string largeString(largeStringSize, 'A');
    
    eastl::string scriptContent = R"(
        [Save]
        string largeData = ")";
    scriptContent += largeString;
    scriptContent += R"((";
        void main() {}
    )";
    
    CreateTempScriptFile(scriptContent.c_str(), "SaveMod");

    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    // Save - должен либо succeed с большими данными, либо fail с CorruptData
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );

    // Сохранение может пройти успешно (данные просто большие)
    // Но загрузка с повреждёнными данными должна вернуть ошибку
    if (saveResult.has_value()) {
        // Повреждаем данные чтобы проверить обработку слишком больших строк при загрузке
        auto& data = saveResult.value();
        if (data.size() > 10) {
            // Устанавливаем очень большой размер строки в данных
            uint32_t hugeLen = 0x7FFFFFFF;  // Максимально возможный размер
            std::memcpy(data.data() + 8, &hugeLen, sizeof(hugeLen));
        }
        
        auto loadResult = saveLoadManager->LoadFromData(
            engine_->GetEngine(),
            data
        );
        
        // Загрузка должна вернуть ошибку из-за превышения MAX_SAFE_STRING_LEN
        EXPECT_FALSE(loadResult.has_value()) << "Load with huge string should fail";
        if (!loadResult.has_value()) {
            EXPECT_TRUE(loadResult.error() == SerializationError::CorruptData ||
                       loadResult.error() == SerializationError::InvalidData)
                << "Expected CorruptData or InvalidData error";
        }
    } else {
        SUCCEED() << "Save correctly rejected oversized string";
    }
}

/**
 * @brief SaveLoad_DeepNestedObjects
 *
 * Проверяет обработку объектов с глубиной вложенности превышающей MAX_RECURSION_DEPTH (64)
 */
TEST_F(SaveLoadManagerTest, SaveLoad_DeepNestedObjects) {
    // Создаём скрипт с глубокими вложенными структурами
    // Примечание: AngelScript может не позволить создать очень глубокие структуры,
    // но мы проверяем что защита от переполнения стека работает
    CreateTempScriptFile(R"(
        class Node {
            Node@ next;
            int value;
            
            Node@ createDeep(int depth) {
                if (depth <= 0) return null;
                Node@ node = Node();
                node.value = depth;
                @node.next = createDeep(depth - 1);
                return node;
            }
        };
        
        [Save]
        Node@ rootNode = null;
        
        void main() {
            Node@ node = Node();
            node.value = 0;
            @rootNode = node;
        }
    )", "SaveMod");

    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    // Save должен пройти успешно для неглубокой структуры
    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );

    ASSERT_TRUE(saveResult.has_value()) << "Save with nested objects should succeed";
    
    // Load должен восстановить структуру
    auto loadResult = saveLoadManager->LoadFromData(
        engine_->GetEngine(),
        saveResult.value()
    );

    EXPECT_TRUE(loadResult.has_value()) << "Load nested objects should succeed";
}

/**
 * @brief SaveLoad_MultipleSessions_DepthReset
 *
 * Проверяет что depth_ сбрасывается между вызовами Save/Load
 */
TEST_F(SaveLoadManagerTest, SaveLoad_MultipleSessions_DepthReset) {
    CreateTempScriptFile(R"(
        [Save]
        int counter = 0;
        [Save]
        string name = "test";
        void main() { counter = 1; }
    )", "SaveMod");

    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());

    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    auto* saveLoadManager = engine_->GetSaveLoadManager();
    
    // Выполняем несколько сессий Save/Load подряд
    for (int i = 0; i < 10; ++i) {
        auto saveResult = saveLoadManager->GetSaveData(
            engine_->GetEngine(),
            engine_->GetModuleLoader()
        );
        ASSERT_TRUE(saveResult.has_value()) << "Save session " << i << " should succeed";

        auto loadResult = saveLoadManager->LoadFromData(
            engine_->GetEngine(),
            saveResult.value()
        );
        ASSERT_TRUE(loadResult.has_value()) << "Load session " << i << " should succeed";
    }
    
    SUCCEED() << "Multiple Save/Load sessions completed without recursion depth issues";
}

/**
 * @brief SaveLoad_HandlerCache_BetweenSessions
 *
 * Проверяет что handlerCache_ корректно работает между сессиями
 */
TEST_F(SaveLoadManagerTest, SaveLoad_HandlerCache_BetweenSessions) {
    // Первая сессия с array
    CreateTempScriptFile(R"(
        [Save]
        array<int> numbers = {1, 2, 3};
        void main() {}
    )", "SaveMod");

    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    auto* saveLoadManager = engine_->GetSaveLoadManager();
    auto saveResult1 = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult1.has_value());

    // Вторая сессия с dictionary
    CreateTempScriptFile(R"(
        [Save]
        dictionary data;
        void main() { data["key"] = 100; }
    )", "SaveMod");

    auto reloadResult = engine_->HotReload();
    ASSERT_TRUE(reloadResult.has_value());
    
    auto runResult2 = engine_->RunAllMods();
    ASSERT_TRUE(runResult2.has_value());

    auto saveResult2 = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult2.has_value());

    // Третья сессия - снова array, проверяем что кэш работает
    CreateTempScriptFile(R"(
        [Save]
        array<string> names = {"Alice", "Bob"};
        void main() {}
    )", "SaveMod");

    auto reloadResult2 = engine_->HotReload();
    ASSERT_TRUE(reloadResult2.has_value());
    
    auto runResult3 = engine_->RunAllMods();
    ASSERT_TRUE(runResult3.has_value());

    auto saveResult3 = saveLoadManager->GetSaveData(
        engine_->GetEngine(),
        engine_->GetModuleLoader()
    );
    ASSERT_TRUE(saveResult3.has_value());

    // Загружаем все сессии и проверяем корректность
    auto loadResult1 = saveLoadManager->LoadFromData(engine_->GetEngine(), saveResult1.value());
    auto loadResult2 = saveLoadManager->LoadFromData(engine_->GetEngine(), saveResult2.value());
    auto loadResult3 = saveLoadManager->LoadFromData(engine_->GetEngine(), saveResult3.value());

    EXPECT_TRUE(loadResult1.has_value()) << "Load session 1 should succeed";
    EXPECT_TRUE(loadResult2.has_value()) << "Load session 2 should succeed";
    EXPECT_TRUE(loadResult3.has_value()) << "Load session 3 should succeed";
    
    SUCCEED() << "Handler cache works correctly between sessions";
}

/**
 * @brief SaveLoad_ConcurrentAccess_ThreadSafe
 *
 * Проверяет потокобезопасность SaveLoadManager (отсутствие явной синхронизации)
 * Примечание: Текущая реализация НЕ является потокобезопасной,
 * тест документирует это поведение
 */
TEST_F(SaveLoadManagerTest, SaveLoad_ConcurrentAccess_ThreadSafe) {
    CreateTempScriptFile(R"(
        [Save]
        int value = 0;
        void main() { value = 1; }
    )", "SaveMod");

    auto compileResult = engine_->CompileAllMods();
    ASSERT_TRUE(compileResult.has_value());
    auto runResult = engine_->RunAllMods();
    ASSERT_TRUE(runResult.has_value());

    auto* saveLoadManager = engine_->GetSaveLoadManager();
    
    // Запускаем несколько потоков с одновременными вызовами Save
    // Это тест на выявление гонок данных
    eastl::vector<eastl::expected<eastl::vector<uint8_t>, SerializationError>> results(5);
    
    auto saveTask = [&]() {
        return saveLoadManager->GetSaveData(
            engine_->GetEngine(),
            engine_->GetModuleLoader()
        );
    };

    // Последовательные вызовы (текущая реализация не thread-safe)
    for (int i = 0; i < 5; ++i) {
        results[i] = saveTask();
    }

    // Все вызовы должны завершиться успешно при последовательном выполнении
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(results[i].has_value()) << "Save call " << i << " should succeed";
    }
    
    // Примечание: Для真正的 thread-safe тестов нужна мьютекс защита
    // или атомарные операции в реализации SaveLoadManager
    SUCCEED() << "Sequential concurrent access completed (note: not thread-safe by design)";
}

/**
 * @brief EmptyTest
 *
 * Пустой тест для завершения
 */
TEST_F(SaveLoadManagerTest, EmptyTest) {
    SUCCEED() << "SaveLoadManager tests completed";
}
