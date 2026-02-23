#pragma once

#include "TestContext.hpp"
#include "TestFramework.hpp"


#include <EASTL/memory.h>
#include <EASTL/unique_ptr.h>
#include <filesystem>
#include <fstream>


import AngelEngine.Interfaces;
import AngelEngine.ScriptEngine;
import AngelEngine.Infrastructure;
import AngelEngine.EventsBinding;
import AngelEngineTest.EventsBinding;

namespace fs = std::filesystem;

namespace AngelEngineTest
{
    class EngineFixture
    {
    public:
        EngineFixture(bool useJIT = false)
        {
            AngelEngine::EngineConfig config{.scriptsPathMod = fs::absolute("angelscripts/mods"),
                                             .asPredefinedPath = fs::absolute("angelscripts/as.predefined"),
                                             .enableAutoReload = false,
                                             .enableWatchdog = true, // Watchdog testing explicitly tested elsewhere
                                             .enableAutoGC = false,
                                             .enableUseJIT = useJIT,
                                             .maxScriptExecutionTimeMs = 1000};

            auto factory = eastl::make_unique<AngelEngine::StandardComponentFactory>(config);
            auto engineResult = AngelEngine::ScriptEngine::MakeEngine(eastl::move(factory));

            ASSERT_TRUE(engineResult.has_value(), "Failed to create ScriptEngine in fixture");

            engine = std::move(engineResult.value());

            // Add bindings
            testBinding = eastl::make_unique<TestBinding>();
            eventsBinding = eastl::make_unique<TestEventBinding>(engine->GetEventManager());

            engine->AddBinding(testBinding.get());
            engine->AddBinding(eventsBinding.get());

            // Initialize
            auto initResult = engine->InitializeEngine();
            ASSERT_TRUE(initResult.has_value(), "Engine initialization failed");

            // Serialization Handlers
            actorHandler = eastl::make_unique<MockActorHandler>(engine->GetEngine());
            engine->GetSaveLoadManager()->AddHandler(actorHandler.get());
        }

        ~EngineFixture()
        {
            MockActor::registry.clear(); // Reset game state

            if (eventsBinding)
            {
                eventsBinding->ClearAllEvents();
            }
        }

        // --- Helper Methods ---
        void CreateScriptFile(const std::string& modName, const std::string& scriptCode, bool publicApi = false,
                              const std::vector<std::string>& dependsOn = {})
        {
            std::string dirPath = "angelscripts/mods/" + modName;
            fs::create_directories(dirPath);
            std::ofstream file(dirPath + "/main.as", std::ios::trunc);
            file << scriptCode;

            std::ofstream jsonFile(dirPath + "/mod.json", std::ios::trunc);
            jsonFile << "{\"public_api\": " << (publicApi ? "true" : "false") << ", \"depends_on\": [";
            for (size_t i = 0; i < dependsOn.size(); ++i)
            {
                jsonFile << "\"" << dependsOn[i] << "\"";
                if (i < dependsOn.size() - 1)
                    jsonFile << ", ";
            }
            jsonFile << "]}";
        }

        void WriteAndCompile(const std::string& modName, const std::string& scriptCode)
        {
            CreateScriptFile(modName, scriptCode);
            auto res = engine->CompileAllMods();
            ASSERT_TRUE(res.has_value(), "Failed to compile scripts");
        }

        eastl::string GetLastOutputLine() const
        {
            if (testBinding->capturedOutput.empty())
                return "";
            return testBinding->capturedOutput.back();
        }

        bool OutputContains(const eastl::string& substring) const
        {
            for (const auto& line : testBinding->capturedOutput)
            {
                if (line.find(substring) != eastl::string::npos)
                    return true;
            }
            return false;
        }

        void ClearOutput() { testBinding->capturedOutput.clear(); }

        // Managed objects
        eastl::unique_ptr<AngelEngine::ScriptEngine> engine;
        eastl::unique_ptr<TestBinding> testBinding;
        eastl::unique_ptr<TestEventBinding> eventsBinding;
        eastl::unique_ptr<MockActorHandler> actorHandler;
    };
} // namespace AngelEngineTest
