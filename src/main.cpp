#include <filesystem>
#include <print>

import AngelEngine.ScriptEngine;
import AngelEngine.ModuleLoader;

int main() {
    
    auto stdPath = std::filesystem::path("angelscripts/std");
    auto modPath = std::filesystem::path("angelscripts/mods");
    auto moduleLoaderConfig = AngelEngine::ModuleLoaderConfig(stdPath, modPath);
    auto engineConfig = AngelEngine::EngineConfig(moduleLoaderConfig, true, false);
    
    auto result = AngelEngine::ScriptEngine::MakeEngine(engineConfig);
    if (!result.has_value())
    {
        std::println("Fail create engine");
        return 0;
    }
    
    auto engine = std::move(result.value());
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
