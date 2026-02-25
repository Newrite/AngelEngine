module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <ostream>
#include <print>


export module AngelEngine.EventsBinding;

import AngelEngine.Interfaces;
import AngelEngine.EventChannel;
import AngelEngine.Logger;

namespace AngelEngine
{
    export namespace EventsName
    {
        constexpr uint32_t OnTick = HashString("Tick");
        constexpr uint32_t OnSave = HashString("Save");
        constexpr uint32_t OnLoad = HashString("Load");
    } // namespace EventsName

    export class EventBinding final : public IBuiltinEventDispatcher
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager)
        {
            // Tick: direct dispatch (synchronous, every game frame)
            if (eventManager_)
            {
                auto r1 = eventManager_->RegisterChannel(EventsName::OnTick, &tickChannel_);
                if (!r1.has_value())
                    Log::Error("[EventBinding] Failed to register OnTick channel.");

                auto r2 = eventManager_->RegisterChannel(EventsName::OnLoad, &loadChannel_);
                if (!r2.has_value())
                    Log::Error("[EventBinding] Failed to register OnLoad channel.");

                auto r3 = eventManager_->RegisterChannel(EventsName::OnSave, &saveChannel_);
                if (!r3.has_value())
                    Log::Error("[EventBinding] Failed to register OnSave channel.");
            }
        }

        ~EventBinding() override
        {
            if (eventManager_)
            {
                eventManager_->UnregisterChannel(EventsName::OnTick);
                eventManager_->UnregisterChannel(EventsName::OnLoad);
                eventManager_->UnregisterChannel(EventsName::OnSave);
            }
        }


        // --- IBuiltinEventDispatcher ---
        // Called by ExecutionManager::Tick with the tick's pooled context.
        // tickChannel_.Dispatch() → 1 C++→AS call → AS dispatcher loops all subscribers (AS→AS).
        void DispatchBuiltinEvents(asIScriptContext* ctx, float dt) override
        {
            tickChannel_.Dispatch(dt);
            // Load/Save remain Enqueue-based (fired manually, not every tick).
        }

        // Enqueue-based push methods (deferred events)
        void PushLoad() { loadChannel_.Enqueue(); }
        void PushSave() { saveChannel_.Enqueue(); }

        // Benchmark compat: still lets tests push ticks via the deferred path if needed
        void PushTick(float dt) { tickChannel_.Enqueue(dt); }

    private:
        IEventManager* eventManager_;

        AngelEngine::EventChannel<float> tickChannel_{AngelEngine::ChannelDescriptor{
            "Tick", "funcdef void TickCallback(float)", "TickCallback", "float dt", "dt"}};

        AngelEngine::EventChannel<> loadChannel_{
            AngelEngine::ChannelDescriptor{"Load", "funcdef void LoadCallback()", "LoadCallback", "", "", true}};

        AngelEngine::EventChannel<> saveChannel_{
            AngelEngine::ChannelDescriptor{"Save", "funcdef void SaveCallback()", "SaveCallback", "", "", true}};
    };
} // namespace AngelEngine
