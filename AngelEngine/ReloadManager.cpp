module;

#include <angelscript.h>
#include <print>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>

export module AngelEngine.ReloadManager;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    export class ReloadManager final : public IReloadManager
    {
    public:
        eastl::expected<void, ModuleLoaderError> ReloadScripts(asIScriptEngine* engine,
                                                         IModuleLoader* moduleLoader,
                                                         IExecutionManager* executionManager,
                                                         IEventManager* eventManager) override
        {
            std::println("[ScriptEngine] Starting Reload...");

            executionManager->AbortAll();
            eventManager->ClearAll();

            executionManager->Renew();
            
            auto compileResult = moduleLoader->CompileAllMods(engine);
            if (!compileResult.has_value())
            {
                return compileResult;
            }


            auto runResult = executionManager->RunAllMods(engine, moduleLoader);
            if (!runResult.has_value())
            {
                return eastl::unexpected(ModuleLoaderError::GenericError);
            }

            std::println("[ScriptEngine] Reload completed.");
            return {};
        }
    };
}