#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(Events, Dispatching)
{
    EngineFixture fixture;

    fixture.WriteAndCompile("EventTest", R"(
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
    )");

    // Initialize subscriptions
    auto res = fixture.engine->RunMod("EventTest");
    ASSERT_TRUE(res.has_value(), "EventTest failed to run");

    asIScriptModule* eventMod = fixture.engine->GetEngine()->GetModule("EventTest");
    asIScriptFunction* subFunc = eventMod->GetFunctionByDecl("void SubscribeToEvents()");

    {
        // ContextPtr is RAII — it auto-returns the context on scope exit. Do NOT call ReturnContext manually.
        auto ctxPtr = fixture.engine->GetExecutionManager()->RequestContext(fixture.engine->GetEngine(), nullptr);
        asIScriptContext* ctx = ctxPtr.get();
        ctx->Prepare(subFunc);
        ctx->Execute();
    } // ctxPtr destructor returns context to pool here

    fixture.ClearOutput();

    // Direct Dispatch
    fixture.eventsBinding->PushCustomEvent(123, 3.14f);
    fixture.engine->Tick(0.0f); // Should process immediate events

    EXPECT_TRUE(fixture.OutputContains("Event received: 123, 3.14"), "Custom immediate event not received");
    fixture.ClearOutput();

    // Deferred Dispatch
    fixture.eventsBinding->PushDeferredEvent();
    EXPECT_TRUE(!fixture.OutputContains("Deferred Executed"), "Deferred event executed too early");

    fixture.engine->Tick(0.1f); // Resolves deferred events
    EXPECT_TRUE(fixture.OutputContains("Deferred Executed"), "Deferred event not executed after Tick");
}
