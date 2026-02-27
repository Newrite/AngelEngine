module;

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <angelscript.h>
#include <scriptarray.h>
#include <scriptdictionary.h>

#include <cstring>

export module AngelEngine.SerializationHandlers;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

// Forward declarations from SaveLoadManager
class ByteStreamWriter;
class ByteStreamReader;

// AngelScript type IDs
constexpr int asTYPEID_STRING = 52;

namespace AngelEngine
{
    // Forward declaration - defined in SaveLoadManager module
    const char* GetPrimitiveTypeName(int typeId);

    // =======================================================================
    // ArraySerializationHandler
    // =======================================================================
    // Handles serialization of array<T> for any element type T.
    // 
    // Problem: array<T> is registered as asOBJ_TEMPLATE | asOBJ_REF,
    // so SaveValueInternal() treats it as SCRIPTOBJECT and iterates
    // properties — but arrays have ZERO properties, causing data loss.
    //
    // Solution: Custom handler that:
    // 1. Saves: writes element count, then serializes each element recursively
    // 2. Restores: reads count, resizes array, deserializes each element
    // =======================================================================
    export class ArraySerializationHandler : public ISerializationHandler
    {
    public:
        ArraySerializationHandler() : engine_(nullptr), serializer_(nullptr) {}

        void SetSerializer(void* serializer) override
        {
            serializer_ = serializer;
        }

        void SetEngine(asIScriptEngine* engine) override
        {
            engine_ = engine;
        }

        bool CanHandle(int typeId) const override
        {
            if (!engine_)
                return false;

            asITypeInfo* type = engine_->GetTypeInfoById(typeId);
            if (!type)
                return false;

            // Check if this is array<T>
            // Arrays are registered with asOBJ_TEMPLATE | asOBJ_REF
            const char* name = type->GetName();
            return (type->GetFlags() & asOBJ_TEMPLATE) &&
                   (type->GetFlags() & asOBJ_REF) &&
                   name != nullptr &&
                   eastl::string(name) == "array";
        }

        const char* GetTypeName() const override
        {
            return "array";
        }

        void Save(asIScriptEngine* engine, void* objectPtr, asIBinaryStream* stream) override
        {
            CScriptArray* arr = static_cast<CScriptArray*>(objectPtr);
            if (!arr)
            {
                Log::Error("[ArraySerializationHandler] Save: null array pointer");
                return;
            }

            asUINT size = arr->GetSize();
            stream->Write(&size, sizeof(size));

            if (size > 0)
            {
                // Get element type info for recursive serialization
                int elemTypeId = arr->GetElementTypeId();

                for (asUINT i = 0; i < size; ++i)
                {
                    void* elem = arr->At(i);
                    // Cast serializer to ISerializationHandler to call SerializeValue
                    if (serializer_)
                    {
                        static_cast<ISerializationHandler*>(serializer_)->SerializeValue(elem, elemTypeId, stream, true);
                    }
                }
            }
        }

        void Restore(asIScriptEngine* engine, void* ptrToHandle, asIBinaryStream* stream) override
        {
            asUINT size = 0;
            if (stream->Read(&size, sizeof(size)) < 0)
            {
                Log::Error("[ArraySerializationHandler] Restore: failed to read size");
                return;
            }

            // ptrToHandle is a handle (CScriptArray**), dereference to get actual array
            CScriptArray*& arrRef = *static_cast<CScriptArray**>(ptrToHandle);

            if (!arrRef)
            {
                Log::Error("[ArraySerializationHandler] Restore: null array handle - arrays must be pre-declared");
                return;
            }

            // Resize existing array
            arrRef->Resize(size);

            if (size > 0)
            {
                int elemTypeId = arrRef->GetElementTypeId();

                for (asUINT i = 0; i < size; ++i)
                {
                    void* elem = arrRef->At(i);
                    if (serializer_)
                    {
                        static_cast<ISerializationHandler*>(serializer_)->SerializeValue(elem, elemTypeId, stream, false);
                    }
                }
            }
        }

        bool SerializeValue(void* ptr, int typeId, asIBinaryStream* stream, bool isSave) override
        {
            // This is a stub - actual implementation is in BinarySerializer
            // This method is here to satisfy the interface but should not be called directly
            return false;
        }

    private:
        asIScriptEngine* engine_;
        void* serializer_;
    };

    // =======================================================================
    // DictionarySerializationHandler
    // =======================================================================
    // Handles serialization of dictionary (key: string -> value: any).
    //
    // Problem: Same as array — dictionary has no script properties,
    // so default serialization saves nothing.
    //
    // Solution: Custom handler that:
    // 1. Saves: writes key count, then for each entry: key string + value with type
    // 2. Restores: reads count, then reconstructs each key-value pair
    // =======================================================================
    export class DictionarySerializationHandler : public ISerializationHandler
    {
    public:
        DictionarySerializationHandler() : engine_(nullptr), serializer_(nullptr) {}

        void SetSerializer(void* serializer) override
        {
            serializer_ = serializer;
        }

        void SetEngine(asIScriptEngine* engine) override
        {
            engine_ = engine;
        }

        bool CanHandle(int typeId) const override
        {
            if (!engine_)
                return false;

            asITypeInfo* type = engine_->GetTypeInfoById(typeId);
            if (!type)
                return false;

            // Dictionary is registered as asOBJ_TEMPLATE | asOBJ_REF
            const char* name = type->GetName();
            return (type->GetFlags() & asOBJ_TEMPLATE) &&
                   (type->GetFlags() & asOBJ_REF) &&
                   name != nullptr &&
                   eastl::string(name) == "dictionary";
        }

        const char* GetTypeName() const override
        {
            return "dictionary";
        }

        void Save(asIScriptEngine* engine, void* objectPtr, asIBinaryStream* stream) override
        {
            CScriptDictionary* dict = static_cast<CScriptDictionary*>(objectPtr);
            if (!dict)
            {
                Log::Error("[DictionarySerializationHandler] Save: null dictionary pointer");
                return;
            }

            // Get all keys - GetKeys() returns a CScriptArray*
            CScriptArray* keysArray = dict->GetKeys();
            if (!keysArray)
            {
                // Empty dictionary
                asUINT keyCount = 0;
                stream->Write(&keyCount, sizeof(keyCount));
                return;
            }

            asUINT keyCount = keysArray->GetSize();
            stream->Write(&keyCount, sizeof(keyCount));

            for (asUINT i = 0; i < keyCount; ++i)
            {
                const dictKey_t* keyPtr = static_cast<const dictKey_t*>(keysArray->At(i));
                const dictKey_t& key = *keyPtr;

                // Write key string
                uint32_t keyLen = static_cast<uint32_t>(key.length());
                stream->Write(&keyLen, sizeof(keyLen));
                if (keyLen > 0)
                {
                    stream->Write(key.c_str(), keyLen);
                }

                // Get value and its type
                int valueTypeId = dict->GetTypeId(key.c_str());
                if (valueTypeId >= 0)
                {
                    // Write type info and value using serializer
                    if (serializer_)
                    {
                        // First write type marker
                        bool isHandle = (valueTypeId & asTYPEID_OBJHANDLE) != 0;
                        stream->Write(&isHandle, sizeof(isHandle));

                        // Write type declaration
                        asITypeInfo* typeInfo = engine->GetTypeInfoById(valueTypeId);
                        if (typeInfo)
                        {
                            eastl::string fullName;
                            const char* ns = typeInfo->GetNamespace();
                            if (ns && ns[0] != '\0')
                            {
                                fullName = eastl::string(ns) + "::";
                            }
                            fullName += typeInfo->GetName();

                            uint32_t len = static_cast<uint32_t>(fullName.length());
                            stream->Write(&len, sizeof(len));
                            if (len > 0)
                                stream->Write(fullName.c_str(), len);
                        }
                        else
                        {
                            // Primitive type
                            const char* primName = GetPrimitiveTypeName(valueTypeId);
                            if (primName)
                            {
                                uint32_t len = static_cast<uint32_t>(std::strlen(primName));
                                stream->Write(&len, sizeof(len));
                                if (len > 0)
                                    stream->Write(primName, len);
                            }
                        }

                        // Get value using operator[]
                        const CScriptDictValue* dictValue = (*dict)[key.c_str()];
                        if (dictValue)
                        {
                            // Write value using serializer
                            static_cast<ISerializationHandler*>(serializer_)->SerializeValue(
                                const_cast<void*>(dictValue->GetAddressOfValue()), valueTypeId, stream, true);
                        }
                    }
                }
                else
                {
                    // Null entry
                    bool isHandle = false;
                    stream->Write(&isHandle, sizeof(isHandle));
                    uint32_t nullLen = 4;
                    stream->Write(&nullLen, sizeof(nullLen));
                    stream->Write("void", 4);
                    uint32_t valSize = 0;
                    stream->Write(&valSize, sizeof(valSize));
                }
            }
        }

        void Restore(asIScriptEngine* engine, void* ptrToHandle, asIBinaryStream* stream) override
        {
            asUINT keyCount = 0;
            if (stream->Read(&keyCount, sizeof(keyCount)) < 0)
            {
                Log::Error("[DictionarySerializationHandler] Restore: failed to read key count");
                return;
            }

            CScriptDictionary*& dictRef = *static_cast<CScriptDictionary**>(ptrToHandle);

            if (!dictRef)
            {
                Log::Error("[DictionarySerializationHandler] Restore: null dictionary handle");
                return;
            }

            // Clear existing dictionary
            dictRef->DeleteAll();

            for (asUINT i = 0; i < keyCount; ++i)
            {
                // Read key
                uint32_t keyLen = 0;
                if (stream->Read(&keyLen, sizeof(keyLen)) < 0)
                {
                    Log::Error("[DictionarySerializationHandler] Restore: failed to read key length");
                    return;
                }

                dictKey_t key;
                key.resize(keyLen);
                if (keyLen > 0)
                {
                    if (stream->Read(key.data(), keyLen) < 0)
                    {
                        Log::Error("[DictionarySerializationHandler] Restore: failed to read key");
                        return;
                    }
                }

                // Read type info
                bool isHandle = false;
                if (stream->Read(&isHandle, sizeof(isHandle)) < 0)
                    return;

                uint32_t typeNameLen = 0;
                if (stream->Read(&typeNameLen, sizeof(typeNameLen)) < 0)
                    return;

                eastl::string typeName;
                typeName.resize(typeNameLen);
                if (typeNameLen > 0)
                {
                    if (stream->Read(typeName.data(), typeNameLen) < 0)
                        return;
                }

                int valueTypeId = engine->GetTypeIdByDecl(typeName.c_str());
                if (valueTypeId < 0)
                {
                    // Try primitive types
                    if (typeName == "int") valueTypeId = asTYPEID_INT32;
                    else if (typeName == "float") valueTypeId = asTYPEID_FLOAT;
                    else if (typeName == "double") valueTypeId = asTYPEID_DOUBLE;
                    else if (typeName == "bool") valueTypeId = asTYPEID_BOOL;
                    else if (typeName == "string") valueTypeId = asTYPEID_STRING;
                }

                if (isHandle && valueTypeId >= 0)
                    valueTypeId |= asTYPEID_OBJHANDLE;

                // Read value data
                uint32_t valSize = 0;
                if (stream->Read(&valSize, sizeof(valSize)) < 0)
                    return;

                if (valSize > 0 && serializer_)
                {
                    // For handle types, create object first
                    if (valueTypeId & asTYPEID_OBJHANDLE)
                    {
                        asITypeInfo* typeInfo = engine->GetTypeInfoById(valueTypeId & ~asTYPEID_OBJHANDLE);
                        if (typeInfo)
                        {
                            void* obj = engine->CreateScriptObject(typeInfo);
                            dictRef->Set(key.c_str(), &obj, valueTypeId);
                            engine->ReleaseScriptObject(obj, typeInfo);
                        }
                    }
                    else if (valueTypeId & asTYPEID_SCRIPTOBJECT)
                    {
                        asITypeInfo* typeInfo = engine->GetTypeInfoById(valueTypeId);
                        if (typeInfo)
                        {
                            void* obj = engine->CreateScriptObject(typeInfo);
                            dictRef->Set(key.c_str(), obj, valueTypeId);
                            engine->ReleaseScriptObject(obj, typeInfo);
                        }
                    }

                    // Get the dict value entry and deserialize into it
                    CScriptDictValue* dictValue = (*dictRef)[key.c_str()];
                    if (dictValue)
                    {
                        static_cast<ISerializationHandler*>(serializer_)->SerializeValue(
                            const_cast<void*>(dictValue->GetAddressOfValue()), valueTypeId, stream, false);
                    }
                }
            }
        }

        bool SerializeValue(void* ptr, int typeId, asIBinaryStream* stream, bool isSave) override
        {
            // Stub implementation
            return false;
        }

    private:
        asIScriptEngine* engine_;
        void* serializer_;
    };
} // namespace AngelEngine
