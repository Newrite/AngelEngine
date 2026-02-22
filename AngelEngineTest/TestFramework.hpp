#pragma once

#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <vector>


namespace AngelEngineTest
{
    // ANSI color codes
    constexpr const char* ColorReset = "\033[0m";
    constexpr const char* ColorGreen = "\033[32m";
    constexpr const char* ColorRed = "\033[31m";
    constexpr const char* ColorCyan = "\033[36m";

    struct TestInfo
    {
        std::string suiteName;
        std::string testName;
        std::function<void()> run;
    };

    class TestRegistry
    {
    public:
        static TestRegistry& Get()
        {
            static TestRegistry instance;
            return instance;
        }

        void RegisterTest(const std::string& suite, const std::string& name, std::function<void()> func)
        {
            tests_.emplace_back(TestInfo{suite, name, std::move(func)});
        }

        int RunAll()
        {
            int passed = 0;
            int failed = 0;

            std::println("{}========================================", ColorCyan);
            std::println(" AngelEngine E2E Test Runner Started");
            std::println("========================================{}", ColorReset);

            for (const auto& test : tests_)
            {
                std::println("\nRunning: {}::{}", test.suiteName, test.testName);

                currentTestFailed_ = false;
                try
                {
                    test.run();
                }
                catch (const std::exception& e)
                {
                    std::println(stderr, "{}FATAL ERROR EXCEPTION: {}{}", ColorRed, e.what(), ColorReset);
                    currentTestFailed_ = true;
                }
                catch (...)
                {
                    std::println(stderr, "{}FATAL ERROR: Unknown Exception Caught!{}", ColorRed, ColorReset);
                    currentTestFailed_ = true;
                }

                if (currentTestFailed_)
                {
                    std::println("{}[FAILED] {}::{}{}", ColorRed, test.suiteName, test.testName, ColorReset);
                    failed++;
                }
                else
                {
                    std::println("{}[PASSED] {}::{}{}", ColorGreen, test.suiteName, test.testName, ColorReset);
                    passed++;
                }
            }

            std::println("\n{}========================================", ColorCyan);
            std::println(" Test Run Completed");
            std::println(" Passed: {}{}{} | Failed: {}{}{}", ColorGreen, passed, ColorCyan, ColorRed, failed,
                         ColorCyan);
            std::println("========================================{}", ColorReset);

            return failed > 0 ? 1 : 0;
        }

        void ReportFailure(const std::string& message, const std::source_location& loc)
        {
            std::println(stderr, "  {}-> ASSERTION FAILED: {}:{} - {}{}", ColorRed, loc.file_name(), loc.line(),
                         message, ColorReset);
            currentTestFailed_ = true;
        }

    private:
        TestRegistry() = default;
        std::vector<TestInfo> tests_;
        bool currentTestFailed_ = false;
    };

    // Auto-registering macro logic
    struct AutoRegistrar
    {
        AutoRegistrar(const char* suite, const char* name, std::function<void()> func)
        {
            TestRegistry::Get().RegisterTest(suite, name, std::move(func));
        }
    };
} // namespace AngelEngineTest

// --- User Macros ---

#define TEST_CASE(suite_name, test_name)                                                                               \
    void TestFunc_##suite_name##_##test_name();                                                                        \
    static AngelEngineTest::AutoRegistrar Registrar_##suite_name##_##test_name(#suite_name, #test_name,                \
                                                                               &TestFunc_##suite_name##_##test_name);  \
    void TestFunc_##suite_name##_##test_name()

#define EXPECT_TRUE(cond, msg)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            AngelEngineTest::TestRegistry::Get().ReportFailure(msg, std::source_location::current());                  \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

#define ASSERT_TRUE(cond, msg)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            AngelEngineTest::TestRegistry::Get().ReportFailure(msg, std::source_location::current());                  \
            throw std::runtime_error("Fatal Assertion Failed");                                                        \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

#define EXPECT_EQ(val1, val2, msg) EXPECT_TRUE((val1) == (val2), msg)
#define ASSERT_EQ(val1, val2, msg) ASSERT_TRUE((val1) == (val2), msg)
