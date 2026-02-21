module;

#include <angelscript.h>
#include <EASTL/vector.h>
#include <EASTL/tuple.h>
#include <EASTL/atomic.h>
#include <EASTL/utility.h>

export module AngelEngine.EventChannel;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    export template<typename... Args>
    class EventChannel : public IEventChannel
    {
    public:
        ~EventChannel() override { Clear(); }

        void AddSubscriber(asIScriptFunction* func)
        {
            if (func)
            {
                while (lock_.test_and_set(eastl::memory_order_acquire));
                func->AddRef();
                subscribers_.push_back(func);
                lock_.clear(eastl::memory_order_release);
            }
        }

        void RemoveSubscriber(asIScriptFunction* func)
        {
             while (lock_.test_and_set(eastl::memory_order_acquire));
             for(auto it = subscribers_.begin(); it != subscribers_.end(); ++it) {
                 if(*it == func) {
                     (*it)->Release();
                     subscribers_.erase(it);
                     break;
                 }
             }
             lock_.clear(eastl::memory_order_release);
        }

        void Enqueue(Args... args)
        {
            while (lock_.test_and_set(eastl::memory_order_acquire));
            if constexpr (sizeof...(Args) > 0) {
                EnqueueImpl(eastl::make_index_sequence<sizeof...(Args)>{}, args...);
            } else {
                count_++;
            }
            lock_.clear(eastl::memory_order_release);
        }

        void ProcessDeferred(asIScriptContext* ctx) override
        {
            eastl::tuple<eastl::vector<Args>...> processingQueues;
            eastl::vector<asIScriptFunction*> subscribersCopy;
            size_t processingCount = 0;

            // 1. Critical Section: Swap queues and copy subscribers
            while (lock_.test_and_set(eastl::memory_order_acquire));
            
            if constexpr (sizeof...(Args) > 0) {
                processingQueues.swap(queues_);
            } else {
                processingCount = count_;
                count_ = 0;
            }
            
            if (subscribers_.empty())
            {
                lock_.clear(eastl::memory_order_release);
                return;
            }

            subscribersCopy.reserve(subscribers_.size());
            for(auto* f : subscribers_) {
                f->AddRef();
                subscribersCopy.push_back(f);
            }
            
            lock_.clear(eastl::memory_order_release);
            
            // 2. Execution Section (No locks)
            size_t count = 0;
            if constexpr (sizeof...(Args) > 0) {
                count = eastl::get<0>(processingQueues).size();
            } else {
                count = processingCount;
            }
            
            for (size_t i = 0; i < count; ++i)
            {
                for (auto* func : subscribersCopy)
                {
                    ctx->Prepare(func);
                    if constexpr (sizeof...(Args) > 0) {
                        SetArgs(ctx, i, processingQueues, eastl::make_index_sequence<sizeof...(Args)>{});
                    }
                    ctx->Execute();
                    ctx->Unprepare();
                }
            }

            // Release temporary references
            for(auto* f : subscribersCopy) f->Release();
        }
        
        void Clear() override
        {
            while (lock_.test_and_set(eastl::memory_order_acquire));
            
            if constexpr (sizeof...(Args) > 0) {
                ClearImpl(eastl::make_index_sequence<sizeof...(Args)>{});
            }
            count_ = 0;
            
            for(auto* f : subscribers_) f->Release();
            subscribers_.clear();
            lock_.clear(eastl::memory_order_release);
        }

    private:
        template<size_t... Is>
        void EnqueueImpl(eastl::index_sequence<Is...>, Args... args)
        {
             (eastl::get<Is>(queues_).push_back(args), ...);
        }

        template<size_t... Is>
        void SetArgs(asIScriptContext* ctx, size_t index, const eastl::tuple<eastl::vector<Args>...>& queues, eastl::index_sequence<Is...>)
        {
            (AngelEngine::SetArg(ctx, static_cast<asUINT>(Is), eastl::get<Is>(queues)[index]), ...);
        }
        
        template<size_t... Is>
        void ClearImpl(eastl::index_sequence<Is...>)
        {
            (eastl::get<Is>(queues_).clear(), ...);
        }

        // SoA Storage: Tuple of Vectors
        eastl::tuple<eastl::vector<Args>...> queues_;
        eastl::vector<asIScriptFunction*> subscribers_;
        eastl::atomic_flag lock_{};
        size_t count_ = 0; // Used only when sizeof...(Args) == 0
    };
}