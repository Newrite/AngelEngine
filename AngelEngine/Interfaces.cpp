module;

#include <EABase/eabase.h>
#include <EASTL/expected.h>
#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <angelscript.h>
#include <chrono>
#include <eastl/memory.h>
#include <eastl/type_traits.h>
#include <filesystem>
#include <mutex>


export module AngelEngine.Interfaces;

export import AngelEngine.Errors;
export import AngelEngine.Events.Interfaces;
export import AngelEngine.Types;
export import AngelEngine.Utils;

namespace AngelEngine
{

    // --- Pattern Interfaces ---

    export struct IScriptSourceProvider
    {
        virtual ~IScriptSourceProvider() = default;
        virtual eastl::vector<eastl::string> GetAvailableMods() const = 0;
        virtual std::filesystem::path GetModPath(const eastl::string& modName) const = 0;
        virtual eastl::vector<std::filesystem::path> GetScriptFiles(const std::filesystem::path& rootPath) const = 0;
    };

    export struct IScriptBinding
    {
        virtual ~IScriptBinding() = default;
        virtual void Bind(asIScriptEngine* engine) = 0;
    };


    // --- Component Interfaces ---

    export struct IModuleLoader
    {
        virtual ~IModuleLoader() = default;
        virtual eastl::expected<void, ModuleLoaderError>
        CompileAllMods(asIScriptEngine* engine, const eastl::vector<ChannelDescriptor>& eventDescriptors = {}) = 0;
        virtual const eastl::vector<eastl::string>& GetLoadedModules() const = 0;
        virtual bool Empty() const = 0;
        virtual const eastl::vector<eastl::string>& GetSaveableVars(const eastl::string& modName) const = 0;
        virtual void RecordCompilationError(const eastl::string& sectionName) = 0;
    };

    export struct IContextPooling;

    export struct IEventChannel
    {
        virtual ~IEventChannel() = default;
        virtual eastl::expected<void, EventError> ProcessDeferred() = 0;
        virtual void Clear() = 0;
        virtual ChannelDescriptor GetDescriptor() const = 0;
        virtual void SetDispatcherFn(asIScriptEngine* engine, IContextPooling* pool, asIScriptFunction* fn) = 0;
        virtual void WarmupJIT() = 0;
    };

    export struct IEventManager
    {
        virtual ~IEventManager() = default;

        virtual eastl::expected<void, EventError> RegisterChannel(uint32_t eventId, IEventChannel* channel) = 0;
        virtual void UnregisterChannel(uint32_t eventId) = 0;
        virtual IEventChannel* GetChannel(uint32_t eventId) const = 0;

        virtual eastl::expected<void, EventError> ProcessAllDeferred(asIScriptContext* sharedCtx) = 0;
        virtual void ClearAll() = 0;

        // Returns descriptors for all currently registered channels.
        // Used by ModuleLoader to generate the AS-side dispatcher code.
        virtual eastl::vector<ChannelDescriptor> GetAllDescriptors() const = 0;
    };


    export struct IContextPooling
    {
        struct ContextDeleter
        {
            IContextPooling* pool;
            asIScriptEngine* engine;
            void operator()(asIScriptContext* ctx) const
            {
                if (pool && ctx)
                {
                    pool->ReturnContext(engine, ctx, nullptr);
                }
            }
        };

        using ContextPtr = eastl::unique_ptr<asIScriptContext, ContextDeleter>;

        virtual ~IContextPooling() = default;
        virtual ContextPtr RequestContext(asIScriptEngine* engine, void* param) = 0;
        virtual void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) = 0;
    };

    // ContextPooling ends

    export struct IExecutionManager : IContextPooling
    {
        virtual ~IExecutionManager() = default;
        virtual eastl::expected<void, ExecutionError> Tick(const float deltaTime, IEventManager* eventManager,
                                                           asIScriptEngine* engine,
                                                           IBuiltinEventDispatcher* dispatcher) = 0;

        virtual void Renew() = 0;
        virtual void WarmUpPool(asIScriptEngine* engine) = 0;
        virtual eastl::expected<int, ExecutionError> ExecuteManaged(asIScriptContext* ctx) = 0;
        virtual eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine,
                                                                 const IModuleLoader* moduleLoader) = 0;
    };

    export struct IReloadManager
    {
        virtual ~IReloadManager() = default;
        virtual eastl::expected<void, ReloadError> ReloadScripts(asIScriptEngine* engine, IModuleLoader* moduleLoader,
                                                                 IExecutionManager* executionManager,
                                                                 IEventManager* eventManager) = 0;
    };

    export struct ISerializationHandler
    {
        virtual ~ISerializationHandler() = default;
        virtual bool CanHandle(int typeId) const = 0;
        // Returns a stable string identifier for this handler's type, written into the save format.
        // Must remain constant across versions — changing it will break existing saves.
        // Example: "ActorHandle", "QuestData", etc.
        virtual const char* GetTypeName() const = 0;
        virtual void Save(asIScriptEngine* engine, void* objectPtr, asIBinaryStream* stream) = 0;
        virtual void Restore(asIScriptEngine* engine, void* ptrToHandle, asIBinaryStream* stream) = 0;
    };

    export struct ISaveLoadManager
    {
        virtual ~ISaveLoadManager() = default;
        virtual eastl::expected<eastl::vector<uint8_t>, SerializationError> GetSaveData(asIScriptEngine* engine,
                                                                                        IModuleLoader* loader) = 0;
        virtual eastl::expected<void, SerializationError> LoadFromData(asIScriptEngine* engine,
                                                                       const eastl::vector<uint8_t>& data) = 0;
        virtual void AddHandler(ISerializationHandler* handler) = 0;
    };

    export struct IBindingManager
    {
        virtual ~IBindingManager() = default;
        virtual eastl::expected<void, BindingError> RegisterStandardAddons(asIScriptEngine* engine) = 0;
        virtual eastl::expected<void, BindingError> BindAll(asIScriptEngine* const engine) = 0;
        virtual eastl::expected<void, BindingError> Bind(asIScriptEngine* const engine, IScriptBinding* binding) = 0;
        virtual void AddBinding(IScriptBinding* binding) = 0;
    };

    // Forward declaration for ScriptWatcher
    export struct IScriptWatcher
    {
        virtual ~IScriptWatcher() = default;
        virtual bool CheckAndResetReloadFlag() = 0;
    };

    export struct IEngineComponentFactory
    {
        virtual ~IEngineComponentFactory() = default;
        virtual eastl::unique_ptr<IModuleLoader> CreateModuleLoader() = 0;
        virtual eastl::unique_ptr<IExecutionManager> CreateExecutionManager() = 0;
        virtual eastl::unique_ptr<IReloadManager> CreateReloadManager() = 0;
        virtual eastl::unique_ptr<ISaveLoadManager> CreateSaveLoadManager() = 0;
        virtual eastl::unique_ptr<IBindingManager> CreateBindingManager() = 0;
        virtual eastl::unique_ptr<IEventManager> CreateEventManager() = 0;
        virtual eastl::unique_ptr<IScriptWatcher> CreateScriptWatcher() = 0;
        virtual EngineConfig GetEngineConfig() = 0;
    };

    export struct IEngineListener
    {
        virtual ~IEngineListener() = default;
        virtual void OnEngineInitialized(asIScriptEngine* engine, IBindingManager* bindingManager,
                                         IModuleLoader* moduleLoader, IExecutionManager* executionManager)
        {
        }
        virtual void OnCompilationStarted(asIScriptEngine* engine) {}
        virtual void OnCompilationFinished(asIScriptEngine* engine, bool success) {}
        virtual void OnHotReloadStarted(asIScriptEngine* engine) {}
        virtual void OnHotReloadFinished(asIScriptEngine* engine) {}
        virtual void OnAddBinding(asIScriptEngine* engine, IScriptBinding* binding) {}
        virtual void OnScriptMessage(asIScriptEngine* engine, const asSMessageInfo* msg) {}
    };

    export struct IScriptEngineGetters
    {
        virtual ~IScriptEngineGetters() = default;
        virtual asIScriptEngine* GetEngine() const = 0;
        virtual IEventManager* GetEventManager() const = 0;
        virtual IBindingManager* GetBindingManager() const = 0;
        virtual IExecutionManager* GetExecutionManager() const = 0;
        virtual IModuleLoader* GetModuleLoader() const = 0;
        virtual IReloadManager* GetReloadManager() const = 0;
        virtual ISaveLoadManager* GetSaveLoadManager() const = 0;
    };

    export struct IScriptEngine
    {
        virtual ~IScriptEngine() = default;
        virtual eastl::expected<void, EngineError> Tick(float deltaTime) = 0;
        virtual eastl::expected<void, EngineError> RunAllMods() = 0;
        virtual eastl::expected<void, EngineError> CompileAllMods() = 0;
        virtual eastl::expected<void, EngineError> HotReload() const = 0;
        virtual void AddBinding(IScriptBinding* binding) const = 0;
        virtual void CallGarbageCollectorFullCycle() = 0;
        virtual void CallGarbageColletorOneStep() = 0;

        virtual void AddListener(IEngineListener* listener) = 0;
        virtual void RemoveListener(IEngineListener* listener) = 0;
    };
} // namespace AngelEngine
