module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <print>


export module AngelEngine.EventsBinding;

import AngelEngine.Interfaces;
import AngelEngine.EventChannel;

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
            // Register channels
            if (eventManager_)
            {
                eventManager_->RegisterChannel(EventsName::OnTick, &tickChannel_);
                eventManager_->RegisterChannel(EventsName::OnLoad, &loadChannel_);
                eventManager_->RegisterChannel(EventsName::OnSave, &saveChannel_);
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

        void Bind(asIScriptEngine* engine) override
        {
            BindTick(engine);
            BindLoad(engine);
            BindSave(engine);
        }

        void SubscribeTick(asIScriptFunction* callback)
        {
            if (!callback) return;
            tickChannel_.AddSubscriber(callback);
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
            loadChannel_.AddSubscriber(callback);
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
            saveChannel_.AddSubscriber(callback);
        }
        
        void BindSave(asIScriptEngine* engine)
        {
            engine->RegisterFuncdef("void SaveCallback()");

            int r = engine->RegisterGlobalFunction(
                "void SubscribeSave(SaveCallback@+)",
                asMETHOD(EventBinding, SubscribeSave), 
                asCALL_THISCALL_ASGLOBAL, 
                this
            );
            if (r < 0) std::println(stderr, "[EventBinding] Failed to register SubscribeSave. Code: {}", r);
        }

        // Public accessors for pushing events
        void PushTick(float dt) { tickChannel_.Enqueue(dt); }
        void PushLoad() { loadChannel_.Enqueue(); }
        void PushSave() { saveChannel_.Enqueue(); }
    
    private:
        IEventManager* eventManager_;
        
        // Channels
        EventChannel<float> tickChannel_;
        EventChannel<> loadChannel_;
        EventChannel<> saveChannel_;
    };
}