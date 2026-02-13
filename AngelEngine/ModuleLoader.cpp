module;

#include <mutex>
#include <print>
#include <scriptbuilder.h>
#include <filesystem>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>

export module AngelEngine.ModuleLoader;

import AngelEngine.Interfaces;

namespace fs = std::filesystem;

namespace AngelEngine
{
    export class ModuleLoader final : public IModuleLoader
    {
    public:
        using PtrType = eastl::unique_ptr<ModuleLoader>;

        explicit ModuleLoader(eastl::unique_ptr<IScriptSourceProvider> provider) 
            : provider_(eastl::move(provider))
        {
            builder_ = eastl::make_unique<CScriptBuilder>();
        }
        
        const eastl::vector<eastl::string>& GetLoadedModules() const override
        {
            return loaded_modules_;
        }
        
        bool Empty() const override
        {
            return loaded_modules_.empty();
        }

        eastl::expected<void, ModuleLoaderError> CompileAllMods(asIScriptEngine* engine) override
        {
            std::scoped_lock lock(mutex_);
            loaded_modules_.clear();

            eastl::vector<eastl::string> availableMods = provider_->GetAvailableMods();
            if (availableMods.empty())
            {
                std::println(stderr, "[ScriptEngine] No mods found.");
                return eastl::unexpected(ModuleLoaderError::PathNotFoundError);
            }

            for (const auto& modName : availableMods)
            {
                if (CompileSingleMod(engine, modName))
                {
                    loaded_modules_.push_back(modName);
                }
            }

            return {};
        }

    private:
        eastl::expected<void, ModuleLoaderError> LoadScriptsFromProvider(const fs::path& rootPath) const
        {
            auto scripts = provider_->GetScriptFiles(rootPath);
            if (scripts.empty()) return eastl::unexpected(ModuleLoaderError::LoadScriptError);

            for (const auto& scriptPath : scripts)
            {
                // We use AddSectionFromFile because the provider currently returns paths.
                int r = builder_->AddSectionFromFile(scriptPath.string().c_str());
                if (r < 0) std::println(stderr, "[ScriptEngine] Failed to add file: {}", scriptPath.string());
            }
            return {};
        }

        eastl::expected<void, ModuleLoaderError> CompileSingleMod(asIScriptEngine* engine,
                                                                const eastl::string& modName) const
        {
            int r = builder_->StartNewModule(engine, modName.c_str());
            if (r < 0)
            {
                std::println(stderr, "[ScriptEngine] Failed to create module: {}", modName.c_str());
                return eastl::unexpected(ModuleLoaderError::CreateModuleError);
            }
            
            // Load Standard Lib
            auto stdLibPath = provider_->GetStdLibPath();
            auto loadStdResult = LoadScriptsFromProvider(stdLibPath);
            if (!loadStdResult)
            {
                std::println(stderr, "[ScriptEngine] Failed to inject STD into: {}", modName.c_str());
                return loadStdResult;
            }

            // Load Mod Scripts
            auto modPath = provider_->GetModPath(modName);
            auto loadModResult = LoadScriptsFromProvider(modPath);
            if (!loadModResult)
            {
                std::println(stderr, "[ScriptEngine] Failed to load scripts for: {}", modName.c_str());
                return loadModResult;
            }

            r = builder_->BuildModule();
            if (r < 0)
            {
                std::println(stderr, "[ScriptEngine] Compilation FAILED for mod: {}", modName.c_str());
                engine->DiscardModule(modName.c_str());
                return eastl::unexpected(ModuleLoaderError::BuildModuleError);
            }

            std::println("[ScriptEngine] + Loaded mod: {}", modName.c_str());
            return {};
        }

        std::mutex mutex_;
        eastl::vector<eastl::string> loaded_modules_;
        eastl::unique_ptr<CScriptBuilder> builder_;
        eastl::unique_ptr<IScriptSourceProvider> provider_;
    };
}
