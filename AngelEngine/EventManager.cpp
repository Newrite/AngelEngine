module;

#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>

#include <angelscript.h>
#include <mutex>
#include <print>



export module AngelEngine.EventManager;

import AngelEngine.Interfaces;
import AngelEngine.EventsInterfaces;
import AngelEngine.Logger;
import AngelEngine.Errors;

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

        eastl::expected<void, EventError> RegisterChannel(uint32_t eventId, IEventChannel* channel) override
        {
            std::scoped_lock lock(mutex_);
            if (channels_.find(eventId) != channels_.end())
            {
                Log::Warning("[EventManager] Channel {} already registered.", eventId);
                return eastl::unexpected(EventError::ChannelAlreadyRegistered);
            }
            channels_[eventId] = channel;
            return {};
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

        eastl::expected<void, EventError> ProcessAllDeferred(asIScriptContext* sharedCtx) override
        {
            if (!sharedCtx)
                return eastl::unexpected(EventError::ContextPreparationFailed);

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
                auto result = channel->ProcessDeferred();
                if (!result.has_value())
                {
                    Log::Error("[EventManager] Failed to process deferred events for a channel: {}",
                               static_cast<int>(result.error()));
                    // Continue processing other channels? Yes.
                }
            }
            return {};
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
            // We only want to clear their state during reload.
        }

        eastl::vector<ChannelDescriptor> GetAllDescriptors() const override
        {
            std::scoped_lock lock(mutex_);
            eastl::vector<ChannelDescriptor> result;
            result.reserve(channels_.size());
            for (const auto& pair : channels_)
            {
                if (pair.second)
                    result.push_back(pair.second->GetDescriptor());
            }
            return result;
        }

    private:
        mutable std::mutex mutex_;
        eastl::vector_map<uint32_t, IEventChannel*> channels_;
    };
} // namespace AngelEngine
