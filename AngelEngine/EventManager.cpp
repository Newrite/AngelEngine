module;

#include <print>
#include <mutex>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>

export module AngelEngine.EventManager;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    export class EventManager final : public IEventManager
    {
    public:
        ~EventManager() override
        {
            ClearAll();
        }

        void Subscribe(const eastl::string& eventName, asIScriptFunction* callback) override
        {
            if (!callback) return;

            std::scoped_lock lock(mutex_);
            
            // IMPORTANT: Increase ref count as we store the pointer in C++
            callback->AddRef(); 
            
            listeners_[eventName].push_back(callback);
            
            std::println("[EventManager] Subscribed to event: {} (Func: {})", eventName.c_str(), callback->GetDeclaration());
        }

        void ClearAll() override
        {
            std::scoped_lock lock(mutex_);
            
            // Release listeners
            for (auto& [name, callbacks] : listeners_)
            {
                for (auto* cb : callbacks)
                {
                    if (cb) cb->Release();
                }
            }
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
            // Copy listeners to avoid holding lock during execution
            eastl::vector<asIScriptFunction*> callbacks;
            {
                std::scoped_lock lock(mutex_);
                auto it = listeners_.find(eventName);
                if (it != listeners_.end())
                {
                    callbacks = it->second;
                }
            }

            if (callbacks.empty()) return;

            // Optimization: Request context once for all listeners of this event
            // This will use the Context Pool via RequestContextCallback in ScriptEngine
            asIScriptContext* ctx = engine->RequestContext();
            if (!ctx) return;

            for (asIScriptFunction* func : callbacks)
            {
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
                    // Abort is handled by caller usually, but here we must ensure it stops
                    ctx->Abort();
                }
                else if (r == asEXECUTION_EXCEPTION)
                {
                    std::println(stderr, "[EventManager] Exception in Direct Event '{}': {}", eventName.c_str(), ctx->GetExceptionString());
                    asIScriptFunction* exFunc = ctx->GetExceptionFunction();
                    if(exFunc)
                         std::println(stderr, "  In function: {}", exFunc->GetDeclaration());
                }
                
                // Clean up for next iteration (release objects held by context)
                ctx->Unprepare();
            }

            // Return context to the pool
            engine->ReturnContext(ctx);
        }

        void DispatchDeferred(const eastl::string& eventName, const ArgInjector& argInjector) override
        {
            std::scoped_lock lock(mutex_);
            
            auto it = listeners_.find(eventName);
            if (it == listeners_.end()) return;

            // Copy functions to queue for ExecutionManager
            for (asIScriptFunction* func : it->second)
            {
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
        eastl::map<eastl::string, eastl::vector<asIScriptFunction*>> listeners_;
        eastl::vector<QueuedEvent> deferredQueue_;
    };
}
