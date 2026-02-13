#include <new>
#include <cstdint>

// EASTL New Operators implementation
// Required because we use EASTL without a custom allocator configuration that overrides this requirement.

void* operator new[](size_t size, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return new uint8_t[size];
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    // Using C++17 aligned new
    return new (std::align_val_t(alignment)) uint8_t[size];
}
