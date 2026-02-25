module;
#include <cstddef>

export module AngelEngine.NativeViewArray;

namespace AngelEngine
{
    /// @brief Read-only lightweight view into an eastl::vector for zero-copy AngelScript passing.
    /// This is instantiated per-tick for deferred events and pushed to JIT directly.
    export template <typename T>
    class NativeViewArray
    {
    public:
        NativeViewArray() : data_(nullptr), size_(0) {}
        NativeViewArray(const T* data, size_t size) : data_(data), size_(size) {}

        size_t GetSize() const { return size_; }
        bool IsEmpty() const { return size_ == 0; }

        const T& At(size_t index) const
        {
            // Note: AS VM must guarantee index < size_ since scripts are bound by get_length
            return data_[index];
        }

    private:
        const T* data_;
        size_t size_;
    };
} // namespace AngelEngine
