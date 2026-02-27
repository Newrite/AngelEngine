#include <EASTL/chrono.h>
#include "EngineFixture.hpp"

import AngelEngine.StickyContext;

using namespace AngelEngineTest;

// ---------------------------------------------------------------------------
// Benchmark: Context pool request / return throughput
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, ContextPoolThroughput)
{
    EngineFixture fixture;
    auto* em = fixture.engine->GetExecutionManager();
    auto* eng = fixture.engine->GetEngine();

    constexpr int kIterations = 10'000;

    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i)
    {
        auto ctx = em->RequestContext(eng, nullptr);
        (void)ctx;
    }

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();

    std::println("[Benchmark] ContextPool: {} iterations in {} µs  ({:.3f} µs/call)", kIterations, totalUs,
                 static_cast<double>(totalUs) / kIterations);

    EXPECT_TRUE(totalUs < 50'000, "Context pool is too slow (> 50ms for 10k iterations)");
}

// ---------------------------------------------------------------------------
// Benchmark: Event dispatch with 4 subscribers over 1000 ticks
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, EventDispatchThroughput)
{
    EngineFixture fixture(false, {"BenchmarkMod"});

    fixture.WriteAndCompile("BenchmarkMod", R"(
        int callCount = 0;

        void main() {}

        void OnCustomEvent(int val, float f) {
            callCount++;
        }

        void SubscribeAll() {
            SubscribeCustomEvent(@OnCustomEvent);
        }
    )");

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "BenchmarkMod failed to run");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    asIScriptFunction* subFn = mod->GetFunctionByDecl("void BenchmarkMod::SubscribeAll()");
    ASSERT_TRUE(subFn != nullptr, "SubscribeAll not found");

    constexpr int kSubscribers = 4;
    for (int s = 0; s < kSubscribers; ++s)
    {
        auto ctxPtr = fixture.engine->GetExecutionManager()->RequestContext(fixture.engine->GetEngine(), nullptr);
        ctxPtr->Prepare(subFn);
        ctxPtr->Execute();
    }

    fixture.ClearOutput();

    constexpr int kTicks = 1000;

    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kTicks; ++i)
    {
        fixture.eventsBinding->PushCustomEvent(i, static_cast<float>(i) * 0.1f);
        fixture.engine->Tick(0.016f);
    }

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();
    double usPerTick = static_cast<double>(totalUs) / kTicks;

    std::println("[Benchmark] EventDispatch: {} ticks × {} subscribers = {} µs total  ({:.2f} µs/tick)", kTicks,
                 kSubscribers, totalUs, usPerTick);

    EXPECT_TRUE(usPerTick < 2000.0, "Event dispatch is too slow (> 2ms per tick)");
}

// ---------------------------------------------------------------------------
// Benchmark: OnTick call — BEFORE StickyContext (baseline)
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, TickCall_Before)
{
    EngineFixture fixture(false, {"TickMod"});

    fixture.WriteAndCompile("TickMod", R"(
        int ticks = 0;
        void main() {}
        void OnTick() { ticks++; }
    )");

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "TickMod failed to run");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    asIScriptFunction* tickFn = mod->GetFunctionByDecl("void TickMod::OnTick()");
    ASSERT_TRUE(tickFn != nullptr, "OnTick() not found");

    auto* em = fixture.engine->GetExecutionManager();
    auto* eng = fixture.engine->GetEngine();

    constexpr int kIterations = 10'000;

    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i)
    {
        auto ctxPtr = em->RequestContext(eng, nullptr); // CAS pop
        ctxPtr->Prepare(tickFn);
        ctxPtr->Execute();
        // RAII returns to pool here — CAS push
    }

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();

    std::println("[Benchmark] TickCall BEFORE StickyCtx: {} calls in {} µs  ({:.3f} µs/call)", kIterations, totalUs,
                 static_cast<double>(totalUs) / kIterations);

    EXPECT_TRUE(totalUs < 500'000, "TickCall baseline is unreasonably slow");
}

// ---------------------------------------------------------------------------
// Benchmark: OnTick call — AFTER StickyContext
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, TickCall_After)
{
    EngineFixture fixture(false, {"TickModSticky"});

    fixture.WriteAndCompile("TickModSticky", R"(
        int ticks = 0;
        void main() {}
        void OnTick() { ticks++; }
    )");

    auto runRes = fixture.engine->RunAllMods();
    ASSERT_TRUE(runRes.has_value(), "TickModSticky failed to run");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    asIScriptFunction* tickFn = mod->GetFunctionByDecl("void TickModSticky::OnTick()");
    ASSERT_TRUE(tickFn != nullptr, "OnTick() not found in TickModSticky");

    auto* em = fixture.engine->GetExecutionManager();
    auto* eng = fixture.engine->GetEngine();

    AngelEngine::StickyContext sticky(em, eng, tickFn);
    ASSERT_TRUE(static_cast<bool>(sticky), "Failed to create StickyContext");

    constexpr int kIterations = 10'000;

    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kIterations; ++i)
    {
        sticky.Execute();
    }

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();

    std::println("[Benchmark] TickCall AFTER  StickyCtx: {} calls in {} µs  ({:.3f} µs/call)", kIterations, totalUs,
                 static_cast<double>(totalUs) / kIterations);

    EXPECT_TRUE(totalUs < 500'000, "TickCall with StickyContext is unreasonably slow");
}

// ---------------------------------------------------------------------------
// Benchmark: Direct Dispatch — overhead floor (no subscribers)
//
// Measures engine->Tick() cost with no OnTick subscribers:
// context pool acquire + DispatchBuiltinEvents (no-op) + return.
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, DirectDispatch_NoSub)
{
    EngineFixture fixture(false, {"DDEmptyMod"});

    fixture.WriteAndCompile("DDEmptyMod", R"(
        void main() {}
    )");
    ASSERT_TRUE(fixture.engine->RunAllMods().has_value(), "DDEmptyMod failed");

    constexpr int kTicks = 10'000;
    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kTicks; ++i)
        fixture.engine->Tick(0.016f);

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();

    std::println("[Benchmark] DirectDispatch (no sub):  {} ticks in {} µs  ({:.3f} µs/tick)", kTicks, totalUs,
                 static_cast<double>(totalUs) / kTicks);

    EXPECT_TRUE(totalUs < 300'000, "Tick overhead (no subscribers) too high");
}

// ---------------------------------------------------------------------------
// Benchmark: Direct Dispatch — 1 OnTick subscriber
//
// Script subscribes to OnTick in main(). Each engine->Tick(dt) calls
// Dispatch(ctx, dt) directly — no Enqueue, no queue swap.
// The delta vs DirectDispatch_NoSub = per-subscriber cost (~0.057 µs).
// ---------------------------------------------------------------------------
TEST_CASE(Benchmark, DirectDispatch_OneSub)
{
    EngineFixture fixture(false, {"DDOneTick"});

    fixture.WriteAndCompile("DDOneTick", R"(
        int ticks = 0;
        void main() {
            ::SubscribeTick(@OnTick);
        }
        void OnTick(float dt) { ticks++; }
    )");
    ASSERT_TRUE(fixture.engine->RunAllMods().has_value(), "DDOneTick failed");

    constexpr int kTicks = 10'000;
    auto start = eastl::chrono::steady_clock::now();

    for (int i = 0; i < kTicks; ++i)
        fixture.engine->Tick(0.016f);

    auto end = eastl::chrono::steady_clock::now();
    auto totalUs = eastl::chrono::duration_cast<eastl::chrono::microseconds>(end - start).count();

    std::println("[Benchmark] DirectDispatch (1 sub):   {} ticks in {} µs  ({:.3f} µs/tick)", kTicks, totalUs,
                 static_cast<double>(totalUs) / kTicks);

    EXPECT_TRUE(totalUs < 500'000, "DirectDispatch_After unreasonably slow");
}
