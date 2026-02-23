module;

#include <angelscript.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>



#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>


export module AngelEngine.Infrastructure;

import AngelEngine.Interfaces;
import AngelEngine.ModuleLoader;
import AngelEngine.ExecutionManager;
import AngelEngine.ReloadManager;
import AngelEngine.SaveLoadManager;
import AngelEngine.BindingManager;
import AngelEngine.EventManager;
import AngelEngine.ScriptWatcher;

namespace fs = std::filesystem;

namespace AngelEngine
{
    // --- Concrete Strategy: File System Source Provider ---

    export struct ModMetadata
    {
        eastl::string name;
        bool publicApi = false;
        eastl::vector<eastl::string> dependsOn;
    };

    export class FileSystemScriptSourceProvider final : public IScriptSourceProvider
    {
    public:
        explicit FileSystemScriptSourceProvider(EngineConfig config) : config_(std::move(config)) {}

        eastl::vector<eastl::string> GetAvailableMods() const override
        {
            eastl::vector<ModMetadata> modsMeta;

            if (!fs::exists(config_.scriptsPathMod))
                return {};

            for (const auto& entry : fs::directory_iterator(config_.scriptsPathMod))
            {
                if (entry.is_directory())
                {
                    fs::path mainScriptPath = entry.path() / "main.as";
                    if (fs::exists(mainScriptPath))
                    {
                        ModMetadata meta;
                        meta.name = entry.path().filename().string().c_str();

                        fs::path jsonPath = entry.path() / "mod.json";
                        if (fs::exists(jsonPath))
                        {
                            try
                            {
                                std::ifstream file(jsonPath);
                                nlohmann::json j;
                                file >> j;

                                if (j.contains("public_api") && j["public_api"].is_boolean())
                                {
                                    meta.publicApi = j["public_api"];
                                }
                                if (j.contains("depends_on") && j["depends_on"].is_array())
                                {
                                    for (const auto& dep : j["depends_on"])
                                    {
                                        if (dep.is_string())
                                        {
                                            meta.dependsOn.push_back(dep.get<std::string>().c_str());
                                        }
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::println(stderr, "[ScriptEngine] Error parsing {}: {}", jsonPath.string(),
                                             e.what());
                            }
                        }
                        modsMeta.push_back(meta);
                    }
                }
            }

            return TopologicalSort(modsMeta);
        }

        fs::path GetModPath(const eastl::string& modName) const override
        {
            return config_.scriptsPathMod / modName.c_str();
        }

        eastl::vector<fs::path> GetScriptFiles(const fs::path& rootPath) const override
        {
            eastl::vector<fs::path> scripts;
            if (!fs::exists(rootPath) || !fs::is_directory(rootPath))
                return scripts;

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
        eastl::vector<eastl::string> TopologicalSort(const eastl::vector<ModMetadata>& mods) const
        {
            eastl::vector<eastl::string> result;
            eastl::vector<const ModMetadata*> publicMods;
            eastl::vector<const ModMetadata*> privateMods;

            for (const auto& mod : mods)
            {
                if (mod.publicApi)
                    publicMods.push_back(&mod);
                else
                    privateMods.push_back(&mod);
            }

            // Public API mods go first
            for (const auto* mod : publicMods)
            {
                result.push_back(mod->name);
            }

            // Then sorted dependencies
            // Simple depth-first search for topological sort
            eastl::vector<eastl::string> visited;
            eastl::vector<eastl::string> visiting;

            auto visit = [&](const ModMetadata* mod, auto& visitRef) -> void
            {
                if (eastl::find(visited.begin(), visited.end(), mod->name) != visited.end())
                    return;

                if (eastl::find(visiting.begin(), visiting.end(), mod->name) != visiting.end())
                {
                    std::println(stderr, "[ScriptEngine] Circular dependency detected involving mod: {}",
                                 mod->name.c_str());
                    return;
                }

                visiting.push_back(mod->name);

                for (const auto& depName : mod->dependsOn)
                {
                    auto it = eastl::find_if(mods.begin(), mods.end(),
                                             [&](const ModMetadata& m) { return m.name == depName; });
                    if (it != mods.end())
                    {
                        visitRef(&*it, visitRef);
                    }
                    else
                    {
                        std::println(stderr, "[ScriptEngine] Missing dependency: {} for mod: {}", depName.c_str(),
                                     mod->name.c_str());
                    }
                }

                visiting.erase(eastl::remove(visiting.begin(), visiting.end(), mod->name), visiting.end());
                visited.push_back(mod->name);

                // Only add if it wasn't already added (e.g. public mods)
                if (eastl::find(result.begin(), result.end(), mod->name) == result.end())
                {
                    result.push_back(mod->name);
                }
            };

            for (const auto* mod : privateMods)
            {
                visit(mod, visit);
            }

            return result;
        }

        EngineConfig config_;
    };

    // --- Concrete Observer: Console Logger ---

    export class ConsoleEngineListener final : public IEngineListener
    {
    public:
        void OnEngineInitialized(asIScriptEngine* engine, IBindingManager* bindingManager, IModuleLoader* moduleLoader,
                                 IExecutionManager* executionManager) override
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

        void OnHotReloadStarted(asIScriptEngine* engine) override { std::println("[Listener] Hot Reload Started..."); }

        void OnHotReloadFinished(asIScriptEngine* engine) override { std::println("[Listener] Hot Reload Finished."); }

        void OnAddBinding(asIScriptEngine* engine, IScriptBinding* binding) override
        {
            std::println("[Listener] Add binding.");
        }

        void OnScriptMessage(asIScriptEngine* engine, const asSMessageInfo* msg) override
        {
            const char* type = "INFO";
            if (msg->type == asMSGTYPE_WARNING)
                type = "WARN";
            else if (msg->type == asMSGTYPE_ERROR)
                type = "ERROR";
            std::println("[AS {}] {}:{}:{}: {}", type, msg->section, msg->row, msg->col, msg->message);
        }
    };

    // --- Concrete Factory: Standard Components ---

    export class StandardComponentFactory final : public IEngineComponentFactory
    {
    public:
        explicit StandardComponentFactory(EngineConfig config) : config_(std::move(config)) {}

        eastl::unique_ptr<IModuleLoader> CreateModuleLoader() override
        {
            auto provider = eastl::make_unique<FileSystemScriptSourceProvider>(config_);
            return eastl::make_unique<ModuleLoader>(eastl::move(provider));
        }

        eastl::unique_ptr<IExecutionManager> CreateExecutionManager() override
        {
            return eastl::make_unique<ExecutionManager>(config_.maxScriptExecutionTimeMs, config_.enableWatchdog);
        }

        eastl::unique_ptr<IReloadManager> CreateReloadManager() override { return eastl::make_unique<ReloadManager>(); }

        eastl::unique_ptr<ISaveLoadManager> CreateSaveLoadManager() override
        {
            return eastl::make_unique<SaveLoadManager>();
        }

        eastl::unique_ptr<IBindingManager> CreateBindingManager() override
        {
            return eastl::make_unique<BindingManager>();
        }

        eastl::unique_ptr<IEventManager> CreateEventManager() override { return eastl::make_unique<EventManager>(); }

        eastl::unique_ptr<IScriptWatcher> CreateScriptWatcher() override
        {
            if (config_.enableAutoReload)
            {
                return eastl::make_unique<ScriptWatcher>(config_.scriptsPathMod);
            }
            return nullptr;
        }

        EngineConfig GetEngineConfig() override { return config_; }

    private:
        EngineConfig config_;
    };
} // namespace AngelEngine
