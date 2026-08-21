# Core smoke test — directly compiles the engine core (no plugin/codegen).
cmake_minimum_required(VERSION 3.20)
project(CoreTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_compile_options(/utf-8)
endif()

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Engine core sources (FAssembly / Fatal require the .cpp; the rest is header-only).
file(GLOB MAHO_PRIVATE CONFIGURE_DEPENDS "${ENGINE_DIR}/Source/Private/**/*.cpp")
file(GLOB MAHO_HEADERS  CONFIGURE_DEPENDS "${ENGINE_DIR}/Source/Public/**/*.h")

add_executable(CoreTest main.cpp ${MAHO_PRIVATE} ${MAHO_HEADERS})
target_include_directories(CoreTest PRIVATE "${ENGINE_DIR}/Source/Public")

# Assembly.cpp uses LoadLibrary/FreeLibrary/getenv on the host platform; Threads for FThreadPool.
find_package(Threads REQUIRED)
target_link_libraries(CoreTest PRIVATE Threads::Threads)
