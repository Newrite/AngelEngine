module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <print>


export module AngelEngineTest.EventsBinding;

import AngelEngine.Interfaces;
import AngelEngine.EventChannel;

namespace AngelEngineTest
{
    export namespace EventsName
    {
        constexpr uint32_t DeferredEvent = AngelEngine::HashString("DeferredEvent");
        constexpr uint32_t CustomEvent = AngelEngine::HashString("CustomEvent");
    }
    
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

            int r = engine->RegisterGlobalFunction(
                "void SubscribeCustomEvent(CustomEventCallback@+)", 
                asMETHOD(TestEventBinding, SubscribeCustomEvent), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeCustomEvent. Code: {}", r);
            
            r = engine->RegisterGlobalFunction(
                "void SubscribeDeferredEvent(DeferredCallback@+)", 
                asMETHOD(TestEventBinding, SubscribeDeferredEvent), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeDeferredEvent. Code: {}", r);
        }

        void SubscribeCustomEvent(asIScriptFunction* callback)
        {
            if (!callback) return;
            customEventChannel_.AddSubscriber(callback);
        }
        
        void SubscribeDeferredEvent(asIScriptFunction* callback)
        {
            if (!callback) return;
            deferredEventChannel_.AddSubscriber(callback);
        }

        // Public accessors for pushing events
        void PushCustomEvent(int val1, float val2) { customEventChannel_.Enqueue(val1, val2); }
        void PushDeferredEvent() { deferredEventChannel_.Enqueue(); }
    
    private:
        AngelEngine::IEventManager* eventManager_;
        
        // Channels
        AngelEngine::EventChannel<int, float> customEventChannel_;
        AngelEngine::EventChannel<> deferredEventChannel_;
    };
}