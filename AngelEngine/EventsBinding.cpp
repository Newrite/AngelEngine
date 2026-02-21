module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <print>

export module AngelEngine.EventsBinding;

import AngelEngine.Interfaces;
import AngelEngine.EventChannel;
import AngelEngine.Logger;

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
            if (eventManager_)
            {
                auto r1 = eventManager_->RegisterChannel(EventsName::OnTick, &tickChannel_);
                if (!r1.has_value()) Log::Error("[EventBinding] Failed to register OnTick channel.");
                
                auto r2 = eventManager_->RegisterChannel(EventsName::OnLoad, &loadChannel_);
                if (!r2.has_value()) Log::Error("[EventBinding] Failed to register OnLoad channel.");
                
                auto r3 = eventManager_->RegisterChannel(EventsName::OnSave, &saveChannel_);
                if (!r3.has_value()) Log::Error("[EventBinding] Failed to register OnSave channel.");
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

        // --- TICK ---
        void SubscribeTick(asIScriptFunction* callback)
        {
            if (!callback) return;
            // Channel сам сделает AddRef(). 
            // AS сам сделает Release() после возврата из функции благодаря @+.
            tickChannel_.AddSubscriber(callback); 
        }
        
        void BindTick(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void TickCallback(float)");
            if (r < 0) Log::Error("[EventBinding] Failed to register TickCallback funcdef: {}", r);

            r = engine->RegisterGlobalFunction(
                "void SubscribeTick(TickCallback@+)", 
                asFUNCTION(SubscribeTickWrapper), 
                asCALL_CDECL_OBJLAST, 
                this
            );
            if (r < 0) Log::Error("[EventBinding] Failed to register SubscribeTick: {}", r);
        }
        
        // --- LOAD ---
        void SubscribeLoad(asIScriptFunction* callback)
        {
            if (!callback) return;
            loadChannel_.AddSubscriber(callback);
        }
        
        void BindLoad(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void LoadCallback()");
            if (r < 0) Log::Error("[EventBinding] Failed to register LoadCallback funcdef: {}", r);

            r = engine->RegisterGlobalFunction(
                "void SubscribeLoad(LoadCallback@+)", 
                asFUNCTION(SubscribeLoadWrapper), 
                asCALL_CDECL_OBJLAST, 
                this
            );
            if (r < 0) Log::Error("[EventBinding] Failed to register SubscribeLoad: {}", r);
        }
        
        // --- SAVE ---
        void SubscribeSave(asIScriptFunction* callback)
        {
            if (!callback) return;
            saveChannel_.AddSubscriber(callback);
        }
        
        void BindSave(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void SaveCallback()");
            if (r < 0) Log::Error("[EventBinding] Failed to register SaveCallback funcdef: {}", r);

            r = engine->RegisterGlobalFunction(
                "void SubscribeSave(SaveCallback@+)",
                asFUNCTION(SubscribeSaveWrapper), 
                asCALL_CDECL_OBJLAST, 
                this
            );
            if (r < 0) Log::Error("[EventBinding] Failed to register SubscribeSave: {}", r);
        }

        void PushTick(float dt) { tickChannel_.Enqueue(dt); }
        void PushLoad() { loadChannel_.Enqueue(); }
        void PushSave() { saveChannel_.Enqueue(); }
    
    private:
        static void SubscribeTickWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeTick(callback);
            // Если self == null, мы всё равно ничего не делаем. 
            // AS сам подчистит callback благодаря авто-хэндлу @+
        }

        static void SubscribeLoadWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeLoad(callback);
        }

        static void SubscribeSaveWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeSave(callback);
        }

        IEventManager* eventManager_;
        
        EventChannel<float> tickChannel_;
        EventChannel<> loadChannel_;
        EventChannel<> saveChannel_;
    };
}