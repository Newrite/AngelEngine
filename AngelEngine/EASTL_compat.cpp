#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdarg>

// EASTL New Operators implementation
// Required because we use EASTL without a custom allocator configuration that overrides this requirement.

void* operator new[](size_t size, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    return new uint8_t[size];
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* /*name*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/)
{
    // ВАЖНО: Мы не используем C++17 std::align_val_t.
    // Дефолтный деаллокатор EASTL вызывает стандартный delete[], который мапится в free().
    // Использование std::align_val_t вызовет _aligned_malloc, что приведет к крашу 
    // _CrtIsValidHeapPointer при попытке сделать free().
    // На x64 стандартный new дает выравнивание 16 байт, что достаточно.
    return new uint8_t[size];
}

// Предоставляем реализацию Vsnprintf, которую ожидает EASTL, 
// и проксируем её в стандартную C-библиотеку.
// Обязательно используем __restrict, иначе MSVC/clang-cl 
// сгенерирует другое mangled-имя и линкер выдаст unresolved external.
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
