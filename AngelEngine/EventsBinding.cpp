module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <asbind20/asbind.hpp>
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
        constexpr uint32_t OnTick = HashString("OnTick");
        constexpr uint32_t OnSave = HashString("OnSave");
        constexpr uint32_t OnLoad = HashString("OnLoad");
    } // namespace EventsName

    export class EventBinding final : public IScriptBinding, public IBuiltinEventDispatcher
    {
    public:
        explicit EventBinding(IEventManager* eventManager) : eventManager_(eventManager)
        {
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

        void Bind(asIScriptEngine* engine) override
        {
            BindTick(engine);
            BindLoad(engine);
            BindSave(engine);
        }

        // --- TICK ---
        void SubscribeTick(asIScriptFunction* cb)
        {
            if (!cb)
                return;
            tickChannel_.AddSubscriber(asbind20::script_function<void(float)>(cb));
        }

        void BindTick(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void TickCallback(float)");
            if (r < 0)
                Log::Error("[EventBinding] Failed to register TickCallback funcdef: {}", r);

            asbind20::global(engine).function("void SubscribeTick(TickCallback@+)", &EventBinding::SubscribeTick,
                                              asbind20::auxiliary(*this));
        }

        // --- LOAD ---
        void SubscribeLoad(asIScriptFunction* cb)
        {
            if (!cb)
                return;
            loadChannel_.AddSubscriber(asbind20::script_function<void()>(cb));
        }

        void BindLoad(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void LoadCallback()");
            if (r < 0)
                Log::Error("[EventBinding] Failed to register LoadCallback funcdef: {}", r);

            asbind20::global(engine).function("void SubscribeLoad(LoadCallback@+)", &EventBinding::SubscribeLoad,
                                              asbind20::auxiliary(*this));
        }

        // --- SAVE ---
        void SubscribeSave(asIScriptFunction* cb)
        {
            if (!cb)
                return;
            saveChannel_.AddSubscriber(asbind20::script_function<void()>(cb));
        }

        void BindSave(asIScriptEngine* engine)
        {
            int r = engine->RegisterFuncdef("void SaveCallback()");
            if (r < 0)
                Log::Error("[EventBinding] Failed to register SaveCallback funcdef: {}", r);

            asbind20::global(engine).function("void SubscribeSave(SaveCallback@+)", &EventBinding::SubscribeSave,
                                              asbind20::auxiliary(*this));
        }

        // --- IBuiltinEventDispatcher ---
        // Called by ExecutionManager::Tick with the tick's pooled context.
        // Replaces the old PushTick/PushLoad/PushSave + Enqueue round-trip.
        void DispatchBuiltinEvents(asIScriptContext* ctx, float dt) override
        {
            tickChannel_.Dispatch(ctx, dt);
            // Load/Save are fired manually by the engine (not every tick),
            // so they remain Enqueue-based — only dispatched when explicitly queued.
        }

        // Enqueue-based tick push (benchmark baseline / compatibility helper).
        // The new production path uses DispatchBuiltinEvents. This remains
        // available for benchmarking and external callers that need the old
        // deferred semantics.
        void PushTick(float dt) { tickChannel_.Enqueue(dt); }

        // Legacy push methods (still used for Load/Save triggered externally)
        void PushLoad() { loadChannel_.Enqueue(); }
        void PushSave() { saveChannel_.Enqueue(); }

    private:
        IEventManager* eventManager_;

        EventChannel<float> tickChannel_;
        EventChannel<> loadChannel_;
        EventChannel<> saveChannel_;
    };
} // namespace AngelEngine
