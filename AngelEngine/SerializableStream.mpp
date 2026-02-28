module;

#include <angelscript.h>

export module AngelEngine.SerializableStream;

namespace AngelEngine
{
    /**
     * @brief A generic IBinaryStream adapter that redirects Read/Write to consumer-provided lambdas.
     *
     * This is useful for integrating AngelEngine serialization with external persistence
     * systems, such as an SKSE plugin's CO-Save interface.
     *
     * Example usage inside an SKSE Serialization handler:
     *
     *   auto reader = [](void* ptr, asUINT size) -> int {
     *       return skseInterface->ReadRecordData(ptr, size) ? 0 : -1;
     *   };
     *
     *   auto writer = [](const void* ptr, asUINT size) -> int {
     *       return skseInterface->WriteRecordData(ptr, size) ? 0 : -1;
     *   };
     *
     *   SerializableStream stream(reader, writer);
     *   handler->Restore(engine, ptr, &stream);
     */
    export template <typename ReadFn, typename WriteFn>
    class SerializableStream : public asIBinaryStream
    {
    public:
        SerializableStream(ReadFn readFn, WriteFn writeFn) : readFn_(readFn), writeFn_(writeFn) {}

        int Read(void* ptr, asUINT size) override { return readFn_(ptr, size); }

        int Write(const void* ptr, asUINT size) override { return writeFn_(ptr, size); }

    private:
        ReadFn readFn_;
        WriteFn writeFn_;
    };
} // namespace AngelEngine
