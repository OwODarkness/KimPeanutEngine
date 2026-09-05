# Third-party dependency orchestration.
# Each dependency owns its target definition in its own directory.

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

# The upstream GoogleTest CMakeLists.txt is preserved unchanged. This wrapper
# adapts the checked-in prebuilt libraries to the engine's target convention.
add_subdirectory(googletest_wrapper)
