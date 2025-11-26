-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
includes("lib/commonlibsse-ng")

-- set project
set_project("AnimationLedgeBlockNG")
set_version("0.2.14")
set_license("MIT")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

-- targets
target("AnimationLedgeBlockNG")
    -- add dependencies to target
    add_deps("commonlibsse-ng")

    -- add commonlibsse-ng plugin
    add_rules("commonlibsse-ng.plugin", {
        name = "AnimationLedgeBlockNG",
        author = "MaskPlague",
        description = "SKSE64 plugin using CommonLibSSE-NG"
    })

    -- add src files
    set_pcxxheader("src/pch.h")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    
