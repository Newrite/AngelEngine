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
            // Register channels
            if (eventManager_)
            {
                // We ignore errors here as this is constructor, but we should log if possible.
                // However, Logger might not be ready? Assuming it is.
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

        void SubscribeTick(asIScriptFunction* callback)
        {
            if (!callback) return;
            tickChannel_.AddSubscriber(callback);
            // Release the callback reference that was added by AngelScript when passing it to this function
            // AngelScript increments ref count when passing as handle @+
            // But wait, if we store it, we should keep the ref.
            // tickChannel_.AddSubscriber calls AddRef internally.
            // So we should release the one passed to us if it was an auto-handle?
            // The signature is "TickCallback@+". The '+' means auto-handle, so AS passes ownership to us.
            // If AddSubscriber adds another ref, we have 2 refs.
            // If we don't release, we leak one.
            // Let's check EventChannel::AddSubscriber.
            // It does func->AddRef().
            // So yes, we should release the one we got from AS if we want to be correct with auto-handles.
            // OR we can change AddSubscriber to take ownership (consume ref).
            // But EventChannel is generic.
            // Let's just Release here.
            (void)callback->Release();
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
        
        void SubscribeLoad(asIScriptFunction* callback)
        {
            if (!callback) return;
            loadChannel_.AddSubscriber(callback);
            (void)callback->Release();
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
        
        void SubscribeSave(asIScriptFunction* callback)
        {
            if (!callback) return;
            saveChannel_.AddSubscriber(callback);
            (void)callback->Release();
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

        // Public accessors for pushing events
        void PushTick(float dt) { tickChannel_.Enqueue(dt); }
        void PushLoad() { loadChannel_.Enqueue(); }
        void PushSave() { saveChannel_.Enqueue(); }
    
    private:
        // Wrappers for CDECL_OBJLAST
        static void SubscribeTickWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeTick(callback);
            else if (callback) (void)callback->Release();
        }

        static void SubscribeLoadWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeLoad(callback);
            else if (callback) (void)callback->Release();
        }

        static void SubscribeSaveWrapper(asIScriptFunction* callback, EventBinding* self)
        {
            if (self) self->SubscribeSave(callback);
            else if (callback) (void)callback->Release();
        }

        IEventManager* eventManager_;
        
        // Channels
        EventChannel<float> tickChannel_;
        EventChannel<> loadChannel_;
        EventChannel<> saveChannel_;
    };
}