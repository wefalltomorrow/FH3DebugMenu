cmake_minimum_required(VERSION 3.20)
project(FH3NativeDebugMenuFoundation VERSION 0.5 LANGUAGES CXX)
enable_testing()
add_subdirectory(NativeCameraCore)
add_subdirectory(NativeCameraDiscovery)
add_subdirectory(NativeCameraOwner)

add_subdirectory(NativeCameraCapabilities)

add_subdirectory(NativeRuntimeTest)
