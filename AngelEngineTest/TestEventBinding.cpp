module;

#include <EASTL/string.h>
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
            }
        }

        ~TestEventBinding() override
        {
            if (eventManager_)
            {
                eventManager_->UnregisterChannel(EventsName::CustomEvent);
                eventManager_->UnregisterChannel(EventsName::DeferredEvent);
            }
        }

        void Bind(asIScriptEngine* engine) override
        {
            engine->RegisterFuncdef("void CustomEventCallback(int, float)");
            engine->RegisterFuncdef("void DeferredCallback()");

            asbind20::global(engine)
                .function("void SubscribeCustomEvent(CustomEventCallback@+)", &TestEventBinding::SubscribeCustomEvent,
                          asbind20::auxiliary(*this))
                .function("void SubscribeDeferredEvent(DeferredCallback@+)", &TestEventBinding::SubscribeDeferredEvent,
                          asbind20::auxiliary(*this));
        }

        void SubscribeCustomEvent(asIScriptFunction* cb)
        {
            if (!cb)
                return;
            customEventChannel_.AddSubscriber(asbind20::script_function<void(int, float)>(cb));
        }

        void SubscribeDeferredEvent(asIScriptFunction* cb)
        {
            if (!cb)
                return;
            deferredEventChannel_.AddSubscriber(asbind20::script_function<void()>(cb));
        }

        // Public accessors for pushing events
        void PushCustomEvent(int val1, float val2) { customEventChannel_.Enqueue(val1, val2); }
        void PushDeferredEvent() { deferredEventChannel_.Enqueue(); }

        void ClearAllEvents()
        {
            customEventChannel_.Clear();
            deferredEventChannel_.Clear();
        }

    private:
        AngelEngine::IEventManager* eventManager_;

        // Channels
        AngelEngine::EventChannel<int, float> customEventChannel_;
        AngelEngine::EventChannel<> deferredEventChannel_;
    };
} // namespace AngelEngineTest
