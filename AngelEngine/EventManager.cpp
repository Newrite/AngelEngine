module;

#include <print>
#include <mutex>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/functional.h>

export module AngelEngine.EventManager;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    export class EventManager final : public IEventManager
    {
    private:
        struct ScopedScriptFunction
        {
            asIScriptFunction* func = nullptr;

            explicit ScopedScriptFunction(asIScriptFunction* f) : func(f)
            {
                if (func) func->AddRef();
            }

            ScopedScriptFunction(const ScopedScriptFunction& other) : func(other.func)
            {
                if (func) func->AddRef();
            }

            ScopedScriptFunction(ScopedScriptFunction&& other) noexcept : func(other.func)
            {
                other.func = nullptr;
            }

            ScopedScriptFunction& operator=(const ScopedScriptFunction& other)
            {
                if (this != &other)
                {
                    if (func) func->Release();
                    func = other.func;
                    if (func) func->AddRef();
                }
                return *this;
            }

            ScopedScriptFunction& operator=(ScopedScriptFunction&& other) noexcept
            {
                if (this != &other)
                {
                    if (func) func->Release();
                    func = other.func;
                    other.func = nullptr;
                }
                return *this;
            }

            ~ScopedScriptFunction()
            {
                if (func) func->Release();
            }

            [[nodiscard]] asIScriptFunction* Get() const { return func; }
        };

    public:
        ~EventManager() override
        {
            ClearAll();
        }

        void Subscribe(const eastl::string& eventName, asIScriptFunction* callback) override
        {
            if (!callback) return;

            std::scoped_lock lock(mutex_);
            
            auto it = listeners_.find(eventName);
            eastl::shared_ptr<eastl::vector<ScopedScriptFunction>> newListeners;

            if (it != listeners_.end() && it->second)
            {
                // Copy-on-Write: Clone existing vector
                newListeners = eastl::make_shared<eastl::vector<ScopedScriptFunction>>(*it->second);
            }
            else
            {
                newListeners = eastl::make_shared<eastl::vector<ScopedScriptFunction>>();
            }
            
            newListeners->push_back(ScopedScriptFunction(callback));
            listeners_[eventName] = newListeners;
            
            std::println("[EventManager] Subscribed to event: {} (Func: {})", eventName.c_str(), callback->GetDeclaration());
        }

        void ClearAll() override
        {
            std::scoped_lock lock(mutex_);
            
            // RAII handles release of listeners
            listeners_.clear();

            // Release deferred events that haven't run yet
            for (auto& ev : deferredQueue_)
            {
                if (ev.func) ev.func->Release();
            }
            deferredQueue_.clear();
        }

        void DispatchDirect(asIScriptEngine* engine, const eastl::string& eventName, const ArgInjector& argInjector) override
        {
            // Zero-allocation (on heap), thread-safe retrieval
            eastl::shared_ptr<eastl::vector<ScopedScriptFunction>> callbacks;
            {
                std::scoped_lock lock(mutex_);
                auto it = listeners_.find(eventName);
                if (it != listeners_.end())
                {
                    callbacks = it->second;
                }
            }

            if (!callbacks || callbacks->empty()) return;

            // Optimization: Request context once for all listeners of this event
            asIScriptContext* ctx = engine->RequestContext();
            if (!ctx) return;

            for (const auto& scopedFunc : *callbacks)
            {
                asIScriptFunction* func = scopedFunc.Get();
                int r = ctx->Prepare(func);
                if (r < 0)
                {
                    std::println(stderr, "[EventManager] Failed to prepare context for event '{}'", eventName.c_str());
                    continue;
                }

                // Inject arguments
                if (argInjector) argInjector(ctx);

                // Execute synchronously
                r = ctx->Execute();

                if (r == asEXECUTION_SUSPENDED)
                {
                    std::println(stderr, "[EventManager] ERROR: Direct Event '{}' attempted to Wait/Suspend. This is forbidden in synchronous hooks!", eventName.c_str());
                    ctx->Abort();
                }
                else if (r == asEXECUTION_EXCEPTION)
                {
                    std::println(stderr, "[EventManager] Exception in Direct Event '{}': {}", eventName.c_str(), ctx->GetExceptionString());
                    asIScriptFunction* exFunc = ctx->GetExceptionFunction();
                    if(exFunc)
                         std::println(stderr, "  In function: {}", exFunc->GetDeclaration());
                }
                
                // Clean up for next iteration
                ctx->Unprepare();
            }

            // Return context to the pool
            engine->ReturnContext(ctx);
        }

        void DispatchDeferred(const eastl::string& eventName, const ArgInjector& argInjector) override
        {
            std::scoped_lock lock(mutex_);
            
            auto it = listeners_.find(eventName);
            if (it == listeners_.end() || !it->second) return;

            // Copy functions to queue for ExecutionManager
            for (const auto& scopedFunc : *it->second)
            {
                asIScriptFunction* func = scopedFunc.Get();
                // Increase Ref so function doesn't die before execution in queue
                func->AddRef();
                deferredQueue_.push_back({func, argInjector});
            }
        }

        eastl::vector<QueuedEvent> PopDeferredEvents() override
        {
            std::scoped_lock lock(mutex_);
            eastl::vector<QueuedEvent> queueCopy = eastl::move(deferredQueue_);
            deferredQueue_.clear();
            return queueCopy;
        }

    private:
        std::mutex mutex_;
        eastl::map<eastl::string, eastl::shared_ptr<eastl::vector<ScopedScriptFunction>>> listeners_;
        eastl::vector<QueuedEvent> deferredQueue_;
    };
}
