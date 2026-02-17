#include <filesystem>
#include <fstream>
#include <print>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#include <angelscript.h>

#include <EASTL/memory.h>
#include <EASTL/unique_ptr.h>

#include "TestContext.hpp"

import AngelEngine.Interfaces;
import AngelEngine.ScriptEngine;
import AngelEngine.Infrastructure;

namespace fs = std::filesystem;
using namespace AngelEngine;

void SetupEnvironment() {
    // Create directories
    fs::create_directories("angelscripts/std");
    fs::create_directories("angelscripts/mods/TestMod");
    fs::create_directories("angelscripts/mods/EventTest");
    fs::create_directories("angelscripts/mods/LoopTest");
    fs::create_directories("angelscripts/mods/AddonTest");

    // Create dummy std lib
    {
        std::ofstream file("angelscripts/std/std.as");
        file << "// Standard Library Dummy";
    }

    // Create TestMod script
    {
        std::ofstream file("angelscripts/mods/TestMod/main.as");
        file << R"(
            [Save] int globalCounter = 0;
            [Save] MockActor@ savedActor;

            funcdef void OnTickCallback(float dt);

            void main() {
                globalCounter = 42;
                @savedActor = GetActor(100);
                // Modify actor state
                if (savedActor !is null) {
                    savedActor.health = 80;
                }
                print("Main executed. Counter: " + globalCounter);
                
                Subscribe("OnTick", @OnTick);
            }

            void OnTick(float dt) {
                globalCounter++;
                if (savedActor !is null) {
                    savedActor.health += 1;
                }
                print("Tick. Counter: " + globalCounter + " ActorHealth: " + savedActor.health);
            }
        )";
    }

    // Create EventTest script
    {
        std::ofstream file("angelscripts/mods/EventTest/main.as");
        file << R"(
            void main() {}

            void OnTestEvent(int val, float f) { 
                print("Event received: " + val + ", " + f); 
            }

            void OnDeferredEvent() { 
                print("Deferred Executed"); 
            }

            void SubscribeToEvents() { 
                Subscribe("CustomEvent", @OnTestEvent); 
                Subscribe("DeferredEvent", @OnDeferredEvent); 
            }
        )";
    }

    // Create LoopTest script (Watchdog test)
    {
        std::ofstream file("angelscripts/mods/LoopTest/main.as");
        file << R"(
            void main() { 
                print("Starting infinite loop..."); 
                while(true) {}
            }
        )";
    }

    // Create AddonTest script
    {
        std::ofstream file("angelscripts/mods/AddonTest/main.as");
        file << R"(
            void main() {
                array<int> arr = {1, 2, 3};
                print("Array size: " + arr.length());
                
                dictionary dict;
                dict["key"] = 42;
                int val;
                dict.get("key", val);
                print("Dict value: " + val);
            }
        )";
    }
}

int main() {
    std::println("Starting E2E Test...");
    SetupEnvironment();

    AngelEngine::ConsoleEngineListener listener;

    // Configuration
    ModuleLoaderConfig config {
        .scriptsPathStd = fs::absolute("angelscripts/std"),
        .scriptsPathMod = fs::absolute("angelscripts/mods")
    };

    // Factory
    auto factory = eastl::make_unique<StandardComponentFactory>(config);

    // Engine
    auto engineResult = ScriptEngine::MakeEngine(std::move(factory));
    if (!engineResult.has_value())
    {
        std::println(stderr, "Failed to create ScriptEngine");
        return 1;
    }
    auto engine = std::move(engineResult.value());
    engine->AddListener(&listener);

    // Bindings
    auto testBinding = eastl::make_unique<TestBinding>();
    // Manually bind because MakeEngine already called BindAll
    engine->AddBinding(testBinding.get());
    
    engine->InitializeEngine();

    // Mock Actor (Initial Health: 50)
    MockActor actor100(100, 50);

    // Register Handler
    auto& saveLoadManager = engine->GetSaveLoadManager();
    auto handler = eastl::make_unique<MockActorHandler>(engine->GetEngine());
    saveLoadManager->AddHandler(handler.get());

    // Compile
    std::println("Compiling mods...");
    auto compileRes = engine->CompileAllMods();
    if (!compileRes.has_value())
    {
        std::println(stderr, "Compilation failed. Check logs.");
        std::exit(1);
    }

    // Run
    std::println("Running mods...");
    auto runRes = engine->RunAllMods();
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }

    // Phase 1: Execution & Modification
    std::println("Phase 1: Execution & Modification");
    
    // IMPORTANT: RunAllMods only schedules main(). We must Tick() to execute it.
    engine->Tick(1.0f/60.0f);

    // Get module and variable pointers
    asIScriptModule* mod = engine->GetEngine()->GetModule("TestMod");
    TEST_ASSERT(mod != nullptr, "Module TestMod not found");
    
    int varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    TEST_ASSERT(varIdx >= 0, "globalCounter not found");
    int* counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);
    
    int actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    TEST_ASSERT(actorIdx >= 0, "savedActor not found");
    MockActor** actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    // Verify initial state (main executed)
    // main sets counter to 42. OnTick increments it.
    // Since we called Tick once, counter should be 43 (42 + 1).
    // main sets actor health to 80. OnTick increments it to 81.
    
    std::println("Counter after first tick: {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 43, "Initial globalCounter should be 43 (42 + 1 tick)");
    TEST_ASSERT(*actorPtr == &actor100, "Initial savedActor should point to actor100");
    TEST_ASSERT(actor100.health == 81, "Actor health should be 81 (50 -> 80 in main -> 81 in OnTick)");

    // Tick 4 more times (total 5 ticks)
    for(int i=0; i<4; ++i) {
        engine->Tick(1.0f/60.0f);
    }

    std::println("Current Counter: {}", *counterPtr);
    // 43 + 4 = 47.
    TEST_ASSERT(*counterPtr == 47, "globalCounter should be 47 (42 + 5 ticks)");
    // Health: 81 + 4 = 85.
    TEST_ASSERT(actor100.health == 85, "Actor health should be 85");

    // Phase 2: Saving
    std::println("Phase 2: Saving");
    eastl::vector<uint8_t> saveBlob;
    bool saveRes = saveLoadManager->GetSaveData(engine->GetEngine(), engine->GetModuleLoader().get(), saveBlob);
    TEST_ASSERT(saveRes, "Save failed");
    TEST_ASSERT(!saveBlob.empty(), "Save blob is empty");
    std::println("Saved {} bytes.", saveBlob.size());

    // Phase 3: Hot Reload
    std::println("Phase 3: Hot Reload");
    auto reloadRes = engine->HotReload();
    TEST_ASSERT(reloadRes.has_value(), "HotReload failed");

    // Verify Reset
    mod = engine->GetEngine()->GetModule("TestMod"); 
    TEST_ASSERT(mod != nullptr, "Module TestMod not found after reload");

    varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);
    
    actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    std::println("Counter after reload (before tick): {}", *counterPtr);
    // Since main() executed during HotReload, it set counter to 42.
    TEST_ASSERT(*counterPtr == 42, "globalCounter should be 42 (reset by main)");
    
    // savedActor is set to GetActor(100) in main(), so it should be valid again.
    TEST_ASSERT(*actorPtr == &actor100, "savedActor should be reset to actor100 by main()");
    
    // Health was reset by main() to 80.
    TEST_ASSERT(actor100.health == 80, "Actor health should be reset to 80 by main()");

    // To properly test restoration, we should modify the state BEFORE loading, 
    // or verify that loading overwrites the current state.
    // Let's modify the state to something else to prove loading works.
    *counterPtr = 999;
    *actorPtr = nullptr;
    actor100.health = 10; // Reset health to verify it loads 85 back

    // Phase 4: Loading
    std::println("Phase 4: Loading");
    bool loadRes = saveLoadManager->LoadFromData(engine->GetEngine(), saveBlob);
    TEST_ASSERT(loadRes, "Load failed");

    // Phase 5: Verification
    std::println("Phase 5: Verification");
    std::println("Counter after load: {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 47, "globalCounter should be restored to 47");
    
    TEST_ASSERT(*actorPtr == &actor100, "savedActor should be restored to actor100");
    TEST_ASSERT(actor100.health == 85, "Actor health should be restored to 85");

    // Tick once more
    engine->Tick(1.0f);
    std::println("Counter after one more tick: {}", *counterPtr);
    
    // main() already ran during HotReload.
    // Loading restored state (47).
    // Tick() runs OnTick().
    // 47 + 1 = 48.
    // Health: 85 + 1 = 86.
    
    TEST_ASSERT(*counterPtr == 48, "globalCounter should be 48 after tick");
    TEST_ASSERT(actor100.health == 86, "Actor health should be 86 after tick");

    // Phase 6: Event System Test
    std::println("Phase 6: Event System Test");
    
    asIScriptModule* eventMod = engine->GetEngine()->GetModule("EventTest");
    TEST_ASSERT(eventMod != nullptr, "Module EventTest not found");

    // Call SubscribeToEvents
    asIScriptFunction* subFunc = eventMod->GetFunctionByDecl("void SubscribeToEvents()");
    TEST_ASSERT(subFunc != nullptr, "Function SubscribeToEvents not found");
    
    // We need a context to run this setup function
    asIScriptContext* ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(subFunc);
    int r = ctx->Execute();
    TEST_ASSERT(r == asEXECUTION_FINISHED, "SubscribeToEvents execution failed");
    ctx->Release();

    // Clear previous output
    testBinding->capturedOutput.clear();

    // Direct Dispatch
    std::println("Testing Direct Dispatch...");
    
    // Use ArgInjector to pass arguments
    engine->GetEventManager()->DispatchDirect(engine->GetEngine(), "CustomEvent", [](asIScriptContext* ctx) {
        ctx->SetArgDWord(0, 123);
        ctx->SetArgFloat(1, 3.14f);
    });

    // Verify Output
    bool foundEvent = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Event received: 123, 3.14") != std::string::npos) {
            foundEvent = true;
            break;
        }
    }
    TEST_ASSERT(foundEvent, "Direct dispatch event not received or output mismatch");
    testBinding->capturedOutput.clear();

    // Deferred Dispatch
    std::println("Testing Deferred Dispatch...");
    engine->GetEventManager()->DispatchDeferred("DeferredEvent");

    // Verify it didn't execute yet
    bool foundDeferred = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != std::string::npos) {
            foundDeferred = true;
            break;
        }
    }
    TEST_ASSERT(!foundDeferred, "Deferred event executed too early");

    // Tick to process deferred events
    engine->Tick(0.1f);

    // Verify it executed now
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != std::string::npos) {
            foundDeferred = true;
            break;
        }
    }
    TEST_ASSERT(foundDeferred, "Deferred event not executed after Tick");

    // Phase 7: Watchdog Test
    std::println("Phase 7: Watchdog Test");
    
    asIScriptModule* loopMod = engine->GetEngine()->GetModule("LoopTest");
    TEST_ASSERT(loopMod != nullptr, "Module LoopTest not found");
    
    asIScriptFunction* loopMain = loopMod->GetFunctionByDecl("void main()");
    TEST_ASSERT(loopMain != nullptr, "LoopTest main not found");

    // Create a context specifically for this test to catch the abort
    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(loopMain);
    
    // We expect this to be aborted by the watchdog (which should be active in the engine)
    // The engine usually sets a line callback or similar.
    // Assuming the engine is configured with a watchdog.
    
    // IMPORTANT: We must set the line callback for this specific context if it's not automatically set by CreateContext.
    // In ScriptEngine::InitializeEngine, we set context callbacks, but let's double check if RequestContextCallback sets the line callback.
    // Looking at ExecutionManager::RequestContext, it DOES set the line callback.
    // However, here we are calling engine->GetEngine()->CreateContext() directly, bypassing ExecutionManager::RequestContext!
    // This is why the watchdog might not be active for this specific context if we don't use the manager.
    // But wait, the engine is configured with SetContextCallbacks, so CreateContext() inside AngelScript might call RequestContextCallback?
    // No, SetContextCallbacks is for when the ENGINE creates a context (e.g. for internal use or maybe when we call CreateContext via the engine interface if it wraps it).
    // Actually, asIScriptEngine::CreateContext() creates a context. The callbacks are used when the engine needs a context (e.g. for ExecuteString or similar helpers, or maybe internal calls).
    // If we manually create a context, we are responsible for setting up the callbacks unless we use a helper from ExecutionManager.
    
    // Let's use the ExecutionManager to request a context to ensure it's set up correctly with the watchdog.
    // But IScriptEngineGetters exposes GetExecutionManager(), so we can use that.
    // However, RequestContext is part of IExecutionManager but it takes (engine, param).
    // Let's just manually set the line callback here to be safe and consistent with the test environment, 
    // OR better, we should fix the test to use the proper way to get a context if possible.
    // Since we don't have easy access to the internal LineCallback function of ExecutionManager from here (it's private/static in ExecutionManager.cpp),
    // we rely on the fact that we should probably use the engine's mechanism or just accept that we need to fix how we run this test.
    
    // Wait, the log shows:
    // [Watchdog] Script aborted! Execution exceeded 1000ms in a single frame.
    // So the watchdog IS working!
    // The problem is "infiniti stack on this" in the user prompt.
    // Ah, the user said "infiniti stack on this" at the end of the output.
    // And the output shows:
    // Executing infinite loop script...
    // [Script] Starting infinite loop...
    //
    // And then it seems to hang or crash?
    // No, the log shows:
    // [Watchdog] Script aborted! ...
    // But then it says "infiniti stack on this" which implies a stack overflow or infinite recursion in the LOGGING or something?
    // Or maybe the user just meant "infinite loop on this" and it's stuck?
    
    // Let's look at the log again.
    // It printed "[Script] Starting infinite loop..."
    // Then nothing after that in the Phase 7 section.
    // The previous "[Watchdog] Script aborted!" was during "Running mods..." at the beginning (Phase 0/Init).
    // Why did it run during Init?
    // Because `engine->RunAllMods()` runs ALL mods, including LoopTest!
    // And LoopTest has a `main()` that runs an infinite loop.
    // So `RunAllMods` executed `LoopTest::main`, which triggered the watchdog, aborted, and then `RunAllMods` continued.
    
    // Then in Phase 7, we manually run it again.
    // `ctx->Execute()` is called.
    // But we created the context using `engine->GetEngine()->CreateContext()`.
    // Does this context have the LineCallback set?
    // As I suspected, `asIScriptEngine::CreateContext()` does NOT automatically set the line callback unless we do it manually or use a wrapper.
    // The `SetContextCallbacks` in `ScriptEngine::InitializeEngine` registers `RequestContextCallback`.
    // AngelScript calls this callback when IT needs a context (e.g. internally).
    // But `engine->CreateContext()` is a direct API call. It returns a new context. It does NOT call the RequestContextCallback.
    // So the context we created in Phase 7 does NOT have the watchdog (LineCallback) configured!
    // That's why it hangs (infinite loop without watchdog).
    
    // Solution:
    // We need to set the line callback on the context we create in Phase 7.
    // But `LineCallback` is a private static method in `ExecutionManager`. We can't access it from `main.cpp`.
    // However, `ExecutionManager` exposes `RequestContext`.
    // We should use `engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr)` to get a context.
    // This method sets up the LineCallback.
    
    // Let's modify main.cpp to use ExecutionManager::RequestContext.
    
    // Also, we should probably prevent LoopTest from running during `RunAllMods` at the start, or just accept it runs and aborts.
    // The log showed it aborted successfully during the initial run, which proves the watchdog works for contexts managed by ExecutionManager (which RunAllMods uses).
    
    // So the fix is to use `RequestContext` in Phase 7.
    
    asIScriptContext* loopCtx = engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr);
    TEST_ASSERT(loopCtx != nullptr, "Failed to request context from ExecutionManager");
    
    loopCtx->Prepare(loopMain);
    
    std::println("Executing infinite loop script...");
    r = loopCtx->Execute();
    
    std::println("Loop execution result: {}", r);
    TEST_ASSERT(r == asEXECUTION_ABORTED, "Infinite loop was not aborted by watchdog");
    
    // Return the context
    engine->GetExecutionManager()->ReturnContext(engine->GetEngine(), loopCtx, nullptr);
    
    // Note: We also used CreateContext() in Phase 6 and 8. 
    // Those are fine because they don't need the watchdog (they are short), but for consistency we could use RequestContext there too.
    // However, the critical one is Phase 7.

    // Phase 8: Standard Addons Test
    std::println("Phase 8: Standard Addons Test");
    testBinding->capturedOutput.clear();
    
    asIScriptModule* addonMod = engine->GetEngine()->GetModule("AddonTest");
    TEST_ASSERT(addonMod != nullptr, "Module AddonTest not found");
    
    asIScriptFunction* addonMain = addonMod->GetFunctionByDecl("void main()");
    TEST_ASSERT(addonMain != nullptr, "AddonTest main not found");
    
    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(addonMain);
    r = ctx->Execute();
    TEST_ASSERT(r == asEXECUTION_FINISHED, "AddonTest execution failed");
    ctx->Release();

    // Verify output
    bool foundArray = false;
    bool foundDict = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Array size: 3") != std::string::npos) foundArray = true;
        if (line.find("Dict value: 42") != std::string::npos) foundDict = true;
    }
    TEST_ASSERT(foundArray, "Array test failed");
    TEST_ASSERT(foundDict, "Dictionary test failed");

    std::println("SUCCESS: All tests passed.");
    return 0;
}
