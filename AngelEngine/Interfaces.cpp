module;

#include <angelscript.h>
#include <filesystem>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/expected.h>
#include <EASTL/functional.h>

export module AngelEngine.Interfaces;

namespace AngelEngine
{
    // --- Error Types ---

    export enum class ModuleLoaderError : std::uint8_t
    {
        CreateModuleError,
        BuildModuleError,
        LoadScriptError,
        PathNotFoundError,
        AngelScriptAPIError,
        ContextPreparationFailed,
        InvalidFunction,
        GenericError
    };

    export enum class ExecutionError : std::uint8_t
    {
        NoModsLoadedToRun,
        FailCreateContext,
        ModWithoutMain,
        FailRunMod,
        AngelScriptAPIError,
        ContextPreparationFailed,
        InvalidFunction,
        GenericError
    };

    export enum class BindingError : std::uint8_t
    {
        BindingGlobalsFailed,
        BindingFaild,
        EngineIsNull,
        BindingIsNull,
        AngelScriptAPIError
    };

    export enum class EngineError : std::uint8_t
    {
        CreateAngelScriptEngineFailed,
        CreateAngelScriptContextFailed,
        CreateModuleFailed,
        CompilationFailed,
        PathNotFound,
        GenericError,
        FailRunMods,
        FailHotReload,
        FailCompileMods,
        AngelScriptAPIError
    };

    export enum class ReloadError : std::uint8_t
    {
        ModuleLoaderFailed,
        ExecutionManagerFailed,
        SerializationFailed,
        ScriptRebuildFailed,
        ContextRestorationFailed,
        GenericError
    };

    export enum class EventError : std::uint8_t
    {
        ChannelNotFound,
        ChannelAlreadyRegistered,
        ContextPreparationFailed,
        ExecutionFailed,
        GenericError
    };

    export enum class SerializationError : std::uint8_t
    {
        SaveFailed,
        LoadFailed,
        InvalidData,
        HandlerNotFound,
        GenericError
    };

    // --- Configuration Structures ---

    export struct EngineConfig final
    {
        std::filesystem::path scriptsPathStd;
        std::filesystem::path scriptsPathMod;
        std::filesystem::path asPredefinedPath;
        
        bool enableAutoReload = false;
        bool enableWatchdog = true;
        bool enableAutoGC = false;
        bool enableUseJIT = false;
        int64_t maxScriptExecutionTimeMs = 1000;
    };
    
    // --- String Hashing (Compile-Time & Runtime) ---
    export constexpr uint32_t HashString(const char* str)
    {
        uint32_t hash = 2166136261u;
        while (*str)
        {
            hash ^= static_cast<uint32_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }

    export constexpr uint32_t HashString(const eastl::string& str)
    {
        uint32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<uint32_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    // --- Pattern Interfaces ---

    export struct IScriptSourceProvider
    {
        virtual ~IScriptSourceProvider() = default;
        virtual std::filesystem::path GetStdLibPath() const = 0;
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
        virtual eastl::expected<void, ModuleLoaderError> CompileAllMods(asIScriptEngine* engine) = 0;
        virtual const eastl::vector<eastl::string>& GetLoadedModules() const = 0;
        virtual bool Empty() const = 0;
        virtual const eastl::vector<eastl::string>& GetSaveableVars(const eastl::string& modName) const = 0;
    };

    export struct IEventChannel
    {
        virtual ~IEventChannel() = default;
        virtual eastl::expected<void, EventError> ProcessDeferred(asIScriptContext* ctx) = 0;
        virtual void Clear() = 0;
    };

    export struct IEventManager
    {
        virtual ~IEventManager() = default;

        virtual eastl::expected<void, EventError> RegisterChannel(uint32_t eventId, IEventChannel* channel) = 0;
        virtual void UnregisterChannel(uint32_t eventId) = 0;
        virtual IEventChannel* GetChannel(uint32_t eventId) const = 0;
        
        virtual eastl::expected<void, EventError> ProcessAllDeferred(asIScriptContext* sharedCtx) = 0;
        virtual void ClearAll() = 0;
    };

    // Helper functions for EventChannel
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, float val) { ctx->SetArgFloat(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, double val) { ctx->SetArgDouble(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, int32_t val) { ctx->SetArgDWord(argIndex, static_cast<asDWORD>(val)); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, uint32_t val) { ctx->SetArgDWord(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, int64_t val) { ctx->SetArgQWord(argIndex, static_cast<asQWORD>(val)); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, uint64_t val) { ctx->SetArgQWord(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, bool val) { ctx->SetArgByte(argIndex, val); }
    export inline void SetArg(asIScriptContext* ctx, asUINT argIndex, void* val) { ctx->SetArgAddress(argIndex, val); }

    export struct IContextPooling
    {
        virtual ~IContextPooling() = default;
        virtual asIScriptContext* RequestContext(asIScriptEngine* engine, void* param) = 0;
        virtual void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) = 0;
    };

    export struct IExecutionManager : IContextPooling
    {
        virtual ~IExecutionManager() = default;
        virtual eastl::expected<void, ExecutionError> Tick(const float deltaTime, IEventManager* eventManager, asIScriptEngine* engine) = 0;
        virtual void AbortAll() const = 0;
        virtual void Renew() = 0;
        virtual void RegisterThreadSupport(asIScriptEngine* engine) = 0;
        virtual eastl::expected<int, ExecutionError> ExecuteManaged(asIScriptContext* ctx) = 0;
        virtual eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine, const IModuleLoader* moduleLoader) = 0;
        virtual eastl::expected<void, ExecutionError> RunMod(asIScriptEngine* engine, const eastl::string& modName) = 0;
    };

    export struct IReloadManager
    {
        virtual ~IReloadManager() = default;
        virtual eastl::expected<void, ReloadError> ReloadScripts(asIScriptEngine* engine, IModuleLoader* moduleLoader, IExecutionManager* executionManager, IEventManager* eventManager) = 0;
    };

    export struct ISerializationHandler
    {
        virtual ~ISerializationHandler() = default;
        virtual bool CanHandle(int typeId) const = 0;
        virtual void Save(asIScriptEngine* engine, void* objectPtr, asIBinaryStream* stream) = 0;
        virtual void Restore(asIScriptEngine* engine, void* ptrToHandle, asIBinaryStream* stream) = 0;
    };

    export struct ISaveLoadManager
    {
        virtual ~ISaveLoadManager() = default;
        virtual eastl::expected<eastl::vector<uint8_t>, SerializationError> GetSaveData(asIScriptEngine* engine, IModuleLoader* loader) = 0;
        virtual eastl::expected<void, SerializationError> LoadFromData(asIScriptEngine* engine, const eastl::vector<uint8_t>& data) = 0;
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
    };
    
    export struct IEngineListener
    {
        virtual ~IEngineListener() = default;
        virtual void OnEngineInitialized(asIScriptEngine* engine, IBindingManager* bindingManager, IModuleLoader* moduleLoader, IExecutionManager* executionManager) {}
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
        virtual eastl::expected<void, EngineError> RunMod(const eastl::string& modName) = 0;
        virtual eastl::expected<void, EngineError> CompileAllMods() = 0;
        virtual eastl::expected<void, EngineError> HotReload() const = 0;
        virtual void AddBinding(IScriptBinding* binding) const = 0;
        virtual void CallGarbageCollectorFullCycle() = 0;
        virtual void CallGarbageColletorOneStep() = 0;

        virtual void AddListener(IEngineListener* listener) = 0;
        virtual void RemoveListener(IEngineListener* listener) = 0;
    };
}