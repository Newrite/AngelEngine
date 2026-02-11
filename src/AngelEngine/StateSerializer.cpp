module;

#include <serializer.h>
#include <filesystem>
#include <mutex>
#include <memory>
#include <print>
#include <expected>
#include <map>

#include <angelscript.h>

export module AngelEngine.StateSerializer;

import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.Interfaces;

namespace fs = std::filesystem;

namespace AngelEngine
{
    
    export struct StateSerializer
    {
        
        using PtrType = std::unique_ptr<StateSerializer>;
        
        std::expected<void, ModuleLoaderError> HotReload(asIScriptEngine* engine,
                                                         const ModuleLoader::PtrType& moduleLoader,
                                                         const ExecutionManager::PtrType& executionManager)
        {
            std::println("[ScriptEngine] Starting Hot Reload...");

            executionManager->AbortAll();

            lastSnapshots = GetSnapshots(engine, moduleLoader);

            const auto compileResult = moduleLoader->CompileAllMods(engine);
            if (!compileResult.has_value())
            {
                return compileResult;
            }
            
            int restoredCount = RestoreState(engine, moduleLoader);

            // Re-create manager to clear old references
            executionManager->Renew();

            std::println("[ScriptEngine] HotReload completed. Restored state for {} mods.", restoredCount);
            return {};
        }

    private:
        // Use unique_ptr to avoid object slicing or invalid pointers when moving CSerializer
        // CSerializer stores a pointer to itself in its children (m_root.m_serializer = this),
        // so moving it invalidates that pointer.
        using SerializerPtr = std::unique_ptr<CSerializer>;

        std::map<std::string, SerializerPtr> GetSnapshots(asIScriptEngine* engine,
                                                        const ModuleLoader::PtrType& moduleLoader) const
        {
            std::map<std::string, SerializerPtr> snapshots;

            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                asIScriptModule* mod = engine->GetModule(modName.c_str());
                if (mod)
                {
                    auto backup = std::make_unique<CSerializer>();
                    if (backup->Store(mod) >= 0)
                    {
                        snapshots[modName] = std::move(backup);
                    }
                }
                engine->DiscardModule(modName.c_str());
            }

            return snapshots;
        }

        int RestoreState(asIScriptEngine* engine, const ModuleLoader::PtrType& moduleLoader)
        {
            int restoredCount = 0;
            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                if (lastSnapshots.contains(modName))
                {
                    asIScriptModule* newMod = engine->GetModule(modName.c_str());
                    if (newMod)
                    {
                        lastSnapshots[modName]->Restore(newMod);
                        restoredCount++;
                    }
                }
            }
            
            return restoredCount;
        }

        std::map<std::string, SerializerPtr> lastSnapshots;
    };
}
