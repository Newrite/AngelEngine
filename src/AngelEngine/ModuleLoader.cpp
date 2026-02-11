module;

#include <memory>
#include <mutex>
#include <print>
#include <scriptbuilder.h>
#include <filesystem>
#include <expected>
#include <angelscript.h>

export module AngelEngine.ModuleLoader;

namespace fs = std::filesystem;

namespace AngelEngine
{
    export enum class ModuleLoaderError : std::uint8_t
    {
        CreateModuleError,
        BuildModuleError,
        LoadScriptError,
        PathNotFoundError,
        GenericError
    };

    export struct ModuleLoaderConfig final
    {
        const fs::path scriptsPathStd;
        const fs::path scriptsPathMod;
    };

    export struct ModuleLoader
    {
        using PtrType = std::unique_ptr<ModuleLoader>;
        using ErrorType = ModuleLoaderError;

        explicit ModuleLoader(ModuleLoaderConfig config) : config_(std::move(config))
        {
            std::println("Module loader created with STD -> {} and MOD -> {}", config_.scriptsPathStd.string(),
                         config_.scriptsPathMod.string());
            builder_ = std::make_unique<CScriptBuilder>();
        }
        
        const std::vector<std::string>& GetLoadedModules() const
        {
            return loaded_modules_;
        }
        
        bool Empty() const
        {
            return loaded_modules_.empty();
        }

        std::expected<void, ModuleLoaderError> LoadScriptsFromDirectory(const fs::path& rootPath) const
        {
            if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) return std::unexpected(ModuleLoaderError::PathNotFoundError);

            try
            {
                for (const auto& entry : fs::recursive_directory_iterator(rootPath))
                {
                    if (entry.is_regular_file() && entry.path().extension() == ".as")
                    {
                        // Use absolute path to avoid issues with relative paths in AngelScript
                        auto absPath = fs::absolute(entry.path());
                        int r = builder_->AddSectionFromFile(absPath.string().c_str());
                        if (r < 0) std::println(stderr, "[ScriptEngine] Failed to add file: {}", absPath.string());
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::println(stderr, "[ScriptEngine] FS Exception: {}", e.what());
                return std::unexpected(ModuleLoaderError::LoadScriptError);
            }
            return {};
        }

        std::expected<void, ModuleLoaderError> CompileSingleMod(asIScriptEngine* engine,
                                                                const std::string& modName,
                                                                const fs::path& modPath) const
        {
            int r = builder_->StartNewModule(engine, modName.c_str());
            if (r < 0)
            {
                std::println(stderr, "[ScriptEngine] Failed to create module: {}", modName);
                return std::unexpected(ModuleLoaderError::CreateModuleError);
            }
            
            auto loadScriptsResult = LoadScriptsFromDirectory(config_.scriptsPathStd);
            if (!loadScriptsResult)
            {
                std::println(stderr, "[ScriptEngine] Failed to inject STD into: {}", modName);
                return loadScriptsResult;
            }

            loadScriptsResult = LoadScriptsFromDirectory(modPath);
            if (!loadScriptsResult)
            {
                std::println(stderr, "[ScriptEngine] Failed to load scripts for: {}", modName);
                return loadScriptsResult;
            }

            r = builder_->BuildModule();
            if (r < 0)
            {
                std::println(stderr, "[ScriptEngine] Compilation FAILED for mod: {}", modName);
                engine->DiscardModule(modName.c_str());
                return std::unexpected(ModuleLoaderError::BuildModuleError);
            }

            std::println("[ScriptEngine] + Loaded mod: {}", modName);
            return {};
        }

        std::expected<void, ModuleLoaderError> CompileAllMods(asIScriptEngine* engine)
        {
            std::scoped_lock lock(mutex_);
            loaded_modules_.clear();

            if (!fs::exists(config_.scriptsPathMod))
            {
                std::println(stderr, "[ScriptEngine] Mods directory not found: {}", config_.scriptsPathMod.string());
                return std::unexpected(ModuleLoaderError::PathNotFoundError);
            }

            std::println("[ScriptEngine] Starting compilation...");

            for (const auto& entry : fs::directory_iterator(config_.scriptsPathMod))
            {
                if (entry.is_directory())
                {
                    std::string modName = entry.path().filename().string();
                    fs::path modPath = entry.path();
                    fs::path mainScriptPath = modPath / "main.as";

                    if (fs::exists(mainScriptPath))
                    {
                        if (CompileSingleMod(engine, modName, modPath))
                        {
                            loaded_modules_.push_back(modName);
                        }
                    }
                }
            }

            std::println("[ScriptEngine] Compilation finished. Loaded {} mods.", loaded_modules_.size());
            return {};
        }

    private:
        std::mutex mutex_;
        std::vector<std::string> loaded_modules_;
        std::unique_ptr<CScriptBuilder> builder_;
        ModuleLoaderConfig config_;
    };
}
