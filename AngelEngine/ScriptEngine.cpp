module;

#include <filesystem>
#include <mutex>

#include <as_jit.h>
#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/algorithm.h>
#include <EASTL/expected.h>

export module AngelEngine.ScriptEngine;

import AngelEngine.BindingManager;
import AngelEngine.PredefinedGenerator;
import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.StateSerializer;
import AngelEngine.Interfaces;

namespace fs = std::filesystem;

namespace AngelEngine
{
    // Configurations are now handled by the Factory

    struct AngelScriptDeleter
    {
        void operator()(asIScriptEngine* engine) const
        {
            if (engine) engine->ShutDownAndRelease();
        }
    };

    export class ScriptEngine final : public IScriptEngine, public IScriptEngineGetters
    {
    public:
        using AsEnginePtr = eastl::unique_ptr<asIScriptEngine, AngelScriptDeleter>;
        using PtrType = eastl::unique_ptr<ScriptEngine>;

        static constexpr const char* AS_PREDEFINED_PATH = "AngelEngine/scripts/as.predefined";

        static eastl::expected<PtrType, EngineError> MakeEngine(eastl::unique_ptr<IEngineComponentFactory> factory, bool useJit = true, bool useAutoGC = false)
        {
            asIScriptEngine* rawEngine = asCreateScriptEngine();
            if (!rawEngine)
            {
                return eastl::unexpected(EngineError::CreateAngelScriptEngineFailed);
            }

            AsEnginePtr engine(rawEngine);
            
            auto scriptEngine = eastl::unique_ptr<ScriptEngine>(new ScriptEngine(
                eastl::move(engine), 
                eastl::move(factory),
                useJit,
                useAutoGC
            ));
            
            scriptEngine->InitializeEngine();
            
            return scriptEngine;
        }

        void AddListener(IEngineListener* listener) override
        {
            std::scoped_lock lock(mutex_);
            if (listener && eastl::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
            {
                listeners_.push_back(listener);
            }
        }

        void RemoveListener(IEngineListener* listener) override
        {
            std::scoped_lock lock(mutex_);
            listeners_.erase(eastl::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
        }

        void CallGarbageCollectorFullCycle() override
        {
            std::scoped_lock lock(mutex_);
            engine_->GarbageCollect();
        }

        void CallGarbageColletorOneStep() override
        {
            std::scoped_lock lock(mutex_);
            engine_->GarbageCollect(asEGCFlags::asGC_ONE_STEP);
        }

        void Tick(float deltaTime) override
        {
            std::scoped_lock lock(mutex_);
            eventManager_->DispatchDeferred("OnTick", [&](asIScriptContext* ctx)
            {
                ctx->SetArgFloat(0, deltaTime);
            });
            executionManager_->Tick(deltaTime, eventManager_.get(), engine_.get());
        }

        eastl::expected<void, EngineError> RunAllMods() override
        {
            std::scoped_lock lock(mutex_);

            auto resultRunMods = executionManager_->RunAllMods(engine_.get(), moduleLoader_.get());
            if (!resultRunMods.has_value())
            {
                // TODO: Broadcast error via listener
                return eastl::unexpected(EngineError::FailRunMods);
            }

            return {};
        }

        eastl::expected<void, EngineError> CompileAllMods() override
        {
            std::scoped_lock lock(mutex_);

            BroadcastCompilationStarted();
            auto resultCompileMods = moduleLoader_->CompileAllMods(engine_.get());
            BroadcastCompilationFinished(resultCompileMods.has_value());

            if (!resultCompileMods.has_value())
            {
                return eastl::unexpected(EngineError::FailCompileMods);
            }

            return {};
        }

        eastl::expected<void, EngineError> HotReload() const override
        {
            BroadcastHotReloadStarted();
            
            auto resultHotReload = stateSerializer_->HotReload(engine_.get(), moduleLoader_, executionManager_, eventManager_);
            BroadcastHotReloadFinished();

            if (!resultHotReload.has_value())
            {
                return eastl::unexpected(EngineError::FailHotReload);
            }
            
            return {};
        }
        
        void AddBinding(IScriptBinding* binding) const override
        {
            bindingManager_->AddBinding(binding);
            BroadcastAddBinding(binding);
        }

        const eastl::unique_ptr<IEventManager>& GetEventManager() const override { return eventManager_; }
        const eastl::unique_ptr<IBindingManager>& GetBindingManager() const override { return bindingManager_; }
        const eastl::unique_ptr<IExecutionManager>& GetExecutionManager() const override { return executionManager_; }
        const eastl::unique_ptr<IModuleLoader>& GetModuleLoader() const override { return moduleLoader_; }
        const eastl::unique_ptr<IStateSerializer>& GetStateSerializer() const override { return stateSerializer_; }
        asIScriptEngine* GetEngine() const override { return engine_.get(); }
        
        void InitializeEngine()
        {
            // Set message callback with 'this' as user param
            engine_->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_CDECL);

            // Set Context Callbacks for Pooling
            // asFUNCTION macro creates an asSFuncPtr, but SetContextCallbacks expects raw function pointers.
            // Since our callbacks are static, we can pass them directly.
            engine_->SetContextCallbacks(
                RequestContextCallback,
                ReturnContextCallback,
                this
            );

            if (useJit_)
            {
                jit_ = eastl::make_unique<asCJITCompiler>();
                engine_->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, 1);
                engine_->SetJITCompiler(jit_.get());
            }

            if (!useAutoGC_)
            {
                engine_->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
            }

            engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 2);
            engine_->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
            engine_->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);

            bindingManager_->RegisterStandardAddons(engine_.get());
            executionManager_->RegisterThreadSupport(engine_.get());

            BindAll();
            GenerateScriptPredefined(engine_.get(), AS_PREDEFINED_PATH);
            BroadcastEngineInitialized();
            
        }

    private:
        explicit ScriptEngine(AsEnginePtr as_engine, 
                              eastl::unique_ptr<IEngineComponentFactory> factory,
                              bool useJit,
                              bool useAutoGC) :
            jit_(nullptr),
            engine_(eastl::move(as_engine)),
            useJit_(useJit),
            useAutoGC_(useAutoGC)
        {
            // Use Factory to create components
            moduleLoader_ = factory->CreateModuleLoader();
            executionManager_ = factory->CreateExecutionManager();
            stateSerializer_ = factory->CreateStateSerializer();
            bindingManager_ = factory->CreateBindingManager();
            eventManager_ = factory->CreateEventManager();

            // Create and register EventBinding
            eventBinding_ = eastl::make_unique<EventBinding>(eventManager_.get());
            // bindingManager_->AddBinding(eventBinding_.get()); // Removed: BindAll will handle this if we add it to the list, but wait...
        }

        static void MessageCallback(const asSMessageInfo* msg, void* param)
        {
            ScriptEngine* self = static_cast<ScriptEngine*>(param);
            if (self)
            {
                self->BroadcastScriptMessage(msg);
            }
        }

        static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param)
        {
            ScriptEngine* self = static_cast<ScriptEngine*>(param);
            if (self && self->executionManager_)
            {
                return self->executionManager_->RequestContext(engine, param);
            }
            return nullptr;
        }

        static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param)
        {
            ScriptEngine* self = static_cast<ScriptEngine*>(param);
            if (self && self->executionManager_)
            {
                self->executionManager_->ReturnContext(engine, ctx, param);
            }
        }

        void BindAll()
        {
            // Manually bind EventBinding first or as part of the process
            // Since EventBinding is not in the BindingManager's list by default (unless added), we bind it here.
            // Wait, in the constructor we created eventBinding_ but didn't add it to bindingManager_ list properly?
            // Ah, the previous code had: bindingManager_->AddBinding(eventBinding_.get());
            // But BindAll iterates over bindings_.
            
            // Let's make sure we bind the event system.
            if (eventBinding_)
            {
                 bindingManager_->Bind(engine_.get(), eventBinding_.get());
            }

            auto result = bindingManager_->BindAll(engine_.get());
            if (!result.has_value())
            {
                // TODO: Broadcast error
            }
        }
        
        void Bind(IScriptBinding* binding)
        {
            auto result = bindingManager_->Bind(engine_.get(), binding);
            if (!result.has_value())
            {
                // TODO: Broadcast error
            }
        }

        // --- Notification Helpers ---

        void BroadcastEngineInitialized() const
        {
            for (auto* listener : listeners_) listener->OnEngineInitialized(engine_.get(), bindingManager_.get(), moduleLoader_.get(), executionManager_.get());
        }

        void BroadcastCompilationStarted() const
        {
            for (auto* listener : listeners_) listener->OnCompilationStarted(engine_.get());
        }

        void BroadcastCompilationFinished(bool success) const
        {
            for (auto* listener : listeners_) listener->OnCompilationFinished(engine_.get(), success);
        }

        void BroadcastHotReloadStarted() const
        {
            for (auto* listener : listeners_) listener->OnHotReloadStarted(engine_.get());
        }

        void BroadcastHotReloadFinished() const
        {
            for (auto* listener : listeners_) listener->OnHotReloadFinished(engine_.get());
        }
        
        void BroadcastAddBinding(IScriptBinding* binding) const
        {
            for (auto* listener : listeners_) listener->OnAddBinding(engine_.get(), binding);
        }

        void BroadcastScriptMessage(const asSMessageInfo* msg) const
        {
            for (auto* listener : listeners_) listener->OnScriptMessage(engine_.get(), msg);
        }

    private:
        eastl::unique_ptr<asCJITCompiler> jit_;
        AsEnginePtr engine_; 

        std::mutex mutex_;
        eastl::vector<IEngineListener*> listeners_;

        // Configurations
        bool useJit_;
        bool useAutoGC_;

        // Components
        eastl::unique_ptr<IModuleLoader> moduleLoader_;
        eastl::unique_ptr<IExecutionManager> executionManager_;
        eastl::unique_ptr<IStateSerializer> stateSerializer_;
        eastl::unique_ptr<IBindingManager> bindingManager_;
        eastl::unique_ptr<IEventManager> eventManager_;
        
        // Bindings
        eastl::unique_ptr<IScriptBinding> eventBinding_;
    };
}
