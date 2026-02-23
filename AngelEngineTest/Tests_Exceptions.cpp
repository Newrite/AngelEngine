#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(Exceptions, DivideByZero)
{
    EngineFixture fixture;

    fixture.WriteAndCompile("ExceptionTest", R"(
        void main() {
            print("About to crash...");
            int a = 0;
            int b = 10 / a; // Divide by zero
        }
    )");

    asIScriptModule* exceptMod = fixture.engine->GetEngine()->GetModule("__Megamodule__");
    asIScriptFunction* exceptMain = exceptMod->GetFunctionByDecl("void ExceptionTest::main()");
    ASSERT_TRUE(exceptMain != nullptr, "ExceptionTest main() not found");

    auto ctxPtr = fixture.engine->GetExecutionManager()->RequestContext(fixture.engine->GetEngine(), nullptr);
    asIScriptContext* exceptCtx = ctxPtr.get();
    exceptCtx->Prepare(exceptMain);

    int r = exceptCtx->Execute();

    EXPECT_EQ(r, asEXECUTION_EXCEPTION, "Script should have thrown an exception");

    eastl::string exceptionStr = exceptCtx->GetExceptionString();
    EXPECT_TRUE(exceptionStr.find("Divide by zero") != eastl::string::npos, "Exception string mismatch");

    // ContextPtr is RAII — auto-returns when it goes out of scope.
    // Do NOT call ReturnContext manually.
}
