#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <mimalloc.h>

// ------------------------------------------------------------------------
// 1. СТАНДАРТНЫЕ C++ АЛЛОКАТОРЫ (Этого нам не хватало)
// ------------------------------------------------------------------------
void* operator new(size_t size) { return mi_malloc(size); }
void* operator new[](size_t size) { return mi_malloc(size); }
void* operator new(size_t size, std::align_val_t al) { return mi_malloc_aligned(size, static_cast<size_t>(al)); }
void* operator new[](size_t size, std::align_val_t al) { return mi_malloc_aligned(size, static_cast<size_t>(al)); }

// ------------------------------------------------------------------------
// 2. EASTL АЛЛОКАТОРЫ
// ------------------------------------------------------------------------
void* operator new[](size_t size, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    return mi_malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    return mi_malloc_aligned(size, alignment);
}

// ------------------------------------------------------------------------
// 3. ГЛОБАЛЬНЫЕ ДЕАЛЛОКАТОРЫ
// ------------------------------------------------------------------------
void operator delete(void* p) noexcept { mi_free(p); }
void operator delete[](void* p) noexcept { mi_free(p); }
void operator delete(void* p, size_t) noexcept { mi_free(p); }
void operator delete[](void* p, size_t) noexcept { mi_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { mi_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { mi_free(p); }

// ------------------------------------------------------------------------
// 4. EASTL СОВМЕСТИМОСТЬ
// ------------------------------------------------------------------------
namespace EA
{
    namespace StdC
    {
        int Vsnprintf(char* __restrict pDestination, size_t n, const char* __restrict pFormat, va_list arguments)
        {
            return std::vsnprintf(pDestination, n, pFormat, arguments);
        }
    }
}