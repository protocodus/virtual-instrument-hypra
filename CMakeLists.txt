cmake_minimum_required(VERSION 3.22)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")
endif()

project(Hypra VERSION 0.7.0 LANGUAGES CXX)

# Source/DSP is C++17 and JUCE-free: the Rack Extension compiles it directly.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HYPRA_ENABLE_SANITIZERS "Build everything with ASan and UBSan" OFF)
if(HYPRA_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()

add_library(HypraDSP STATIC Source/DSP/HypraEngine.cpp Source/DSP/HypraControls.cpp)
target_include_directories(HypraDSP PUBLIC Source/DSP)
target_compile_options(HypraDSP PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

add_executable(HypraTests Tests/EngineTests.cpp)
target_link_libraries(HypraTests PRIVATE HypraDSP)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tools/RenderDemos.cpp")
    add_executable(HypraRenderDemos Tools/RenderDemos.cpp)
    target_link_libraries(HypraRenderDemos PRIVATE HypraDSP)
endif()

add_executable(HypraRenderEvents Tools/RenderEvents.cpp)
target_link_libraries(HypraRenderEvents PRIVATE HypraDSP)

enable_testing()
foreach(suite SuperSaw HardwareRecording MeasuredLaws Filter Circuit Engine Feedback Effects Stability Timing Modulation ToneControl SustainLaw KeyFollowShift XmodDestination EffectLevels Controls)
    add_test(NAME Hypra.${suite} COMMAND HypraTests ${suite})
    set_tests_properties(Hypra.${suite} PROPERTIES LABELS unit)
endforeach()
