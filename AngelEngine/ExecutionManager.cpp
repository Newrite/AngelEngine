module;

#include <condition_variable>
#include <filesystem>
#include <format>
#include <mutex>
#include <print>
#include <thread>


#include <angelscript.h>

#include <EASTL/atomic.h>
#include <EASTL/chrono.h>
#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>


export module AngelEngine.ExecutionManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;
import AngelEngine.Errors;


namespace fs = std::filesystem;

namespace AngelEngine
{
    export class ExecutionManager final : public IExecutionManager
    {
    public:
        using PtrType = eastl::unique_ptr<ExecutionManager>;

        explicit ExecutionManager(const int64_t maxScriptExecutionTimeMs, const bool enableWatchdog) :
            maxScriptExecutionTimeMs_(maxScriptExecutionTimeMs), enableWatchdog_(enableWatchdog)
        {
            Init();
        }

        ~ExecutionManager() override
        {
            {
                std::lock_guard lock(cvMutex_);
                threadStop_.store(true, eastl::memory_order_relaxed);
            }
            cv_.notify_all();
            if (watchdogThread_.joinable())
            {
                watchdogThread_.join();
            }

            CleanPools();
        }


        void Renew() override
        {
            {
                std::lock_guard lock(cvMutex_);
                threadStop_.store(true, eastl::memory_order_relaxed);
            }
            cv_.notify_all();

            if (watchdogThread_.joinable())
            {
                watchdogThread_.join();
            }

            CleanPools();

            threadStop_.store(false, eastl::memory_order_relaxed);
            activeContext_.store(nullptr, eastl::memory_order_relaxed);
            executionStartTimeMs_.store(0, eastl::memory_order_relaxed);

            Init();
        }

        eastl::expected<void, ExecutionError> Tick(const float deltaTime, IEventManager* eventManager,
                                                   asIScriptEngine* engine,
                                                   IBuiltinEventDispatcher* dispatcher) override
        {
            if (eventManager || dispatcher)
            {
                auto ctxPtr = RequestContext(engine, nullptr);
                if (ctxPtr)
                {
                    // 1. Direct dispatch: built-in events (OnTick etc.) run immediately
                    //    with the already-acquired context — no queue round-trip.
                    if (dispatcher)
                        dispatcher->DispatchBuiltinEvents(ctxPtr.get(), deltaTime);

                    // 2. Deferred dispatch: user-enqueued events (PushLoad, PushSave,
                    //    any custom channels) processed here.
                    if (eventManager)
                    {
                        auto result = eventManager->ProcessAllDeferred(ctxPtr.get());
                        if (!result.has_value())
                            Log::Error("[ExecutionManager] Event processing failed: {}",
                                       static_cast<int>(result.error()));
                    }

                    // RAII destruction of ctxPtr implicitly calls ReturnContext
                }
                else
                {
                    Log::Error("[ExecutionManager] Failed to request context for event processing.");
                    return eastl::unexpected(ExecutionError::FailCreateContext);
                }
            }

            return {};
        }

        eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine,
                                                         const IModuleLoader* moduleLoader) override
        {
            if (moduleLoader->Empty())
            {
                Log::Info("[ScriptEngine] No mods loaded to run.");
                return eastl::unexpected(ExecutionError::NoModsLoadedToRun);
            }

            for (const auto& modName : moduleLoader->GetLoadedModules())
            {
                auto result = this->StartContextHelper(engine, modName.c_str());
                if (!result.has_value())
                {
                    Log::Error("[ExecutionManager] Failed to start mod context for {}: {}", modName.c_str(),
                               static_cast<int>(result.error()));
                }
            }

            return {};
        }

        void RegisterThreadSupport(asIScriptEngine* engine) override
        {
            // Здесь раньше регистрировался CContextMgr (sleep, yield). Нам это больше не нужно.
        }

        ContextPtr RequestContext(asIScriptEngine* engine, void* param) override
        {
            // Lock-free pop: intrusive nodes — no heap allocation at all
            ContextNode* node = contextPoolHead_.load(eastl::memory_order_acquire);
            while (node)
            {
                if (contextPoolHead_.compare_exchange_weak(node, node->next, eastl::memory_order_acq_rel,
                                                           eastl::memory_order_acquire))
                {
                    node->next = nullptr;

                    // Lazily create the AS context the first time this slot is used
                    if (!node->ctx)
                    {
                        node->ctx = engine->CreateContext();
                        if (node->ctx)
                            node->ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
                    }

                    return ContextPtr(node->ctx, ContextDeleter{this, engine});
                }
            }

            // Pool exhausted — create a temporary context (uncommon, high-contention case)
            asIScriptContext* ctx = engine->CreateContext();
            if (ctx)
            {
                ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
            }
            return ContextPtr(ctx, ContextDeleter{this, engine});
        }

        void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) override
        {
            if (isShuttingDown_.load(eastl::memory_order_acquire) || !ctx)
            {
                if (ctx)
                    ctx->Release();
                return;
            }

            // Try to return to the pre-allocated pool (match ctx to its original node)
            for (auto& node : nodeStorage_)
            {
                if (node.ctx == ctx)
                {
                    // Intrusive lock-free push — no heap allocation.
                    // ctx->Unprepare() intentionally omitted: Prepare() resets state itself.
                    node.next = contextPoolHead_.load(eastl::memory_order_relaxed);
                    while (!contextPoolHead_.compare_exchange_weak(node.next, &node, eastl::memory_order_release,
                                                                   eastl::memory_order_relaxed))
                    {
                    }
                    return;
                }
            }

            // Context was created outside the pool (overflow case) — release it
            ctx->Unprepare();
            ctx->Release();
        }

        eastl::expected<int, ExecutionError> ExecuteManaged(asIScriptContext* ctx) override
        {
            if (!ctx)
                return eastl::unexpected(ExecutionError::FailCreateContext);

            // Публикуем текущий контекст для Watchdog потока
            activeContext_.store(ctx, eastl::memory_order_release);
            executionStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);

            int r = ctx->Execute();

            // Снимаем контекст с контроля
            activeContext_.store(nullptr, eastl::memory_order_release);

            if (r < 0 && r != asEXECUTION_SUSPENDED)
            {
                if (r != asEXECUTION_ABORTED)
                {
                    Log::Error("[ExecutionManager] ExecuteManaged failed with code: {}", r);
                }
                return eastl::unexpected(ExecutionError::FailRunMod);
            }

            return r;
        }

    private:
        // ------------------------------------------------------------------
        // Intrusive pool node — node IS the storage slot, zero heap per call
        // ------------------------------------------------------------------
        struct ContextNode
        {
            asIScriptContext* ctx = nullptr;
            ContextNode* next = nullptr; // intrusive link for lock-free stack
        };

        static constexpr int kPoolSize = 8; // pre-allocated context slots

        static int64_t GetSystemTimeMs()
        {
            using namespace eastl::chrono;
            auto now = steady_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
            return static_cast<int64_t>(ms);
        }

        static void ExceptionCallback(asIScriptContext* ctx, void* param)
        {
            Log::Error("[Script Exception] {}", ctx->GetExceptionString());
            const asIScriptFunction* func = ctx->GetExceptionFunction();
            if (func)
            {
                Log::Error("  Function: {}", func->GetDeclaration());
                Log::Error("  Section:  {}", func->GetModuleName());
            }
            Log::Error("  Line:     {}", ctx->GetExceptionLineNumber());
        }

        eastl::expected<void, ExecutionError> StartContextHelper(asIScriptEngine* engine, const eastl::string& modName)
        {
            asIScriptModule* mod = engine->GetModule("__Megamodule__", asGM_ONLY_IF_EXISTS);
            if (!mod)
                return eastl::unexpected(ExecutionError::NoModsLoadedToRun);

            // Attempt to find `void main()` in the module, potentially under the `modName` namespace
            asIScriptFunction* func = nullptr;

            // First try globally (for public APIs that aren't wrapped)
            func = mod->GetFunctionByDecl("void main()");

            // If not found, try within the namespace
            if (!func)
            {
                eastl::string namespacedDecl = "void " + modName + "::main()";
                func = mod->GetFunctionByDecl(namespacedDecl.c_str());
            }

            if (!func)
                return eastl::unexpected(ExecutionError::ModWithoutMain);

            auto ctxPtr = RequestContext(engine, nullptr);
            if (!ctxPtr)
                return eastl::unexpected(ExecutionError::FailCreateContext);

            ctxPtr->Prepare(func);

            int r = ExecuteManaged(ctxPtr.get()).value_or(asEXECUTION_ERROR);

            // Если он Suspended (co_await), promise оставит его висеть в памяти и разбудит позже!
            // В нашей архитектуре RAII `ctxPtr` выйдет из области видимости и вернет контекст в пул
            // Для корутин, promise должен удержать AddRef.
            if (r == asEXECUTION_SUSPENDED)
            {
                // TODO: If promise architecture requires context to NOT be returned, release ownership
                // ctxPtr.release() or AddRef it manually. Currently assuming standard cleanup.
            }

            Log::Info("[ScriptEngine] Mod started: {}", modName.c_str());
            return {};
        }

        void CleanPools()
        {
            isShuttingDown_.store(true, eastl::memory_order_release);

            // Clear the lock-free stack head — nodes live in nodeStorage_, no separate free needed
            contextPoolHead_.store(nullptr, eastl::memory_order_relaxed);

            // Release all pre-allocated AngelScript contexts
            for (auto& node : nodeStorage_)
            {
                if (node.ctx)
                {
                    if (node.ctx->GetState() == asEXECUTION_ACTIVE)
                    {
                        node.ctx->Abort();
                    }
                    node.ctx->SetExceptionCallback(asSFuncPtr(), nullptr, asCALL_CDECL);
                    node.ctx->Release();
                    node.ctx = nullptr;
                }
                node.next = nullptr;
            }

            isShuttingDown_.store(false, eastl::memory_order_release);
        }

        void Init()
        {
            // Build the intrusive lock-free stack from nodeStorage_.
            // Contexts are created lazily on first RequestContext (we don't have an engine here).
            for (int i = kPoolSize - 1; i >= 0; --i)
            {
                nodeStorage_[i].ctx = nullptr;
                nodeStorage_[i].next = contextPoolHead_.load(eastl::memory_order_relaxed);
                contextPoolHead_.store(&nodeStorage_[i], eastl::memory_order_relaxed);
            }

            if (enableWatchdog_)
            {
                watchdogThread_ = std::thread(
                    [this]()
                    {
                        while (!threadStop_.load(eastl::memory_order_acquire))
                        {
                            {
                                std::unique_lock lock(cvMutex_);
                                cv_.wait_for(lock, std::chrono::milliseconds(50),
                                             [this] { return threadStop_.load(eastl::memory_order_acquire); });
                            }

                            if (threadStop_.load(eastl::memory_order_acquire))
                                break;

                            asIScriptContext* currentCtx = activeContext_.load(eastl::memory_order_acquire);
                            if (currentCtx)
                            {
                                auto elapsed =
                                    GetSystemTimeMs() - executionStartTimeMs_.load(eastl::memory_order_relaxed);
                                if (elapsed > maxScriptExecutionTimeMs_)
                                {
                                    Log::Error("[Watchdog] Script aborted asynchronously! Execution exceeded {}ms.",
                                               maxScriptExecutionTimeMs_);
                                    currentCtx->Abort();
                                }
                            }
                        }
                    });
            }
        }

        int64_t maxScriptExecutionTimeMs_;
        bool enableWatchdog_;
        eastl::atomic<bool> isShuttingDown_{false};

        // Pre-allocated pool — kPoolSize slots, zero heap allocations after Init()
        ContextNode nodeStorage_[kPoolSize]{};
        eastl::atomic<ContextNode*> contextPoolHead_{nullptr};

        // Атомарный указатель для потокобезопасного Watchdog'а
        eastl::atomic<asIScriptContext*> activeContext_{nullptr};
        eastl::atomic<bool> threadStop_{false};
        eastl::atomic<int64_t> executionStartTimeMs_{0};

        std::thread watchdogThread_;
        std::condition_variable cv_;
        std::mutex cvMutex_;
    };
} // namespace AngelEngine