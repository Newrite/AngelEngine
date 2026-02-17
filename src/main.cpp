#include <angelscript.h>
#include <filesystem>
#include <print>
#include <asbind20/asbind.hpp>

#include <EASTL/unique_ptr.h>

import AngelEngine.ScriptEngine;
import AngelEngine.ModuleLoader;
import AngelEngine.Interfaces;
import AngelEngine.Infrastructure;

struct PrintBinding final : AngelEngine::IScriptBinding
{
    // Function for script output (replaces cout)
    static void scriptPrint(const std::string& msg)
    {
        // Здесь используется std::println из C++23, который пишет в консоль
        std::println("[Script]: {}", msg);
    }
        
    void Bind(asIScriptEngine* engine) override
    {
        // Регистрация функции "void print(const string &in msg)"
        asbind20::global(engine)
            .function("void print(const string &in msg)", &scriptPrint);
    }
} printBinding;

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
    
    // engine->AddBinding(&printBinding);
    engine->GetBindingManager()->Bind(engine->GetEngine(), &printBinding);
    // engine->InitializeEngine();

    if (!engine->CompileAllMods())
    {
        std::println("Fail compile");
    }
    if (!engine->RunAllMods())
    {
        std::println("Fail run");
    }
    
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    
    // if (!engine->HotReload())
    // {
    //     std::println("Fail hot reload");
    // }
    
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);
    engine->Tick(0.2);

    return 0;
}
