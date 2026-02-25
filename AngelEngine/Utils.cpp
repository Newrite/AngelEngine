module;

#include <EASTL/string.h>
#include <angelscript.h>
#include <cstdint>


export module AngelEngine.Utils;

namespace AngelEngine
{
    // --- String Hashing (Compile-Time & Runtime) ---
    export constexpr uint32_t HashString(const char* str)
    {
        uint32_t hash = 2166136261u;
        while (*str)
        {
            hash ^= static_cast<uint32_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }

    export constexpr uint32_t HashString(const eastl::string& str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    // --- Helper functions for EventChannel ---
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, float val) { ctx->SetArgFloat(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, double val) { ctx->SetArgDouble(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, int32_t val)
    {
        ctx->SetArgDWord(argIndex, static_cast<asDWORD>(val));
    }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, uint32_t val) { ctx->SetArgDWord(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, int64_t val)
    {
        ctx->SetArgQWord(argIndex, static_cast<asQWORD>(val));
    }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, uint64_t val) { ctx->SetArgQWord(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, bool val) { ctx->SetArgByte(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, void* val) { ctx->SetArgAddress(argIndex, val); }

    // Generic overload for value-type objects registered with asOBJ_VALUE.
    // Less specialized than the above scalar overloads — compiler picks exact matches first.
    // T must be registered in the AS engine via RegisterObjectType with asOBJ_VALUE.
    export template <typename T>
    inline void SetArg(asIScriptContext* ctx, asUINT argIndex, T& val)
    {
        // AngelScript's SetArgObject takes void*, but for value types (asOBJ_VALUE)
        // it only reads the data to make a copy.
        // We use const_cast here because EventChannel::ProcessDeferred passes
        // elements from a const tuple/vector queue.
        ctx->SetArgObject(argIndex, const_cast<void*>(static_cast<const void*>(&val)));
    }
} // namespace AngelEngine
