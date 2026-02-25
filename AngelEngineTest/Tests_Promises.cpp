#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(Promises, AsyncCoAwait)
{
    EngineFixture fixture(false, {"PromiseTest"});

    fixture.WriteAndCompile("PromiseTest", R"(
        promise<int>@ asyncTask() {
            promise<int>@ p = promise<int>();
            p.wrap(42); 
            return p;
        }

        void main() {
            print("Promise Start");
            int result = co_await asyncTask();
            print("Promise End: " + result);
        }
    )");

    auto res = fixture.engine->RunAllMods();
    ASSERT_TRUE(res.has_value(), "PromiseTest failed to run");

    EXPECT_TRUE(fixture.OutputContains("Promise Start"), "Promise should have started");
    EXPECT_TRUE(fixture.OutputContains("Promise End: 42"), "Promise should have ended with value 42");
}
