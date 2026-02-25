module;

#include <EABase/eabase.h>
#include <angelscript.h>
#include <cstring>

#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>


export module AngelEngine.SaveLoadManager;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace AngelEngine
{
    // --- Format Constants ---
    constexpr uint32_t SAVE_MAGIC = 0x414E4745; // "ANGE"
    constexpr uint32_t SAVE_VERSION = 3; // V3: Universal length prefixing for all variables

    // Safety Limits
    constexpr uint32_t MAX_VAR_NAME_LEN = 1024;
    constexpr uint32_t MAX_SAFE_STRING_LEN = 1024 * 1024; // 1 MB limit for actual string values
    constexpr int MAX_RECURSION_DEPTH = 64;

    class ByteStreamWriter : public asIBinaryStream
    {
    public:
        ByteStreamWriter(eastl::vector<uint8_t>& buffer) : buffer_(buffer) {}

        int Write(const void* ptr, asUINT size) override
        {
            if (size == 0)
                return 0;
            const size_t currentSize = buffer_.size();
            const size_t newSize = currentSize + size;
            if (newSize > buffer_.capacity())
            {
                const size_t doubled = buffer_.capacity() * 2;
                buffer_.reserve(newSize > doubled ? newSize : (doubled > 1024 ? doubled : static_cast<size_t>(1024)));
            }
            buffer_.resize(newSize);
            std::memcpy(buffer_.data() + currentSize, ptr, size);
            return 0;
        }

        int Read(void* ptr, asUINT size) override { return -1; }

    private:
        eastl::vector<uint8_t>& buffer_;
    };

    class ByteStreamReader : public asIBinaryStream
    {
    public:
        ByteStreamReader(const eastl::vector<uint8_t>& buffer) : buffer_(buffer), readPos_(0) {}

        int Write(const void* ptr, asUINT size) override { return -1; }

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
        const eastl::vector<uint8_t>& buffer_;
        size_t readPos_;
    };

    // --- Helpers for Type Name Serialization ---
    const char* GetPrimitiveTypeName(int typeId)
    {
        switch (typeId)
        {
        case asTYPEID_VOID:
            return "void";
        case asTYPEID_BOOL:
            return "bool";
        case asTYPEID_INT8:
            return "int8";
        case asTYPEID_INT16:
            return "int16";
        case asTYPEID_INT32:
            return "int";
        case asTYPEID_INT64:
            return "int64";
        case asTYPEID_UINT8:
            return "uint8";
        case asTYPEID_UINT16:
            return "uint16";
        case asTYPEID_UINT32:
            return "uint";
        case asTYPEID_UINT64:
            return "uint64";
        case asTYPEID_FLOAT:
            return "float";
        case asTYPEID_DOUBLE:
            return "double";
        default:
            return nullptr;
        }
    }

    class BinarySerializer
    {
    public:
        BinarySerializer(asIScriptEngine* engine, asIBinaryStream* stream,
                         const eastl::vector<ISerializationHandler*>& handlers) :
            engine_(engine), stream_(stream), handlers_(handlers), depth_(0)
        {
            stringTypeId_ = engine_->GetTypeIdByDecl("string");
        }

        bool SaveValue(void* ptr, int typeId)
        {
            if (depth_ >= MAX_RECURSION_DEPTH)
            {
                Log::Error("[BinarySerializer] Max recursion depth exceeded ({})", MAX_RECURSION_DEPTH);
                return false;
            }
            depth_++;
            bool result = SaveValueInternal(ptr, typeId, stream_);
            depth_--;
            return result;
        }

        bool LoadValue(void* ptr, int typeId)
        {
            if (depth_ >= MAX_RECURSION_DEPTH)
            {
                Log::Error("[BinarySerializer] Max recursion depth exceeded ({})", MAX_RECURSION_DEPTH);
                return false;
            }
            depth_++;
            bool result = LoadValueInternal(ptr, typeId, stream_);
            depth_--;
            return result;
        }

        bool WriteStableType(int typeId)
        {
            bool isHandle = (typeId & asTYPEID_OBJHANDLE) != 0;
            stream_->Write(&isHandle, sizeof(isHandle));

            if (auto* handler = GetHandler(typeId))
            {
                eastl::string typeName = eastl::string("@") + handler->GetTypeName();
                uint32_t len = static_cast<uint32_t>(typeName.length());
                stream_->Write(&len, sizeof(len));
                if (len > 0)
                    stream_->Write(typeName.c_str(), len);
                return true;
            }

            asITypeInfo* type = engine_->GetTypeInfoById(typeId);
            if (type)
            {
                eastl::string fullName;
                const char* ns = type->GetNamespace();
                if (ns && ns[0] != '\0')
                {
                    fullName = eastl::string(ns) + "::";
                }
                fullName += type->GetName();

                uint32_t len = static_cast<uint32_t>(fullName.length());
                stream_->Write(&len, sizeof(len));
                if (len > 0)
                    stream_->Write(fullName.c_str(), len);
                return true;
            }

            const char* primName = GetPrimitiveTypeName(typeId);
            if (primName)
            {
                uint32_t len = static_cast<uint32_t>(std::strlen(primName));
                stream_->Write(&len, sizeof(len));
                if (len > 0)
                    stream_->Write(primName, len);
                return true;
            }

            Log::Error("[BinarySerializer] Failed to resolve stable type name for typeId: {}", typeId);
            return false;
        }

        eastl::expected<int, SerializationError> ReadStableType()
        {
            bool isHandle = false;
            if (stream_->Read(&isHandle, sizeof(isHandle)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);

            uint32_t len = 0;
            if (stream_->Read(&len, sizeof(len)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);
            if (len > MAX_SAFE_STRING_LEN)
                return eastl::unexpected(SerializationError::CorruptData);

            eastl::string typeName;
            typeName.resize(len);
            if (len > 0)
            {
                if (stream_->Read(typeName.data(), len) < 0)
                    return eastl::unexpected(SerializationError::InvalidData);
            }

            auto ApplyFlags = [isHandle](int typeId) -> int
            { return isHandle ? (typeId | asTYPEID_OBJHANDLE) : typeId; };

            if (typeName.length() > 0 && typeName[0] == '@')
            {
                eastl::string handlerName = typeName.substr(1);
                int typeId = engine_->GetTypeIdByDecl(handlerName.c_str());
                if (typeId >= 0 && GetHandler(typeId) != nullptr)
                {
                    return ApplyFlags(typeId);
                }

                int handleTypeId = typeId | asTYPEID_OBJHANDLE;
                if (typeId >= 0 && GetHandler(handleTypeId) != nullptr)
                {
                    return ApplyFlags(typeId);
                }

                if (typeId < 0)
                {
                    Log::Error("[BinarySerializer] Restored type {} not found in engine", handlerName.c_str());
                    return eastl::unexpected(SerializationError::TypeMismatch);
                }
                return ApplyFlags(typeId);
            }

            int typeId = engine_->GetTypeIdByDecl(typeName.c_str());
            if (typeId >= 0)
                return typeId;

            if (typeName == "void")
                return asTYPEID_VOID;
            if (typeName == "bool")
                return asTYPEID_BOOL;
            if (typeName == "int8")
                return asTYPEID_INT8;
            if (typeName == "int16")
                return asTYPEID_INT16;
            if (typeName == "int")
                return asTYPEID_INT32;
            if (typeName == "int64")
                return asTYPEID_INT64;
            if (typeName == "uint8")
                return asTYPEID_UINT8;
            if (typeName == "uint16")
                return asTYPEID_UINT16;
            if (typeName == "uint")
                return asTYPEID_UINT32;
            if (typeName == "uint64")
                return asTYPEID_UINT64;
            if (typeName == "float")
                return asTYPEID_FLOAT;
            if (typeName == "double")
                return asTYPEID_DOUBLE;

            Log::Error("[BinarySerializer] Failed to map restored type name: {}", typeName.c_str());
            return eastl::unexpected(SerializationError::TypeMismatch);
        }

    public:
        // Public access to Internal methods for top-level length wrapping
        bool SaveValueInternal(void* ptr, int typeId, asIBinaryStream* targetStream)
        {
            if (auto* handler = GetHandler(typeId))
            {
                void* objectPtr = ptr;
                if (typeId & asTYPEID_OBJHANDLE)
                {
                    objectPtr = *static_cast<void**>(ptr);
                }
                handler->Save(engine_, objectPtr, targetStream);
                return true;
            }

            if (typeId == stringTypeId_)
            {
                eastl::string* str = static_cast<eastl::string*>(ptr);
                uint32_t len = static_cast<uint32_t>(str->length());
                targetStream->Write(&len, sizeof(len));
                if (len > 0)
                {
                    targetStream->Write(str->data(), len);
                }
                return true;
            }

            if (typeId & asTYPEID_OBJHANDLE)
            {
                void* obj = *static_cast<void**>(ptr);
                bool isNull = (obj == nullptr);
                targetStream->Write(&isNull, sizeof(bool));

                if (!isNull)
                {
                    asITypeInfo* type = engine_->GetTypeInfoById(typeId);
                    if (type)
                    {
                        if (depth_ >= MAX_RECURSION_DEPTH)
                            return false;
                        depth_++;
                        bool res = SaveValueInternal(obj, type->GetTypeId(), targetStream);
                        depth_--;
                        return res;
                    }
                    return false;
                }
                return true;
            }

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

                    if (depth_ >= MAX_RECURSION_DEPTH)
                        return false;
                    depth_++;
                    bool res = SaveValueInternal(propPtr, propTypeId, targetStream);
                    depth_--;
                    if (!res)
                        return false;
                }
                return true;
            }

            int size = engine_->GetSizeOfPrimitiveType(typeId);
            if (size > 0)
            {
                targetStream->Write(ptr, size);
                return true;
            }

            Log::Error("[BinarySerializer] Unknown type ID: {}", typeId);
            return false;
        }

        bool LoadValueInternal(void* ptr, int typeId, asIBinaryStream* sourceStream)
        {
            if (auto* handler = GetHandler(typeId))
            {
                handler->Restore(engine_, ptr, sourceStream);
                return true;
            }

            if (typeId == stringTypeId_)
            {
                uint32_t len = 0;
                if (sourceStream->Read(&len, sizeof(len)) < 0)
                    return false;
                if (len > MAX_SAFE_STRING_LEN)
                    return false;

                eastl::string* str = static_cast<eastl::string*>(ptr);
                str->resize(len);
                if (len > 0)
                {
                    if (sourceStream->Read(str->data(), len) < 0)
                        return false;
                }
                return true;
            }

            if (typeId & asTYPEID_OBJHANDLE)
            {
                bool isNull = true;
                if (sourceStream->Read(&isNull, sizeof(bool)) < 0)
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

                    if (*handlePtr == nullptr)
                    {
                        *handlePtr = engine_->CreateScriptObject(type);
                    }

                    if (depth_ >= MAX_RECURSION_DEPTH)
                        return false;
                    depth_++;
                    bool res = LoadValueInternal(*handlePtr, type->GetTypeId(), sourceStream);
                    depth_--;
                    return res;
                }
                return true;
            }

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

                    if (depth_ >= MAX_RECURSION_DEPTH)
                        return false;
                    depth_++;
                    bool res = LoadValueInternal(propPtr, propTypeId, sourceStream);
                    depth_--;
                    if (!res)
                        return false;
                }
                return true;
            }

            int size = engine_->GetSizeOfPrimitiveType(typeId);
            if (size > 0)
            {
                if (sourceStream->Read(ptr, size) < 0)
                    return false;
                return true;
            }

            Log::Error("[BinarySerializer] Unknown type ID during load: {}", typeId);
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
        int depth_;
    };

    export class SaveLoadManager final : public ISaveLoadManager
    {
    public:
        void AddHandler(ISerializationHandler* handler) override { handlers_.push_back(handler); }

        eastl::expected<eastl::vector<uint8_t>, SerializationError> GetSaveData(asIScriptEngine* engine,
                                                                                IModuleLoader* loader) override
        {
            eastl::vector<uint8_t> outData;
            ByteStreamWriter stream(outData);
            BinarySerializer serializer(engine, &stream, handlers_);

            // Write Header
            stream.Write(&SAVE_MAGIC, sizeof(SAVE_MAGIC));
            stream.Write(&SAVE_VERSION, sizeof(SAVE_VERSION));

            const auto& modules = loader->GetLoadedModules();
            uint32_t modCount = static_cast<uint32_t>(modules.size());
            stream.Write(&modCount, sizeof(modCount));

            for (const auto& modName : modules)
            {
                uint32_t nameLen = static_cast<uint32_t>(modName.length());
                stream.Write(&nameLen, sizeof(nameLen));
                if (nameLen > 0)
                    stream.Write(modName.c_str(), nameLen);

                asIScriptModule* mod = engine->GetModule(MegaModuleName, asGM_ONLY_IF_EXISTS);
                if (!mod)
                {
                    Log::Error("[SaveLoadManager] __Megamodule__ not found during save for: {}", modName.c_str());
                    return eastl::unexpected(SerializationError::SaveFailed);
                }

                const auto& saveableVars = loader->GetSaveableVars(modName);
                uint32_t varCount = static_cast<uint32_t>(saveableVars.size());
                stream.Write(&varCount, sizeof(varCount));

                for (const auto& varName : saveableVars)
                {
                    uint32_t varNameLen = static_cast<uint32_t>(varName.length());
                    stream.Write(&varNameLen, sizeof(varNameLen));
                    if (varNameLen > 0)
                        stream.Write(varName.c_str(), varNameLen);

                    eastl::string namespacedVarName = modName + "::" + varName;
                    int varIdx = mod->GetGlobalVarIndexByName(namespacedVarName.c_str());
                    if (varIdx < 0)
                    {
                        varIdx = mod->GetGlobalVarIndexByName(varName.c_str());
                    }
                    if (varIdx >= 0)
                    {
                        int typeId = 0;
                        mod->GetGlobalVar(varIdx, nullptr, nullptr, &typeId);
                        void* ref = mod->GetAddressOfGlobalVar(varIdx);

                        if (!serializer.WriteStableType(typeId))
                        {
                            Log::Error("[SaveLoadManager] Failed to write type for variable: {}", varName.c_str());
                            return eastl::unexpected(SerializationError::SaveFailed);
                        }

                        // V3: Wrap value in a length-prefix
                        eastl::vector<uint8_t> valBuffer;
                        ByteStreamWriter valStream(valBuffer);
                        if (!serializer.SaveValueInternal(ref, typeId, &valStream))
                        {
                            Log::Error("[SaveLoadManager] Failed to save variable: {} (TypeID: {})", varName.c_str(),
                                       typeId);
                            return eastl::unexpected(SerializationError::SaveFailed);
                        }

                        uint32_t valSize = static_cast<uint32_t>(valBuffer.size());
                        stream.Write(&valSize, sizeof(valSize));
                        if (valSize > 0)
                            stream.Write(valBuffer.data(), valSize);
                    }
                    else
                    {
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
            ByteStreamReader stream(data);
            BinarySerializer serializer(engine, &stream, handlers_);

            // Read & Verify Header
            uint32_t magic = 0;
            if (stream.Read(&magic, sizeof(magic)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);
            if (magic != SAVE_MAGIC)
            {
                Log::Error("[SaveLoadManager] Bad magic number in save data. Expected {}, got {}", SAVE_MAGIC, magic);
                return eastl::unexpected(SerializationError::VersionMismatch);
            }

            uint32_t version = 0;
            if (stream.Read(&version, sizeof(version)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);
            if (version != SAVE_VERSION)
            {
                Log::Error("[SaveLoadManager] Unsupported save version. Expected {}, got {}", SAVE_VERSION, version);
                return eastl::unexpected(SerializationError::VersionMismatch);
            }

            uint32_t modCount = 0;
            if (stream.Read(&modCount, sizeof(modCount)) < 0)
                return eastl::unexpected(SerializationError::InvalidData);

            for (uint32_t i = 0; i < modCount; ++i)
            {
                uint32_t nameLen = 0;
                if (stream.Read(&nameLen, sizeof(nameLen)) < 0)
                    return eastl::unexpected(SerializationError::InvalidData);
                if (nameLen > MAX_VAR_NAME_LEN)
                    return eastl::unexpected(SerializationError::CorruptData);

                eastl::string modName;
                modName.resize(nameLen);
                if (nameLen > 0)
                {
                    if (stream.Read(modName.data(), nameLen) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);
                }

                asIScriptModule* mod = engine->GetModule(MegaModuleName, asGM_ONLY_IF_EXISTS);
                if (!mod)
                {
                    Log::Error("[SaveLoadManager] __Megamodule__ not found during load for: {}", modName.c_str());
                    return eastl::unexpected(SerializationError::LoadFailed);
                }

                uint32_t varCount = 0;
                if (stream.Read(&varCount, sizeof(varCount)) < 0)
                    return eastl::unexpected(SerializationError::InvalidData);

                for (uint32_t j = 0; j < varCount; ++j)
                {
                    uint32_t varNameLen = 0;
                    if (stream.Read(&varNameLen, sizeof(varNameLen)) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);
                    if (varNameLen > MAX_VAR_NAME_LEN)
                        return eastl::unexpected(SerializationError::CorruptData);

                    eastl::string varName;
                    varName.resize(varNameLen);
                    if (varNameLen > 0)
                    {
                        if (stream.Read(varName.data(), varNameLen) < 0)
                            return eastl::unexpected(SerializationError::InvalidData);
                    }

                    auto typeRes = serializer.ReadStableType();
                    if (!typeRes)
                    {
                        Log::Error("[SaveLoadManager] Failed to read valid type for variable {}", varName.c_str());
                        return eastl::unexpected(typeRes.error());
                    }
                    int storedTypeId = typeRes.value();

                    eastl::string namespacedVarName = modName + "::" + varName;
                    int varIdx = mod->GetGlobalVarIndexByName(namespacedVarName.c_str());
                    if (varIdx < 0)
                    {
                        varIdx = mod->GetGlobalVarIndexByName(varName.c_str());
                    }

                    if (varIdx < 0)
                    {
                        Log::Warning("[SaveLoadManager] Variable {} (mod {}) not found in current scripts. Skipping.",
                                     varName.c_str(), modName.c_str());
                        uint32_t valSize = 0;
                        if (stream.Read(&valSize, sizeof(valSize)) < 0)
                            return eastl::unexpected(SerializationError::InvalidData);

                        if (valSize > 0)
                        {
                            uint8_t dummy[1024];
                            uint32_t toSkip = valSize;
                            while (toSkip > 0)
                            {
                                uint32_t chunk = toSkip > 1024 ? 1024 : toSkip;
                                if (stream.Read(dummy, chunk) < 0)
                                    return eastl::unexpected(SerializationError::InvalidData);
                                toSkip -= chunk;
                            }
                        }
                        continue;
                    }

                    int currentTypeId = 0;
                    mod->GetGlobalVar(varIdx, nullptr, nullptr, &currentTypeId);

                    // V3: We have the size, so even on TypeMismatch we could potentially skip,
                    // but for now we follow the policy: found + mismatch = error.
                    uint32_t valSize = 0;
                    if (stream.Read(&valSize, sizeof(valSize)) < 0)
                        return eastl::unexpected(SerializationError::InvalidData);

                    if (currentTypeId != storedTypeId)
                    {
                        Log::Error("[SaveLoadManager] Type mismatch for variable {}: stored {}, current {}",
                                   varName.c_str(), storedTypeId, currentTypeId);
                        return eastl::unexpected(SerializationError::TypeMismatch);
                    }

                    void* ref = mod->GetAddressOfGlobalVar(varIdx);
                    if (!serializer.LoadValueInternal(ref, currentTypeId, &stream))
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
