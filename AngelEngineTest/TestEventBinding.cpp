module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <print>


export module AngelEngineTest.EventsBinding;

import AngelEngine.Interfaces;

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
            if (eventManager_) eventManager_->Subscribe(EventsName::CustomEvent, callback);
        }
        
        void SubscribeDeferredEvent(asIScriptFunction* callback)
        {
            if (!callback) return;
            if (eventManager_) eventManager_->Subscribe(EventsName::DeferredEvent, callback);
        }
    
    private:
        AngelEngine::IEventManager* eventManager_;
    };
}