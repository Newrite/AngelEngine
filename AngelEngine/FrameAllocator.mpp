module;

#include <EASTL/vector.h>

export module AngelEngine.FrameAllocator;

import AngelEngine.Logger;

namespace AngelEngine
{
    export class FrameMemoryPool
    {
    public:
        static constexpr size_t POOL_SIZE = 2 * 1024 * 1024; // 2MB

        static FrameMemoryPool& Get()
        {
            static FrameMemoryPool instance;
            return instance;
        }

        void* Allocate(size_t size, size_t alignment)
        {
            uintptr_t currentAddress = reinterpret_cast<uintptr_t>(buffer_ + offset_);
            uintptr_t alignedAddress = (currentAddress + (alignment - 1)) & ~(alignment - 1);
            
            size_t padding = alignedAddress - currentAddress;
            size_t totalSize = size + padding;

            if (offset_ + totalSize <= POOL_SIZE)
            {
                offset_ += totalSize;
                return reinterpret_cast<void*>(alignedAddress);
            }

            Log::Warning("FrameMemoryPool overflow! Using fallback allocator. Size: {}, Alignment: {}", size, alignment);
            
            // Fallback to standard new
            void* ptr = ::operator new(size);
            fallback_allocations_.push_back(ptr);
            return ptr;
        }

        void Reset()
        {
            offset_ = 0;
            if (!fallback_allocations_.empty())
            {
                for (void* ptr : fallback_allocations_)
                {
                    ::operator delete(ptr);
                }
                fallback_allocations_.clear();
            }
        }

    private:
        FrameMemoryPool() = default;
        ~FrameMemoryPool() { Reset(); }

        alignas(16) char buffer_[POOL_SIZE];
        size_t offset_ = 0;
        eastl::vector<void*> fallback_allocations_;
    };

    export class LinearFrameAllocator
    {
    public:
        LinearFrameAllocator(const char* name = "LinearFrameAllocator") : name_(name) {}
        LinearFrameAllocator(const LinearFrameAllocator&) = default;
        LinearFrameAllocator& operator=(const LinearFrameAllocator&) = default;

        void* allocate(size_t n, int flags = 0)
        {
            return FrameMemoryPool::Get().Allocate(n, 8);
        }

        void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0)
        {
            return FrameMemoryPool::Get().Allocate(n, alignment);
        }

        void deallocate(void* p, size_t n)
        {
            // No-op
        }

        const char* get_name() const { return name_; }
        void set_name(const char* name) { name_ = name; }

        bool operator==(const LinearFrameAllocator&) const { return true; }
        bool operator!=(const LinearFrameAllocator&) const { return false; }

    private:
        const char* name_;
    };
}