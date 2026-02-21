module;

#include <filesystem>
#include <mutex>
#include <print>
#include <thread>
#include <condition_variable>
#include <format>

#include <angelscript.h>

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

        explicit ExecutionManager(const int64_t maxScriptExecutionTimeMs, const bool enableWatchdog) 
            : maxScriptExecutionTimeMs_(maxScriptExecutionTimeMs), enableWatchdog_(enableWatchdog)
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

        void AbortAll() const override
        {
            std::scoped_lock lock(mutex_);
            for (auto* ctx : issuedContexts_)
            {
                // Теперь мы уверены, что ctx — валидный объект, так как мы держим на него ссылку
                if (ctx)
                {
                    asEContextState state = ctx->GetState();
                    if (state == asEXECUTION_ACTIVE || state == asEXECUTION_SUSPENDED)
                    {
                        ctx->Abort();
                    }
                }
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

            CleanPools();

            threadStop_.store(false, eastl::memory_order_relaxed);
            activeContext_.store(nullptr, eastl::memory_order_relaxed);
            executionStartTimeMs_.store(0, eastl::memory_order_relaxed);

            Init();
        }

        eastl::expected<void, ExecutionError> Tick(const float deltaTime, IEventManager* eventManager, asIScriptEngine* engine) override
        {
            if (eventManager)
            {
                asIScriptContext* ctx = RequestContext(engine, nullptr);
                if (ctx)
                {
                    auto result = eventManager->ProcessAllDeferred(ctx);
                    if (!result.has_value()) {
                        Log::Error("[ExecutionManager] Event processing failed: {}", static_cast<int>(result.error()));
                    }
                    
                    // Release вернет контекст обратно в пул через ReturnContextCallback в ScriptEngine
                    // ctx->Release(); 
                    ReturnContext(engine, ctx, this);
                }
                else
                {
                    Log::Error("[ExecutionManager] Failed to request context for event processing.");
                    return eastl::unexpected(ExecutionError::FailCreateContext);
                }
            }

            return {};
        }

        eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine, const IModuleLoader* moduleLoader) override
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
                    Log::Error("[ExecutionManager] Failed to start mod context for {}: {}", modName.c_str(), static_cast<int>(result.error()));
                }
            }

            return {};
        }

        eastl::expected<void, ExecutionError> RunMod(asIScriptEngine* engine, const eastl::string& modName) override
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

            return {};
        }

        void RegisterThreadSupport(asIScriptEngine* engine) override
        {
            // Здесь раньше регистрировался CContextMgr (sleep, yield). Нам это больше не нужно.
        }

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
                if (ctx)
                {
                    ctx->SetExceptionCallback(asFUNCTION(ExceptionCallback), this, asCALL_CDECL);
                }
            }

            if (ctx)
            {
                // При выдаче контекста — добавляем его в список отслеживания.
                // МЫ НЕ ДЕЛАЕМ AddRef здесь, так как сам факт нахождения в issuedContexts_ 
                // гарантируется тем, что у контекста есть внешний владелец (тот, кто его запросил).
                ctx->AddRef();
                issuedContexts_.push_back(ctx);
            }

            return ctx;
        }

        void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) override
        {
            std::scoped_lock lock(mutex_);
            if (isShuttingDown_ || !ctx) return;

            // 1. Убираем из списка активных (issuedContexts_)
            auto it = eastl::find(issuedContexts_.begin(), issuedContexts_.end(), ctx);
            if (it != issuedContexts_.end())
            {
                issuedContexts_.erase(it);
                ctx->Release();
            }

            // 2. Возвращаем в пул. 
            // Здесь AddRef необходим, так как мы берем владение над объектом, 
            // у которого счетчик ссылок вот-вот станет 0 (движок его отпускает).
            // ctx->AddRef();
            ctx->Unprepare();
            contextPool_.push_back(ctx);
        }

        eastl::expected<int, ExecutionError> ExecuteManaged(asIScriptContext* ctx) override
        {
            if (!ctx) return eastl::unexpected(ExecutionError::FailCreateContext);

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
            asIScriptModule* mod = engine->GetModule(modName.c_str(), asGM_ONLY_IF_EXISTS);
            if (!mod) return eastl::unexpected(ExecutionError::NoModsLoadedToRun);

            asIScriptFunction* func = mod->GetFunctionByDecl("void main()");
            if (!func) return eastl::unexpected(ExecutionError::ModWithoutMain);

            asIScriptContext* ctx = RequestContext(engine, nullptr);
            if (!ctx) return eastl::unexpected(ExecutionError::FailCreateContext);

            ctx->Prepare(func);
            
            int r = ExecuteManaged(ctx).value_or(asEXECUTION_ERROR);

            // Если контекст прервался или завершился, отпускаем его обратно в пул
            if (r != asEXECUTION_SUSPENDED)
            {
                // ctx->Release();
                ReturnContext(engine, ctx, this);
            }
            // Если он Suspended (co_await), promise оставит его висеть в памяти и разбудит позже!

            Log::Info("[ScriptEngine] Mod started: {}", modName.c_str());
            return {};
        }

        void CleanPools()
        {
            eastl::vector<asIScriptContext*> toRelease;

            {
                std::scoped_lock lock(mutex_);
                isShuttingDown_ = true; // Блокируем новые возвраты в пул

                // 1. Собираем контексты из пула (менеджер — их единственный владелец)
                for (auto* ctx : contextPool_) {
                    if (ctx) toRelease.push_back(ctx);
                }
                contextPool_.clear();

                // 2. Собираем выданные контексты (менеджер — один из владельцев)
                for (auto* ctx : issuedContexts_) {
                    if (ctx) toRelease.push_back(ctx);
                }
                issuedContexts_.clear();
            }

            // 3. Финальная очистка вне лока (чтобы не поймать дедлок при деструкции в AS)
            for (auto* ctx : toRelease)
            {
                // Прерываем выполнение, если скрипт все еще бежит
                if (ctx->GetState() == asEXECUTION_ACTIVE) {
                    ctx->Abort();
                }
        
                // Снимаем наш колбэк исключений, чтобы он не дернулся при финальном Release
                ctx->SetExceptionCallback(asSFuncPtr(), nullptr, asCALL_CDECL);

                // ВАЖНО: Мы делаем прямой Release(). 
                // Это уничтожит контексты из пула и уменьшит счетчик для активных.
                ctx->Release();
            }

            {
                std::scoped_lock lock(mutex_);
                isShuttingDown_ = false;
            }
        }

        void Init()
        {
            if (enableWatchdog_)
            {
                watchdogThread_ = std::thread([this]()
                {
                    while (!threadStop_.load(eastl::memory_order_acquire))
                    {
                        {
                            std::unique_lock lock(cvMutex_);
                            cv_.wait_for(lock, std::chrono::milliseconds(50), [this]
                            {
                                return threadStop_.load(eastl::memory_order_acquire);
                            });
                        }

                        if (threadStop_.load(eastl::memory_order_acquire)) break;

                        // АСИНХРОННЫЙ WATCHDOG ZERO-OVERHEAD
                        asIScriptContext* currentCtx = activeContext_.load(eastl::memory_order_acquire);
                        if (currentCtx)
                        {
                            auto elapsed = GetSystemTimeMs() - executionStartTimeMs_.load(eastl::memory_order_relaxed);
                            if (elapsed > maxScriptExecutionTimeMs_)
                            {
                                Log::Error("[Watchdog] Script aborted asynchronously! Execution exceeded {}ms.", maxScriptExecutionTimeMs_);
                                currentCtx->Abort(); // ПРЯМОЙ ВЫСТРЕЛ В КОНТЕКСТ
                            }
                        }
                    }
                });
            }
        }

        mutable std::recursive_mutex mutex_{};
        
        int64_t maxScriptExecutionTimeMs_;
        bool enableWatchdog_;
        bool isShuttingDown_{false};

        eastl::vector<asIScriptContext*> contextPool_;
        eastl::vector<asIScriptContext*> issuedContexts_;

        // Атомарный указатель для потокобезопасного Watchdog'а
        eastl::atomic<asIScriptContext*> activeContext_{nullptr};
        eastl::atomic<bool> threadStop_{false};
        eastl::atomic<int64_t> executionStartTimeMs_{0};
        
        std::thread watchdogThread_;
        std::condition_variable cv_;
        std::mutex cvMutex_;
    };
}