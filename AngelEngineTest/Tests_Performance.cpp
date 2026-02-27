#include <EASTL/chrono.h>
#include "EngineFixture.hpp"


using namespace AngelEngineTest;

TEST_CASE(Performance, HeavyMathAndBranching)
{
    // By passing true we ensure AngelSea JIT is enabled which is critical for the benchmarks.
    EngineFixture fixture(true);

    // Simulation Data script
    fixture.WriteAndCompile("MathStressTest", R"(
        void SimulateGravity(int iterations, int particleCount) {
            array<double> posX(particleCount), posY(particleCount), posZ(particleCount);
            array<double> velX(particleCount), velY(particleCount), velZ(particleCount);
            array<double> mass(particleCount);
            
            for (int i = 0; i < particleCount; i++) {
                posX[i] = double(i) * 0.1; posY[i] = double(i) * 0.2; posZ[i] = double(i) * 0.3;
                mass[i] = 10.0;
            }

            for (int iter = 0; iter < iterations; iter++) {
                for (int i = 0; i < particleCount; i++) {
                    double px = posX[i]; double py = posY[i]; double pz = posZ[i];
                    double vx = velX[i]; double vy = velY[i]; double vz = velZ[i];
                    double m1 = mass[i];

                    for (int j = 0; j < particleCount; j++) {
                        if (i == j) continue;
                        double dx = posX[j] - px; double dy = posY[j] - py; double dz = posZ[j] - pz;
                        double distSq = dx*dx + dy*dy + dz*dz;
                        double invDist = 1.0 / (distSq + 0.001); 
                        double force = (m1 * mass[j]) * (invDist * invDist);
                        vx += dx * force; vy += dy * force; vz += dz * force;
                    }
                    velX[i] = vx; velY[i] = vy; velZ[i] = vz;
                }
            }
        }

        void QuickSort(array<int>@ arr, int left, int right) {
            int i = left, j = right;
            int pivot = arr[(left + right) / 2];

            while (i <= j) {
                while (arr[i] < pivot) i++;
                while (arr[j] > pivot) j--;
                if (i <= j) {
                    int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
                    i++; j--;
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
    )");

    asIScriptModule* mathMod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);

    // N-Body Math Stress
    auto ctxPtr = fixture.engine->GetExecutionManager()->RequestContext(fixture.engine->GetEngine(), nullptr);
    asIScriptContext* ctx = ctxPtr.get();

    asIScriptFunction* nbodyFunc = mathMod->GetFunctionByDecl("void MathStressTest::SimulateGravity(int, int)");
    ctx->Prepare(nbodyFunc);
    ctx->SetArgDWord(0, 10); // Iterations (lowered to prevent blocking CI execution for too long)
    ctx->SetArgDWord(1, 100); // Particles

    auto mathStart = eastl::chrono::steady_clock::now();
    int r = ctx->Execute();
    auto mathEnd = eastl::chrono::steady_clock::now();
    EXPECT_EQ(r, asEXECUTION_FINISHED, "N-body failed.");

    std::println("[Performance] N-Body Math executed in: {} ms",
                 eastl::chrono::duration_cast<eastl::chrono::milliseconds>(mathEnd - mathStart).count());

    // Branching QuickSort Stress
    asIScriptFunction* sortFunc = mathMod->GetFunctionByDecl("void MathStressTest::RunSort(int)");
    ctx->Prepare(sortFunc);
    ctx->SetArgDWord(0, 10000); // 10,000 items

    auto sortStart = eastl::chrono::steady_clock::now();
    r = ctx->Execute();
    auto sortEnd = eastl::chrono::steady_clock::now();
    EXPECT_EQ(r, asEXECUTION_FINISHED, "QuickSort failed.");

    std::println("[Performance] QuickSort executed in: {} ms",
                 eastl::chrono::duration_cast<eastl::chrono::milliseconds>(sortEnd - sortStart).count());

    // ContextPtr is RAII — auto-returns when it goes out of scope.
    // Do NOT call ReturnContext manually.
}
