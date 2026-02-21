module;

#include <filesystem>
#include <mutex>
#include <print>
#include <thread>
#include <condition_variable>
#include <format>

#include <angelscript.h>
#include <contextmgr.h>

#include <EASTL/atomic.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>
#include <EASTL/chrono.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>

export module AngelEngine.ExecutionManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

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
            {
                std::lock_guard lock(cvMutex_);
                threadStop_.store(true, eastl::memory_order_relaxed);
            }
            cv_.notify_all();
            if (watchdogThread_.joinable())
            {
                watchdogThread_.join();
            }

            // Destroy ContextMgr first so it returns all active contexts to the pool
            contextMgr_.reset();

            // Clean up pooled contexts
            for (auto* ctx : contextPool_)
            {
                if (ctx) (void)ctx->Release();
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
            {
                std::lock_guard lock(cvMutex_);
                threadStop_.store(true, eastl::memory_order_relaxed);
            }
            cv_.notify_all();

            if (watchdogThread_.joinable())
            {
                watchdogThread_.join();
            }

            // 1. СНАЧАЛА убиваем менеджер контекстов! 
            // Он вернет все активные контексты, вызвав ReturnContext, и они попадут в contextPool_
            contextMgr_.reset();

            // ОЧИСТКА ПУЛА (Добавь этот блок!)
            for (auto* ctx : contextPool_)
            {
                if (ctx) (void)ctx->Release();
            }
            contextPool_.clear();

            threadStop_.store(false, eastl::memory_order_relaxed);
            abortRequested_.store(false, eastl::memory_order_relaxed);
            executionDepth_.store(0, eastl::memory_order_relaxed);
            executionStartTimeMs_.store(0, eastl::memory_order_relaxed);

            Init();
        }

        eastl::expected<void, ExecutionError> Tick(const float deltaTime, IEventManager* eventManager,
                                                   asIScriptEngine* engine) override
        {
            // 1. Process deferred events using a shared context
            if (eventManager)
            {
                // Request a context for event processing
                asIScriptContext* ctx = RequestContext(engine, nullptr);
                if (ctx)
                {
                    // Process all deferred events across all channels
                    auto result = eventManager->ProcessAllDeferred(ctx);
                    if (!result.has_value())
                    {
                        Log::Error("[ExecutionManager] Failed to process deferred events: {}",
                                   static_cast<int>(result.error()));
                        // We continue execution even if events failed, but we log it.
                    }

                    // Return context to pool
                    ReturnContext(engine, ctx, nullptr);
                }
                else
                {
                    Log::Error("[ExecutionManager] Failed to request context for event processing.");
                    return eastl::unexpected(ExecutionError::FailCreateContext);
                }
            }

            return this->AtomicExecutionHelper();
        }

        eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine,
                                                         const IModuleLoader* moduleLoader) override
        {
            std::scoped_lock lock(mutex_);

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
                    // Continue trying to start other mods? Or fail?
                    // Usually we want to try running as much as possible.
                }
            }

            return this->AtomicExecutionHelper();
        }

        eastl::expected<void, ExecutionError> RunMod(asIScriptEngine* engine,
                                                     const eastl::string& modName) override
        {
            std::scoped_lock lock(mutex_);

            if (modName.empty())
            {
                Log::Info("[ScriptEngine] No mod load to run.");
                return eastl::unexpected(ExecutionError::NoModsLoadedToRun);
            }

            auto resultStartModContext = this->StartContextHelper(engine, modName.c_str());
            if (!resultStartModContext.has_value())
            {
                return resultStartModContext;
            }

            return this->AtomicExecutionHelper();
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
                int r = ctx->SetLineCallback(asFUNCTION(LineCallback), this, asCALL_CDECL);
                if (r < 0) Log::Error("[ExecutionManager] Failed to set line callback: {}", r);

                r = ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
                if (r < 0) Log::Error("[ExecutionManager] Failed to set exception callback: {}", r);
            }

            return ctx;
        }

        void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) override
        {
            std::scoped_lock lock(mutex_);
            if (ctx)
            {
                int r = ctx->Unprepare();
                if (r < 0) Log::Error("[ExecutionManager] Failed to unprepare context: {}", r);
                contextPool_.push_back(ctx);
            }
        }

        eastl::expected<int, ExecutionError> ExecuteManaged(asIScriptContext* ctx) override
        {
            if (!ctx) return eastl::unexpected(ExecutionError::FailCreateContext);

            // Увеличиваем глубину. Если это первый (самый внешний) вызов, взводим таймер.
            if (executionDepth_.fetch_add(1, eastl::memory_order_acquire) == 0)
            {
                abortRequested_.store(false, eastl::memory_order_relaxed);
                executionStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);
            }

            int r = ctx->Execute();

            // Уменьшаем глубину выполнения
            executionDepth_.fetch_sub(1, eastl::memory_order_release);

            if (r < 0 && r != asEXECUTION_SUSPENDED)
            {
                Log::Error("[ExecutionManager] ExecuteManaged failed with code: {}", r);
                return eastl::unexpected(ExecutionError::FailRunMod);
            }

            return r;
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

            if (self->abortRequested_.load(eastl::memory_order_relaxed))
            {
                // 1. Сразу сбрасываем флаг в любом случае
                self->abortRequested_.store(false, eastl::memory_order_relaxed);

                // 2. ДОП. ПРОВЕРКА: Действительно ли этот конкретный скрипт превысил лимит?
                // (Защита от "шальной пули" из-за Race Condition)
                auto elapsed = GetSystemTimeMs() - self->executionStartTimeMs_.load(eastl::memory_order_relaxed);

                if (elapsed > MAX_SCRIPT_EXEC_TIME_MS)
                {
                    // Сбрасываем таймер для следующих скриптов в очереди
                    self->executionStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);

                    Log::Error("[Watchdog] Script aborted! Execution exceeded {}ms in a single frame.",
                               MAX_SCRIPT_EXEC_TIME_MS);

                    const char* section = "";
                    int line = ctx->GetLineNumber(0, 0, &section);
                    auto* func = ctx->GetFunction(0);
                    Log::Error("           At: {} ({}:{})", func ? func->GetDeclaration() : "null", section, line);

                    int r = ctx->Abort();
                    if (r < 0) Log::Error("[Watchdog] Failed to abort context: {}", r);
                }
                // Если elapsed <= MAX, значит это была "шальная пуля". 
                // Мы просто съели сигнал и продолжаем нормальную работу скрипта Б.
            }
        }

        // --- Exception Callback ---
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

            // Print Call Stack
            Log::Error("--- Call Stack ---");
            for (asUINT n = 0; n < ctx->GetCallstackSize(); n++)
            {
                asIScriptFunction* stackFunc = ctx->GetFunction(n);
                if (stackFunc)
                {
                    int line, column;
                    const char* section;
                    line = ctx->GetLineNumber(n, &column, &section);
                    Log::Error("  {}: {} ({}, {})",
                               stackFunc->GetDeclaration(),
                               section ? section : "<unknown>",
                               line, column);
                }
            }
            Log::Error("------------------");
        }

        eastl::expected<void, ExecutionError> StartModContext(asIScriptEngine* engine,
                                                              const eastl::string& modName)
        {
            asIScriptModule* mod = engine->GetModule(modName.c_str(), asGM_ONLY_IF_EXISTS);
            if (!mod) return eastl::unexpected(ExecutionError::NoModsLoadedToRun);

            asIScriptFunction* func = mod->GetFunctionByDecl("void main()");
            if (!func) return eastl::unexpected(ExecutionError::ModWithoutMain); // Mod without main is normal (library)

            // Use AddContext from CContextMgr
            asIScriptContext* ctx = contextMgr_->AddContext(engine, func);

            if (ctx)
            {
                // Set Exception Handler (LineCallback is set by RequestContextCallback via ContextMgr)
                int r = ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
                if (r < 0) Log::Error("[ExecutionManager] Failed to set exception callback for mod {}: {}",
                                      modName.c_str(), r);

                Log::Info("[ScriptEngine] Mod started via ContextMgr: {}", modName.c_str());
                return {};
            }
            Log::Error("[ScriptEngine] Failed to create context for {}", modName.c_str());
            return eastl::unexpected(ExecutionError::FailCreateContext);
        }

        inline eastl::expected<void, ExecutionError> AtomicExecutionHelper()
        {
            // Если это самый первый вход (глубина 0 -> 1)
            if (executionDepth_.fetch_add(1, eastl::memory_order_acquire) == 0)
            {
                abortRequested_.store(false, eastl::memory_order_relaxed);
                executionStartTimeMs_.store(GetSystemTimeMs(), eastl::memory_order_relaxed);
            }

            // Выполнение всех скриптов в очереди
            int r = contextMgr_->ExecuteScripts();
            if (r < 0)
            {
                Log::Error("[ExecutionManager] ExecuteScripts failed with code: {}", r);
                // We don't return error here because ExecuteScripts might have executed some scripts successfully.
                // But we should probably indicate something went wrong.
                // However, CContextMgr::ExecuteScripts returns the result of the last executed script or something similar?
                // Looking at CContextMgr source (not provided here but assuming standard add-on), it returns 0 on success.
            }

            // Выходим из исполнения (глубина уменьшается)
            executionDepth_.fetch_sub(1, eastl::memory_order_release);

            if (r < 0) return eastl::unexpected(ExecutionError::FailRunMod);
            return {};
        }

        inline eastl::expected<void, ExecutionError> StartContextHelper(
            asIScriptEngine* engine, const eastl::string& modName)
        {
            auto resultStartModContext = StartModContext(engine, modName.c_str());
            if (!resultStartModContext.has_value())
            {
                // Log::Error is already called inside StartModContext for critical failures, 
                // but we can log specific failure here if needed.
                // Actually StartModContext logs "Failed to create context" or returns ModWithoutMain (which is fine).
                if (resultStartModContext.error() != ExecutionError::ModWithoutMain)
                {
                    Log::Error("[ExecutionManager] Failed to start mod {}, error code: {}", modName.c_str(),
                               static_cast<int>(resultStartModContext.error()));
                    return eastl::unexpected(ExecutionError::FailRunMod);
                }
                // ModWithoutMain is not a failure for the batch run, just skip it.
            }
            return {};
        }

        void Init()
        {
            contextMgr_ = eastl::make_unique<CContextMgr>();
            contextMgr_->SetGetTimeCallback(GetSystemTimeAsUInt);

            watchdogThread_ = std::thread([this]()
            {
                while (!threadStop_.load(eastl::memory_order_acquire))
                {
                    // Спим интервалами по 100мс (достаточно точно для лимита в 1000мс).
                    // Используем CV только для того, чтобы мгновенно прервать сон при выходе.
                    {
                        std::unique_lock lock(cvMutex_);
                        cv_.wait_for(lock, std::chrono::milliseconds(100), [this]
                        {
                            return threadStop_.load(eastl::memory_order_acquire);
                        });
                    }

                    if (threadStop_.load(eastl::memory_order_acquire)) break;

                    // Если скрипт работает прямо сейчас (глубина > 0)
                    if (executionDepth_.load(eastl::memory_order_acquire) > 0)
                    {
                        auto elapsed = GetSystemTimeMs() - executionStartTimeMs_.load(eastl::memory_order_relaxed);
                        if (elapsed > MAX_SCRIPT_EXEC_TIME_MS)
                        {
                            abortRequested_.store(true, eastl::memory_order_release);
                        }
                    }
                }
            });
        }

        ContextManagerPtr contextMgr_;
        std::recursive_mutex mutex_{};

        // Context Pool
        eastl::vector<asIScriptContext*> contextPool_;

        // Watchdog
        eastl::atomic<int> executionDepth_{0};
        eastl::atomic<bool> abortRequested_{false};
        eastl::atomic<bool> threadStop_{false};
        eastl::atomic<int64_t> executionStartTimeMs_{0};
        std::thread watchdogThread_;
        std::condition_variable cv_;
        std::mutex cvMutex_;
    };
}
