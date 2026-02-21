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
            void main() {} // ПУСТОЙ MAIN, ЧТОБЫ НЕ ВЕШАТЬ HOT RELOAD

            void RunInfiniteLoop() { 
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
    
    // Create PromiseTest script
    fs::create_directories("angelscripts/mods/PromiseTest");
    {
        std::ofstream file("angelscripts/mods/PromiseTest/main.as");
        file << R"(
            promise<int>@ asyncTask() {
                promise<int>@ p = promise<int>();
                // Эмулируем асинхронную работу (в реальности это был бы C++ таск)
                p.wrap(42); 
                return p;
            }

            void main() {
                print("Promise Start");
                
                // Используем синтаксический сахар co_await!
                int result = co_await asyncTask();
                
                print("Promise End: " + result);
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
            uint64 loopCounter = 0; 
            
            void main() {} // ПУСТОЙ MAIN
            
            void RunPerfLoop(uint64 iterations) {
                for(uint64 i = 0; i < iterations; i++) { 
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
    
    // Скрипт стресс-теста математики (N-Body Simulation)
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

            void main() {} 
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

            void main() {} 
        )";
    }
}

int main() {
    std::println("Starting E2E Test...");
    SetupEnvironment();

    AngelEngine::ConsoleEngineListener listener;

    EngineConfig config {
        .scriptsPathStd = fs::absolute("angelscripts/std"),
        .scriptsPathMod = fs::absolute("angelscripts/mods"),
        .asPredefinedPath = fs::absolute("angelscripts/as.predefined"),
        .enableAutoReload = false,
        .enableWatchdog = true,
        .enableAutoGC = false,
        .enableUseJIT = true, // JIT ВКЛЮЧЕН!
        .maxScriptExecutionTimeMs = 1000
    };

    auto factory = eastl::make_unique<StandardComponentFactory>(config);
    auto engineResult = ScriptEngine::MakeEngine(eastl::move(factory));
    if (!engineResult.has_value())
    {
        std::println(stderr, "Failed to create ScriptEngine");
        return 1;
    }
    auto engine = std::move(engineResult.value());
    engine->AddListener(&listener);

    auto testBinding = eastl::make_unique<TestBinding>();
    auto eventsBinding = eastl::make_unique<AngelEngineTest::TestEventBinding>(engine->GetEventManager());
    auto* eventsBindingPtr = eventsBinding.get();
    
    engine->AddBinding(testBinding.get());
    engine->AddBinding(eventsBinding.get());
    
    engine->InitializeEngine();

    MockActor actor100(100, 50);

    auto saveLoadManager = engine->GetSaveLoadManager();
    auto handler = eastl::make_unique<MockActorHandler>(engine->GetEngine());
    saveLoadManager->AddHandler(handler.get());

    std::println("Compiling mods...");
    auto compileRes = engine->CompileAllMods();
    if (!compileRes.has_value())
    {
        std::println(stderr, "Compilation failed. Check logs.");
        std::exit(1);
    }

    std::println("Phase 1: Execution & Modification");
    auto runRes = engine->RunMod("TestMod");
    if (!runRes.has_value())
    {
        std::println(stderr, "Run failed. Check logs.");
        std::exit(1);
    }

    asIScriptModule* mod = engine->GetEngine()->GetModule("TestMod");
    TEST_ASSERT(mod != nullptr, "Module TestMod not found");
    
    int varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    TEST_ASSERT(varIdx >= 0, "globalCounter not found");
    int* counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);
    
    int actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    TEST_ASSERT(actorIdx >= 0, "savedActor not found");
    MockActor** actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    engine->Tick(1.0f/60.0f);
    
    std::println("Counter after first tick: {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 43, "Initial globalCounter should be 43 (42 + 1 tick)");
    TEST_ASSERT(*actorPtr == &actor100, "Initial savedActor should point to actor100");
    TEST_ASSERT(actor100.health == 81, "Actor health should be 81 (50 -> 80 in main -> 81 in OnTick)");

    for(int i=0; i<4; ++i) {
        engine->Tick(1.0f/60.0f);
    }

    std::println("Current Counter: {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 47, "globalCounter should be 47 (42 + 5 ticks)");
    TEST_ASSERT(actor100.health == 85, "Actor health should be 85");

    std::println("Phase 2: Saving");
    auto saveRes = saveLoadManager->GetSaveData(engine->GetEngine(), engine->GetModuleLoader());
    TEST_ASSERT(saveRes.has_value(), "Save failed");
    eastl::vector<uint8_t> saveBlob = saveRes.value();
    TEST_ASSERT(!saveBlob.empty(), "Save blob is empty");
    std::println("Saved {} bytes.", saveBlob.size());

    std::println("Phase 3: Hot Reload");
    auto reloadRes = engine->HotReload();
    TEST_ASSERT(reloadRes.has_value(), "HotReload failed");

    mod = engine->GetEngine()->GetModule("TestMod"); 
    TEST_ASSERT(mod != nullptr, "Module TestMod not found after reload");

    varIdx = mod->GetGlobalVarIndexByName("globalCounter");
    counterPtr = (int*)mod->GetAddressOfGlobalVar(varIdx);
    
    actorIdx = mod->GetGlobalVarIndexByName("savedActor");
    actorPtr = (MockActor**)mod->GetAddressOfGlobalVar(actorIdx);

    std::println("Counter after reload (before tick): {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 42, "globalCounter should be 42 (reset by main)");
    TEST_ASSERT(*actorPtr == &actor100, "savedActor should be reset to actor100 by main()");
    TEST_ASSERT(actor100.health == 80, "Actor health should be reset to 80 by main()");

    *counterPtr = 999;
    *actorPtr = nullptr;
    actor100.health = 10;

    std::println("Phase 4: Loading");
    auto loadRes = saveLoadManager->LoadFromData(engine->GetEngine(), saveBlob);
    TEST_ASSERT(loadRes.has_value(), "Load failed");

    std::println("Phase 5: Verification");
    std::println("Counter after load: {}", *counterPtr);
    TEST_ASSERT(*counterPtr == 47, "globalCounter should be restored to 47");
    TEST_ASSERT(*actorPtr == &actor100, "savedActor should be restored to actor100");
    TEST_ASSERT(actor100.health == 85, "Actor health should be restored to 85");

    engine->Tick(1.0f);
    std::println("Counter after one more tick: {}", *counterPtr);
    
    TEST_ASSERT(*counterPtr == 48, "globalCounter should be 48 after tick");
    TEST_ASSERT(actor100.health == 86, "Actor health should be 86 after tick");

    std::println("Phase 6: Event System Test");
    runRes = engine->RunMod("EventTest");
    
    asIScriptModule* eventMod = engine->GetEngine()->GetModule("EventTest");
    asIScriptFunction* subFunc = eventMod->GetFunctionByDecl("void SubscribeToEvents()");
    
    asIScriptContext* ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(subFunc);
    int r = ctx->Execute();
    ctx->Release();

    testBinding->capturedOutput.clear();
    std::println("Testing Direct Dispatch...");
    eventsBindingPtr->PushCustomEvent(123, 3.14f);
    engine->Tick(0.0f);

    bool foundEvent = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Event received: 123, 3.14") != eastl::string::npos) foundEvent = true;
    }
    TEST_ASSERT(foundEvent, "Custom event not received or output mismatch");
    testBinding->capturedOutput.clear();

    std::println("Testing Deferred Dispatch...");
    eventsBindingPtr->PushDeferredEvent();

    bool foundDeferred = false;
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != eastl::string::npos) foundDeferred = true;
    }
    TEST_ASSERT(!foundDeferred, "Deferred event executed too early");

    engine->Tick(0.1f);

    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Deferred Executed") != eastl::string::npos) foundDeferred = true;
    }
    TEST_ASSERT(foundDeferred, "Deferred event not executed after Tick");

    // Phase 7: Watchdog Test
    std::println("Phase 7: Watchdog Test");
    
    if (config.enableUseJIT) {
        std::println(">>> Skipping Watchdog test because JIT is enabled (JIT ignores loop line callbacks).");
    } else {
        asIScriptModule* loopMod = engine->GetEngine()->GetModule("LoopTest");
        asIScriptFunction* loopFunc = loopMod->GetFunctionByDecl("void RunInfiniteLoop()");
        TEST_ASSERT(loopFunc != nullptr, "LoopTest RunInfiniteLoop not found");

        asIScriptContext* loopCtx = engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr);
        loopCtx->Prepare(loopFunc);
        
        std::println("Executing infinite loop script...");
        r = engine->GetExecutionManager()->ExecuteManaged(loopCtx).value_or(asEXECUTION_ERROR);
        
        std::println("Loop execution result: {}", r);
        TEST_ASSERT(r == asEXECUTION_ABORTED, "Infinite loop was not aborted by watchdog");
        engine->GetExecutionManager()->ReturnContext(engine->GetEngine(), loopCtx, nullptr);
    }

    std::println("Phase 9: Promise Test");
    testBinding->capturedOutput.clear();
    
    // Запускаем мод. В отличие от sleep(), promise.wrap() работает мгновенно в этом тесте,
    // так что нам не нужно ждать std::this_thread::sleep_for.
    auto promiseRes = engine->RunMod("PromiseTest");
    TEST_ASSERT(promiseRes.has_value(), "PromiseTest failed to run");
    
    bool promiseStart = false;
    bool promiseEnd = false;
    
    for (const auto& line : testBinding->capturedOutput) {
        if (line.find("Promise Start") != eastl::string::npos) promiseStart = true;
        if (line.find("Promise End: 42") != eastl::string::npos) promiseEnd = true;
    }
    
    TEST_ASSERT(promiseStart, "Promise should have started");
    TEST_ASSERT(promiseEnd, "Promise should have ended with value 42");

    std::println("Phase 10: Exception Test");
    
    asIScriptModule* exceptMod = engine->GetEngine()->GetModule("ExceptionTest");
    asIScriptFunction* exceptMain = exceptMod->GetFunctionByDecl("void main()");
    
    asIScriptContext* exceptCtx = engine->GetExecutionManager()->RequestContext(engine->GetEngine(), nullptr);
    exceptCtx->Prepare(exceptMain);
    
    std::println("Executing exception script...");
    r = exceptCtx->Execute();
    
    std::println("Exception execution result: {}", r);
    TEST_ASSERT(r == asEXECUTION_EXCEPTION, "Script should have thrown an exception");
    
    eastl::string exceptionStr = exceptCtx->GetExceptionString();
    std::println("Exception string: {}", exceptionStr.c_str());
    TEST_ASSERT(exceptionStr.find("Divide by zero") != eastl::string::npos, "Exception should be 'Divide by zero'");
    
    engine->GetExecutionManager()->ReturnContext(engine->GetEngine(), exceptCtx, nullptr);
    std::println("Exception test passed");
    
    // ========================================
    // Phase 11: Performance Benchmarks
    // ========================================
    std::println("\n========================================");
    std::println("Phase 11: Performance Benchmarks");
    std::println("========================================");
    
    engine->GetExecutionManager()->Renew();
    engine->GetEventManager()->ClearAll();
    testBinding->capturedOutput.clear();

    // --- Benchmark 1: Raw Bounded Loop Throughput ---
    std::println("Starting Benchmark 1 (Raw Bounded Loop)...");
    asIScriptModule* perfLoopMod = engine->GetEngine()->GetModule("PerfLoopTest");
    asIScriptFunction* runPerfLoop = perfLoopMod->GetFunctionByDecl("void RunPerfLoop(uint64)");
    
    int varLoopIdx = perfLoopMod->GetGlobalVarIndexByName("loopCounter");
    uint64_t* loopCounterPtr = (uint64_t*)perfLoopMod->GetAddressOfGlobalVar(varLoopIdx);
    *loopCounterPtr = 0;

    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(runPerfLoop);
    // Для JIT ставим сумасшедшие 500 МИЛЛИОНОВ итераций.
    // Если тестируешь без JIT, лучше уменьши до 50M, иначе будешь ждать минуту.
    ctx->SetArgQWord(0, 500000000); 

    auto start1 = eastl::chrono::steady_clock::now();
    ctx->Execute();
    auto end1 = eastl::chrono::steady_clock::now();
    
    std::println("\n[RESULT 1] Executed {:L} iterations in: {} ms", *loopCounterPtr, 
        eastl::chrono::duration_cast<eastl::chrono::milliseconds>(end1 - start1).count());
    ctx->Release();

    // --- Benchmark 2: Event Dispatching Throughput ---
    std::println("\nStarting Benchmark 2 (Event Dispatching)... Please wait 1 second.");
    auto perfRunRes = engine->RunMod("PerfTickTest");
    TEST_ASSERT(perfRunRes.has_value(), "PerfTickTest failed to run"); // ДОБАВЛЕН ASSERT
    
    asIScriptModule* perfTickMod = engine->GetEngine()->GetModule("PerfTickTest");
    TEST_ASSERT(perfTickMod != nullptr, "Module PerfTickTest not found"); // На всякий случай проверим и модуль
    
    int varTickIdx = perfTickMod->GetGlobalVarIndexByName("tickCounter");
    uint64_t* tickCounterPtr = (uint64_t*)perfTickMod->GetAddressOfGlobalVar(varTickIdx);
    *tickCounterPtr = 0;

    auto startTime = eastl::chrono::steady_clock::now();
    uint64_t cppTicks = 0;
    auto second = eastl::chrono::seconds(1);

    while (eastl::chrono::steady_clock::now() - startTime < second) {
        engine->Tick(0.016f);
        cppTicks++;
    }

    std::println("\n[RESULT 2] C++ Ticks executed in 1 second:  {:L}", cppTicks);
    std::println("[RESULT 2] Script OnTick handled in 1 second: {:L}", *tickCounterPtr);
    TEST_ASSERT(*tickCounterPtr > 0, "Tick performance test failed");
    TEST_ASSERT(*tickCounterPtr == cppTicks, "Tick drift detected!");
    
    // --- BENCHMARK 3: FPU STRESS (N-Body) ---
    std::println("\nStarting Benchmark 3 (N-Body FPU Stress)...");
    asIScriptModule* mathMod = engine->GetEngine()->GetModule("MathStressTest");
    asIScriptFunction* nbodyFunc = mathMod->GetFunctionByDecl("void SimulateGravity(int, int)");
    
    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(nbodyFunc);
    ctx->SetArgDWord(0, 100);  // 100 итераций
    ctx->SetArgDWord(1, 1000); // 1000 частиц 
    
    auto mathStart = eastl::chrono::steady_clock::now();
    ctx->Execute();
    auto mathEnd = eastl::chrono::steady_clock::now();
    
    std::println("[RESULT 3] N-Body Math executed in: {} ms", 
        eastl::chrono::duration_cast<eastl::chrono::milliseconds>(mathEnd - mathStart).count());
    ctx->Release();

    // --- BENCHMARK 4: BRANCHING STRESS (QuickSort) ---
    std::println("\nStarting Benchmark 4 (QuickSort Branching Stress)...");
    auto sortRunRes = engine->RunMod("JumpStressTest");
    TEST_ASSERT(sortRunRes.has_value(), "JumpStressTest failed to run");
    
    asIScriptModule* jumpMod = engine->GetEngine()->GetModule("JumpStressTest");
    asIScriptFunction* sortFunc = jumpMod->GetFunctionByDecl("void RunSort(int)");
    
    ctx = engine->GetEngine()->CreateContext();
    ctx->Prepare(sortFunc);
    ctx->SetArgDWord(0, 1000000); // 1 миллион элементов
    
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