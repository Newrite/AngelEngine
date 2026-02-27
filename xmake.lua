set_xmakever("3.0.5")
set_project("AngelEngine")
set_version("1.0.0")

set_policy("package.requires_lock", true)
set_policy("check.auto_ignore_flags", false)

set_policy("build.c++.modules", true)

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

set_languages("c++latest")
set_warnings("all", "error")
set_arch("x64")

if is_plat("windows") then
    set_toolchains("clang-cl")
    -- Говорим xmake использовать lld-link для LTO
    set_toolset("ld", "lld-link")
    set_toolset("sh", "lld-link")
    set_toolset("ar", "llvm-ar") -- <--- ДОБАВИТЬ ЭТО
end

-- Electonic Art Standard Template Library
add_requires("eastl")
add_requires("mimalloc")
add_requires("fmt")
add_requires("nlohmann_json")
add_requires("gtest", {configs = {use_gtest_main = true}})

if is_mode("release") or is_mode("releasedbg") then
    -- 1. Высший уровень абстракции xmake (включает -O3 / -Ofast)
    set_optimize("aggressive")

    -- 2. Включаем LTO (Link-Time Optimization) - критически важно для производительности!
    set_policy("build.optimization.lto", true)

    -- 3. Агрессивные флаги, которые понимают и MSVC, и clang-cl
    add_cxflags(
        "/Ob3",                          -- Максимальный инлайнинг
        "/Oi",                           -- Intrinsic-функции
        "/Ot",                           -- Favor Fast Code
        "/fp:fast",                      -- Быстрая математика FPU/SIMD
        "/GS-",                          -- Отключение Security Cookie (чистый буст)
        "/arch:AVX2",                    -- Векторизация YMM
        "/Zc:inline",                    -- Удаление неиспользуемых COMDAT
        { tools = { "cl", "clang_cl" } } -- <--- Применяем к обоим!
    )

    -- 4. Специфичные флаги ТОЛЬКО для Clang (LLVM Superpowers)
    -- clang-cl позволяет прокидывать родные флаги Clang через /clang:
    add_cxflags(
        "/clang:-fstrict-aliasing", -- Строгое алиасирование (LLVM обожает его для оптимизации указателей)
        "/clang:-fvectorize",       -- Принудительная векторизация циклов
        { tools = "clang_cl" }
    )
end

-------------------------------------------------------------------------------
-- Target: MIR (Lightweight JIT Compiler Runtime)
-------------------------------------------------------------------------------
target("mir")
    set_kind("static")
    set_languages("c11")
    set_warnings("none")
    
    local mir_root = "GitModules/angelsea/vendor/mir"
    
    add_files(mir_root .. "/mir.c")
    add_files(mir_root .. "/mir-gen.c")
    add_files(mir_root .. "/c2mir/c2mir.c")
    
    add_includedirs(mir_root, { public = true })
    
    -- Флаги оптимизации MIR из их CMake
    add_defines("MIR_NO_SCAN=1", "MIR_NO_BIN_COMPRESSION=1", "MIR_NO_IO=1", { public = true })
    
    -- MIR требует signed char для корректной работы
    add_cxflags("/clang:-fsigned-char", { tools = "clang_cl" })

-------------------------------------------------------------------------------
-- Target: Angelsea (AngelScript JIT)
-------------------------------------------------------------------------------
target("angelsea")
    set_kind("static")
    set_languages("c++latest")
    
    add_packages("fmt", { public = true })
    
    local asea_src = "GitModules/angelsea/src/angelsea"
    add_files(asea_src .. "/*.cpp")
    add_files(asea_src .. "/detail/*.cpp")
    
    add_includedirs("GitModules/angelsea/include", { public = true })

    add_includedirs("GitModules/angelscript/sdk/angelscript/source")
    
    -- Angelsea зависит от MIR и заголовков AngelScript
    add_deps("mir")
    add_deps("AngelScript")

-------------------------------------------------------------------------------
-- Target: AngelScript Static Library
-------------------------------------------------------------------------------
target("AngelScript")
    set_kind("static")
    set_warnings("none")
    
    local as_root = "GitModules/angelscript/sdk/angelscript"
    local as_addon = "GitModules/angelscript/sdk/add_on"
    
    -- Core AngelScript
    add_files(as_root .. "/source/*.cpp")
    add_files(as_root .. "/source/as_callfunc_x64_msvc_asm.asm")
    add_includedirs(as_root .. "/include", { public = true })
    
    -- String
    add_files(as_addon .. "/scriptstdstring/*.cpp")
    add_includedirs(as_addon .. "/scriptstdstring", { public = true })
    
    -- Array
    add_files(as_addon .. "/scriptarray/*.cpp")
    add_includedirs(as_addon .. "/scriptarray", { public = true })
    
    -- Dictionary
    add_files(as_addon .. "/scriptdictionary/*.cpp")
    add_includedirs(as_addon .. "/scriptdictionary", { public = true })
    
    -- Math
    add_files(as_addon .. "/scriptmath/*.cpp")
    add_includedirs(as_addon .. "/scriptmath", { public = true })
    
    -- File System
    add_files(as_addon .. "/scriptfile/*.cpp")
    add_includedirs(as_addon .. "/scriptfile", { public = true })
    
    -- scriptbuilder
    add_files(as_addon .. "/scriptbuilder/*.cpp")
    add_includedirs(as_addon .. "/scriptbuilder", { public = true })
    
    -- serializer
    add_files(as_addon .. "/serializer/*.cpp")
    add_includedirs(as_addon .. "/serializer", { public = true })
    
    -- any
    add_files(as_addon .. "/scriptany/*.cpp")
    add_includedirs(as_addon .. "/scriptany", { public = true })
    
    -- contextmgr
    -- add_files(as_addon .. "/contextmgr/*.cpp")
    -- add_includedirs(as_addon .. "/contextmgr", { public = true })
    
    -- datetime
    add_files(as_addon .. "/datetime/*.cpp")
    add_includedirs(as_addon .. "/datetime", { public = true })
    
    -- scripthandle
    add_files(as_addon .. "/scripthandle/*.cpp")
    add_includedirs(as_addon .. "/scripthandle", { public = true })
    
    -- scripthelper
    add_files(as_addon .. "/scripthelper/*.cpp")
    add_includedirs(as_addon .. "/scripthelper", { public = true })
    
    -- weakref
    add_files(as_addon .. "/weakref/*.cpp")
    add_includedirs(as_addon .. "/weakref", { public = true })
    
    -- debugger
    add_files(as_addon .. "/debugger/*.cpp")
    add_includedirs(as_addon .. "/debugger", { public = true })
    
    -- AS_NO_EXCEPTIONS disable exception inside AS
    add_defines("AS_NO_EXCEPTIONS")
    
    add_cxflags(
        "-Wno-deprecated-declarations",
        { tools = "clang_cl" }
    )


-------------------------------------------------------------------------------
-- Target: Main Static Library
-------------------------------------------------------------------------------
target("AngelEngine")
    set_kind("static")
    
    add_packages("mimalloc", { public = true })
    add_packages("eastl", { public = true })
    add_packages("nlohmann_json", { public = true })
    add_defines("EASTL_OPENSOURCE")
    add_defines("AS_PROCESS_METADATA=1")
    
    add_files("AngelEngine/*.cpp|MemoryHooks.cpp|Infrastructure.cpp", { public = true })
    add_files("AngelEngine/MemoryHooks.cpp")
    add_files("AngelEngine/SerializationHandlers.cpp")
    add_files("AngelEngine/Infrastructure.cpp")

    add_files("AngelEngine/Addons/scripteastlstring/*.cpp", {
        cxflags = "-Wno-unused-but-set-variable"
    })
    add_headerfiles("AngelEngine/**.h", { public = true })
    add_headerfiles("AngelEngine/**.hpp", { public = true,  })
    
    add_includedirs("GitModules/asbind20/include", { public = true })

    -- Custom addons for angel script, replace std::string with eastl::string
    add_includedirs("AngelEngine/Addons")
    
    -- ScriptArray and ScriptDictionary headers
    add_includedirs("GitModules/angelscript/sdk/add_on/scriptarray")
    add_includedirs("GitModules/angelscript/sdk/add_on/scriptdictionary")

    add_deps("AngelScript")
    add_deps("angelsea")
    
    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")
    
    add_cxflags(
        "-Wno-unused-command-line-argument",
        { tools = "clang_cl" }
    )
    
    add_defines("_CRT_SECURE_NO_WARNINGS")

-------------------------------------------------------------------------------
-- Target: GTest Executable
-------------------------------------------------------------------------------
target("AngelEngineGTest")
    set_kind("binary")
    set_default(false)  -- Не билдить по умолчанию

    add_files("AngelEngineGTest/*.cpp")
    add_headerfiles("AngelEngineGTest/*.h")
    add_deps("AngelEngine")

    add_packages("gtest", { public = true })
    add_packages("eastl", { public = true })
    add_defines("EASTL_OPENSOURCE")

    add_includedirs("GitModules/asbind20/include")
    add_includedirs("AngelEngineGTest")
    
    -- ScriptArray headers for tests
    add_includedirs("GitModules/angelscript/sdk/add_on/scriptarray")

    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")

    add_cxflags(
        "-Wno-unused-command-line-argument",
        { tools = "clang_cl" }
    )

    add_defines("_CRT_SECURE_NO_WARNINGS")

    set_rundir("$(projectdir)")
    