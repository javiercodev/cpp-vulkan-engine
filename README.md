# cpp-vulkan-engine

<p align="center">
  <img src="assets/vulkan_logo.png" alt="Vulkan logo" width="650">
</p>

A Vulkan rendering engine in C++, built while learning from the official [Khronos Vulkan Documentation](https://docs.vulkan.org/spec/latest/index.html).

## Tech stack

- C++20, Vulkan (via Vulkan-Hpp RAII)
- GLFW, GLM
- CMake + vcpkg

## Building

```bash
cmake -B build -S src -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

## Structure

```
cpp-vulkan-engine/
├── CMake/     # Custom Find*.cmake modules
├── assets/    # Models and textures
└── src/       # Source code and shaders
```

## License

This project is licensed under the MIT License. See the bundled LICENSE file for details.