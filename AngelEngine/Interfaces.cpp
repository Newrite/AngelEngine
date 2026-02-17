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
        GenericError
    };

    export enum class ExecutionError : std::uint8_t
    {
        NoModsLoadedToRun,
        FailCreateContext
    };

    export enum class BindingError : std::uint8_t
    {
        BindingGlobalsFailed,
        BindingFaild,
        EngineIsNull,
        BindingIsNull
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
    };

    // --- Configuration Structures ---

    export struct ModuleLoaderConfig final
    {
        const std::filesystem::path scriptsPathStd;
        const std::filesystem::path scriptsPathMod;
    };

    // --- Pattern Interfaces ---

    // Strategy: Decouples script discovery from file system
    export struct IScriptSourceProvider
    {
        virtual ~IScriptSourceProvider() = default;
        // Returns root path for Std lib (used by CScriptBuilder include resolving)
        virtual std::filesystem::path GetStdLibPath() const = 0;
        // Returns list of available Mod names
        virtual eastl::vector<eastl::string> GetAvailableMods() const = 0;
        // Returns root path for a specific Mod
        virtual std::filesystem::path GetModPath(const eastl::string& modName) const = 0;
        // Returns all script files (absolute paths) in a directory (recursive)
        virtual eastl::vector<std::filesystem::path> GetScriptFiles(const std::filesystem::path& rootPath) const = 0;
    };

    // Strategy: Interface for external bindings
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
    };

    // Функция, которую передает пользователь (код Скайрима), 
    // чтобы заполнить аргументы контекста перед его запуском.
    export using ArgInjector = eastl::function<void(asIScriptContext*)>;

    export struct IEventManager
    {
        virtual ~IEventManager() = default;

        // Подписка из скрипта
        virtual void Subscribe(const eastl::string& eventName, asIScriptFunction* callback) = 0;
        
        // Очистка при перезагрузке (HotReload)
        virtual void ClearAll() = 0;

        // 1. Прямой вызов (мгновенно, блокирует поток, можно менять inout аргументы)
        virtual void DispatchDirect(asIScriptEngine* engine, const eastl::string& eventName, const ArgInjector& argInjector = nullptr) = 0;

        // 2. Отложенный вызов (добавляет в очередь на следующий Tick)
        virtual void DispatchDeferred(const eastl::string& eventName, const ArgInjector& argInjector = nullptr) = 0;
        
        // Метод для ExecutionManager, чтобы забрать очередь отложенных событий
        struct QueuedEvent {
            asIScriptFunction* func;
            ArgInjector argInjector;
        };
        virtual eastl::vector<QueuedEvent> PopDeferredEvents() = 0;
    };

    export struct IExecutionManager
    {
        virtual ~IExecutionManager() = default;
        virtual void Tick(const float deltaTime, IEventManager* eventManager, asIScriptEngine* engine) = 0;
        virtual void AbortAll() const = 0;
        virtual void Renew() = 0;
        virtual void RegisterThreadSupport(asIScriptEngine* engine) = 0;
        virtual eastl::expected<void, ExecutionError> RunAllMods(asIScriptEngine* engine, const IModuleLoader* moduleLoader) = 0;
        
        // Context Pooling Callbacks
        virtual asIScriptContext* RequestContext(asIScriptEngine* engine, void* param) = 0;
        virtual void ReturnContext(asIScriptEngine* engine, asIScriptContext* ctx, void* param) = 0;
    };

    export struct IStateSerializer
    {
        virtual ~IStateSerializer() = default;
        virtual eastl::expected<void, ModuleLoaderError> HotReload(asIScriptEngine* engine, const eastl::unique_ptr<IModuleLoader>& moduleLoader, const eastl::unique_ptr<IExecutionManager>& executionManager, const eastl::unique_ptr<IEventManager>& eventManager) = 0;
    };

    export struct IBindingManager
    {
        virtual ~IBindingManager() = default;
        virtual void RegisterStandardAddons(asIScriptEngine* engine) = 0;
        virtual eastl::expected<void, BindingError> BindAll(asIScriptEngine* const engine) = 0;
        virtual eastl::expected<void, BindingError> Bind(asIScriptEngine* const engine, IScriptBinding* binding) = 0;
        virtual void AddBinding(IScriptBinding* binding) = 0;
    };

    // Abstract Factory: Creates the family of engine components
    export struct IEngineComponentFactory
    {
        virtual ~IEngineComponentFactory() = default;
        virtual eastl::unique_ptr<IModuleLoader> CreateModuleLoader() = 0;
        virtual eastl::unique_ptr<IExecutionManager> CreateExecutionManager() = 0;
        virtual eastl::unique_ptr<IStateSerializer> CreateStateSerializer() = 0;
        virtual eastl::unique_ptr<IBindingManager> CreateBindingManager() = 0;
        virtual eastl::unique_ptr<IEventManager> CreateEventManager() = 0;
    };
    
    // Observer: Listener for engine events
    export struct IEngineListener
    {
        virtual ~IEngineListener() = default;
        virtual void OnEngineInitialized(asIScriptEngine* engine, IBindingManager* bindingManager, IModuleLoader* moduleLoader, IExecutionManager* executionManager) {}
        virtual void OnCompilationStarted(asIScriptEngine* engine) {}
        virtual void OnCompilationFinished(asIScriptEngine* engine, bool success) {}
        virtual void OnHotReloadStarted(asIScriptEngine* engine) {}
        virtual void OnHotReloadFinished(asIScriptEngine* engine) {}
        virtual void OnAddBinding(asIScriptEngine* engine, IScriptBinding* binding) {}
        virtual void OnScriptMessage(asIScriptEngine* engine, const asSMessageInfo* msg) {} // Redirects AS logs
    };
    
    export struct IScriptEngineGetters
    {
        virtual ~IScriptEngineGetters() = default;
        virtual asIScriptEngine* GetEngine() const = 0;
        virtual const eastl::unique_ptr<IEventManager>& GetEventManager() const = 0;
        virtual const eastl::unique_ptr<IBindingManager>& GetBindingManager() const = 0;
        virtual const eastl::unique_ptr<IExecutionManager>& GetExecutionManager() const = 0;
        virtual const eastl::unique_ptr<IModuleLoader>& GetModuleLoader() const = 0;
        virtual const eastl::unique_ptr<IStateSerializer>& GetStateSerializer() const = 0;
    };

    export struct IScriptEngine
    {
        virtual ~IScriptEngine() = default;
        virtual void Tick(float deltaTime) = 0;
        virtual eastl::expected<void, EngineError> RunAllMods() = 0;
        virtual eastl::expected<void, EngineError> CompileAllMods() = 0;
        virtual eastl::expected<void, EngineError> HotReload() const = 0;
        virtual void AddBinding(IScriptBinding* binding) const = 0;
        virtual void CallGarbageCollectorFullCycle() = 0;
        virtual void CallGarbageColletorOneStep() = 0;

        // Observer management
        virtual void AddListener(IEngineListener* listener) = 0;
        virtual void RemoveListener(IEngineListener* listener) = 0;
    };
}
