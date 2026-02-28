#pragma once

#include <gtest/gtest.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#include <EASTL/expected.h>

namespace AngelEngine::Test {

/**
 * @brief Helper для создания временных скрипт-файлов
 * 
 * @param content Содержимое скрипта
 * @param modName Имя модуля (по умолчанию "TestMod")
 * @return Путь к созданному файлу
 */
inline eastl::string CreateTempScriptFile(
    const eastl::string& content, 
    const eastl::string& modName = "TestMod")
{
    namespace fs = std::filesystem;
    
    fs::path basePath = fs::current_path() / "angelscripts" / "mods" / modName.c_str();
    fs::create_directories(basePath);
    
    fs::path filePath = basePath / "main.as";
    std::ofstream file(filePath.string());
    if (file.is_open())
    {
        file << content.c_str();
        file.close();
    }
    
    // Также создаём mod.json
    fs::path jsonPath = basePath / "mod.json";
    std::ofstream jsonFile(jsonPath.string());
    if (jsonFile.is_open())
    {
        jsonFile << "{\"public_api\": false, \"depends_on\": []}";
        jsonFile.close();
    }
    
    return eastl::string(filePath.string().c_str());
}

/**
 * @brief Helper для ожидания указанного количества миллисекунд
 * 
 * @param ms Количество миллисекунд для ожидания
 */
inline void WaitForMilliseconds(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/**
 * @brief Matcher для проверки что eastl::expected содержит значение
 */
template<typename T, typename E>
::testing::Matcher<const eastl::expected<T, E>&> HasValue()
{
    return ::testing::Truly([](const eastl::expected<T, E>& exp) { 
        return exp.has_value(); 
    });
}

/**
 * @brief Matcher для проверки что eastl::expected содержит ошибку
 */
template<typename T, typename E>
::testing::Matcher<const eastl::expected<T, E>&> HasError()
{
    return ::testing::Truly([](const eastl::expected<T, E>& exp) { 
        return !exp.has_value(); 
    });
}

/**
 * @brief Helper для очистки всех временных скрипт-файлов
 */
inline void CleanupAllTempScriptFiles()
{
    namespace fs = std::filesystem;
    
    fs::path basePath = fs::current_path() / "angelscripts" / "mods";
    if (fs::exists(basePath))
    {
        fs::remove_all(basePath);
    }
}

/**
 * @brief Helper для очистки временных скрипт-файлов
 * 
 * @param modName Имя модуля для очистки
 */
inline void CleanupTempScriptFiles(const eastl::string& modName = "TestMod")
{
    namespace fs = std::filesystem;
    
    fs::path basePath = fs::current_path() / "angelscripts" / "mods" / modName.c_str();
    if (fs::exists(basePath))
    {
        fs::remove_all(basePath);
    }
}

/**
 * @brief RAII класс для автоматической очистки временных файлов
 */
class TempScriptGuard
{
public:
    explicit TempScriptGuard(const eastl::string& modName = "TestMod")
        : modName_(modName)
    {
    }
    
    ~TempScriptGuard()
    {
        CleanupTempScriptFiles(modName_);
    }
    
    // Запрет копирования
    TempScriptGuard(const TempScriptGuard&) = delete;
    TempScriptGuard& operator=(const TempScriptGuard&) = delete;
    
    // Разрешение перемещения
    TempScriptGuard(TempScriptGuard&&) = default;
    TempScriptGuard& operator=(TempScriptGuard&&) = default;
    
private:
    eastl::string modName_;
};

} // namespace AngelEngine::Test

// Глобальные матчеры для часто используемых проверок
MATCHER(IsSuccess, "") { return arg.has_value(); }
MATCHER(IsError, "") { return !arg.has_value(); }
