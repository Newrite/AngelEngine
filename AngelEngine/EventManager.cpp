module;

#include <print>
#include <mutex>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
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
            // In destructor we want to clear everything, including the map
            std::scoped_lock lock(mutex_);
            for (auto& pair : channels_)
            {
                if (pair.second)
                {
                    pair.second->Clear();
                }
            }
            channels_.clear();
        }

        void RegisterChannel(uint32_t eventId, IEventChannel* channel) override
        {
            std::scoped_lock lock(mutex_);
            channels_[eventId] = channel;
        }

        void UnregisterChannel(uint32_t eventId) override
        {
            std::scoped_lock lock(mutex_);
            channels_.erase(eventId);
        }

        IEventChannel* GetChannel(uint32_t eventId) const override
        {
            std::scoped_lock lock(mutex_);
            auto it = channels_.find(eventId);
            if (it != channels_.end())
            {
                return it->second;
            }
            return nullptr;
        }

        void ProcessAllDeferred(asIScriptContext* sharedCtx) override
        {
            eastl::vector<IEventChannel*> activeChannels;
            {
                std::scoped_lock lock(mutex_);
                activeChannels.reserve(channels_.size());
                for (auto& pair : channels_)
                {
                    if (pair.second)
                    {
                        activeChannels.push_back(pair.second);
                    }
                }
            }

            for (auto* channel : activeChannels)
            {
                channel->ProcessDeferred(sharedCtx);
            }
        }

        void ClearAll() override
        {
            std::scoped_lock lock(mutex_);
            for (auto& pair : channels_)
            {
                if (pair.second)
                {
                    pair.second->Clear();
                }
            }
            // Do NOT clear the channels map here. 
            // Channels are structural components registered by bindings.
            // We only want to clear their state (subscribers/events) during reload.
        }

    private:
        mutable std::mutex mutex_;
        eastl::vector_map<uint32_t, IEventChannel*> channels_;
    };
}