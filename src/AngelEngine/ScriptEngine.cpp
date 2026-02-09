module;

#include <memory>
#include <expected>
#include <filesystem>
#include <mutex>
#include <print>
#include <vector>
#include <map>
#include <string>
#include <chrono>

#include <as_jit.h>
#include <angelscript.h>

#include <scriptbuilder.h>
#include <serializer.h>
#include <contextmgr.h>
#include <debugger.h>

export module AngelEngine.ScriptEngine;

import AngelEngine.BindingManager;
import AngelEngine.PredefinedGenerator;
import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.StateSerializer;

namespace fs = std::filesystem;

namespace AngelEngine
{
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

    export struct EngineConfig final
    {
        explicit EngineConfig(ModuleLoaderConfig moduleLoaderConfig_, bool useJitCompiler_,
                              bool useAutoGC_) :
            moduleLoaderConfig(std::move(moduleLoaderConfig_)), useJitCompiler(useJitCompiler_),
            useAutoGC(useAutoGC_)
        {
        }

        const ModuleLoaderConfig moduleLoaderConfig;
        const bool useJitCompiler;
        const bool useAutoGC;
    };

    struct AngelScriptDeleter
    {
        void operator()(asIScriptEngine* engine) const
        {
            if (engine) engine->ShutDownAndRelease();
        }
    };

    export struct ScriptEngine final
    {
        using AsEnginePtr = std::unique_ptr<asIScriptEngine, AngelScriptDeleter>;
        using PtrType = std::unique_ptr<ScriptEngine>;

        static constexpr const char* AS_PREDEFINED_PATH = "angelscripts/as.predefined";

        static std::expected<PtrType, EngineError> MakeEngine(EngineConfig config)
        {
            asIScriptEngine* rawEngine = asCreateScriptEngine();
            if (!rawEngine)
            {
                return std::unexpected(EngineError::CreateAngelScriptEngineFailed);
            }

            AsEnginePtr engine(rawEngine);
            return std::unique_ptr<ScriptEngine>(new ScriptEngine(std::move(engine), std::move(config)));
        }

        void CallGarbageCollectorFullCycle()
        {
            std::scoped_lock lock(mutex_);
            engine_->GarbageCollect();
        }

        void CallGarbageColletorOneStep()
        {
            std::scoped_lock lock(mutex_);
            engine_->GarbageCollect(asEGCFlags::asGC_ONE_STEP);
        }

        void Tick(float deltaTime)
        {
            std::scoped_lock lock(mutex_);
            executionManager_->Tick(deltaTime);
        }

        std::expected<void, EngineError> RunAllMods()
        {
            std::scoped_lock lock(mutex_);

            auto resultRunMods = executionManager_->RunAllMods(engine_.get(), moduleLoader_);
            if (!resultRunMods.has_value())
            {
                std::println(stderr, "[ScriptEngine] Error when try RunAllMods, error code: {}", static_cast<int>(resultRunMods.error()));
                return std::unexpected(EngineError::FailRunMods);
            }

            return {};
        }

        std::expected<void, EngineError> CompileAllMods()
        {
            std::scoped_lock lock(mutex_);

            auto resultCompileMods = moduleLoader_->CompileAllMods(engine_.get());
            if (!resultCompileMods.has_value())
            {
                std::println(stderr, "[ScriptEngine] Error when try CompileAllMods, error code: {}", static_cast<int>(resultCompileMods.error()));
                return std::unexpected(EngineError::FailCompileMods);
            }

            return {};
        }

        std::expected<void, EngineError> HotReload() const
        {
            auto resultHotReload = stateSerializer_->HotReload(engine_.get(), moduleLoader_, executionManager_);
            if (!resultHotReload.has_value())
            {
                std::println(stderr, "[ScriptEngine] Error when try HotReload, error code: {}", static_cast<int>(resultHotReload.error()));
                return std::unexpected(EngineError::FailHotReload);
            }
            
            return {};
        }

    private:
        explicit ScriptEngine(AsEnginePtr as_engine, EngineConfig config) :
            config_(std::move(config)),
            engine_(std::move(as_engine))
        {
            InitializeEngine();
        }

        void InitializeEngine()
        {
            engine_->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);

            if (config_.useJitCompiler)
            {
                jit_ = std::make_unique<asCJITCompiler>();
                engine_->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, 1);
                engine_->SetJITCompiler(jit_.get());
            }

            if (!config_.useAutoGC)
            {
                engine_->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
            }

            engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 2);
            engine_->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
            engine_->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);

            // Register addons via BindingManager
            BindingManager::RegisterStandardAddons(engine_.get());

            moduleLoader_ = std::make_unique<ModuleLoader>(config_.moduleLoaderConfig);
            stateSerializer_ = std::make_unique<StateSerializer>();
            executionManager_ = std::make_unique<ExecutionManager>();

            executionManager_->RegisterThreadSupport(engine_.get());

            BindAll();
            GenerateScriptPredefined(engine_.get(), AS_PREDEFINED_PATH);
        }

        static void MessageCallback(const asSMessageInfo* msg, void* param)
        {
            const char* type = "INFO";
            if (msg->type == asMSGTYPE_WARNING) type = "WARN";
            else if (msg->type == asMSGTYPE_ERROR) type = "ERROR";
            std::println("[AS {}] {}:{}:{}: {}", type, msg->section, msg->row, msg->col, msg->message);
        }

        void BindAll()
        {
            auto result = AngelEngine::BindingManager::BindGlobals(engine_.get());
            if (!result.has_value())
            {
                std::println("Error when try binding globals");
            }
        }

        std::mutex mutex_;

        // Field order changed for correct initialization
        EngineConfig config_;
        ModuleLoader::PtrType moduleLoader_;
        ExecutionManager::PtrType executionManager_;
        StateSerializer::PtrType stateSerializer_;

        std::unique_ptr<asCJITCompiler> jit_;

        AsEnginePtr engine_;
    };
}
