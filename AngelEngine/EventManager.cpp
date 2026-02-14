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
        void Subscribe(const eastl::string& eventName, asIScriptFunction* callback) override
        {
            if (!callback) return;

            // Basic validation: Ensure function signature is compatible with void return type
            // (Events usually don't return values that we capture here)
            // We could also check parameter counts if we had a schema.
            
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
            std::scoped_lock lock(mutex_);
            
            auto it = listeners_.find(eventName);
            if (it == listeners_.end()) return;

            // Optimization: Request context once for all listeners of this event
            asIScriptContext* ctx = engine->RequestContext();
            if (!ctx) return;

            for (asIScriptFunction* func : it->second)
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
