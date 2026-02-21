#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <thread>
#include <chrono>

#include <angelscript.h>
#include <EASTL/chrono.h>

#include <EASTL/memory.h>
#include <EASTL/unique_ptr.h>

#include "TestContext.hpp"

import AngelEngine.Interfaces;
import AngelEngine.ScriptEngine;
import AngelEngine.Infrastructure;
import AngelEngine.EventsBinding;
import AngelEngineTest.EventsBinding;

namespace fs = std::filesystem;
using namespace AngelEngine;

void SetupEnvironment() {
    // Create directories
    fs::create_directories("angelscripts/std");
    fs::create_directories("angelscripts/mods/TestMod");
    fs::create_directories("angelscripts/mods/EventTest");
    fs::create_directories("angelscripts/mods/LoopTest");
    fs::create_directories("angelscripts/mods/AddonTest");
    fs::create_directories("angelscripts/mods/CoroutineTest");
    fs::create_directories("angelscripts/mods/ExceptionTest");

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
                
                SubscribeTick(@OnTick);
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
                SubscribeCustomEvent(@OnTestEvent); 
                SubscribeDeferredEvent(@OnDeferredEvent); 
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

    // Create CoroutineTest script
    {
        std::ofstream file("angelscripts/mods/CoroutineTest/main.as");
        file << R"(
            void main() {
                print("Routine Start");
                sleep(200); // Sleep 200ms
                print("Routine End");
            }
        )";
    }

    // Create ExceptionTest script
    {
        std::ofstream file("angelscripts/mods/ExceptionTest/main.as");
        file << R"(
            void main() {
                print("About to crash...");
                int a = 0;
                int b = 10 / a; // Divide by zero
            }
        )";
    }
    
    // --- ПЕРФОРМАНС ТЕСТЫ ---

    fs::create_directories("angelscripts/mods/PerfLoopTest");
    {
        std::ofstream file("angelscripts/mods/PerfLoopTest/main.as");
        file << R"(
            uint64 loopCounter = 0; // Используем 64 бита, чтобы избежать переполнения
            
            void main() {
                // Наш Watchdog убьет этот цикл ровно через 1 секунду (1000мс)
                while(true) { 
                    loopCounter++; 
                }
            }
        )";
    }

    fs::create_directories("angelscripts/mods/PerfTickTest");
    {
        std::ofstream file("angelscripts/mods/PerfTickTest/main.as");
        file << R"(
            uint64 tickCounter = 0;
            
            void main() {
                SubscribeTick(@OnTick);
            }

            void OnTick(float dt) {
                tickCounter++;
            }
        )";
    }
    
// Скрипт стресс-теста математики (N-Body Simulation - Data Oriented)
    fs::create_directories("angelscripts/mods/MathStressTest");
    {
        std::ofstream file("angelscripts/mods/MathStressTest/main.as", std::ios::trunc);
        file << R"(
            void SimulateGravity(int iterations, int particleCount) {
                array<double> posX(particleCount), posY(particleCount), posZ(particleCount);
                array<double> velX(particleCount), velY(particleCount), velZ(particleCount);
                array<double> mass(particleCount);
                
                for (int i = 0; i < particleCount; i++) {
                    posX[i] = double(i) * 0.1; posY[i] = double(i) * 0.2; posZ[i] = double(i) * 0.3;
                    velX[i] = 0.0; velY[i] = 0.0; velZ[i] = 0.0;
                    mass[i] = 10.0;
                }

                for (int iter = 0; iter < iterations; iter++) {
                    for (int i = 0; i < particleCount; i++) {
                        double px = posX[i]; double py = posY[i]; double pz = posZ[i];
                        double vx = velX[i]; double vy = velY[i]; double vz = velZ[i];
                        double m1 = mass[i];

                        for (int j = 0; j < particleCount; j++) {
                            if (i == j) continue;
                            
                            double dx = posX[j] - px;
                            double dy = posY[j] - py;
                            double dz = posZ[j] - pz;
                            
                            double distSq = dx*dx + dy*dy + dz*dz;
                            double invDist = 1.0 / (distSq + 0.001); 
                            double force = (m1 * mass[j]) * (invDist * invDist);
                            
                            vx += dx * force;
                            vy += dy * force;
                            vz += dz * force;
                        }
                        velX[i] = vx; velY[i] = vy; velZ[i] = vz;
                    }
                }
            }

            void main() {} // <--- ТОЧКА ВХОДА ДЛЯ RUNMOD
        )";
    }
    
    // Скрипт стресс-теста памяти и прыжков (QuickSort)
    fs::create_directories("angelscripts/mods/JumpStressTest");
    {
        std::ofstream file("angelscripts/mods/JumpStressTest/main.as", std::ios::trunc);
        file << R"(
            void QuickSort(array<int>@ arr, int left, int right) {
                int i = left, j = right;
                int pivot = arr[(left + right) / 2];

                while (i <= j) {
                    while (arr[i] < pivot) i++;
                    while (arr[j] > pivot) j--;
                    if (i <= j) {
                        int tmp = arr[i];
                        arr[i] = arr[j];
                        arr[j] = tmp;
                        i++;
                        j--;
                    }
                }

                if (left < j) QuickSort(arr, left, j);
                if (i < right) QuickSort(arr, i, right);
            }

            void RunSort(int size) {
                array<int> data(size);
                int seed = 1337;
                for (int i = 0; i < size; i++) {
                    seed = (seed * 214013 + 2531011);
                    data[i] = (seed >> 16) & 0x7FFF;
                }
                QuickSort(data, 0, size - 1);
            }

            void main() {} // <--- ТОЧКА ВХОДА ДЛЯ RUNMOD
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
    auto engineResult = ScriptEngine::MakeEngine(std::move(factory), false);
    if (!engineResult.has_value())
    {
        std::println(stderr, "Failed to create ScriptEngine");
        return 1;
    }
    auto engine = std::move(engineResult.value());
    engine->AddListener(&listener);

    // Bindings
    auto testBinding = eastl::make_unique<TestBinding>();
    // We need to keep a pointer to the test event binding to push events
    auto eventsBinding = eastl::make_unique<AngelEngineTest::TestEventBinding>(engine->GetEventManager());
    auto* eventsBindingPtr = eventsBinding.get();
    
    // Manually bind because MakeEngine already called BindAll
    engine->AddBinding(testBinding.get());
    engine->AddBinding(eventsBinding.get());
    
    engine->InitializeEngine();

    // Mock Actor (Initial Health: 50)
    MockActor actor100(100, 50);

    // Register Handler
    auto saveLoadManager = engine->GetSaveLoadManager();
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
    // std::println("Running mods...");
    // auto runRes = engine->RunAllMods();
    // if (!runRes.has_value())
    // {
    //     std::println(stderr, "Run failed. Check logs.");
    //     std::exit(1);
    // }

    // Phase 1: Execution & Modification
    std::println("Phase 1: Execution & Modification");
    
    auto runRes = engine->RunMod("TestMod");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }

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
    
    engine->Tick(1.0f/60.0f);
    
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
    bool saveRes = saveLoadManager->GetSaveData(engine->GetEngine(), engine->GetModuleLoader(), saveBlob);
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
    
    runRes = engine->RunMod("EventTest");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }
    
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
    
    // Use the binding helper to push events directly to the channel
    eventsBindingPtr->PushCustomEvent(123, 3.14f);
    
    // We need to process deferred events to see the result, as all events are now deferred/channel-based
    // But wait, the test expects "Direct Dispatch" to be immediate?
    // The new architecture makes everything deferred via channels for performance.
    // So we must Tick to process it.
    
    // However, the previous test logic assumed DispatchDirect was immediate.
    // If we want immediate execution, we would need to manually process the channel.
    // But let's stick to the engine flow: Push -> Tick.
    
    // Tick to process the event
    engine->Tick(0.0f);

    // Verify Output
    bool foundEvent = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Event received: 123, 3.14") != eastl::string::npos) {
            foundEvent = true;
            break;
        }
    }
    TEST_ASSERT(foundEvent, "Custom event not received or output mismatch");
    testBinding->capturedOutput.clear();

    // Deferred Dispatch
    std::println("Testing Deferred Dispatch...");
    eventsBindingPtr->PushDeferredEvent();

    // Verify it didn't execute yet (before Tick)
    // Note: In the new architecture, Push enqueues to the channel.
    // Execution happens in Tick.
    // So checking before Tick is correct.
    bool foundDeferred = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != eastl::string::npos) {
            foundDeferred = true;
            break;
        }
    }
    TEST_ASSERT(!foundDeferred, "Deferred event executed too early");

    // Tick to process deferred events
    engine->Tick(0.1f);

    // Verify it executed now
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != eastl::string::npos) {
            foundDeferred = true;
            break;
        }
    }
    TEST_ASSERT(foundDeferred, "Deferred event not executed after Tick");

    // Phase 7: Watchdog Test
    std::println("Phase 7: Watchdog Test");
    
    runRes = engine->RunMod("LoopTest");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }
    
    asIScriptModule* loopMod = engine->GetEngine()->GetModule("LoopTest");
    TEST_ASSERT(loopMod != nullptr, "Module LoopTest not found");
    
    asIScriptFunction* loopMain = loopMod->GetFunctionByDecl("void main()");
    TEST_ASSERT(loopMain != nullptr, "LoopTest main not found");

    // Create a context specifically for this test to catch the abort
    // Use RequestContext to ensure Watchdog (LineCallback) is set up
    asIScriptContext* loopCtx = engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr);
    TEST_ASSERT(loopCtx != nullptr, "Failed to request context from ExecutionManager");
    
    loopCtx->Prepare(loopMain);
    
    std::println("Executing infinite loop script...");
    r = engine->GetExecutionManager()->ExecuteManaged(loopCtx);
    
    std::println("Loop execution result: {}", r);
    TEST_ASSERT(r == asEXECUTION_ABORTED, "Infinite loop was not aborted by watchdog");
    
    // Return the context
    engine->GetExecutionManager()->ReturnContext(engine->GetEngine(), loopCtx, nullptr);

    // Phase 8: Standard Addons Test
    std::println("Phase 8: Standard Addons Test");
    testBinding->capturedOutput.clear();
    
    runRes = engine->RunMod("AddonTest");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }
    
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
        if (line.find("Array size: 3") != eastl::string::npos) foundArray = true;
        if (line.find("Dict value: 42") != eastl::string::npos) foundDict = true;
    }
    TEST_ASSERT(foundArray, "Array test failed");
    TEST_ASSERT(foundDict, "Dictionary test failed");

    // Phase 9: Coroutine Test
    std::println("Phase 9: Coroutine Test");
    testBinding->capturedOutput.clear();

    // We need to run CoroutineTest via ExecutionManager so it handles sleep/yield
    // But RunAllMods runs everything. We want to target CoroutineTest.
    // ExecutionManager doesn't expose StartModContext directly (it's private).
    // However, we can manually add a context to ContextMgr if we had access to it, but we don't.
    // Wait, RunAllMods runs all mods that are loaded.
    // We can just rely on the fact that we can manually create a context and use the ContextMgr if exposed,
    // OR we can use the fact that `RunAllMods` iterates over loaded modules.
    // But we want to test the sleep functionality which requires the ContextMgr to manage the context.
    // The `ExecutionManager` manages contexts.
    // If we look at `ExecutionManager::RunAllMods`, it calls `StartModContext` for each mod.
    // `StartModContext` adds the context to `contextMgr_`.
    // So if we call `RunAllMods`, it will start `CoroutineTest`.
    // But it will also restart `TestMod`, `EventTest`, `LoopTest`, `AddonTest`...
    // That might be noisy but acceptable if we filter output.
    // Alternatively, we can unload other modules? No, IModuleLoader interface doesn't show Unload.
    
    // Let's try to use `RunAllMods` but we need to be careful about side effects.
    // Actually, `RunAllMods` returns `eastl::expected<void, ExecutionError>`.
    // It starts the contexts.
    // Then we call `Tick`.
    
    // We only care about CoroutineTest output.
    // "Routine Start" -> sleep -> "Routine End"
    
    // We can't easily isolate just one mod with the current `RunAllMods` API.
    // However, for this test, maybe we can just run everything and check the logs.
    // But `LoopTest` will abort again, which is annoying.
    // `TestMod` will print stuff.
    
    // Ideally we would have `engine->GetExecutionManager()->ExecuteMod("CoroutineTest")`.
    // Since we don't, let's just manually create a context and register it with the ContextMgr?
    // `ExecutionManager` has `RequestContext` but that just gives a context, it doesn't add it to the `CContextMgr` for scheduling.
    // `CContextMgr` is internal to `ExecutionManager`.
    
    // Wait, `ExecutionManager` has `Tick`.
    // If we can somehow inject the context into the manager...
    // `IExecutionManager` interface:
    // virtual void Tick(...)
    // virtual eastl::expected<void, ExecutionError> RunAllMods(...)
    
    // It seems we are forced to use `RunAllMods` to use the `CContextMgr` features (like sleep) unless we modify the engine API.
    // But wait, `LoopTest` aborting is fine, it just prints an error.
    // `TestMod` is fine.
    
    // Let's use `RunAllMods`.
    
    runRes = engine->RunMod("CoroutineTest");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }
    
    // Step 1: Tick(0.0f) - Should execute "Routine Start" and then sleep.
    engine->Tick(0.0f);
    
    bool routineStart = false;
    bool routineEnd = false;
    
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Routine Start") != eastl::string::npos) routineStart = true;
        if (line.find("Routine End") != eastl::string::npos) routineEnd = true;
    }
    
    TEST_ASSERT(routineStart, "Coroutine should have started");
    TEST_ASSERT(!routineEnd, "Coroutine should be sleeping, not ended");
    
    // Step 2: Simulate waiting 100ms.
    // We need to advance time. The `Tick` takes `deltaTime`.
    // But `CContextMgr` uses `GetSystemTimeAsUInt` callback which uses `steady_clock`.
    // So we actually need to sleep the thread or mock the time.
    // The `ExecutionManager` implementation uses `GetSystemTimeMs` from `std::chrono::steady_clock`.
    // So passing `deltaTime` to `Tick` might not affect `sleep()` if `sleep()` relies on system time.
    // Let's check `ExecutionManager.cpp`.
    // `contextMgr_->SetGetTimeCallback(GetSystemTimeAsUInt);`
    // So yes, it uses real time.
    
    std::println("Sleeping 100ms...");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine->Tick(0.0f); // Delta time doesn't matter for sleep() if it uses system time
    
    routineEnd = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Routine End") != eastl::string::npos) routineEnd = true;
    }
    TEST_ASSERT(!routineEnd, "Coroutine should still be sleeping (100ms < 200ms)");
    
    // Step 3: Simulate waiting enough time to pass the 1500ms mark.
    // RunAllMods ate ~1000ms. Step 2 ate 100ms. We are at ~1100ms.
    // We need to wait at least 400ms more. Let's wait 500ms to be safe.
    std::println("Sleeping 500ms...");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    engine->Tick(0.0f);

    routineEnd = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Routine End") != eastl::string::npos) routineEnd = true;
    }
    TEST_ASSERT(routineEnd, "Coroutine should have ended");

    // Phase 10: Exception Test
    std::println("Phase 10: Exception Test");
    
    runRes = engine->RunMod("ExceptionTest");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }
    
    asIScriptModule* exceptMod = engine->GetEngine()->GetModule("ExceptionTest");
    TEST_ASSERT(exceptMod != nullptr, "Module ExceptionTest not found");
    
    asIScriptFunction* exceptMain = exceptMod->GetFunctionByDecl("void main()");
    TEST_ASSERT(exceptMain != nullptr, "ExceptionTest main not found");
    
    // Use RequestContext to ensure we have exception callback set up (if RequestContext sets it)
    // ExecutionManager::RequestContext sets LineCallback.
    // ExecutionManager::StartModContext sets ExceptionCallback.
    // ExecutionManager::Tick sets ExceptionCallback for events.
    // But RequestContext does NOT set ExceptionCallback in the current implementation of ExecutionManager.cpp!
    // It only sets LineCallback.
    // So we need to manually set the exception callback or rely on the default one if any?
    // The engine doesn't have a default exception callback for contexts unless set.
    // However, `ScriptEngine::InitializeEngine` sets `SetContextCallbacks` which might handle it?
    // No, that's for context creation/return.
    
    // We should manually set the exception callback or just check the return value.
    // `ctx->Execute()` returns `asEXECUTION_EXCEPTION`.
    // And we can check `ctx->GetExceptionString()`.
    
    asIScriptContext* exceptCtx = engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr);
    TEST_ASSERT(exceptCtx != nullptr, "Failed to request context");
    
    exceptCtx->Prepare(exceptMain);
    
    std::println("Executing exception script...");
    r = exceptCtx->Execute();
    
    std::println("Exception execution result: {}", r);
    TEST_ASSERT(r == asEXECUTION_EXCEPTION, "Script should have thrown an exception");
    
    eastl::string exceptionStr = exceptCtx->GetExceptionString();
    std::println("Exception string: {}", exceptionStr);
    TEST_ASSERT(exceptionStr.find("Divide by zero") != eastl::string::npos, "Exception should be 'Divide by zero'");
    
    engine->GetExecutionManager()->ReturnContext(engine->GetEngine(), exceptCtx, nullptr);
    
    std::println("Exception test passed");
    
    // Phase 11: Performance Benchmarks
    std::println("\n========================================");
    std::println("Phase 11: Performance Benchmarks");
    std::println("========================================");
    
    // --- ОЧИСТКА СОСТОЯНИЯ ---
    // Убиваем все висящие контексты и сбрасываем Watchdog
    engine->GetExecutionManager()->Renew();
    // Отписываем ВСЕ старые моды (TestMod и т.д.) от эвентов, чтобы они не спамили в консоль
    engine->GetEventManager()->ClearAll();
    
    // Очищаем буфер вывода на всякий случай
    testBinding->capturedOutput.clear();

    // --- Benchmark 1: Raw Loop Throughput ---
    std::println("Starting Benchmark 1 (Raw While Loop)... Please wait 1 second.");
    
    // Мы ожидаем, что этот мод упадет по Watchdog, поэтому не проверяем has_value()
    engine->RunMod("PerfLoopTest");

    asIScriptModule* perfLoopMod = engine->GetEngine()->GetModule("PerfLoopTest");
    TEST_ASSERT(perfLoopMod != nullptr, "PerfLoopTest module not found");
    
    int varLoopIdx = perfLoopMod->GetGlobalVarIndexByName("loopCounter");
    uint64_t* loopCounterPtr = (uint64_t*)perfLoopMod->GetAddressOfGlobalVar(varLoopIdx);
    
    // Печатаем результат с разделителями разрядов для красоты (C++23)
    std::println("\n[RESULT 1] Raw loop iterations in 1 second: {:L}", *loopCounterPtr);


    // --- Benchmark 2: Event Dispatching Throughput ---
    std::println("\nStarting Benchmark 2 (Event Dispatching)... Please wait 1 second.");
    
    auto perfRunRes = engine->RunMod("PerfTickTest");
    TEST_ASSERT(perfRunRes.has_value(), "PerfTickTest failed to start");

    asIScriptModule* perfTickMod = engine->GetEngine()->GetModule("PerfTickTest");
    TEST_ASSERT(perfTickMod != nullptr, "PerfTickTest module not found");
    
    int varTickIdx = perfTickMod->GetGlobalVarIndexByName("tickCounter");
    uint64_t* tickCounterPtr = (uint64_t*)perfTickMod->GetAddressOfGlobalVar(varTickIdx);
    
    // Сбрасываем старые тики, накопленные за время предыдущих фаз теста
    *tickCounterPtr = 0;

    // Насилуем движок вызовами Tick() ровно 1 секунду
    auto startTime = eastl::chrono::steady_clock::now();
    uint64_t cppTicks = 0;
    auto second = eastl::chrono::seconds(1);

    while (eastl::chrono::steady_clock::now() - startTime < second)
    {
        engine->Tick(0.016f);
        cppTicks++;
    }

    std::println("\n[RESULT 2] C++ Ticks executed in 1 second:  {:L}", cppTicks);
    std::println("[RESULT 2] Script OnTick handled in 1 second: {:L}", *tickCounterPtr);
    
    TEST_ASSERT(*tickCounterPtr > 0, "Tick performance test failed (0 ticks handled)");
    TEST_ASSERT(*tickCounterPtr == cppTicks, "Tick drift detected! Handled events do not match dispatched events.");
    
    // --- BENCHMARK 3: FPU STRESS (N-Body) ---
    std::println("\nStarting Benchmark 3 (N-Body FPU Stress)...");
    auto res3 = engine->RunMod("MathStressTest");
    TEST_ASSERT(res3.has_value(), "MathStressTest failed to compile or run!");

    asIScriptModule* mathMod = engine->GetEngine()->GetModule("MathStressTest");
    TEST_ASSERT(mathMod != nullptr, "MathStressTest module is null!");

    asIScriptFunction* nbodyFunc = mathMod->GetFunctionByDecl("void SimulateGravity(int, int)");
    TEST_ASSERT(nbodyFunc != nullptr, "Function SimulateGravity not found!");
    
    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(nbodyFunc);
    ctx->SetArgDWord(0, 100);  // 100 итераций
    ctx->SetArgDWord(1, 1000); // 1000 частиц (100 миллионов циклов математики)
    
    auto mathStart = eastl::chrono::steady_clock::now();
    ctx->Execute();
    auto mathEnd = eastl::chrono::steady_clock::now();
    
    std::println("[RESULT 3] N-Body Math executed in: {} ms", 
        eastl::chrono::duration_cast<eastl::chrono::milliseconds>(mathEnd - mathStart).count());

    // --- BENCHMARK 4: BRANCHING STRESS (QuickSort) ---
    std::println("\nStarting Benchmark 4 (QuickSort Branching Stress)...");
    engine->RunMod("JumpStressTest");
    asIScriptModule* jumpMod = engine->GetEngine()->GetModule("JumpStressTest");
    asIScriptFunction* sortFunc = jumpMod->GetFunctionByDecl("void RunSort(int)");
    
    ctx->Prepare(sortFunc);
    ctx->SetArgDWord(0, 1000000); // Сортируем массив из 1 миллиона элементов
    
    auto sortStart = eastl::chrono::steady_clock::now();
    ctx->Execute();
    auto sortEnd = eastl::chrono::steady_clock::now();
    
    std::println("[RESULT 4] 1 Million items QuickSort executed in: {} ms", 
        eastl::chrono::duration_cast<eastl::chrono::milliseconds>(sortEnd - sortStart).count());
    
    ctx->Release();
    
    std::println("========================================\n");

    std::println("SUCCESS: All tests passed.");
    return 0;
}