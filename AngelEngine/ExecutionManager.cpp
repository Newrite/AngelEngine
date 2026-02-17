module;

#include <filesystem>
#include <mutex>
#include <print>

#include <angelscript.h>
#include <contextmgr.h>

#include <EASTL/atomic.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>
#include <EASTL/chrono.h>
#include <EASTL/vector.h>

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
        
        ~ExecutionManager()
        {
            // Destroy ContextMgr first so it returns all active contexts to the pool
            contextMgr_.reset();

            // Clean up pooled contexts
            for (auto* ctx : contextPool_)
            {
                if (ctx) ctx->Release();
            }
            contextPool_.clear();
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
            frameStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);
            
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
                        // Note: RequestContextCallback already sets LineCallback, but we set ExceptionCallback here too just in case
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
            
            contextMgr_->ExecuteScripts();

            return {};
        }

        void RegisterThreadSupport(asIScriptEngine* engine) override
        {
            // Register AS void sleep(ms) function
            contextMgr_->RegisterThreadSupport(engine);
            // Register standard CoRoutine support (yield, createCoRoutine)
            contextMgr_->RegisterCoRoutineSupport(engine);
        }
        
        // --- Context Pooling Implementation ---

        asIScriptContext* RequestContext(asIScriptEngine* engine, void* param) override
        {
            std::scoped_lock lock(mutex_);
            asIScriptContext* ctx = nullptr;

            if (!contextPool_.empty())
            {
                ctx = contextPool_.back();
                contextPool_.pop_back();
            }
            else
            {
                ctx = engine->CreateContext();
            }

            if (ctx)
            {
                // Set Watchdog for EVERY context requested (including those for events)
                ctx->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
            }

            return ctx;
        }

        void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) override
        {
            std::scoped_lock lock(mutex_);
            if (ctx)
            {
                ctx->Unprepare();
                contextPool_.push_back(ctx);
            }
        }

    private:
        static int64_t GetSystemTimeMs()
        {
            using namespace eastl::chrono;
            auto now = steady_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
            return static_cast<int64_t>(ms);
        }

        static asUINT GetSystemTimeAsUInt()
        {
            return static_cast<asUINT>(GetSystemTimeMs());
        }

        // --- Watchdog (LineCallback) ---
        static void LineCallback(asIScriptContext* ctx, void* param)
        {
            ExecutionManager* self = static_cast<ExecutionManager*>(param);

            // Optimization: Only check time every 1000 instructions
            // Use thread_local to avoid race conditions and false sharing
            thread_local int instructionCounter = 0;
            if (++instructionCounter < 1000) return;
            instructionCounter = 0;

            // Check how much time has passed since the start of the frame (Tick)
            auto now = GetSystemTimeMs();
            auto start = self->frameStartTimeMs_.load(eastl::memory_order_relaxed);
            auto duration = now - start;
            // std::println("Now - {} FrameStart - {} Duration - {}", now.time_since_epoch().count(), self->frameStartTime_.time_since_epoch().count(), duration);

            if (duration > MAX_SCRIPT_EXEC_TIME_MS)
            {
                std::println(stderr, "[Watchdog] Script aborted! Execution exceeded {}ms in a single frame.",
                             MAX_SCRIPT_EXEC_TIME_MS);
                
                // Add useful context information before aborting
                const char* section = "";
                int line = ctx->GetLineNumber(0, 0, &section);
                auto* func = ctx->GetFunction(0);
                std::println(stderr, "           At: {} ({}:{})", func ? func->GetDeclaration() : "null", section, line);

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
                // Set Exception Handler (LineCallback is set by RequestContextCallback via ContextMgr)
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
            contextMgr_->SetGetTimeCallback(GetSystemTimeAsUInt);
            frameStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);
        }

        ContextManagerPtr contextMgr_;
        std::recursive_mutex mutex_{};
        
        // Context Pool
        eastl::vector<asIScriptContext*> contextPool_;
        
        // Frame start time (for Watchdog)
        eastl::atomic<int64_t> frameStartTimeMs_;
    };
}