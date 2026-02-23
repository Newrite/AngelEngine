module;

#include <EABase/eabase.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <angelscript.h>
#include <asbind20/asbind.hpp>


export module AngelEngineTest.EventsBinding;

import AngelEngine.Interfaces;
import AngelEngine.EventChannel;

namespace AngelEngineTest
{
    export namespace EventsName
    {
        constexpr uint32_t DeferredEvent = AngelEngine::HashString("DeferredEvent");
        constexpr uint32_t CustomEvent = AngelEngine::HashString("CustomEvent");
        constexpr uint32_t OnTick = AngelEngine::HashString("Tick");
    } // namespace EventsName

    export class TestEventBinding final : public AngelEngine::IScriptBinding
    {
    public:
        explicit TestEventBinding(AngelEngine::IEventManager* eventManager) : eventManager_(eventManager)
        {
            if (eventManager_)
            {
                eventManager_->RegisterChannel(EventsName::CustomEvent, &customEventChannel_);
                eventManager_->RegisterChannel(EventsName::DeferredEvent, &deferredEventChannel_);
                eventManager_->RegisterChannel(EventsName::OnTick, &tickChannel_);
            }
        }

        ~TestEventBinding() override
        {
            if (eventManager_)
            {
                eventManager_->UnregisterChannel(EventsName::CustomEvent);
                eventManager_->UnregisterChannel(EventsName::DeferredEvent);
                eventManager_->UnregisterChannel(EventsName::OnTick);
            }
        }

        void Bind(asIScriptEngine* engine) override
        {
            // Registration now handled by AS-side dispatcher section
        }


        // Public accessors for pushing events
        void PushCustomEvent(int val1, float val2) { customEventChannel_.Enqueue(val1, val2); }
        void PushDeferredEvent() { deferredEventChannel_.Enqueue(); }
        void PushTick(float dt) { tickChannel_.Enqueue(dt); }

        // Inject the AS dispatcher function pointer after module (re)compile.
        void SetTickDispatcherFn(asIScriptEngine* engine, AngelEngine::IContextPooling* pool, asIScriptFunction* fn)
        {
            tickChannel_.SetDispatcherFn(engine, pool, fn);
        }
        void SetCustomEventDispatcherFn(asIScriptEngine* engine, AngelEngine::IContextPooling* pool,
                                        asIScriptFunction* fn)
        {
            customEventChannel_.SetDispatcherFn(engine, pool, fn);
        }
        void SetDeferredEventDispatcherFn(asIScriptEngine* engine, AngelEngine::IContextPooling* pool,
                                          asIScriptFunction* fn)
        {
            deferredEventChannel_.SetDispatcherFn(engine, pool, fn);
        }

        void ClearAllEvents()
        {
            customEventChannel_.Clear();
            deferredEventChannel_.Clear();
            tickChannel_.Clear();
        }

    private:
        AngelEngine::IEventManager* eventManager_;

        // Channels with full descriptors for AS-side generation
        AngelEngine::EventChannel<int, float> customEventChannel_{
            AngelEngine::ChannelDescriptor{"CustomEvent", "funcdef void CustomEventCallback(int, float)",
                                           "CustomEventCallback", "int val, float delta", "val, delta"}};

        AngelEngine::EventChannel<> deferredEventChannel_{AngelEngine::ChannelDescriptor{
            "DeferredEvent", "funcdef void DeferredCallback()", "DeferredCallback", "", ""}};

        AngelEngine::EventChannel<float> tickChannel_{AngelEngine::ChannelDescriptor{
            "Tick", "funcdef void TickCallback(float)", "TickCallback", "float dt", "dt"}};
    };
} // namespace AngelEngineTest
