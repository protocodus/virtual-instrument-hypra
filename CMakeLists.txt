cmake_minimum_required(VERSION 3.22)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")
endif()

project(Trancera VERSION 0.9.0 LANGUAGES CXX)

# Source/DSP is C++17 and JUCE-free: the Rack Extension compiles it directly.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TRANCERA_ENABLE_SANITIZERS "Build everything with ASan and UBSan" OFF)
if(TRANCERA_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()

add_library(TranceraDSP STATIC Source/DSP/TranceraEngine.cpp Source/DSP/TranceraControls.cpp)
target_include_directories(TranceraDSP PUBLIC Source/DSP)
target_compile_options(TranceraDSP PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

add_executable(TranceraTests Tests/EngineTests.cpp)
target_link_libraries(TranceraTests PRIVATE TranceraDSP)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tools/RenderDemos.cpp")
    add_executable(TranceraRenderDemos Tools/RenderDemos.cpp)
    target_link_libraries(TranceraRenderDemos PRIVATE TranceraDSP)
endif()

add_executable(TranceraRenderEvents Tools/RenderEvents.cpp)
target_link_libraries(TranceraRenderEvents PRIVATE TranceraDSP)

enable_testing()
foreach(suite SuperSaw HardwareRecording MeasuredLaws Filter Circuit Engine Feedback Effects Stability Timing Modulation ToneControl SustainLaw KeyFollowShift XmodDestination EffectLevels Controls TriangleShape MultiFxTypes EnvelopeSmoother ToneSaturation Osc2AndSync ControlAccuracy AnalogColor)
    add_test(NAME Trancera.${suite} COMMAND TranceraTests ${suite})
    set_tests_properties(Trancera.${suite} PROPERTIES LABELS unit)
endforeach()
