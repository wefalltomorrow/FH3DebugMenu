add_library(FH3ExternalCallStub STATIC RemoteCallStub.cpp)
target_compile_features(FH3ExternalCallStub PUBLIC cxx_std_20)
target_include_directories(FH3ExternalCallStub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(FH3ExternalCallStubTests test_RemoteCallStub.cpp)
target_link_libraries(FH3ExternalCallStubTests PRIVATE FH3ExternalCallStub)
add_test(NAME ExternalCallStub COMMAND FH3ExternalCallStubTests)

if(WIN32)
    add_executable(FH3ExternalCamera FH3ExternalCameraWin.cpp)
    target_compile_features(FH3ExternalCamera PRIVATE cxx_std_20)
    target_compile_definitions(FH3ExternalCamera PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(FH3ExternalCamera PRIVATE ${PROJECT_SOURCE_DIR}/NativeRuntimeTest)
    target_link_libraries(FH3ExternalCamera PRIVATE FH3ExternalCallStub
       FH3NativeCameraDiscovery FH3NativeCameraOwner FH3NativeCameraCapabilities bcrypt)
endif()
