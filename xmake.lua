set_xmakever("3.0.5")
set_project("AngelEngine")
set_version("1.0.0")

set_policy("package.requires_lock", true)
set_policy("check.auto_ignore_flags", false)

set_policy("build.c++.modules", true)
set_policy("build.optimization.lto", true)

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

set_languages("c++latest")
set_warnings("all", "error")
set_toolchains("clang-cl")
set_arch("x64")

-- Electonic Art Standard Template Library
add_requires("eastl")

-- Include Angel Script JIT
includes("GitModules/AngelScript-JIT-Compiler")

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
    add_files(as_addon .. "/contextmgr/*.cpp")
    add_includedirs(as_addon .. "/contextmgr", { public = true })

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

    add_packages("eastl", { public = true })
    add_defines("EASTL_OPENSOURCE")
    add_defines("AS_PROCESS_METADATA=1")

    add_files("AngelEngine/*.cpp|EASTL_compat.cpp", { public = true })
    add_files("AngelEngine/EASTL_compat.cpp")

    add_files("AngelEngine/Addons/**.cpp", {
        cxflags = "-Wno-unused-but-set-variable"
    })
    add_headerfiles("AngelEngine/**.h", { public = true })

    add_includedirs("GitModules/asbind20/include")

    -- Custom addons for angel script, replace std::string with eastl::string
    add_includedirs("AngelEngine/Addons")

    add_deps("AngelScript")
    add_deps("AngelScriptJIT")

    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")

    add_cxflags(
        "-Wno-unused-command-line-argument",
        { tools = "clang_cl" }
    )

    add_defines("_CRT_SECURE_NO_WARNINGS")

-------------------------------------------------------------------------------
-- Target: Test Executable
-------------------------------------------------------------------------------
target("AngelEngineTest")
    set_kind("binary")

    add_files("AngelEngineTest/**.cpp")
    add_headerfiles("AngelEngineTest/TestContext.hpp")
    add_deps("AngelEngine")

    add_includedirs("GitModules/asbind20/include")
    add_includedirs("AngelEngineTest")

    add_packages("eastl", { public = true })
    add_defines("EASTL_OPENSOURCE")

    add_ldflags("-fuse-ld=lld", { tools = "clang-cl" })
    add_cxflags("/utf-8")

    add_cxflags(
        "-Wno-unused-command-line-argument",
        { tools = "clang_cl" }
    )

    add_defines("_CRT_SECURE_NO_WARNINGS")

    set_rundir("$(projectdir)")
