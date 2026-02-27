#include <EASTL/vector.h>
#include "EngineFixture.hpp"

using namespace AngelEngineTest;

TEST_CASE(E2E, CombatSimulationWithGarbageCollection)
{
    EngineFixture fixture(false, {"CombatSim"});

    fixture.WriteAndCompile("CombatSim", R"(
        class Entity {
            int id;
            float hp;
            Entity(int id, float hp) { this.id = id; this.hp = hp; }
            void TakeDamage(float dmg) { hp -= dmg; }
        }

        array<Entity@> entities;

        void SpawnWave(int count) {
            for(int i = 0; i < count; i++) {
                entities.insertLast(Entity(i, 100.0f));
            }
        }

        void SimulateCombatRound() {
            for(uint i = 0; i < entities.length(); i++) {
                if (entities[i] !is null) {
                    entities[i].TakeDamage(10.5f);
                    if (entities[i].hp <= 0.0f) {
                        @entities[i] = null; // Mark for GC
                    }
                }
            }
            
            // Periodically clean nulls to shrink array
            for(uint i = 0; i < entities.length(); i++) {
                if (entities[i] is null) {
                    entities.removeAt(i);
                    i--;
                }
            }
        }

        int GetActiveEntityCount() {
            return entities.length();
        }

        void main() {} // Required entry point for RunMod
    )");

    auto ms = fixture.engine->RunAllMods();
    ASSERT_TRUE(ms.has_value(), "Failed to run all mods");

    asIScriptModule* mod = fixture.engine->GetEngine()->GetModule(AngelEngine::MegaModuleName);
    asIScriptFunction* spawnFunc = mod->GetFunctionByDecl("void CombatSim::SpawnWave(int)");
    asIScriptFunction* combatFunc = mod->GetFunctionByDecl("void CombatSim::SimulateCombatRound()");
    asIScriptFunction* countFunc = mod->GetFunctionByDecl("int CombatSim::GetActiveEntityCount()");

    // Spawn Wave
    auto ctxPtr = fixture.engine->GetExecutionManager()->RequestContext(fixture.engine->GetEngine(), nullptr);
    asIScriptContext* ctx = ctxPtr.get();

    ctx->Prepare(spawnFunc);
    ctx->SetArgDWord(0, 500); // 500 Entities
    int r = ctx->Execute();
    EXPECT_EQ(r, asEXECUTION_FINISHED, "SpawnWave execution failed.");

    // Simulate 10 rounds of combat (everyone takes 105 dmg and dies since start HP is 100)
    for (int i = 0; i < 10; i++)
    {
        ctx->Prepare(combatFunc);
        ctx->Execute();

        // Explicitly step Garbage Collector mid-combat to test stability
        fixture.engine->CallGarbageColletorOneStep();
        fixture.engine->Tick(0.016f); // Allow background async engines to breathe
    }

    // Verify death
    ctx->Prepare(countFunc);
    r = ctx->Execute();
    EXPECT_EQ(r, asEXECUTION_FINISHED, "Count execution failed.");
    int survivors = ctx->GetReturnDWord();

    EXPECT_EQ(survivors, 0, "Not all entities died during combat simulation. Memory leak or logic error.");

    // Force a full GC cycle to ensure everything is swept before fixture destruction
    fixture.engine->CallGarbageCollectorFullCycle();

    // ContextPtr is RAII — auto-returns when it goes out of scope.
    // Do NOT call ReturnContext manually.
}
