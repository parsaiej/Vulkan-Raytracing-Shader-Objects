# Vulkan Raytraced Shader Objects

This project was meant to be a test of Vulkan's ray tracing extension usage with the VK_EXT_shader_object extension, until I learned that the two are not compatible!

What remains is this small demo app that uploads a Stanford bunny to the bottom-level acceleration structure and instances it about a thousand times in the top-level acceleration structure.

<img width="957" alt="image" src="https://github.com/user-attachments/assets/d511727f-2bb7-4601-83dd-6af225d21bb8">


# Build

```
mkdir build && cd build
cmake .. -G Ninja
cmake --build . --config Release
```
