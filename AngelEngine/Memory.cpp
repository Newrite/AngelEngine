module;
#include <mimalloc.h>

export module AngelEngine.Memory;

namespace AngelEngine
{
    // Export explicit wrappers for AngelScript
    export void* EngineAlloc(size_t size) { return mi_malloc(size); }
    export void EngineFree(void* ptr) { mi_free(ptr); }
}