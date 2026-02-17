#pragma once

#include <angelscript.h>
#include <print>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <EASTL/string.h>
#include <EASTL/vector.h>

import AngelEngine.Interfaces;

// --- Simple Assertion Helper ---
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::println(stderr, "[TEST FAILED] {}:{} - {}", __FILE__, __LINE__, message); \
            std::exit(1); \
        } else { \
            std::println("[TEST PASSED] {}:{} - {}", __FILE__, __LINE__, message); \
        } \
    } while (0)

// --- Mock Game Entity ---
struct MockActor
{
    int id;
    int health;

    static std::map<int, MockActor*> registry;

    MockActor(int _id, int _health) : id(_id), health(_health)
    {
        if (registry.find(id) != registry.end())
        {
            std::println(stderr, "[MockActor] Duplicate ID: {}", id);
            std::exit(1);
        }
        registry[id] = this;
    }

    ~MockActor()
    {
        registry.erase(id);
    }

    // Reference counting for AngelScript (dummy implementation for asOBJ_REF | asOBJ_NOCOUNT)
    void AddRef() {}
    void Release() {}
};

// Definition of static member
inline std::map<int, MockActor*> MockActor::registry;

// --- Serialization Handler ---
class MockActorHandler final : public AngelEngine::ISerializationHandler
{
public:
    MockActorHandler(asIScriptEngine* engine)
    {
        typeId_ = engine->GetTypeIdByDecl("MockActor");
        if (typeId_ < 0)
        {
            std::println(stderr, "[MockActorHandler] Failed to get TypeID for MockActor: {}", typeId_);
        }
        else
        {
            std::println("[MockActorHandler] Registered handler for TypeID: {}", typeId_);
        }
    }

    bool CanHandle(int typeId) const override
    {
        // If typeId is exactly typeId_, it's the object.
        if (typeId == typeId_) return true;
        
        // If typeId is a handle to typeId_, it's the handle.
        if (typeId == (typeId_ | asTYPEID_OBJHANDLE)) return true;
        
        return false;
    }

    void Save(asIScriptEngine* engine, void* objectPtr, asIBinaryStream* stream) override
    {
        MockActor* actor = static_cast<MockActor*>(objectPtr);
        int id = actor ? actor->id : -1;
        stream->Write(&id, sizeof(id));
        
        // Save state (Health)
        if (actor)
        {
            stream->Write(&actor->health, sizeof(actor->health));
        }
    }

    void Restore(asIScriptEngine* engine, void* ptrToHandle, asIBinaryStream* stream) override
    {
        int id = -1;
        stream->Read(&id, sizeof(id));

        MockActor* foundActor = nullptr;
        if (id != -1)
        {
            auto it = MockActor::registry.find(id);
            if (it != MockActor::registry.end())
            {
                foundActor = it->second;
            }
            else
            {
                std::println(stderr, "[MockActorHandler] Warning: Actor with ID {} not found during restore.", id);
            }
            
            // Restore state (Health)
            int storedHealth = 0;
            stream->Read(&storedHealth, sizeof(storedHealth));
            
            if (foundActor)
            {
                foundActor->health = storedHealth;
            }
        }

        // Assign pointer to the handle location
        MockActor** handle = static_cast<MockActor**>(ptrToHandle);
        *handle = foundActor;
    }

private:
    int typeId_;
};

// --- Test Bindings ---
class TestBinding final : public AngelEngine::IScriptBinding
{
public:
    eastl::vector<std::string> capturedOutput;

    void Bind(asIScriptEngine* engine) override
    {
        int r = engine->RegisterObjectType("MockActor", 0, asOBJ_REF | asOBJ_NOCOUNT);
        if (r < 0) std::println(stderr, "RegisterObjectType failed: {}", r);
        TEST_ASSERT(r >= 0, "Failed to register MockActor type");

        r = engine->RegisterObjectProperty("MockActor", "int id", asOFFSET(MockActor, id));
        if (r < 0) std::println(stderr, "RegisterObjectProperty id failed: {}", r);
        TEST_ASSERT(r >= 0, "Failed to register MockActor::id");

        r = engine->RegisterObjectProperty("MockActor", "int health", asOFFSET(MockActor, health));
        if (r < 0) std::println(stderr, "RegisterObjectProperty health failed: {}", r);
        TEST_ASSERT(r >= 0, "Failed to register MockActor::health");

        r = engine->RegisterGlobalFunction("MockActor@ GetActor(int id)", asFUNCTION(GetActor), asCALL_CDECL);
        if (r < 0) std::println(stderr, "RegisterGlobalFunction GetActor failed: {}", r);
        TEST_ASSERT(r >= 0, "Failed to register GetActor");

        r = engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(PrintCallback), asCALL_CDECL_OBJLAST, this);
        if (r < 0) std::println(stderr, "RegisterGlobalFunction print failed: {}", r);
        TEST_ASSERT(r >= 0, "Failed to register print");
    }

    static MockActor* GetActor(int id)
    {
        auto it = MockActor::registry.find(id);
        if (it != MockActor::registry.end())
        {
            return it->second;
        }
        return nullptr;
    }

    static void PrintCallback(const std::string& msg, TestBinding* self)
    {
        if (self)
        {
            self->capturedOutput.push_back(msg);
            std::println("[Script] {}", msg);
        }
    }
};
