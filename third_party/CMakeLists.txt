# Third-party dependency orchestration.
# Each dependency owns its target definition in its own directory.

# The checked-in binary packages below are built with the MSVC x64 ABI. Do not
# let a MinGW/Clang configuration reach the linker with these imported targets:
# C++ name mangling, runtime libraries, and COFF import/static-library details
# are not a portable ABI boundary.
if(NOT MSVC)
    message(FATAL_ERROR
        "KimPeanutEngine's checked-in third-party binaries require MSVC x64. "
        "Configure with -G \"Visual Studio 17 2022\" in the build/ directory, "
        "or rebuild every imported dependency for the selected toolchain before "
        "adding a non-MSVC configuration.")
endif()

add_subdirectory(assimp)
add_subdirectory(entt)
add_subdirectory(glad)
add_subdirectory(glfw)
add_subdirectory(vulkan)
add_subdirectory(imgui)
add_subdirectory(lua)
add_subdirectory(sol2)
add_subdirectory(magic_enum)
add_subdirectory(nlohmann)
add_subdirectory(httplib)
add_subdirectory(miniaudio)
add_subdirectory(meshoptimizer)
add_subdirectory(stb_image)
add_subdirectory(sqlite)

# The upstream GoogleTest CMakeLists.txt is preserved unchanged. This wrapper
# adapts the checked-in prebuilt libraries to the engine's target convention.
add_subdirectory(googletest_wrapper)
