module;

#include <asbind20/asbind.hpp>
#include <angelscript.h>
#include <print>

#include <scriptstdstring.h>
#include <scriptarray.h>
#include <scriptdictionary.h>
#include <scriptmath.h>
#include <scriptfile.h>
#include <scriptany.h>
#include <datetime.h>
#include <scripthandle.h>
#include <weakref.h>
#include <scripthelper.h>

export module AngelEngine.BindingManager;

namespace RE
{
    // Mock class representing a game engine actor
    class Actor
    {
        
        std::uint32_t form_id_;
        std::string name_{""};
        float health_{0.f};
        
    public:
        
        using ActorPtr = Actor*;
        
        explicit Actor(const std::uint32_t form_id) : form_id_(form_id)
        {
            std::println("RE::Actor created in c++");
        }
        
        static ActorPtr GetActorByFormId(const std::uint32_t form_id)
        {
            return new Actor{form_id};
        }

        std::uint32_t GetFormId() const
        {
            return form_id_;
        }

        const std::string& GetName() const
        {
            return name_;
        }

        float GetHealth() const
        {
            return health_;
        }
    };
}

namespace AngelEngine
{
    
    struct Actor
    {
        
        explicit Actor(const std::uint32_t form_id) : form_id_(form_id), ref_count_(1)
        {
            actor_ = RE::Actor::GetActorByFormId(form_id_);
            std::println("AngelEngine::Actor created in c++ by ref_id");
        }
        
        explicit Actor(RE::Actor::ActorPtr actor) : ref_count_(1), actor_(actor)
        {
            form_id_ = actor ? actor->GetFormId() : 0;
        }
        
        // --- Reference Counting for AngelScript ---
        void AddRef()
        {
            ref_count_.fetch_add(1);
        }

        void Release()
        {
            if (ref_count_.fetch_sub(1) == 1)
            {
                delete this; 
                // IMPORTANT: We delete ONLY the wrapper (this).
                // We DO NOT delete actor_, as the actor is owned by the game engine.
            }
        }
        
        std::uint32_t GetFormId() const
        {
            if (!actor_) return 0;
            return form_id_;
        }
        
        std::string GetName() const
        {
            if (!actor_) return "Null Actor";
            return actor_->GetName();
        }
        
        float GetHealth() const
        {
            if (!actor_) return -1.f;
            return actor_->GetHealth();
        }
        
        bool IsValid() const 
        { 
            return actor_ != nullptr; 
        }
        
        static void Bind(asIScriptEngine* const engine)
        {
            
            // Create wrapper class "Actor"
            asbind20::ref_class<Actor>(engine, "Actor")
                // 1. Memory management
                .addref(&Actor::AddRef)
                .release(&Actor::Release)

                // 2. Factory (Constructor)
                .factory<std::uint32_t>("uint32", asbind20::use_explicit)

                // 3. Methods
                .method("uint32 get_FormId() const", &Actor::GetFormId)
                .method("string get_Name() const", &Actor::GetName)
                .method("float get_Health() const", &Actor::GetHealth)
                .method("bool get_IsValid() const", &Actor::IsValid);
            
        }
        
    private:
        
        std::uint32_t form_id_;
        std::atomic<int> ref_count_;
        RE::Actor::ActorPtr actor_;
        
        ~Actor() = default;
    };
    
    
    // Function for script output (replaces cout)
    void scriptPrint(const std::string& msg)
    {
        std::println("[Script]: {}", msg);
    }

    enum class BindingError : std::uint8_t
    {
        BindingGlobalsFailed,
    };

    export struct BindingManager final
    {
        static void RegisterStandardAddons(asIScriptEngine* engine)
        {
            RegisterStdString(engine);
            RegisterScriptArray(engine, true);
            RegisterScriptDictionary(engine);
            RegisterScriptMath(engine);
            RegisterScriptFile(engine);
            RegisterScriptAny(engine);
            RegisterScriptDateTime(engine);
            RegisterScriptHandle(engine);
            RegisterScriptWeakRef(engine);
            RegisterStdStringUtils(engine);
        }

        static std::expected<void, BindingError> BindGlobals(asIScriptEngine* const engine)
        {
            try
            {
                asbind20::global(engine)
                    .function("void print(const string &in msg)", &scriptPrint);
                Actor::Bind(engine);
            }
            catch (const std::exception& e)
            {
                std::println("Binding error: {}", e.what());
                return std::unexpected(BindingError::BindingGlobalsFailed);
            }

            return {};
        }
    };
}
