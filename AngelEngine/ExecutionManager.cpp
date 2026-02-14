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

        void Tick(const float deltaTime, IEventManager* eventManager, asIScriptEngine* engine) override
        {
            // Update frame start time for Watchdog
            frameStartTime_ = eastl::chrono::steady_clock::now();
            
            // 1. Retrieve deferred events
            if (eventManager)
            {
                auto queuedEvents = eventManager->PopDeferredEvents();
                for (const auto& ev : queuedEvents)
                {
                    // Add them to ContextMgr (they will run asynchronously, supporting Wait)
                    asIScriptContext* ctx = contextMgr_->AddContext(engine, ev.func);
                    if (ctx)
                    {
                        if (ev.argInjector) ev.argInjector(ctx);
                        
                        // Set callbacks for safety and debugging
                        ctx->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
                        ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
                    }
                    // Release the Ref we took when queuing
                    ev.func->Release();
                }
            }

            contextMgr_->ExecuteScripts();
        }

        eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine,
                                                       const IModuleLoader* moduleLoader) override
        {
            std::scoped_lock lock(mutex_);

            // Ensure we don't have leftover contexts from previous runs
            // contextMgr_->AbortAll(); // Optional: depends on if RunAllMods is additive or a reset

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
            // Register AS void sleep(ms) function
            contextMgr_->RegisterThreadSupport(engine);
            // Register standard CoRoutine support (yield, createCoRoutine)
            contextMgr_->RegisterCoRoutineSupport(engine);
        }

    private:
        static asUINT GetSystemTimeMs()
        {
            using namespace std::chrono;
            auto now = steady_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
            return static_cast<asUINT>(ms);
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

        // --- Exception Callback ---
        static void ExceptionCallback(asIScriptContext* ctx, void* param)
        {
            std::println(stderr, "[Script Exception] {}", ctx->GetExceptionString());
            
            const asIScriptFunction* func = ctx->GetExceptionFunction();
            if (func)
            {
                std::println(stderr, "  Function: {}", func->GetDeclaration());
                std::println(stderr, "  Section:  {}", func->GetModuleName()); 
            }
            
            std::println(stderr, "  Line:     {}", ctx->GetExceptionLineNumber());

            // Print Call Stack
            std::println(stderr, "--- Call Stack ---");
            for (asUINT n = 0; n < ctx->GetCallstackSize(); n++)
            {
                asIScriptFunction* stackFunc = ctx->GetFunction(n);
                if (stackFunc)
                {
                    int line, column;
                    const char* section;
                    line = ctx->GetLineNumber(n, &column, &section);
                    std::println(stderr, "  {}: {} ({}, {})", 
                        stackFunc->GetDeclaration(), 
                        section ? section : "<unknown>", 
                        line, column);
                }
            }
            std::println(stderr, "------------------");
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
                // Set Watchdog and Exception Handler
                ctx->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
                ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);

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
