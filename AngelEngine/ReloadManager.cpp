module;

#include <angelscript.h>
#include <print>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>

export module AngelEngine.ReloadManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace AngelEngine
{
    export class ReloadManager final : public IReloadManager
    {
    public:
        eastl::expected<void, ReloadError> ReloadScripts(asIScriptEngine* engine,
                                                         IModuleLoader* moduleLoader,
                                                         IExecutionManager* executionManager,
                                                         IEventManager* eventManager) override
        {
            Log::Info("[ScriptEngine] Starting Reload...");

            executionManager->AbortAll();
            eventManager->ClearAll();
            
            int r = engine->GarbageCollect();
            if (r < 0) Log::Error("[ScriptEngine] GarbageCollect failed with code: {} while reload scripts", r);

            executionManager->Renew();
            
            auto compileResult = moduleLoader->CompileAllMods(engine);
            if (!compileResult.has_value())
            {
                Log::Error("[ReloadManager] Compilation failed during reload: {}", static_cast<int>(compileResult.error()));
                return eastl::unexpected(ReloadError::ScriptRebuildFailed);
            }


            auto runResult = executionManager->RunAllMods(engine, moduleLoader);
            if (!runResult.has_value())
            {
                Log::Error("[ReloadManager] Execution failed during reload: {}", static_cast<int>(runResult.error()));
                return eastl::unexpected(ReloadError::ExecutionManagerFailed);
            }

            Log::Info("[ScriptEngine] Reload completed.");
            return {};
        }
    };
}