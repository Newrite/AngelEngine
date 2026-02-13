module;

#include <filesystem>
#include <mutex>
#include <print>

#include <angelscript.h>
#include <contextmgr.h>

#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>
#include <EASTL/chrono.h>

export module AngelEngine.ExecutionManager;

import AngelEngine.Interfaces;

namespace fs = std::filesystem;

namespace AngelEngine
{
    // ID for UserData to store the pointer to CContextMgr (constant from contextmgr.cpp)
    static const asPWORD CONTEXT_MGR_USER_DATA = 1002;

    export class ExecutionManager final : public IExecutionManager
    {
    public:
        using PtrType = eastl::unique_ptr<ExecutionManager>;
        using ContextManagerPtr = eastl::unique_ptr<CContextMgr>;

        // Script execution time limit per frame (protection against while true)
        static constexpr long long MAX_SCRIPT_EXEC_TIME_MS = 1000;

        explicit ExecutionManager()
        {
            Init();
        }

        void AbortAll() const override
        {
            if (contextMgr_)
            {
                contextMgr_->AbortAll();
            }
        }

        void Renew() override
        {
            Init();
        }

        void Tick(const float deltaTime) override
        {
            // Update frame start time for Watchdog
            frameStartTime_ = eastl::chrono::steady_clock::now();
            
            contextMgr_->ExecuteScripts();
        }

        eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine,
                                                       const IModuleLoader* moduleLoader) override
        {
            std::scoped_lock lock(mutex_);

            if (moduleLoader->Empty())
            {
                std::println("[ScriptEngine] No mods loaded to run.");
                return eastl::unexpected(ExecutionError::NoModsLoadedToRun);
            }

            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                auto resultStartModContext = StartModContext(engine, modName.c_str());
                if (!resultStartModContext.has_value())
                {
                    std::println(stderr, "[ExecutionManager] Failed to start mod, error code: {}", static_cast<int>(resultStartModContext.error()));
                }
            }

            return {};
        }

        void RegisterThreadSupport(asIScriptEngine* engine) override
        {
             // Register our Wait(float seconds) function
            int r = engine->RegisterGlobalFunction("void Wait(float)", asFUNCTION(Game_Wait_Callback), asCALL_CDECL);
            if (r < 0) std::println(stderr, "Failed to register Wait function");
        }

    private:
        static asUINT GetSystemTimeMs()
        {
            using namespace std::chrono;
            auto now = steady_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
            return static_cast<asUINT>(ms);
        }

        // --- Implementation of Wait function ---
        static void Game_Wait_Callback(float seconds)
        {
            // Get current active context
            asIScriptContext* ctx = asGetActiveContext();
            if (!ctx) return;

            // Get pointer to manager from UserData
            CContextMgr* mgr = reinterpret_cast<CContextMgr*>(ctx->GetUserData(CONTEXT_MGR_USER_DATA));

            if (mgr)
            {
                // Convert seconds to milliseconds
                asUINT ms = static_cast<asUINT>(seconds * 1000.0f);

                // Tell manager that this context should sleep
                mgr->SetSleeping(ctx, ms);

                // Suspend VM execution
                ctx->Suspend();
            }
        }

        // --- Watchdog (LineCallback) ---
        static void LineCallback(asIScriptContext* ctx, void* param)
        {
            ExecutionManager* self = static_cast<ExecutionManager*>(param);

            // Check how much time has passed since the start of the frame (Tick)
            auto now = eastl::chrono::steady_clock::now();
            auto duration = eastl::chrono::duration_cast<eastl::chrono::milliseconds>(now - self->frameStartTime_).count();

            if (duration > MAX_SCRIPT_EXEC_TIME_MS)
            {
                std::println(stderr, "[Watchdog] Script aborted! Execution exceeded {}ms in a single frame.",
                             MAX_SCRIPT_EXEC_TIME_MS);
                ctx->Abort();
            }
        }

        eastl::expected<void, ExecutionError> StartModContext(asIScriptEngine* engine,
                                                            const std::string& modName)
        {
            asIScriptModule* mod = engine->GetModule(modName.c_str());
            if (!mod) return {};

            asIScriptFunction* func = mod->GetFunctionByDecl("void main()");
            if (!func) return {}; // Mod without main is normal (library)

            // Use AddContext from CContextMgr
            asIScriptContext* ctx = contextMgr_->AddContext(engine, func);

            if (ctx)
            {
                // Set Watchdog
                ctx->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);

                std::println("[ScriptEngine] Mod started via ContextMgr: {}", modName);
                return {};
            }
            std::println(stderr, "[ScriptEngine] Failed to create context for {}", modName);
            return eastl::unexpected(ExecutionError::FailCreateContext);
        }

        void Init()
        {
            contextMgr_ = eastl::make_unique<CContextMgr>();
            contextMgr_->SetGetTimeCallback(GetSystemTimeMs);
        }

        ContextManagerPtr contextMgr_;
        std::mutex mutex_{};
        
        // Frame start time (for Watchdog)
        eastl::chrono::steady_clock::time_point frameStartTime_;
    };
}
