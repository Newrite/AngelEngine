#include <filesystem>
#include <print>
#include <memory>

#include <EASTL/unique_ptr.h>

import AngelEngine.ScriptEngine;
import AngelEngine.ModuleLoader;
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;

int main() {
    
    auto stdPath = std::filesystem::path("AngelEngine/scripts");
    auto modPath = std::filesystem::path("angelscripts/mods");
    auto moduleLoaderConfig = AngelEngine::ModuleLoaderConfig(stdPath, modPath);
    
    // Create the Factory with configuration
    auto factory = eastl::make_unique<AngelEngine::StandardComponentFactory>(moduleLoaderConfig);

    // Create Engine using the Factory
    auto result = AngelEngine::ScriptEngine::MakeEngine(eastl::move(factory), true, false);
    
    if (!result.has_value())
    {
        std::println("Fail create engine");
        return 0;
    }
    
    auto engine = eastl::move(result.value());

    // Create and Register Listener (Observer)
    AngelEngine::ConsoleEngineListener listener;
    engine->AddListener(&listener);

    if (!engine->CompileAllMods())
    {
        std::println("Fail compile");
    }
    if (!engine->RunAllMods())
    {
        std::println("Fail run");
    }
    if (!engine->HotReload())
    {
        std::println("Fail hot reload");
    }
    
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);

    return 0;
}
