module;

#include <angelscript.h>
#include <EASTL/vector.h>
#include <EASTL/tuple.h>
#include <EASTL/atomic.h>
#include <EASTL/utility.h>
#include <EASTL/expected.h>
#include <cstdio>
#include <format>

export module AngelEngine.EventChannel;

import AngelEngine.Interfaces;
import AngelEngine.Logger;
import AngelEngine.FrameAllocator;

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

        eastl::expected<void, EventError> ProcessDeferred(asIScriptContext* ctx) override
        {
            if (!ctx) return eastl::unexpected(EventError::ContextPreparationFailed);

            eastl::tuple<eastl::vector<Args>...> processingQueues;
            eastl::vector<asIScriptFunction*, LinearFrameAllocator> subscribersCopy;
            size_t processingCount = 0;

            // 1. Critical Section: Swap queues and copy subscribers
            while (lock_.test_and_set(eastl::memory_order_acquire));
            
            if constexpr (sizeof...(Args) > 0) {
                // We need to move the content, but tuple swap might be tricky with vectors if not careful.
                // eastl::swap should work.
                using eastl::swap;
                swap(processingQueues, queues_);
            } else {
                processingCount = count_;
                count_ = 0;
            }
            
            if (subscribers_.empty())
            {
                lock_.clear(eastl::memory_order_release);
                return {};
            }

            subscribersCopy.reserve(subscribers_.size());
            for(auto* f : subscribers_) {
                f->AddRef();
                subscribersCopy.push_back(f);
            }
            
            lock_.clear(eastl::memory_order_release);
            
            // RAII Wrapper to ensure Release() is called on subscribers
            struct ScopedSubscriberList {
                eastl::vector<asIScriptFunction*, LinearFrameAllocator>& list;
                ScopedSubscriberList(eastl::vector<asIScriptFunction*, LinearFrameAllocator>& l) : list(l) {}
                ~ScopedSubscriberList() {
                    for (auto* f : list) {
                        if (f) f->Release();
                    }
                }
            } scopedSubscribers(subscribersCopy);

            // 2. Execution Section (No locks)
            size_t count = 0;
            if constexpr (sizeof...(Args) > 0) {
                // Assuming all vectors in tuple have same size (they should if Enqueue is consistent)
                count = eastl::get<0>(processingQueues).size();
            } else {
                count = processingCount;
            }
            
            for (size_t i = 0; i < count; ++i)
            {
                for (auto* func : subscribersCopy)
                {
                    int r = ctx->Prepare(func);
                    if (r < 0)
                    {
                        Log::Error("Failed to prepare context for event subscriber: {}", r);
                        continue;
                    }

                    if constexpr (sizeof...(Args) > 0) {
                        SetArgs(ctx, i, processingQueues, eastl::make_index_sequence<sizeof...(Args)>{});
                    }
                    
                    r = ctx->Execute();
                    if (r == asEXECUTION_EXCEPTION) {
                        const char* exceptionDesc = ctx->GetExceptionString();
                        Log::Error("Script Exception in event handler: {}", exceptionDesc);
                    } else if (r == asEXECUTION_ABORTED) {
                        Log::Error("Script Execution Aborted in event handler.");
                    } else if (r < 0) {
                        Log::Error("Script Execution Failed in event handler: {}", r);
                    }

                    ctx->Unprepare();
                }
            }
            return {};
        }
        
        void Clear() override
        {
            // Локальный вектор для безопасного освобождения вне критической секции
            eastl::vector<asIScriptFunction*> toRelease;
    
            {
                while (lock_.test_and_set(eastl::memory_order_acquire));
        
                if constexpr (sizeof...(Args) > 0) {
                    ClearImpl(eastl::make_index_sequence<sizeof...(Args)>{});
                }
                count_ = 0;
        
                // Переносим данные в локальный список и очищаем основной вектор под локом
                toRelease = eastl::move(subscribers_);
                subscribers_.clear();
        
                lock_.clear(eastl::memory_order_release);
            }
    
            // Теперь безопасно вызываем Release. Даже если это вызовет цепочку разрушений,
            // наш лок уже свободен, и дедлока не будет.
            for(auto* f : toRelease) {
                if (f) f->Release();
            }
        }

    private:
        template<size_t... Is>
        void EnqueueImpl(eastl::index_sequence<Is...>, Args... args)
        {
             // We need to push back to each vector in the tuple
             // Using fold expression
             (eastl::get<Is>(queues_).push_back(args), ...);
        }

        template<size_t... Is>
        void SetArgs(asIScriptContext* ctx, size_t index, const eastl::tuple<eastl::vector<Args>...>& queues, eastl::index_sequence<Is...>)
        {
            // SetArg is overloaded in Interfaces.ixx
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