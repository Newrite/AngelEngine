module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <print>


export module AngelEngine.EventsBinding;

import AngelEngine.Interfaces;

namespace AngelEngine
{
    export namespace EventsName
    {
        constexpr uint32_t OnTick = HashString("OnTick");
    }
    
    export class EventBinding final : public IScriptBinding
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager)
        {
        }

        void Bind(asIScriptEngine* engine) override
        {
            engine->RegisterFuncdef("void TickCallback(float)");

            int r = engine->RegisterGlobalFunction(
                "void SubscribeTick(TickCallback@+)", 
                asMETHOD(EventBinding, SubscribeTick), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeTick. Code: {}", r);
        }

        void SubscribeTick(asIScriptFunction* callback)
        {
            if (!callback) return;
            if (eventManager_) eventManager_->Subscribe(EventsName::OnTick, callback);
        }
    
    private:
        IEventManager* eventManager_;
    };
}