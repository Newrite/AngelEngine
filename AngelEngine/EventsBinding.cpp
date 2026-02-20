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
        constexpr uint32_t OnSave = HashString("OnSave");
        constexpr uint32_t OnLoad = HashString("OnLoad");
    }
    
    export class EventBinding final : public IScriptBinding
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager)
        {
        }

        void Bind(asIScriptEngine* engine) override
        {
            BindTick(engine);
            BindLoad(engine);
            BindSave(engine);
        }

        void SubscribeTick(asIScriptFunction* callback)
        {
            if (!callback) return;
            if (eventManager_) eventManager_->Subscribe(EventsName::OnTick, callback);
        }
        
        void BindTick(asIScriptEngine* engine)
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
        
        void SubscribeLoad(asIScriptFunction* callback)
        {
            if (!callback) return;
            if (eventManager_) eventManager_->Subscribe(EventsName::OnLoad, callback);
        }
        
        void BindLoad(asIScriptEngine* engine)
        {
            engine->RegisterFuncdef("void LoadCallback()");

            int r = engine->RegisterGlobalFunction(
                "void SubscribeLoad(LoadCallback@+)", 
                asMETHOD(EventBinding, SubscribeLoad), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeLoad. Code: {}", r);
        }
        
        void SubscribeSave(asIScriptFunction* callback)
        {
            if (!callback) return;
            if (eventManager_) eventManager_->Subscribe(EventsName::OnSave, callback);
        }
        
        void BindSave(asIScriptEngine* engine)
        {
            engine->RegisterFuncdef("void SaveCallback()");

            int r = engine->RegisterGlobalFunction(
                "void SubscribeLoad(SaveCallback@+)", 
                asMETHOD(EventBinding, SubscribeSave), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeSave. Code: {}", r);
        }
    
    private:
        IEventManager* eventManager_;
    };
}