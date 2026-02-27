module;

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <angelscript.h>

export module AngelEngine.EventsInterfaces;

namespace AngelEngine
{
    // Describes how an event channel should appear in the AS-side dispatcher.
    // Provided by each channel; collected by EventManager for code generation.
    export struct ChannelDescriptor
    {
        eastl::string eventName; // e.g. "Tick"
        eastl::string funcdefDecl; // e.g. "void TickCallback(float)"
        eastl::string callbackType; // e.g. "TickCallback"
        eastl::string argDecl; // e.g. "float dt"
        eastl::string dispatchArgs; // e.g. "dt"

        bool isDeferred = false;
        eastl::vector<eastl::string> argTypes;
        eastl::vector<eastl::string> argNames;
    };

    // Interface for dispatching built-in engine events (OnTick, OnSave, OnLoad) synchronously.
    // Implemented by EventBinding; passed to ExecutionManager::Tick so dispatch can run
    // with the already-acquired context — no queue round-trip.
    export struct IBuiltinEventDispatcher
    {
        virtual ~IBuiltinEventDispatcher() = default;
        virtual void DispatchBuiltinEvents(asIScriptContext* ctx, float dt) = 0;
    };
} // namespace AngelEngine
