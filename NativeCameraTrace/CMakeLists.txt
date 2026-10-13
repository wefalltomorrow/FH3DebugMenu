add_library(FH3CameraDelta STATIC CameraDelta.cpp)
target_compile_features(FH3CameraDelta PUBLIC cxx_std_20)
target_include_directories(FH3CameraDelta PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(FH3CameraDeltaTests test_CameraDelta.cpp)
target_compile_features(FH3CameraDeltaTests PRIVATE cxx_std_20)
target_link_libraries(FH3CameraDeltaTests PRIVATE FH3CameraDelta)
add_test(NAME CameraDelta COMMAND FH3CameraDeltaTests)

if(WIN32)
    add_executable(FH3CameraTrace FH3CameraTraceWin.cpp)
    target_compile_features(FH3CameraTrace PRIVATE cxx_std_20)
    target_compile_definitions(FH3CameraTrace PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(FH3CameraTrace PRIVATE ${CMAKE_SOURCE_DIR}/NativeRuntimeTest)
    target_link_libraries(FH3CameraTrace PRIVATE FH3CameraDelta FH3NativeCameraDiscovery
        FH3NativeCameraOwner FH3NativeCameraCapabilities bcrypt)
endif()
