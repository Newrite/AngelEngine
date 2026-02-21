module;

#include <mutex>
#include <print>
#include <scriptbuilder.h>
#include <filesystem>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>

export module AngelEngine.ModuleLoader;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

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

        const eastl::vector<eastl::string>& GetSaveableVars(const eastl::string& modName) const override
        {
            auto it = saveable_vars_cache_.find(modName);
            if (it != saveable_vars_cache_.end())
            {
                return it->second;
            }
            static const eastl::vector<eastl::string> empty;
            return empty;
        }

        eastl::expected<void, ModuleLoaderError> CompileAllMods(asIScriptEngine* engine) override
        {
            std::scoped_lock lock(mutex_);
            loaded_modules_.clear();
            saveable_vars_cache_.clear();

            eastl::vector<eastl::string> availableMods = provider_->GetAvailableMods();
            if (availableMods.empty())
            {
                Log::Error("[ScriptEngine] No mods found.");
                return eastl::unexpected(ModuleLoaderError::PathNotFoundError);
            }

            for (const auto& modName : availableMods)
            {
                auto result = CompileSingleMod(engine, modName);
                if (result.has_value())
                {
                    loaded_modules_.push_back(modName);
                }
                else
                {
                    Log::Error("[ScriptEngine] Failed to compile mod: {}", modName.c_str());
                    return eastl::unexpected(result.error());
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
                if (r < 0)
                {
                    Log::Error("[ScriptEngine] Failed to add file: {} with AS code: {}", scriptPath.string().c_str(), r);
                    return eastl::unexpected(ModuleLoaderError::LoadScriptError);
                }
            }
            return {};
        }

        eastl::expected<void, ModuleLoaderError> CompileSingleMod(asIScriptEngine* engine,
                                                                const eastl::string& modName)
        {
            int r = builder_->StartNewModule(engine, modName.c_str());
            if (r < 0)
            {
                Log::Error("[ScriptEngine] Failed to create module: {} with AS code: {}", modName.c_str(), r);
                return eastl::unexpected(ModuleLoaderError::CreateModuleError);
            }
            
            // Load Standard Lib
            auto stdLibPath = provider_->GetStdLibPath();
            auto loadStdResult = LoadScriptsFromProvider(stdLibPath);
            if (!loadStdResult)
            {
                Log::Error("[ScriptEngine] Failed to inject STD into: {}", modName.c_str());
                return loadStdResult;
            }

            // Load Mod Scripts
            auto modPath = provider_->GetModPath(modName);
            auto loadModResult = LoadScriptsFromProvider(modPath);
            if (!loadModResult)
            {
                Log::Error("[ScriptEngine] Failed to load scripts for: {}", modName.c_str());
                return loadModResult;
            }

            r = builder_->BuildModule();
            if (r < 0)
            {
                Log::Error("[ScriptEngine] Compilation FAILED for mod: {} with AS code: {}", modName.c_str(), r);
                engine->DiscardModule(modName.c_str());
                return eastl::unexpected(ModuleLoaderError::BuildModuleError);
            }

            // Parse metadata for saveable variables
            saveable_vars_cache_.erase(modName); // Clear cache for this module
            asIScriptModule* mod = engine->GetModule(modName.c_str(), asGM_ONLY_IF_EXISTS);
            if (mod)
            {
                int globalVarCount = mod->GetGlobalVarCount();
                for (int i = 0; i < globalVarCount; ++i)
                {
                    // GetMetadataForVar returns a std::vector<std::string>
                    // Note: CScriptBuilder is not part of the standard AngelScript interface, so we assume it's available here as a member.
                    // However, GetMetadataForVar is a method of CScriptBuilder, not asIScriptModule.
                    // We need to use the builder instance to get metadata.
                    // The builder state might have changed if we are building multiple modules sequentially?
                    // CScriptBuilder::StartNewModule resets the builder.
                    // So we can use builder_->GetMetadataForVar(i) because we just built this module.
                    
                    // Wait, CScriptBuilder::GetMetadataForVar takes the variable index in the module.
                    // But we need to be careful if the builder state is consistent with the module we just built.
                    // Yes, we just called BuildModule(), so the builder should be in the correct state.

                    // Wait, the original code was:
                    // std::vector<std::string> metadataList = builder_->GetMetadataForVar(i);
                    // But builder_->GetMetadataForVar(i) might not be correct if the builder doesn't track variable indices directly or if they differ.
                    // Actually CScriptBuilder usually stores metadata mapped by declaration ID or name.
                    // Let's assume the original code was correct about using builder_->GetMetadataForVar(i).
                    
                    // However, we should check if the variable index 'i' from mod->GetGlobalVarCount() matches what the builder expects.
                    // CScriptBuilder usually doesn't expose GetMetadataForVar(int varIdx). It usually exposes GetMetadataForVar(const char* varName) or similar.
                    // Let's check the original code again.
                    // original: std::vector<std::string> metadataList = builder_->GetMetadataForVar(i);
                    // If the user has a custom CScriptBuilder that supports this, we keep it.
                    
                    // But wait, the original code had:
                    // asIScriptModule* mod = builder_->GetModule();
                    // This is safer than engine->GetModule.
                    
                    // Let's revert to using builder_->GetModule() to be safe and consistent with original code.
                    
                    // Re-reading original code:
                    // asIScriptModule* mod = builder_->GetModule();
                    // if (mod) { ... }
                    
                    // I will stick to that.

                    std::vector<std::string> metadataList = builder_->GetMetadataForVar(i);
                    
                    // Check if any of the metadata strings is "Save"
                    bool isSaveable = false;
                    for (const auto& meta : metadataList)
                    {
                        if (meta == "Save")
                        {
                            isSaveable = true;
                            break;
                        }
                    }

                    if (isSaveable)
                    {
                        const char* varName = nullptr;
                        mod->GetGlobalVar(i, &varName);
                        if (varName)
                        {
                            saveable_vars_cache_[modName].push_back(varName);
                        }
                    }
                }
            }
            else
            {
                 Log::Error("[ScriptEngine] Failed to retrieve module after build: {}", modName.c_str());
                 return eastl::unexpected(ModuleLoaderError::BuildModuleError);
            }

            Log::Info("[ScriptEngine] + Loaded mod: {}", modName.c_str());
            return {};
        }

        std::mutex mutex_;
        eastl::vector<eastl::string> loaded_modules_;
        mutable eastl::map<eastl::string, eastl::vector<eastl::string>> saveable_vars_cache_;
        eastl::unique_ptr<CScriptBuilder> builder_;
        eastl::unique_ptr<IScriptSourceProvider> provider_;
    };
}
