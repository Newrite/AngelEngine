module;

#include <filesystem>
#include <mutex>
#include <format>

// ЗАМЕНИЛИ <as_jit.h> на <angelsea.hpp>
#include <angelsea.hpp>
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
import AngelEngine.ReloadManager;
import AngelEngine.SaveLoadManager;
import AngelEngine.Interfaces;
import AngelEngine.EventsBinding;
import AngelEngine.ScriptWatcher;
import AngelEngine.Logger;
import AngelEngine.FrameAllocator;
import AngelEngine.Memory;

namespace fs = std::filesystem;

namespace AngelEngine
{
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

        static eastl::expected<PtrType, EngineError> MakeEngine(eastl::unique_ptr<IEngineComponentFactory> factory)
        {
            asSetGlobalMemoryFunctions(EngineAlloc, EngineFree);
            asIScriptEngine* rawEngine = asCreateScriptEngine();
            if (!rawEngine)
            {
                Log::Error("[ScriptEngine] Failed to create AngelScript engine.");
                return eastl::unexpected(EngineError::CreateAngelScriptEngineFailed);
            }

            AsEnginePtr engine(rawEngine);

            auto scriptEngine = eastl::unique_ptr<ScriptEngine>( new ScriptEngine (
                eastl::move(engine),
                eastl::move(factory)
            ));

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
            int r = engine_->GarbageCollect();
            if (r < 0) Log::Error("[ScriptEngine] GarbageCollect failed with code: {}", r);
        }

        void CallGarbageColletorOneStep() override
        {
            std::scoped_lock lock(mutex_);
            int r = engine_->GarbageCollect(asEGCFlags::asGC_ONE_STEP);
            if (r < 0) Log::Error("[ScriptEngine] GarbageCollect (OneStep) failed with code: {}", r);
        }

        void PushTick(float deltaTime)
        {
            std::scoped_lock lock(mutex_);

            if (eventBinding_)
            {
                static_cast<EventBinding*>(eventBinding_.get())->PushTick(deltaTime);
            }
        }

        eastl::expected<void, EngineError> Tick(float deltaTime) override
        {
            std::scoped_lock lock(mutex_);

            if (scriptWatcher_ && scriptWatcher_->CheckAndResetReloadFlag())
            {
                auto reloadResult = HotReload();
                if (!reloadResult.has_value())
                {
                    Log::Error("[ScriptEngine] Auto-reload failed with code: {}",
                               static_cast<int>(reloadResult.error()));
                }
            }
            
            if (eventBinding_)
            {
                static_cast<EventBinding*>(eventBinding_.get())->PushTick(deltaTime);
            }

            auto tickResult = executionManager_->Tick(deltaTime, eventManager_.get(), engine_.get());

            FrameMemoryPool::Get().Reset();

            if (!tickResult.has_value())
            {
                Log::Error("[ScriptEngine] ExecutionManager Tick failed: {}", static_cast<int>(tickResult.error()));
                return eastl::unexpected(EngineError::GenericError);
            }

            return {};
        }

        eastl::expected<void, EngineError> RunAllMods() override
        {
            std::scoped_lock lock(mutex_);

            auto resultRunMods = executionManager_->RunAllMods(engine_.get(), moduleLoader_.get());
            if (!resultRunMods.has_value())
            {
                Log::Error("[ScriptEngine] Failed to run all mods: {}", static_cast<int>(resultRunMods.error()));
                return eastl::unexpected(EngineError::FailRunMods);
            }

            return {};
        }

        eastl::expected<void, EngineError> RunMod(const eastl::string& modName) override
        {
            std::scoped_lock lock(mutex_);

            auto resultRunMod = executionManager_->RunMod(engine_.get(), modName);
            if (!resultRunMod.has_value())
            {
                Log::Error("[ScriptEngine] Failed to run mod {}: {}", modName.c_str(),
                           static_cast<int>(resultRunMod.error()));
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
                Log::Error("[ScriptEngine] Failed to compile all mods: {}",
                           static_cast<int>(resultCompileMods.error()));
                return eastl::unexpected(EngineError::FailCompileMods);
            }

            return {};
        }

        eastl::expected<void, EngineError> HotReload() const override
        {
            BroadcastHotReloadStarted();

            auto resultHotReload = reloadManager_->ReloadScripts(engine_.get(), moduleLoader_.get(),
                                                                 executionManager_.get(), eventManager_.get());
            BroadcastHotReloadFinished();

            if (!resultHotReload.has_value())
            {
                Log::Error("[ScriptEngine] Hot reload failed: {}", static_cast<int>(resultHotReload.error()));
                return eastl::unexpected(EngineError::FailHotReload);
            }

            return {};
        }

        void AddBinding(IScriptBinding* binding) const override
        {
            bindingManager_->AddBinding(binding);
            BroadcastAddBinding(binding);
        }

        IEventManager* GetEventManager() const override { return eventManager_.get(); }
        IBindingManager* GetBindingManager() const override { return bindingManager_.get(); }
        IExecutionManager* GetExecutionManager() const override { return executionManager_.get(); }
        IModuleLoader* GetModuleLoader() const override { return moduleLoader_.get(); }
        IReloadManager* GetReloadManager() const override { return reloadManager_.get(); }
        ISaveLoadManager* GetSaveLoadManager() const override { return saveLoadManager_.get(); }
        asIScriptEngine* GetEngine() const override { return engine_.get(); }

        eastl::expected<void, EngineError> InitializeEngine()
        {
            int r = engine_->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_CDECL);
            if (r < 0)
            {
                Log::Error("[ScriptEngine] Failed to set message callback: {}", r);
                return eastl::unexpected(EngineError::GenericError);
            }

            r = engine_->SetContextCallbacks(
                RequestContextCallback,
                ReturnContextCallback,
                this
            );
            if (r < 0)
            {
                Log::Error("[ScriptEngine] Failed to set context callbacks: {}", r);
                return eastl::unexpected(EngineError::CreateAngelScriptContextFailed);
            }

            // --- ИНИЦИАЛИЗАЦИЯ ANGELSEA JIT ---
            if (engine_config_.enableUseJIT)
            {
                engine_->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, 1);
                engine_->SetEngineProperty(asEP_JIT_INTERFACE_VERSION, 2); // Обязательно для Angelsea

                // Если Watchdog будет отключен, здесь можно будет поставить 1 для максимальной скорости
                Log::Info("Watch dog is: {}", engine_config_.enableWatchdog);
                engine_->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, engine_config_.enableWatchdog ? 0 : 1);

                jitConfig_ = eastl::make_unique<angelsea::JitConfig>();

                // Для наших тестов производительности мы хотим, чтобы JIT компилировал функции сразу (AOT-style), 
                // а не ждал, пока они "прогреются".
                jitConfig_->triggers.hits_before_func_compile = 0;

                jit_ = eastl::make_unique<angelsea::Jit>(*jitConfig_, *engine_);

                r = engine_->SetJITCompiler(jit_.get());
                if (r < 0)
                {
                    Log::Error("[ScriptEngine] Failed to attach Angelsea JIT Compiler: {}", r);
                }
                else
                {
                    Log::Info("[ScriptEngine] Angelsea JIT Compiler successfully attached!");
                }
            }

            if (!engine_config_.enableAutoGC)
            {
                engine_->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
            }

            engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 2);
            engine_->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
            engine_->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);
            engine_->SetEngineProperty(asEP_MAX_NESTED_CALLS, 100);

            auto result = bindingManager_->RegisterStandardAddons(engine_.get());
            if (!result.has_value())
            {
                Log::Error("[ScriptEngine] Failed to register standard addons.");
                return eastl::unexpected(EngineError::GenericError);
            }

            executionManager_->RegisterThreadSupport(engine_.get());

            auto bindResult = BindAll();
            if (!bindResult.has_value())
            {
                return eastl::unexpected(EngineError::GenericError);
            }

            GenerateScriptPredefined(engine_.get(), engine_config_.asPredefinedPath);
            BroadcastEngineInitialized();

            return {};
        }

    private:
        explicit ScriptEngine(AsEnginePtr as_engine,
                              eastl::unique_ptr<IEngineComponentFactory> factory) :
            jit_(nullptr),
            jitConfig_(nullptr),
            engine_(eastl::move(as_engine))
        {
            
            engine_config_ = factory->GetEngineConfig();
            
            moduleLoader_ = factory->CreateModuleLoader();
            executionManager_ = factory->CreateExecutionManager();
            reloadManager_ = factory->CreateReloadManager();
            saveLoadManager_ = factory->CreateSaveLoadManager();
            bindingManager_ = factory->CreateBindingManager();
            eventManager_ = factory->CreateEventManager();

            eventBinding_ = eastl::make_unique<EventBinding>(eventManager_.get());
            scriptWatcher_ = factory->CreateScriptWatcher();
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

        eastl::expected<void, BindingError> BindAll()
        {
            if (eventBinding_)
            {
                auto result = bindingManager_->Bind(engine_.get(), eventBinding_.get());
                if (!result.has_value())
                {
                    Log::Error("[ScriptEngine] Failed to bind EventBinding.");
                    return result;
                }
            }

            auto result = bindingManager_->BindAll(engine_.get());
            if (!result.has_value())
            {
                Log::Error("[ScriptEngine] Failed to bind all bindings: {}", static_cast<int>(result.error()));
                return result;
            }
            return {};
        }

        void Bind(IScriptBinding* binding)
        {
            auto result = bindingManager_->Bind(engine_.get(), binding);
            if (!result.has_value())
            {
                Log::Error("[ScriptEngine] Failed to bind: {}", static_cast<int>(result.error()));
            }
        }

        void BroadcastEngineInitialized() const
        {
            for (auto* listener : listeners_) listener->OnEngineInitialized(
                engine_.get(), bindingManager_.get(), moduleLoader_.get(), executionManager_.get());
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
        // --- ANGELSEA JIT POINTERS ---
        eastl::unique_ptr<angelsea::Jit> jit_;
        eastl::unique_ptr<angelsea::JitConfig> jitConfig_;

        AsEnginePtr engine_;
        EngineConfig engine_config_;

        std::mutex mutex_;
        eastl::vector<IEngineListener*> listeners_;

        eastl::unique_ptr<IModuleLoader> moduleLoader_;
        eastl::unique_ptr<IExecutionManager> executionManager_;
        eastl::unique_ptr<IReloadManager> reloadManager_;
        eastl::unique_ptr<ISaveLoadManager> saveLoadManager_;
        eastl::unique_ptr<IBindingManager> bindingManager_;
        eastl::unique_ptr<IEventManager> eventManager_;

        eastl::unique_ptr<IScriptBinding> eventBinding_;
        eastl::unique_ptr<IScriptWatcher> scriptWatcher_;
    };
}
