module;

#include <serializer.h>
#include <filesystem>
#include <mutex>
#include <print>
#include <string>

#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>

export module AngelEngine.StateSerializer;

import AngelEngine.Interfaces;

namespace fs = std::filesystem;

namespace AngelEngine
{
    // CStringType handles the AngelScript 'string' type, which is bound to std::string.
    // Therefore, we MUST use std::string here, not eastl::string.
    struct CStringType final : public CUserType
    {
        void* AllocateUnitializedMemory(CSerializedValue* val) override
        {
            return new std::string;
        }
        
        void Store(CSerializedValue* val, void* ptr) override
        {
            val->SetUserData(new std::string(*static_cast<std::string*>(ptr)));
        }
        
        void Restore(CSerializedValue* val, void* ptr) override
        {
            const auto buffer = static_cast<std::string*>(val->GetUserData());
            *static_cast<std::string*>(ptr) = *buffer;
        }
        
        void CleanupUserData(CSerializedValue* val) override
        {
            delete static_cast<std::string*>(val->GetUserData());
        }
    };

    export class StateSerializer final : public IStateSerializer
    {
    public:
        using PtrType = eastl::unique_ptr<StateSerializer>;
        
        eastl::expected<void, ModuleLoaderError> HotReload(asIScriptEngine* engine,
                                                         const eastl::unique_ptr<IModuleLoader>& moduleLoader,
                                                         const eastl::unique_ptr<IExecutionManager>& executionManager, const eastl::unique_ptr<IEventManager>& eventManager) override
        {
            std::println("[ScriptEngine] Starting Hot Reload...");

            eventManager->ClearAll();
            
            executionManager->AbortAll();

            lastSnapshots = GetSnapshots(engine, moduleLoader.get());

            const auto compileResult = moduleLoader->CompileAllMods(engine);
            if (!compileResult.has_value())
            {
                return compileResult;
            }
            
            int restoredCount = RestoreState(engine, moduleLoader.get());

            // Re-create manager to clear old references
            executionManager->Renew();

            std::println("[ScriptEngine] HotReload completed. Restored state for {} mods.", restoredCount);
            return {};
        }

    private:
        // Use unique_ptr to avoid object slicing or invalid pointers when moving CSerializer
        // CSerializer stores a pointer to itself in its children (m_root.m_serializer = this),
        // so moving it invalidates that pointer.
        // We use std::unique_ptr for CSerializer because it's an external class (std-based usually, but here pointer is fine)
        // Wait, CSerializer is just a class. We can hold it in eastl::unique_ptr.
        using SerializerPtr = eastl::unique_ptr<CSerializer>;

        eastl::map<eastl::string, SerializerPtr> GetSnapshots(asIScriptEngine* engine,
                                                        const IModuleLoader* moduleLoader) const
        {
            eastl::map<eastl::string, SerializerPtr> snapshots;

            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                asIScriptModule* mod = engine->GetModule(modName.c_str());
                if (mod)
                {
                    auto backup = eastl::make_unique<CSerializer>();
                    // CSerializer expects std::string for type name
                    backup->AddUserType(new CStringType(), std::string("string")); 
                    if (backup->Store(mod) >= 0)
                    {
                        snapshots[modName] = eastl::move(backup);
                    }
                }
                engine->DiscardModule(modName.c_str());
            }

            return snapshots;
        }

        int RestoreState(asIScriptEngine* engine, const IModuleLoader* moduleLoader)
        {
            int restoredCount = 0;
            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                if (lastSnapshots.find(modName) != lastSnapshots.end())
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

        eastl::map<eastl::string, SerializerPtr> lastSnapshots;
    };
}
