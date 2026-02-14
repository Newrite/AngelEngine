module;

#include <filesystem>

#include <print>

#include <angelscript.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

export module AngelEngine.Infrastructure;

import AngelEngine.Interfaces;
import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.StateSerializer;
import AngelEngine.BindingManager;
import AngelEngine.EventManager;

namespace fs = std::filesystem;

namespace AngelEngine
{
    // --- Concrete Strategy: File System Source Provider ---
    
    export class FileSystemScriptSourceProvider final : public IScriptSourceProvider
    {
    public:
        explicit FileSystemScriptSourceProvider(ModuleLoaderConfig config)
            : config_(std::move(config))
        {}

        fs::path GetStdLibPath() const override
        {
            return config_.scriptsPathStd;
        }

        eastl::vector<eastl::string> GetAvailableMods() const override
        {
            eastl::vector<eastl::string> mods;
            if (!fs::exists(config_.scriptsPathMod)) return mods;

            for (const auto& entry : fs::directory_iterator(config_.scriptsPathMod))
            {
                if (entry.is_directory())
                {
                    fs::path mainScriptPath = entry.path() / "main.as";
                    if (fs::exists(mainScriptPath))
                    {
                        mods.push_back(entry.path().filename().string().c_str());
                    }
                }
            }
            return mods;
        }

        fs::path GetModPath(const eastl::string& modName) const override
        {
            return config_.scriptsPathMod / modName.c_str();
        }

        eastl::vector<fs::path> GetScriptFiles(const fs::path& rootPath) const override
        {
            eastl::vector<fs::path> scripts;
            if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) return scripts;

            for (const auto& entry : fs::recursive_directory_iterator(rootPath))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".as")
                {
                    scripts.push_back(fs::absolute(entry.path()));
                }
            }
            return scripts;
        }

    private:
        ModuleLoaderConfig config_;
    };

    // --- Concrete Observer: Console Logger ---

    export class ConsoleEngineListener final : public IEngineListener
    {
    public:
        void OnEngineInitialized(asIScriptEngine* engine, IBindingManager* bindingManager, IModuleLoader* moduleLoader, IExecutionManager* executionManager) override
        {
            std::println("[Listener] Engine Initialized.");
        }

        void OnCompilationStarted(asIScriptEngine* engine) override
        {
            std::println("[Listener] Compilation Started...");
        }

        void OnCompilationFinished(asIScriptEngine* engine, bool success) override
        {
            if (success)
                std::println("[Listener] Compilation Finished Successfully.");
            else
                std::println(stderr, "[Listener] Compilation Failed.");
        }

        void OnHotReloadStarted(asIScriptEngine* engine) override
        {
            std::println("[Listener] Hot Reload Started...");
        }

        void OnHotReloadFinished(asIScriptEngine* engine) override
        {
            std::println("[Listener] Hot Reload Finished.");
        }
        
        void OnAddBinding(asIScriptEngine* engine, IScriptBinding* binding) override
        {
            std::println("[Listener] Add binding.");
        }

        void OnScriptMessage(asIScriptEngine* engine, const asSMessageInfo* msg) override
        {
            const char* type = "INFO";
            if (msg->type == asMSGTYPE_WARNING) type = "WARN";
            else if (msg->type == asMSGTYPE_ERROR) type = "ERROR";
            std::println("[AS {}] {}:{}:{}: {}", type, msg->section, msg->row, msg->col, msg->message);
        }
    };

    // --- Concrete Factory: Standard Components ---

    export class StandardComponentFactory final : public IEngineComponentFactory
    {
    public:
        explicit StandardComponentFactory(ModuleLoaderConfig config)
            : config_(std::move(config))
        {}

        eastl::unique_ptr<IModuleLoader> CreateModuleLoader() override
        {
            // Inject the FileSystem Strategy into the ModuleLoader
            auto provider = eastl::make_unique<FileSystemScriptSourceProvider>(config_);
            return eastl::make_unique<ModuleLoader>(eastl::move(provider));
        }

        eastl::unique_ptr<IExecutionManager> CreateExecutionManager() override
        {
            return eastl::make_unique<ExecutionManager>();
        }

        eastl::unique_ptr<IStateSerializer> CreateStateSerializer() override
        {
            return eastl::make_unique<StateSerializer>();
        }

        eastl::unique_ptr<IBindingManager> CreateBindingManager() override
        {
            return eastl::make_unique<BindingManager>();
        }

        eastl::unique_ptr<IEventManager> CreateEventManager() override
        {
            return eastl::make_unique<EventManager>();
        }

    private:
        ModuleLoaderConfig config_;
    };
}
