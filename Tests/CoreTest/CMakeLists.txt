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

# Isolated FThreadPool stress test — header-only, no engine private sources.
add_executable(PoolStress PoolStress.cpp ${MAHO_HEADERS})
target_include_directories(PoolStress PRIVATE "${ENGINE_DIR}/Source/Public")

# Isolated FQueue MPMC stress test — header-only.
add_executable(QueueStress QueueStress.cpp ${MAHO_HEADERS})
target_include_directories(QueueStress PRIVATE "${ENGINE_DIR}/Source/Public")

# Self-referential FLayerBase : FQueue<FLayerBase> compile/run check.
add_executable(LayerBaseStress LayerBaseStress.cpp ${MAHO_HEADERS})
target_include_directories(LayerBaseStress PRIVATE "${ENGINE_DIR}/Source/Public")

# MAHO_DECLARE_LAYER scaffold macro compile/run check.
add_executable(DeclareLayerStress DeclareLayerStress.cpp ${MAHO_HEADERS})
target_include_directories(DeclareLayerStress PRIVATE "${ENGINE_DIR}/Source/Public")

# EntryPoint.h drivers.compile + link check (needs Fatal.cpp).
add_executable(EntryPointStress EntryPointStress.cpp ${MAHO_PRIVATE} ${MAHO_HEADERS})
target_include_directories(EntryPointStress PRIVATE "${ENGINE_DIR}/Source/Public")
target_link_libraries(EntryPointStress PRIVATE Threads::Threads)

# Assembly.cpp uses LoadLibrary/FreeLibrary/getenv on the host platform; Threads for FThreadPool.
find_package(Threads REQUIRED)
target_link_libraries(CoreTest PRIVATE Threads::Threads)
target_link_libraries(PoolStress PRIVATE Threads::Threads)
target_link_libraries(QueueStress PRIVATE Threads::Threads)
target_link_libraries(LayerBaseStress PRIVATE Threads::Threads)
target_link_libraries(DeclareLayerStress PRIVATE Threads::Threads)
