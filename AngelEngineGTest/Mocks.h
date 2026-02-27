#pragma once

#include <gmock/gmock.h>
#include <EASTL/vector.h>
#include <EASTL/expected.h>

#include <angelscript.h>

// Forward declarations from AngelEngine modules
import AngelEngine.Interfaces;
import AngelEngine.Errors;
import AngelEngine.Types;

namespace AngelEngine::Test {

/**
 * @brief Mock для IEventManager
 */
class MockEventManager : public IEventManager
{
public:
    MOCK_METHOD(
        (eastl::expected<void, EventError>),
        RegisterChannel,
        (uint32_t eventId, IEventChannel* channel),
        (override)
    );
    
    MOCK_METHOD(
        void,
        UnregisterChannel,
        (uint32_t eventId),
        (override)
    );
    
    MOCK_METHOD(
        IEventChannel*,
        GetChannel,
        (uint32_t eventId),
        (const, override)
    );
    
    MOCK_METHOD(
        (eastl::expected<void, EventError>),
        ProcessAllDeferred,
        (asIScriptContext* sharedCtx),
        (override)
    );
    
    MOCK_METHOD(
        void,
        ClearAll,
        (),
        (override)
    );
    
    MOCK_METHOD(
        (eastl::vector<ChannelDescriptor>),
        GetAllDescriptors,
        (),
        (const, override)
    );
};

/**
 * @brief Mock для IModuleLoader
 */
class MockModuleLoader : public IModuleLoader
{
public:
    MOCK_METHOD(
        (eastl::expected<void, ModuleLoaderError>),
        CompileAllMods,
        (asIScriptEngine* engine, const eastl::vector<ChannelDescriptor>& eventDescriptors),
        (override)
    );
    
    MOCK_METHOD(
        (const eastl::vector<eastl::string>&),
        GetLoadedModules,
        (),
        (const, override)
    );
    
    MOCK_METHOD(
        bool,
        Empty,
        (),
        (const, override)
    );
    
    MOCK_METHOD(
        (const eastl::vector<eastl::string>&),
        GetSaveableVars,
        (const eastl::string& modName),
        (const, override)
    );
    
    MOCK_METHOD(
        void,
        RecordCompilationError,
        (const eastl::string& sectionName),
        (override)
    );
};

/**
 * @brief Mock для IExecutionManager
 */
class MockExecutionManager : public IExecutionManager
{
public:
    MOCK_METHOD(
        (eastl::expected<void, ExecutionError>),
        Tick,
        (float deltaTime, IEventManager* eventManager, asIScriptEngine* engine, IBuiltinEventDispatcher* dispatcher),
        (override)
    );
    
    MOCK_METHOD(
        void,
        Renew,
        (),
        (override)
    );
    
    MOCK_METHOD(
        (eastl::expected<int, ExecutionError>),
        ExecuteManaged,
        (asIScriptContext* ctx),
        (override)
    );
    
    MOCK_METHOD(
        (eastl::expected<void, ExecutionError>),
        RunAllMods,
        (asIScriptEngine* engine, const IModuleLoader* moduleLoader),
        (override)
    );
    
    // IContextPooling methods
    MOCK_METHOD(
        ContextPtr,
        RequestContext,
        (asIScriptEngine* engine, void* param),
        (override)
    );
    
    MOCK_METHOD(
        void,
        ReturnContext,
        (asIScriptEngine* engine, asIScriptContext* ctx, void* param),
        (override)
    );
};

/**
 * @brief Mock для ISaveLoadManager
 */
class MockSaveLoadManager : public ISaveLoadManager
{
public:
    MOCK_METHOD(
        (eastl::expected<eastl::vector<uint8_t>, SerializationError>),
        GetSaveData,
        (asIScriptEngine* engine, IModuleLoader* loader),
        (override)
    );
    
    MOCK_METHOD(
        (eastl::expected<void, SerializationError>),
        LoadFromData,
        (asIScriptEngine* engine, const eastl::vector<uint8_t>& data),
        (override)
    );
    
    MOCK_METHOD(
        void,
        AddHandler,
        (ISerializationHandler* handler),
        (override)
    );
};

/**
 * @brief Mock для IEventChannel
 */
class MockEventChannel : public IEventChannel
{
public:
    MOCK_METHOD(
        (eastl::expected<void, EventError>),
        ProcessDeferred,
        (),
        (override)
    );
    
    MOCK_METHOD(
        void,
        Clear,
        (),
        (override)
    );
    
    MOCK_METHOD(
        ChannelDescriptor,
        GetDescriptor,
        (),
        (const, override)
    );
    
    MOCK_METHOD(
        void,
        SetDispatcherFn,
        (asIScriptEngine* engine, IContextPooling* pool, asIScriptFunction* fn),
        (override)
    );
    
    MOCK_METHOD(
        void,
        WarmupJIT,
        (),
        (override)
    );
};

/**
 * @brief Mock для IBuiltinEventDispatcher
 */
class MockBuiltinEventDispatcher : public IBuiltinEventDispatcher
{
public:
    MOCK_METHOD(
        void,
        DispatchBuiltinEvents,
        (asIScriptContext* ctx, float deltaTime),
        (override)
    );
};

} // namespace AngelEngine::Test
