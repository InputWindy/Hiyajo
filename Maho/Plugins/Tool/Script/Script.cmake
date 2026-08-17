# Script plugin: Lua + sol2 are owned here.
# Game code also consumes sol2 + lua (ScriptDispatchSystem), so this file keeps
# the global `lua` target and MAHO_SOL2_INCLUDE_DIR for the game target to reuse.
maho_set_plugin_output_dirs(${_MOD_TARGET} "${_MOD_PLUGIN_DIR}")

# Lua 5.4 — FetchContent (populates under the build Intermediate).
FetchContent_Declare(
	lua_src
	GIT_REPOSITORY https://github.com/lua/lua.git
	GIT_TAG v5.4.7
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(lua_src lua.h)
set(_SCRIPT_LUA_SOURCE_DIR "${lua_src_SOURCE_DIR}")
message(STATUS "Maho: Script Lua (FetchContent) at ${_SCRIPT_LUA_SOURCE_DIR}")

set(_SCRIPT_LUA_SOURCES
	"${_SCRIPT_LUA_SOURCE_DIR}/lapi.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lauxlib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lbaselib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lcode.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lcorolib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lctype.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ldblib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ldebug.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ldo.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ldump.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lfunc.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lgc.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/linit.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/liolib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/llex.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lmathlib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lmem.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/loadlib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lobject.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lopcodes.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/loslib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lparser.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lstate.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lstring.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lstrlib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ltable.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ltablib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/ltm.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lundump.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lutf8lib.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lvm.c"
	"${_SCRIPT_LUA_SOURCE_DIR}/lzio.c"
)

if(NOT TARGET lua)
	add_library(lua STATIC ${_SCRIPT_LUA_SOURCES})
	target_include_directories(lua PUBLIC "${_SCRIPT_LUA_SOURCE_DIR}")
	set_target_properties(lua PROPERTIES
		FOLDER "Plugins/Script"
		POSITION_INDEPENDENT_CODE ON
		C_STANDARD 99
	)
	if(MSVC)
		target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
endif()

# sol2 — header-only bindings; FetchContent.
FetchContent_Declare(
	sol2
	GIT_REPOSITORY https://github.com/ThePhD/sol2.git
	GIT_TAG v3.3.1
	GIT_SHALLOW TRUE
)
maho_fetchcontent_populate_or_reuse(sol2 include/sol/sol.hpp)
set(_SCRIPT_SOL2_INCLUDE_DIR "${sol2_SOURCE_DIR}/include")
message(STATUS "Maho: Script sol2 (FetchContent) at ${_SCRIPT_SOL2_INCLUDE_DIR}")
set(MAHO_SOL2_INCLUDE_DIR "${_SCRIPT_SOL2_INCLUDE_DIR}" CACHE INTERNAL "sol2 include directory" FORCE)

target_link_libraries(${_MOD_TARGET} PRIVATE lua)
target_include_directories(${_MOD_TARGET} PRIVATE "${_SCRIPT_SOL2_INCLUDE_DIR}")
