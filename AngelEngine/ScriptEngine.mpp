module;

#include <EASTL/algorithm.h>
#include <EASTL/expected.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <format>
#include <shared_mutex>

#include <angelscript.h>
#include <angelsea.hpp>

import AngelEngine.NativeViewArray;


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
import AngelEngine.Errors;
import AngelEngine.Types;
import AngelEngine.EventsInterfaces;
import AngelEngine.SerializationHandlers;
import AngelEngine.Utils;

namespace AngelEngine
{
    struct AngelScriptDeleter
    {
        void operator()(asIScriptEngine* engine) const
        {
            if (engine)
                engine->ShutDownAndRelease();
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

            auto scriptEngine =
                eastl::unique_ptr<ScriptEngine>(new ScriptEngine(eastl::move(engine), eastl::move(factory)));

            return scriptEngine;
        }

        void AddListener(IEngineListener* listener) override
        {
            std::unique_lock lock(mutex_);
            if (listener && eastl::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
            {
                listeners_.push_back(listener);
            }
        }

        void RemoveListener(IEngineListener* listener) override
        {
            std::unique_lock lock(mutex_);
            listeners_.erase(eastl::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
        }

        void CallGarbageCollectorFullCycle() override
        {
            std::unique_lock lock(mutex_);
            int r = engine_->GarbageCollect();
            if (r < 0)
                Log::Error("[ScriptEngine] GarbageCollect failed with code: {}", r);
        }

        void CallGarbageColletorOneStep() override
        {
            std::unique_lock lock(mutex_);
            int r = engine_->GarbageCollect(asEGCFlags::asGC_ONE_STEP);
            if (r < 0)
                Log::Error("[ScriptEngine] GarbageCollect (OneStep) failed with code: {}", r);
        }


        eastl::expected<void, EngineError> Tick(float deltaTime) override
        {
            if (scriptWatcher_ && scriptWatcher_->CheckAndResetReloadFlag())
            {
                auto reloadResult = HotReload();
                if (!reloadResult.has_value())
                {
                    Log::Error("[ScriptEngine] Auto-reload failed with code: {}",
                               static_cast<int>(reloadResult.error()));
                }
            }

            std::shared_lock lock(mutex_);

            // eventBinding_ implements IBuiltinEventDispatcher — ExecutionManager::Tick
            // calls DispatchBuiltinEvents(ctx, dt) directly with its pooled context.
            // No more Enqueue + queue swap for OnTick.
            IBuiltinEventDispatcher* dispatcher = eventBinding_.get();

            auto tickResult = executionManager_->Tick(deltaTime, eventManager_.get(), engine_.get(), dispatcher);

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
            std::shared_lock lock(mutex_);

            auto resultRunMods = executionManager_->RunAllMods(engine_.get(), moduleLoader_.get());
            if (!resultRunMods.has_value())
            {
                Log::Error("[ScriptEngine] Failed to run all mods: {}", static_cast<int>(resultRunMods.error()));
                return eastl::unexpected(EngineError::FailRunMods);
            }

            return {};
        }

        eastl::expected<void, EngineError> CompileAllMods() override
        {
            std::unique_lock lock(mutex_);

            // Gather AS event descriptors from all registered channels.
            // These drive the auto-generated __dispatcher__ section injected by ModuleLoader.
            eastl::vector<ChannelDescriptor> descriptors;
            if (eventManager_)
                descriptors = eventManager_->GetAllDescriptors();

            BroadcastCompilationStarted();
            auto resultCompileMods = moduleLoader_->CompileAllMods(engine_.get(), descriptors);
            BroadcastCompilationFinished(resultCompileMods.has_value());

            if (!resultCompileMods.has_value())
            {
                Log::Error("[ScriptEngine] Failed to compile all mods: {}",
                           static_cast<int>(resultCompileMods.error()));
                return eastl::unexpected(EngineError::FailCompileMods);
            }

            // Wire AS dispatcher function pointers into each EventChannel.
            // After this, each channel's Dispatch() / ProcessDeferred() calls the
            // AS dispatcher (1 C++→AS hop) instead of N individual subscribers.
            if (!descriptors.empty())
                WireDispatchers(descriptors);

            return {};
        }

        eastl::expected<void, EngineError> HotReload() const override
        {
            std::unique_lock lock(mutex_);
            BroadcastHotReloadStarted();

            auto resultHotReload = reloadManager_->ReloadScripts(engine_.get(), moduleLoader_.get(),
                                                                 executionManager_.get(), eventManager_.get());

            if (resultHotReload.has_value() && eventManager_)
            {
                auto descriptors = eventManager_->GetAllDescriptors();
                if (!descriptors.empty())
                    WireDispatchers(descriptors);
            }

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
            std::unique_lock lock(mutex_);
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

            r = engine_->SetContextCallbacks(RequestContextCallback, ReturnContextCallback, this);
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

            // Register serialization handlers for array<T> and dictionary
            // These handlers must be registered BEFORE any save/load operations
            // Note: serializer will be set by SaveLoadManager when GetSaveData/LoadFromData is called
            auto* arrayHandler = new ArraySerializationHandler();
            auto* dictHandler = new DictionarySerializationHandler();
            arrayHandler->SetEngine(engine_.get());
            dictHandler->SetEngine(engine_.get());
            saveLoadManager_->AddHandler(arrayHandler);
            saveLoadManager_->AddHandler(dictHandler);

            RegisterNativeViewArray();

            auto bindResult = BindAll();
            if (!bindResult.has_value())
            {
                return eastl::unexpected(EngineError::GenericError);
            }

            BroadcastEngineInitialized();


            return {};
        }

        void GeneratePredefined()
        {
            auto descriptors = eventManager_->GetAllDescriptors();
            GenerateScriptPredefined(engine_.get(), engine_config_.asPredefinedPath, descriptors);
        }

    private:
        explicit ScriptEngine(AsEnginePtr as_engine, eastl::unique_ptr<IEngineComponentFactory> factory) :
            jitConfig_(nullptr), jit_(nullptr), engine_(eastl::move(as_engine))
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
            if (msg->type == asMSGTYPE_ERROR)
            {
                std::printf("[AS-COMPILER-ERROR] %s (%d, %d): %s\n", msg->section, msg->row, msg->col, msg->message);
            }

            ScriptEngine* self = static_cast<ScriptEngine*>(param);
            if (self)
            {
                if (msg->type == asMSGTYPE_ERROR && self->moduleLoader_)
                {
                    self->moduleLoader_->RecordCompilationError(msg->section);
                }
                self->BroadcastScriptMessage(msg);
            }
        }

        static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param)
        {
            ScriptEngine* self = static_cast<ScriptEngine*>(param);
            if (self && self->executionManager_)
            {
                // Engine requests context. We return raw pointer and engine takes ownership.
                // Later it will be returned via ReturnContextCallback.
                auto ctxPtr = self->executionManager_->RequestContext(engine, param);
                return ctxPtr.release();
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

        // Wire AS dispatcher function pointers into each EventChannel after compile.
        // For each descriptor, looks up the generated __EngineDispatch<Event>__ function
        // in __Megamodule__ and calls the appropriate Set*DispatcherFn on EventBinding.
        void RegisterNativeViewArray()
        {
            int r =
                engine_->RegisterObjectType("NativeViewArray<class T>", 0, asOBJ_REF | asOBJ_NOCOUNT | asOBJ_TEMPLATE);
            if (r < 0)
                Log::Error("[ScriptEngine] Failed to register NativeViewArray<T> object type: {}", r);

            // Register factory (we don't actually need one since C++ instantiates them, but asOBJ_TEMPLATE might
            // complain without it) Wait, asOBJ_NOCOUNT doesn't strictly need a factory if scripts only receive it as
            // parameters.

            // length() method
            r = engine_->RegisterObjectMethod("NativeViewArray<T>", "uint length() const",
                                              asMETHOD(NativeViewArray<void*>, GetSize), asCALL_THISCALL);
            if (r < 0)
                Log::Error("[ScriptEngine] Failed to register NativeViewArray<T>::length: {}", r);

            // opIndex - returns const reference
            r = engine_->RegisterObjectMethod("NativeViewArray<T>", "const T& opIndex(uint) const",
                                              asMETHOD(NativeViewArray<void*>, At), asCALL_THISCALL);
            if (r < 0)
                Log::Error("[ScriptEngine] Failed to register NativeViewArray<T>::opIndex: {}", r);
        }

        void WireDispatchers(const eastl::vector<ChannelDescriptor>& descriptors) const
        {
            asIScriptModule* mod = engine_->GetModule(MegaModuleName, asGM_ONLY_IF_EXISTS);
            if (!mod)
            {
                Log::Error("[ScriptEngine] WireDispatchers: __Megamodule__ not found.");
                return;
            }

            for (const auto& d : descriptors)
            {
                eastl::string funcName = "__EngineDispatch" + d.eventName + "__";
                asIScriptFunction* fn = mod->GetFunctionByName(funcName.c_str());
                if (!fn)
                {
                    Log::Warning("[ScriptEngine] WireDispatchers: function not found: {}", funcName.c_str());
                    continue;
                }

                uint32_t eventId = HashString(d.eventName.c_str());
                auto* channel = eventManager_->GetChannel(eventId);
                if (channel)
                {
                    Log::Info("[ScriptEngine] Wired dispatcher: {} to channel {}", funcName.c_str(),
                              d.eventName.c_str());
                    channel->SetDispatcherFn(engine_.get(), executionManager_.get(), fn);
                    channel->WarmupJIT();
                }
                else
                {
                    Log::Warning("[ScriptEngine] WireDispatchers: channel not found for event: {}",
                                 d.eventName.c_str());
                }
            }
        }

        void BroadcastEngineInitialized() const
        {
            for (auto* listener : listeners_)
                listener->OnEngineInitialized(engine_.get(), bindingManager_.get(), moduleLoader_.get(),
                                              executionManager_.get());
        }

        void BroadcastCompilationStarted() const
        {
            for (auto* listener : listeners_)
                listener->OnCompilationStarted(engine_.get());
        }

        void BroadcastCompilationFinished(bool success) const
        {
            for (auto* listener : listeners_)
                listener->OnCompilationFinished(engine_.get(), success);
        }

        void BroadcastHotReloadStarted() const
        {
            for (auto* listener : listeners_)
                listener->OnHotReloadStarted(engine_.get());
        }

        void BroadcastHotReloadFinished() const
        {
            for (auto* listener : listeners_)
                listener->OnHotReloadFinished(engine_.get());
        }

        void BroadcastAddBinding(IScriptBinding* binding) const
        {
            for (auto* listener : listeners_)
                listener->OnAddBinding(engine_.get(), binding);
        }

        void BroadcastScriptMessage(const asSMessageInfo* msg) const
        {
            for (auto* listener : listeners_)
                listener->OnScriptMessage(engine_.get(), msg);
        }

    private:
        // --- ANGELSEA JIT POINTERS ---
        // Must be declared FIRST so it outlives the engine itself. The engine depends on the JIT compiler pointer
        // and may call it during ShutDownAndRelease.
        eastl::unique_ptr<angelsea::JitConfig> jitConfig_;
        eastl::unique_ptr<angelsea::Jit> jit_;

        // --- ANGELSCRIPT ENGINE ---
        // Must be declared BEFORE other components so it's destroyed AFTER them.
        AsEnginePtr engine_;

        EngineConfig engine_config_;

        mutable std::shared_mutex mutex_;
        eastl::vector<IEngineListener*> listeners_;

        eastl::unique_ptr<IModuleLoader> moduleLoader_;
        eastl::unique_ptr<IExecutionManager> executionManager_;
        eastl::unique_ptr<IReloadManager> reloadManager_;
        eastl::unique_ptr<ISaveLoadManager> saveLoadManager_;
        eastl::unique_ptr<IBindingManager> bindingManager_;
        eastl::unique_ptr<IEventManager> eventManager_;

        eastl::unique_ptr<IBuiltinEventDispatcher> eventBinding_;
        eastl::unique_ptr<IScriptWatcher> scriptWatcher_;
    };
} // namespace AngelEngine
