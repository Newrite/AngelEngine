module;

#include <angelscript.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <print>
#include <format>

#include <EASTL/vector.h>
#include <EASTL/string.h>

export module AngelEngine.SaveLoadManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace AngelEngine
{
    class ByteStream : public asIBinaryStream
    {
    public:
        ByteStream(eastl::vector<uint8_t>& buffer) : buffer_(buffer), readPos_(0) {}
        ByteStream(const eastl::vector<uint8_t>& buffer) : buffer_(const_cast<eastl::vector<uint8_t>&>(buffer)), readPos_(0) {}

        int Write(const void* ptr, asUINT size) override
        {
            if (size == 0) return 0;
            size_t currentSize = buffer_.size();
            buffer_.resize(currentSize + size);
            std::memcpy(buffer_.data() + currentSize, ptr, size);
            return 0;
        }

        int Read(void* ptr, asUINT size) override
        {
            if (size == 0) return 0;
            if (readPos_ + size > buffer_.size())
            {
                std::memset(ptr, 0, size);
                return -1;
            }
            std::memcpy(ptr, buffer_.data() + readPos_, size);
            readPos_ += size;
            return 0;
        }

    private:
        eastl::vector<uint8_t>& buffer_;
        size_t readPos_;
    };

    class BinarySerializer
    {
    public:
        BinarySerializer(asIScriptEngine* engine, asIBinaryStream* stream, const eastl::vector<ISerializationHandler*>& handlers)
            : engine_(engine), stream_(stream), handlers_(handlers)
        {
            stringTypeId_ = engine_->GetTypeIdByDecl("string");
        }

        bool SaveValue(void* ptr, int typeId)
        {
            // 0. Custom Handlers
            for (auto* handler : handlers_)
            {
                if (handler->CanHandle(typeId))
                {
                    void* objectPtr = ptr;
                    if (typeId & asTYPEID_OBJHANDLE)
                    {
                        objectPtr = *static_cast<void**>(ptr);
                    }
                    handler->Save(engine_, objectPtr, stream_);
                    return true;
                }
            }

            // 1. Handle std::string
            if (typeId == stringTypeId_)
            {
                std::string* str = static_cast<std::string*>(ptr);
                uint32_t len = static_cast<uint32_t>(str->length());
                stream_->Write(&len, sizeof(len));
                if (len > 0)
                {
                    stream_->Write(str->data(), len);
                }
                return true;
            }

            // 2. Handle Object Handles
            if (typeId & asTYPEID_OBJHANDLE)
            {
                void* obj = *static_cast<void**>(ptr);
                bool isNull = (obj == nullptr);
                stream_->Write(&isNull, sizeof(bool));

                if (!isNull)
                {
                    // Dereference handle to get the object type
                    asITypeInfo* type = engine_->GetTypeInfoById(typeId);
                    if (type)
                    {
                        // Recursively save the object
                        return SaveValue(obj, type->GetTypeId());
                    }
                    return false;
                }
                return true;
            }

            // 3. Handle Script Objects (Classes/Structs)
            if (typeId & asTYPEID_SCRIPTOBJECT)
            {
                asIScriptObject* obj = static_cast<asIScriptObject*>(ptr);
                if (!obj) return false; // Should not happen if not handle

                asITypeInfo* type = obj->GetObjectType();
                uint32_t propCount = type->GetPropertyCount();

                for (uint32_t i = 0; i < propCount; ++i)
                {
                    int propTypeId;
                    type->GetProperty(i, nullptr, &propTypeId);
                    void* propPtr = obj->GetAddressOfProperty(i);
                    
                    if (!SaveValue(propPtr, propTypeId))
                    {
                        return false;
                    }
                }
                return true;
            }

            // 4. Handle Primitives
            int size = engine_->GetSizeOfPrimitiveType(typeId);
            if (size > 0)
            {
                stream_->Write(ptr, size);
                return true;
            }

            // Unknown type
            Log::Error("[BinarySerializer] Unknown type ID: {}", typeId);
            return false;
        }

        bool LoadValue(void* ptr, int typeId)
        {
            // 0. Custom Handlers
            for (auto* handler : handlers_)
            {
                if (handler->CanHandle(typeId))
                {
                    handler->Restore(engine_, ptr, stream_);
                    return true;
                }
            }

            // 1. Handle std::string
            if (typeId == stringTypeId_)
            {
                uint32_t len = 0;
                if (stream_->Read(&len, sizeof(len)) < 0) return false;
                
                std::string* str = static_cast<std::string*>(ptr);
                str->resize(len);
                if (len > 0)
                {
                    if (stream_->Read(str->data(), len) < 0) return false;
                }
                return true;
            }

            // 2. Handle Object Handles
            if (typeId & asTYPEID_OBJHANDLE)
            {
                bool isNull = true;
                if (stream_->Read(&isNull, sizeof(bool)) < 0) return false;

                void** handlePtr = static_cast<void**>(ptr);
                
                if (isNull)
                {
                    if (*handlePtr)
                    {
                        engine_->ReleaseScriptObject(*handlePtr, engine_->GetTypeInfoById(typeId));
                        *handlePtr = nullptr;
                    }
                }
                else
                {
                    asITypeInfo* type = engine_->GetTypeInfoById(typeId);
                    if (!type) return false;

                    // If handle is null, create new object
                    if (*handlePtr == nullptr)
                    {
                        *handlePtr = engine_->CreateScriptObject(type);
                    }
                    
                    // Recursively load into the object
                    return LoadValue(*handlePtr, type->GetTypeId());
                }
                return true;
            }

            // 3. Handle Script Objects
            if (typeId & asTYPEID_SCRIPTOBJECT)
            {
                asIScriptObject* obj = static_cast<asIScriptObject*>(ptr);
                if (!obj) return false;

                asITypeInfo* type = obj->GetObjectType();
                uint32_t propCount = type->GetPropertyCount();

                for (uint32_t i = 0; i < propCount; ++i)
                {
                    int propTypeId;
                    type->GetProperty(i, nullptr, &propTypeId);
                    void* propPtr = obj->GetAddressOfProperty(i);

                    if (!LoadValue(propPtr, propTypeId))
                    {
                        return false;
                    }
                }
                return true;
            }

            // 4. Handle Primitives
            int size = engine_->GetSizeOfPrimitiveType(typeId);
            if (size > 0)
            {
                if (stream_->Read(ptr, size) < 0) return false;
                return true;
            }

            Log::Error("[BinarySerializer] Unknown type ID: {}", typeId);
            return false;
        }

    private:
        asIScriptEngine* engine_;
        asIBinaryStream* stream_;
        const eastl::vector<ISerializationHandler*>& handlers_;
        int stringTypeId_;
    };

    export class SaveLoadManager final : public ISaveLoadManager
    {
    public:
        void AddHandler(ISerializationHandler* handler) override
        {
            handlers_.push_back(handler);
        }

        bool GetSaveData(asIScriptEngine* engine, IModuleLoader* loader, eastl::vector<uint8_t>& outData) override
        {
            outData.clear();
            ByteStream stream(outData);
            BinarySerializer serializer(engine, &stream, handlers_);

            const auto& modules = loader->GetLoadedModules();
            uint32_t modCount = static_cast<uint32_t>(modules.size());
            stream.Write(&modCount, sizeof(modCount));

            for (const auto& modName : modules)
            {
                // Write Module Name
                uint32_t nameLen = static_cast<uint32_t>(modName.length());
                stream.Write(&nameLen, sizeof(nameLen));
                if (nameLen > 0) stream.Write(modName.c_str(), nameLen);

                asIScriptModule* mod = engine->GetModule(modName.c_str());
                if (!mod) 
                {
                    // Should not happen if loaded_modules is in sync, but handle gracefully
                    uint32_t zero = 0;
                    stream.Write(&zero, sizeof(zero));
                    continue;
                }

                const auto& saveableVars = loader->GetSaveableVars(modName);
                uint32_t varCount = static_cast<uint32_t>(saveableVars.size());
                stream.Write(&varCount, sizeof(varCount));

                for (const auto& varName : saveableVars)
                {
                    // Write Var Name
                    uint32_t varNameLen = static_cast<uint32_t>(varName.length());
                    stream.Write(&varNameLen, sizeof(varNameLen));
                    if (varNameLen > 0) stream.Write(varName.c_str(), varNameLen);

                    int varIdx = mod->GetGlobalVarIndexByName(varName.c_str());
                    if (varIdx >= 0)
                    {
                        int typeId = 0;
                        mod->GetGlobalVar(varIdx, nullptr, nullptr, &typeId);
                        void* ref = mod->GetAddressOfGlobalVar(varIdx);

                        // Write TypeID
                        stream.Write(&typeId, sizeof(typeId));

                        // Save Value
                        if (!serializer.SaveValue(ref, typeId))
                        {
                            Log::Error("[SaveLoadManager] Failed to save variable: {} (TypeID: {})", varName.c_str(), typeId);
                            return false;
                        }
                    }
                    else
                    {
                        // Variable not found? Write invalid type or handle error.
                        // For now, we assume GetSaveableVars returns valid vars.
                        // If not found, we might corrupt stream if we don't write anything but wrote varCount.
                        // Let's write a dummy typeId -1 to indicate skip?
                        // But the loader expects valid data.
                        // We should probably filter saveableVars before writing count.
                        // But for simplicity, let's assume valid.
                        int invalidType = -1;
                        stream.Write(&invalidType, sizeof(invalidType));
                    }
                }
            }
            return true;
        }

        bool LoadFromData(asIScriptEngine* engine, const eastl::vector<uint8_t>& data) override
        {
            if (data.empty()) return false;
            ByteStream stream(data);
            BinarySerializer serializer(engine, &stream, handlers_);

            uint32_t modCount = 0;
            if (stream.Read(&modCount, sizeof(modCount)) < 0) return false;

            for (uint32_t i = 0; i < modCount; ++i)
            {
                // Read Module Name
                uint32_t nameLen = 0;
                if (stream.Read(&nameLen, sizeof(nameLen)) < 0) return false;
                
                eastl::string modName;
                modName.resize(nameLen);
                if (nameLen > 0)
                {
                    if (stream.Read(modName.data(), nameLen) < 0) return false;
                }

                asIScriptModule* mod = engine->GetModule(modName.c_str());
                // If mod is null, we have a mismatch. We should return false as per requirements.
                if (!mod) return false;

                uint32_t varCount = 0;
                if (stream.Read(&varCount, sizeof(varCount)) < 0) return false;

                for (uint32_t j = 0; j < varCount; ++j)
                {
                    // Read Var Name
                    uint32_t varNameLen = 0;
                    if (stream.Read(&varNameLen, sizeof(varNameLen)) < 0) return false;

                    eastl::string varName;
                    varName.resize(varNameLen);
                    if (varNameLen > 0)
                    {
                        if (stream.Read(varName.data(), varNameLen) < 0) return false;
                    }

                    // Read TypeID
                    int storedTypeId = 0;
                    if (stream.Read(&storedTypeId, sizeof(storedTypeId)) < 0) return false;

                    if (storedTypeId == -1) continue; // Skip invalid vars

                    int varIdx = mod->GetGlobalVarIndexByName(varName.c_str());
                    if (varIdx < 0) return false; // Variable missing in current script

                    int currentTypeId = 0;
                    mod->GetGlobalVar(varIdx, nullptr, nullptr, &currentTypeId);
                    
                    // Simple type compatibility check
                    // Note: TypeIDs might change between compilations if types are reordered/changed.
                    // Ideally we should check type names, but TypeID check is requested.
                    // If types mismatch, return false.
                    if (currentTypeId != storedTypeId) return false;

                    void* ref = mod->GetAddressOfGlobalVar(varIdx);
                    if (!serializer.LoadValue(ref, currentTypeId))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

    private:
        eastl::vector<ISerializationHandler*> handlers_;
    };
}