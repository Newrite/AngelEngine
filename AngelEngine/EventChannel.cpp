module;

#include <EASTL/atomic.h>
#include <EASTL/expected.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/tuple.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <angelscript.h>
#include <asbind20/invoke.hpp>
#include <cstdio>
#include <format>
#include <ostream>


export module AngelEngine.EventChannel;

import AngelEngine.Interfaces;
import AngelEngine.Logger;
import AngelEngine.FrameAllocator;

namespace AngelEngine
{
    export template <typename... Args>
    class EventChannel : public IEventChannel
    {
        struct ContextPrepareGuard
        {
            asIScriptContext* ctx;
            ContextPrepareGuard(asIScriptContext* c) : ctx(c) {}
            ~ContextPrepareGuard()
            {
                if (ctx)
                    ctx->Unprepare();
            }
        };

    public:
        ~EventChannel() override { Clear(); }

        void AddSubscriber(asbind20::script_function<void(Args...)> func)
        {
            if (func)
            {
                asIScriptFunction* key = func.target();
                while (lock_.test_and_set(eastl::memory_order_acquire))
                    ;
                // script_function handles AddRef internally.
                // Inserting by raw pointer key gives O(1) add and O(1) remove.
                subscribers_.insert_or_assign(key, eastl::move(func));
                lock_.clear(eastl::memory_order_release);
            }
        }

        void RemoveSubscriber(asIScriptFunction* func)
        {
            // O(1) — hash_map erase by key, no linear scan.
            asbind20::script_function<void(Args...)> toRelease;
            {
                while (lock_.test_and_set(eastl::memory_order_acquire))
                    ;
                auto it = subscribers_.find(func);
                if (it != subscribers_.end())
                {
                    toRelease = eastl::move(it->second); // defer Release outside lock
                    subscribers_.erase(it);
                }
                lock_.clear(eastl::memory_order_release);
            }
            // script_function destructor calls Release here, outside the spinlock
        }

        void Enqueue(Args... args)
        {
            while (lock_.test_and_set(eastl::memory_order_acquire))
                ;
            if constexpr (sizeof...(Args) > 0)
            {
                EnqueueImpl(eastl::make_index_sequence<sizeof...(Args)>{}, args...);
            }
            else
            {
                count_++;
            }
            lock_.clear(eastl::memory_order_release);
        }

        // Dispatch: synchronous immediate execution — bypasses the queue entirely.
        // Call this instead of Enqueue when the event must fire right now (e.g. OnTick).
        // ctx must already be acquired from the pool and must not be nullptr.
        eastl::expected<void, EventError> Dispatch(asIScriptContext* ctx, Args... args)
        {
            if (!ctx)
                return eastl::unexpected(EventError::ContextPreparationFailed);

            // Snapshot raw subscriber pointers under spinlock (no AddRef — same as ProcessDeferred)
            eastl::fixed_vector<asIScriptFunction*, 16, true> funcPtrs;
            while (lock_.test_and_set(eastl::memory_order_acquire))
                ;
            if (!subscribers_.empty())
            {
                funcPtrs.reserve(subscribers_.size());
                for (auto& [key, f] : subscribers_)
                    funcPtrs.push_back(key); // key IS the raw pointer
            }
            lock_.clear(eastl::memory_order_release);

            if (funcPtrs.empty())
                return {};

            for (asIScriptFunction* fn : funcPtrs)
            {
                int r = ctx->Prepare(fn);
                if (r < 0)
                {
                    Log::Error("[EventChannel] Dispatch: failed to prepare context: {}", r);
                    continue;
                }

                ContextPrepareGuard guard(ctx);

                if constexpr (sizeof...(Args) > 0)
                    DispatchSetArgs(ctx, eastl::make_index_sequence<sizeof...(Args)>{}, args...);

                r = ctx->Execute();
                if (r == asEXECUTION_EXCEPTION)
                    Log::Error("Script Exception in Dispatch handler: {}", ctx->GetExceptionString());
                else if (r == asEXECUTION_ABORTED)
                    Log::Error("Script Execution Aborted in Dispatch handler.");
                else if (r < 0)
                    Log::Error("Script Execution Failed in Dispatch handler: {}", r);
            }
            return {};
        }

        eastl::expected<void, EventError> ProcessDeferred(asIScriptContext* ctx) override
        {
            if (!ctx)
                return eastl::unexpected(EventError::ContextPreparationFailed);

            eastl::tuple<eastl::vector<Args>...> processingQueues;
            size_t processingCount = 0;

            // Stack-allocated snapshot of raw function pointers (no AddRef — subscribers_ owns them).
            // Max 16 subscribers per channel; overflow falls back to heap via the small-buffer.
            eastl::fixed_vector<asIScriptFunction*, 16, true> funcPtrs;

            // --- Critical Section: swap queues + snapshot subscriber pointers ---
            while (lock_.test_and_set(eastl::memory_order_acquire))
                ;

            if constexpr (sizeof...(Args) > 0)
            {
                using eastl::swap;
                swap(processingQueues, queues_);
            }
            else
            {
                processingCount = count_;
                count_ = 0;
            }

            if (!subscribers_.empty())
            {
                funcPtrs.reserve(subscribers_.size());
                for (auto& [key, f] : subscribers_)
                    funcPtrs.push_back(key); // key IS the raw pointer, zero ref-count touch
            }

            lock_.clear(eastl::memory_order_release);
            // --- End Critical Section ---

            if (funcPtrs.empty())
                return {};

            // --- Execution Section (no locks, no AddRef overhead) ---
            size_t count = 0;
            if constexpr (sizeof...(Args) > 0)
                count = eastl::get<0>(processingQueues).size();
            else
                count = processingCount;

            for (size_t i = 0; i < count; ++i)
            {
                for (asIScriptFunction* fn : funcPtrs)
                {
                    int r = ctx->Prepare(fn);
                    if (r < 0)
                    {
                        Log::Error("Failed to prepare context for event subscriber: {}", r);
                        continue;
                    }

                    ContextPrepareGuard guard(ctx); // Unprepare on scope exit

                    if constexpr (sizeof...(Args) > 0)
                        SetArgs(ctx, i, processingQueues, eastl::make_index_sequence<sizeof...(Args)>{});

                    r = ctx->Execute();
                    if (r == asEXECUTION_EXCEPTION)
                        Log::Error("Script Exception in event handler: {}", ctx->GetExceptionString());
                    else if (r == asEXECUTION_ABORTED)
                        Log::Error("Script Execution Aborted in event handler.");
                    else if (r < 0)
                        Log::Error("Script Execution Failed in event handler: {}", r);
                }
            }
            return {};
        }

        void Clear() override
        {
            // Move the entire map out under lock, then release script_functions outside.
            eastl::hash_map<asIScriptFunction*, asbind20::script_function<void(Args...)>> toRelease;

            {
                while (lock_.test_and_set(eastl::memory_order_acquire))
                    ;

                if constexpr (sizeof...(Args) > 0)
                {
                    ClearImpl(eastl::make_index_sequence<sizeof...(Args)>{});
                }
                count_ = 0;

                toRelease = eastl::move(subscribers_);

                lock_.clear(eastl::memory_order_release);
            }

            // Destructors of script_function values call Release here, outside the spinlock
            toRelease.clear();
        }

    private:
        template <size_t... Is>
        void EnqueueImpl(eastl::index_sequence<Is...>, Args... args)
        {
            (eastl::get<Is>(queues_).push_back(args), ...);
        }

        // Used by Dispatch() — sets args directly from the variadic pack (no queue index lookup)
        template <size_t... Is>
        void DispatchSetArgs(asIScriptContext* ctx, eastl::index_sequence<Is...>, Args... args)
        {
            (AngelEngine::SetArg(ctx, static_cast<asUINT>(Is), eastl::get<Is>(eastl::forward_as_tuple(args...))), ...);
        }

        template <size_t... Is>
        void SetArgs(asIScriptContext* ctx, size_t index, const eastl::tuple<eastl::vector<Args>...>& queues,
                     eastl::index_sequence<Is...>)
        {
            // SetArg is overloaded in Interfaces.ixx
            (AngelEngine::SetArg(ctx, static_cast<asUINT>(Is), eastl::get<Is>(queues)[index]), ...);
        }

        template <size_t... Is>
        void ClearImpl(eastl::index_sequence<Is...>)
        {
            (eastl::get<Is>(queues_).clear(), ...);
        }

        // SoA Storage: Tuple of Vectors
        eastl::tuple<eastl::vector<Args>...> queues_;
        // Subscribers keyed by raw asIScriptFunction* for O(1) add and O(1) remove.
        eastl::hash_map<asIScriptFunction*, asbind20::script_function<void(Args...)>> subscribers_;
        eastl::atomic_flag lock_{};
        size_t count_ = 0; // Used only when sizeof...(Args) == 0
    };
} // namespace AngelEngine
