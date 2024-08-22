@echo off
C:\VulkanSDK\1.3.283.0\Bin\dxc.exe -E Main -T lib_6_8 -spirv -fspv-target-env=vulkan1.3 -Fo Compiled\\%1.spv %1.hlsl