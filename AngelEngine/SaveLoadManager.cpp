module;

#include <EABase/eabase.h>
#include <angelscript.h>
#include <format>

#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>


export module AngelEngine.SaveLoadManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace AngelEngine
{
    class ByteStream : public asIBinaryStream
    {
    public:
        ByteStream(eastl::vector<uint8_t>& buffer) : buffer_(buffer), readPos_(0) {}
        ByteStream(const eastl::vector<uint8_t>& buffer) :
            buffer_(const_cast<eastl::vector<uint8_t>&>(buffer)), readPos_(0)
        {
        }

        int Write(const void* ptr, asUINT size) override
        {
            if (size == 0)
                return 0;
            size_t currentSize = buffer_.size();
            size_t newSize = currentSize + size;
            if (newSize > buffer_.capacity())
            {
                buffer_.reserve(eastl::max<size_t>(newSize, eastl::max<size_t>(buffer_.capacity() * 2, 1024)));
            }
            buffer_.resize(newSize);
            std::memcpy(buffer_.data() + currentSize, ptr, size);
            return 0;
        }

        int Read(void* ptr, asUINT size) override
        {
            if (size == 0)
                return 0;
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
        BinarySerializer(asIScriptEngine* engine, asIBinaryStream* stream,
                         const eastl::vector<ISerializationHandler*>& handlers) :
            engine_(engine), stream_(stream), handlers_(handlers)
        {
            stringTypeId_ = engine_->GetTypeIdByDecl("string");
        }

        bool SaveValue(void* ptr, int typeId)
        {
            // 0. Custom Handlers
            if (auto* handler = GetHandler(typeId))
            {
                void* objectPtr = ptr;
                if (typeId & asTYPEID_OBJHANDLE)
                {
                    objectPtr = *static_cast<void**>(ptr);
                }
                handler->Save(engine_, objectPtr, stream_);
                return true;
            }

            // 1. Handle eastl::string
            if (typeId == stringTypeId_)
            {
                eastl::string* str = static_cast<eastl::string*>(ptr);
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
                        // Note: This is simplified. Real serialization needs to handle circular references and
                        // polymorphism properly. But for this task, we stick to the provided logic structure.
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
                if (!obj)
                    return false; // Should not happen if not handle

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
            if (auto* handler = GetHandler(typeId))
            {
                handler->Restore(engine_, ptr, stream_);
                return true;
            }

            // 1. Handle eastl::string
            if (typeId == stringTypeId_)
            {
                uint32_t len = 0;
                if (stream_->Read(&len, sizeof(len)) < 0)
                    return false;

                eastl::string* str = static_cast<eastl::string*>(ptr);
                str->resize(len);
                if (len > 0)
                {
                    if (stream_->Read(str->data(), len) < 0)
                        return false;
                }
                return true;
            }

            // 2. Handle Object Handles
            if (typeId & asTYPEID_OBJHANDLE)
            {
                bool isNull = true;
                if (stream_->Read(&isNull, sizeof(bool)) < 0)
                    return false;

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
                    if (!type)
                        return false;

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
                if (!obj)
                    return false;

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
                if (stream_->Read(ptr, size) < 0)
                    return false;
                return true;
            }

            Log::Error("[BinarySerializer] Unknown type ID: {}", typeId);
            return false;
        }

    private:
        ISerializationHandler* GetHandler(int typeId)
        {
            auto it = handlerCache_.find(typeId);
            if (it != handlerCache_.end())
                return it->second;

            for (auto* handler : handlers_)
            {
                if (handler->CanHandle(typeId))
                {
                    handlerCache_[typeId] = handler;
                    return handler;
                }
            }
            handlerCache_[typeId] = nullptr;
            return nullptr;
        }

        asIScriptEngine* engine_;
        asIBinaryStream* stream_;
        const eastl::vector<ISerializationHandler*>& handlers_;
        int stringTypeId_;
        eastl::vector_map<int, ISerializationHandler*> handlerCache_;
    };

    export class SaveLoadManager final : public ISaveLoadManager
    {
    public:
        void AddHandler(ISerializationHandler* handler) override { handlers_.push_back(handler); }

        eastl::expected<eastl::vector<uint8_t>, SerializationError> GetSaveData(asIScriptEngine* engine,
                                                                                IModuleLoader* loader) override
        {
            eastl::vector<uint8_t> outData;
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
                if (nameLen > 0)
                    stream.Write(modName.c_str(), nameLen);

                asIScriptModule* mod = engine->GetModule(modName.c_str(), asGM_ONLY_IF_EXISTS);
                if (!mod)
                {
                    Log::Error("[SaveLoadManager] Module not found during save: {}", modName.c_str());
                    return eastl::unexpected(SerializationError::SaveFailed);
                }

                const auto& saveableVars = loader->GetSaveableVars(modName);
                uint32_t varCount = static_cast<uint32_t>(saveableVars.size());
                stream.Write(&varCount, sizeof(varCount));

                for (const auto& varName : saveableVars)
                {
                    // Write Var Name
                    uint32_t varNameLen = static_cast<uint32_t>(varName.length());
                    stream.Write(&varNameLen, sizeof(varNameLen));
                    if (varNameLen > 0)
                        stream.Write(varName.c_str(), varNameLen);

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
                            Log::Error("[SaveLoadManager] Failed to save variable: {} (TypeID: {})", varName.c_str(),
                                       typeId);
                            return eastl::unexpected(SerializationError::SaveFailed);
                        }
                    }
                    else
                    {
                        // Variable not found?
                        Log::Error("[SaveLoadManager] Variable {} not found in module {}", varName.c_str(),
                                   modName.c_str());
                        return eastl::unexpected(SerializationError::SaveFailed);
                    }
                }
            }
            return outData;
        }

        eastl::expected<void, SerializationError> LoadFromData(asIScriptEngine* engine,
                                                               const eastl::vector<uint8_t>& data) override
        {
            if (data.empty())
                return eastl::unexpected(SerializationError::InvalidData);
            ByteStream stream(data);
            BinarySerializer serializer(engine, &stream, handlers_);

            uint32_t modCount = 0;
            if (stream.Read(&modCount, sizeof(modCount)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);

            for (uint32_t i = 0; i < modCount; ++i)
            {
                // Read Module Name
                uint32_t nameLen = 0;
                if (stream.Read(&nameLen, sizeof(nameLen)) < 0)
                    return eastl::unexpected(SerializationError::InvalidData);

                eastl::string modName;
                modName.resize(nameLen);
                if (nameLen > 0)
                {
                    if (stream.Read(modName.data(), nameLen) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);
                }

                asIScriptModule* mod = engine->GetModule(modName.c_str(), asGM_ONLY_IF_EXISTS);
                if (!mod)
                {
                    Log::Error("[SaveLoadManager] Module not found during load: {}", modName.c_str());
                    return eastl::unexpected(SerializationError::LoadFailed);
                }

                uint32_t varCount = 0;
                if (stream.Read(&varCount, sizeof(varCount)) < 0)
                    return eastl::unexpected(SerializationError::InvalidData);

                for (uint32_t j = 0; j < varCount; ++j)
                {
                    // Read Var Name
                    uint32_t varNameLen = 0;
                    if (stream.Read(&varNameLen, sizeof(varNameLen)) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);

                    eastl::string varName;
                    varName.resize(varNameLen);
                    if (varNameLen > 0)
                    {
                        if (stream.Read(varName.data(), varNameLen) < 0)
                            return eastl::unexpected(SerializationError::InvalidData);
                    }

                    // Read TypeID
                    int storedTypeId = 0;
                    if (stream.Read(&storedTypeId, sizeof(storedTypeId)) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);

                    int varIdx = mod->GetGlobalVarIndexByName(varName.c_str());
                    if (varIdx < 0)
                    {
                        Log::Error("[SaveLoadManager] Variable {} not found in module {}", varName.c_str(),
                                   modName.c_str());
                        return eastl::unexpected(SerializationError::LoadFailed);
                    }

                    int currentTypeId = 0;
                    mod->GetGlobalVar(varIdx, nullptr, nullptr, &currentTypeId);

                    if (currentTypeId != storedTypeId)
                    {
                        Log::Error("[SaveLoadManager] Type mismatch for variable {}: stored {}, current {}",
                                   varName.c_str(), storedTypeId, currentTypeId);
                        return eastl::unexpected(SerializationError::LoadFailed);
                    }

                    void* ref = mod->GetAddressOfGlobalVar(varIdx);
                    if (!serializer.LoadValue(ref, currentTypeId))
                    {
                        Log::Error("[SaveLoadManager] Failed to load value for variable {}", varName.c_str());
                        return eastl::unexpected(SerializationError::LoadFailed);
                    }
                }
            }
            return {};
        }

    private:
        eastl::vector<ISerializationHandler*> handlers_;
    };
} // namespace AngelEngine
