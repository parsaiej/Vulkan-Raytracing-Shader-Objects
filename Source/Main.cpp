#include <Common.h>
#include <RenderContext.h>

// Forwards
// --------------------------------------

void InitializeResources(RenderContext* pRenderContext);
void FreeResources(RenderContext* pRenderContext);

// Resources
// --------------------------------------

Image g_ColorAttachment {};
Image g_DepthAttachment {};

Buffer g_MeshVertexBuffer {};
Buffer g_MeshIndexBuffer {};

Buffer g_BLASBackingMemory {};
Buffer g_TLASBackingMemory {};

uint64_t g_BLASDeviceAddress;
uint64_t g_TLASDeviceAddress;

VkAccelerationStructureKHR g_BLAS;
VkAccelerationStructureKHR g_TLAS;

VkPipeline            g_RaytracingPipeline;
VkDescriptorSetLayout g_DescriptorSetLayout;
VkPipelineLayout      g_PipelineLayout;
VkDescriptorPool      g_DescriptorPool;
VkDescriptorSet       g_DescriptorSet;
VkImageView           g_ColorImageStorageView;

std::atomic<bool> g_ResourcesReadyFence;

// Layout of the standard Vertex for this application.
// ---------------------------------------------------------

struct Vertex
{
    glm::vec3 positionOS;
    glm::vec3 normalOS;
};

struct RaytracingPushConstants
{
    glm::mat4 InverseMatrixV;
    glm::mat4 InverseMatrixP;
};

// Entry-point
// --------------------------------------

int main()
{
    // Configure logging.
    // --------------------------------------

    auto loggerMemory = std::make_shared<std::stringstream>();
    auto loggerSink   = std::make_shared<spdlog::sinks::ostream_sink_mt>(*loggerMemory);
    auto logger       = std::make_shared<spdlog::logger>("", loggerSink);

    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%^[%l] %v%$");

    // Launch Vulkan + OS Window
    // --------------------------------------

    std::unique_ptr<RenderContext> pRenderContext = std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);

    // Initialize
    // ------------------------------------------------

    std::jthread loadResourcesAsync(InitializeResources, pRenderContext.get());

    // UI
    // ------------------------------------------------

    auto RecordInterface = [&]()
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));

        if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::BeginChild("LogSubWindow", ImVec2(600, 100), 1, ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::TextUnformatted(loggerMemory->str().c_str());

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0F);
            }
            ImGui::EndChild();

            // Display the FPS in the window
            ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, ImGui::GetIO().DeltaTime * 1000.0F);

            ImGui::End();
        }
    };

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](FrameParams frameParams)
    {
        if (!g_ResourcesReadyFence.load())
        {
            VulkanColorImageBarrier(frameParams.cmd,
                                    frameParams.backBuffer,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_ACCESS_2_MEMORY_READ_BIT,
                                    VK_ACCESS_2_MEMORY_READ_BIT,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

            return;
        }

        // Configure Attachments
        // --------------------------------------------

        VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        {
            colorAttachmentInfo.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachmentInfo.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachmentInfo.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachmentInfo.imageView        = g_ColorAttachment.imageView;
            colorAttachmentInfo.clearValue.color = {
                { 1.0, 0.0, 0.0, 1.0 }
            };
        }

        VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        {
            depthAttachmentInfo.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachmentInfo.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachmentInfo.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAttachmentInfo.imageView               = g_DepthAttachment.imageView;
            depthAttachmentInfo.clearValue.depthStencil = { 1.0, 0x0 };
        }

        // Record
        // --------------------------------------------

        VulkanColorImageBarrier(frameParams.cmd,
                                g_ColorAttachment.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_ACCESS_2_MEMORY_READ_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
        {
            vkRenderingInfo.colorAttachmentCount = 1U;
            vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
            vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
            vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
            vkRenderingInfo.layerCount           = 1U;
            vkRenderingInfo.renderArea           = {
                {            0,             0 },
                { kWindowWidth, kWindowHeight }
            };
        }
        vkCmdBeginRendering(frameParams.cmd, &vkRenderingInfo);

        // vkCmdTraceRaysKHR(frameParams.cmd, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, kWindowWidth, kWindowHeight, 1U);

        vkCmdEndRendering(frameParams.cmd);

        // Copy the internal color attachment to back buffer.

        VulkanColorImageBarrier(frameParams.cmd,
                                g_ColorAttachment.image,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_2_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT);

        VulkanColorImageBarrier(frameParams.cmd,
                                frameParams.backBuffer,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_ACCESS_2_MEMORY_READ_BIT,
                                VK_ACCESS_2_MEMORY_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT);

        VkImageCopy backBufferCopy = {};
        {
            backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1U };
            backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
            backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        }

        vkCmdCopyImage(frameParams.cmd,
                       g_ColorAttachment.image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       frameParams.backBuffer,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1U,
                       &backBufferCopy);

        VulkanColorImageBarrier(frameParams.cmd,
                                frameParams.backBuffer,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                VK_ACCESS_2_MEMORY_WRITE_BIT,
                                VK_ACCESS_2_MEMORY_READ_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    };

    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands, RecordInterface);

    // Shutdown
    // ------------------------------------------------

    FreeResources(pRenderContext.get());
}

// Implementations
// --------------------------------------

uint64_t GetBufferDeviceAddress(RenderContext* pRenderContext, const Buffer& buffer)
{
    VkBufferDeviceAddressInfoKHR deviceAddressInfo {};
    {
        deviceAddressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        deviceAddressInfo.buffer = buffer.buffer;
    }
    return vkGetBufferDeviceAddressKHR(pRenderContext->GetDevice(), &deviceAddressInfo);
}

void BuildBLAS(RenderContext* pRenderContext, VkCommandPool vkCommandPool, uint32_t vertexCount, uint32_t indexCount)
{
    VkAccelerationStructureGeometryKHR blasGeometryInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    {
        blasGeometryInfo.geometryType                                = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        blasGeometryInfo.flags                                       = VK_GEOMETRY_OPAQUE_BIT_KHR;
        blasGeometryInfo.geometry.triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        blasGeometryInfo.geometry.triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
        blasGeometryInfo.geometry.triangles.vertexData.deviceAddress = GetBufferDeviceAddress(pRenderContext, g_MeshVertexBuffer);
        blasGeometryInfo.geometry.triangles.maxVertex                = vertexCount;
        blasGeometryInfo.geometry.triangles.vertexStride             = sizeof(Vertex);
        blasGeometryInfo.geometry.triangles.indexType                = VK_INDEX_TYPE_UINT32;
        blasGeometryInfo.geometry.triangles.indexData.deviceAddress  = GetBufferDeviceAddress(pRenderContext, g_MeshIndexBuffer);
    }

    VkAccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    {
        blasBuildGeometryInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        blasBuildGeometryInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        blasBuildGeometryInfo.geometryCount = 1;
        blasBuildGeometryInfo.pGeometries   = &blasGeometryInfo;
    }

    VkAccelerationStructureBuildSizesInfoKHR blasBuildSizesInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };

    const uint32_t primitiveCount = indexCount / 3U;

    vkGetAccelerationStructureBuildSizesKHR(pRenderContext->GetDevice(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &blasBuildGeometryInfo,
                                            &primitiveCount,
                                            &blasBuildSizesInfo);

    // Create backing memory for the BLAS
    // ------------------------------------------------

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size               = blasBuildSizesInfo.accelerationStructureSize;
    bufferInfo.usage              = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;

    Check(vmaCreateBuffer(pRenderContext->GetAllocator(),
                          &bufferInfo,
                          &allocInfo,
                          &g_BLASBackingMemory.buffer,
                          &g_BLASBackingMemory.bufferAllocation,
                          nullptr),
          "Failed to create dedicated buffer memory.");

    // Create intermediate scratch memory
    // ------------------------------------------------

    bufferInfo.size  = blasBuildSizesInfo.buildScratchSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    allocInfo.usage  = VMA_MEMORY_USAGE_GPU_ONLY;

    Buffer scratchBuffer;
    Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &scratchBuffer.buffer, &scratchBuffer.bufferAllocation, nullptr),
          "Failed to create dedicated buffer memory.");

    // Create BLAS Primitive
    // ------------------------------------------------

    VkAccelerationStructureCreateInfoKHR acceleration_structure_create_info { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    {
        acceleration_structure_create_info.buffer = g_BLASBackingMemory.buffer;
        acceleration_structure_create_info.size   = blasBuildSizesInfo.accelerationStructureSize;
        acceleration_structure_create_info.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    }
    Check(vkCreateAccelerationStructureKHR(pRenderContext->GetDevice(), &acceleration_structure_create_info, nullptr, &g_BLAS),
          "Failed to create acceleration structure");

    // Build
    // ------------------------------------------------

    {
        // Rest is defined above.
        blasBuildGeometryInfo.scratchData.deviceAddress = GetBufferDeviceAddress(pRenderContext, scratchBuffer);
        blasBuildGeometryInfo.dstAccelerationStructure  = g_BLAS;
    }

    VkAccelerationStructureBuildRangeInfoKHR blasBuildRangeInfo;
    {
        blasBuildRangeInfo.primitiveCount  = primitiveCount;
        blasBuildRangeInfo.primitiveOffset = 0;
        blasBuildRangeInfo.firstVertex     = 0;
        blasBuildRangeInfo.transformOffset = 0;
    }
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> blasBuildRangeInfos = { &blasBuildRangeInfo };

    VkCommandBuffer vkCommand = VK_NULL_HANDLE;
    SingleShotCommandBegin(pRenderContext, vkCommand, vkCommandPool);
    {
        vkCmdBuildAccelerationStructuresKHR(vkCommand, 1U, &blasBuildGeometryInfo, blasBuildRangeInfos.data());
    }
    SingleShotCommandEnd(pRenderContext, vkCommand);

    VkAccelerationStructureDeviceAddressInfoKHR blasDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    {
        blasDeviceAddressInfo.accelerationStructure = g_BLAS;
    }
    g_BLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(pRenderContext->GetDevice(), &blasDeviceAddressInfo);

    // Release scratch memory
    // ------------------------------------------------

    vmaDestroyBuffer(pRenderContext->GetAllocator(), scratchBuffer.buffer, scratchBuffer.bufferAllocation);

    NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)g_BLAS, "BLAS");

    spdlog::info("Built bottom-level acceleration structure.");
}

void BuildTLAS(RenderContext* pRenderContext, VkCommandPool vkCommandPool, uint32_t indexCount)
{
    VkAccelerationStructureInstanceKHR tlasInstance {};
    {
        tlasInstance.transform                              = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
        tlasInstance.instanceCustomIndex                    = 0;
        tlasInstance.mask                                   = 0xFF;
        tlasInstance.instanceShaderBindingTableRecordOffset = 0;
        tlasInstance.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        tlasInstance.accelerationStructureReference         = g_BLASDeviceAddress;
    }

    // Create Instances Buffer.
    // ------------------------------------------------

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.usage              = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.size               = sizeof(VkAccelerationStructureInstanceKHR);

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_CPU_TO_GPU;

    Buffer instanceBuffer;
    Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &instanceBuffer.buffer, &instanceBuffer.bufferAllocation, nullptr),
          "Failed to create staging buffer memory.");

    // Copy Instances Host -> Staging Memory.
    // -----------------------------------------------------

    void* pMappedData = nullptr;
    Check(vmaMapMemory(pRenderContext->GetAllocator(), instanceBuffer.bufferAllocation, &pMappedData), "Failed to map a pointer to staging memory.");
    {
        // Copy from Host -> Staging memory.
        memcpy(pMappedData, &tlasInstance, sizeof(VkAccelerationStructureInstanceKHR));

        vmaUnmapMemory(pRenderContext->GetAllocator(), instanceBuffer.bufferAllocation);
    }

    // The top level acceleration structure contains (bottom level) instance as the input geometry
    VkAccelerationStructureGeometryKHR tlasGeometryInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    tlasGeometryInfo.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometryInfo.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tlasGeometryInfo.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeometryInfo.geometry.instances.arrayOfPointers    = VK_FALSE;
    tlasGeometryInfo.geometry.instances.data.deviceAddress = GetBufferDeviceAddress(pRenderContext, instanceBuffer);

    // Get the size requirements for buffers involved in the acceleration structure build process
    VkAccelerationStructureBuildGeometryInfoKHR tlasGeometryBuildInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    tlasGeometryBuildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasGeometryBuildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasGeometryBuildInfo.geometryCount = 1;
    tlasGeometryBuildInfo.pGeometries   = &tlasGeometryInfo;

    const uint32_t primitiveCount = indexCount / 3U;

    VkAccelerationStructureBuildSizesInfoKHR tlasBuildSizeInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(pRenderContext->GetDevice(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &tlasGeometryBuildInfo,
                                            &primitiveCount,
                                            &tlasBuildSizeInfo);

    // Create Instances Buffer.
    // ------------------------------------------------

    bufferInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.size  = tlasBuildSizeInfo.accelerationStructureSize;
    allocInfo.usage  = VMA_MEMORY_USAGE_GPU_ONLY;

    Check(vmaCreateBuffer(pRenderContext->GetAllocator(),
                          &bufferInfo,
                          &allocInfo,
                          &g_TLASBackingMemory.buffer,
                          &g_TLASBackingMemory.bufferAllocation,
                          nullptr),
          "Failed to create backing memory for TLAS.");

    // Create intermediate scratch memory
    // ------------------------------------------------

    bufferInfo.size  = tlasBuildSizeInfo.buildScratchSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    allocInfo.usage  = VMA_MEMORY_USAGE_GPU_ONLY;

    Buffer scratchBuffer;
    Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &scratchBuffer.buffer, &scratchBuffer.bufferAllocation, nullptr),
          "Failed to create scratch buffer memory.");

    // Create TLAS
    // ------------------------------------------------

    VkAccelerationStructureCreateInfoKHR tlasCreateInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    {
        tlasCreateInfo.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        tlasCreateInfo.buffer = g_TLASBackingMemory.buffer;
        tlasCreateInfo.size   = tlasBuildSizeInfo.accelerationStructureSize;
        tlasCreateInfo.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    }
    vkCreateAccelerationStructureKHR(pRenderContext->GetDevice(), &tlasCreateInfo, nullptr, &g_TLAS);

    {
        tlasGeometryBuildInfo.dstAccelerationStructure  = g_TLAS;
        tlasGeometryBuildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(pRenderContext, scratchBuffer);
    }

    VkAccelerationStructureBuildRangeInfoKHR tlasBuildRangeInfo;
    {
        tlasBuildRangeInfo.primitiveCount  = primitiveCount;
        tlasBuildRangeInfo.primitiveOffset = 0;
        tlasBuildRangeInfo.firstVertex     = 0;
        tlasBuildRangeInfo.transformOffset = 0;
    }
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> tlasBuildRangeInfos = { &tlasBuildRangeInfo };

    VkCommandBuffer vkCommand = VK_NULL_HANDLE;
    SingleShotCommandBegin(pRenderContext, vkCommand, vkCommandPool);
    {
        vkCmdBuildAccelerationStructuresKHR(vkCommand, 1U, &tlasGeometryBuildInfo, tlasBuildRangeInfos.data());
    }
    SingleShotCommandEnd(pRenderContext, vkCommand);

    VkAccelerationStructureDeviceAddressInfoKHR tlasDeviceAddressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    {
        tlasDeviceAddressInfo.accelerationStructure = g_TLAS;
    }
    g_TLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(pRenderContext->GetDevice(), &tlasDeviceAddressInfo);

    // Release scratch memory
    // ------------------------------------------------

    vmaDestroyBuffer(pRenderContext->GetAllocator(), scratchBuffer.buffer, scratchBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), instanceBuffer.buffer, instanceBuffer.bufferAllocation);

    NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, (uint64_t)g_TLAS, "TLAS");

    spdlog::info("Built top-level acceleration structure.");
}

void CreateRaytracingPipeline(RenderContext* pRenderContext)
{
    std::vector<VkPipelineShaderStageCreateInfo> stageInfos;

    auto PushRaytracingShaderStage = [&](const char* shaderFilePath, VkShaderStageFlagBits stageFlags)
    {
        VkPipelineShaderStageCreateInfo stageInfo { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        {
            stageInfo.pName = "Main";
            stageInfo.stage = stageFlags;

            std::vector<char> byteCode;
            LoadByteCode(shaderFilePath, byteCode);

            VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            {
                shaderModuleInfo.pCode    = reinterpret_cast<uint32_t*>(byteCode.data());
                shaderModuleInfo.codeSize = byteCode.size();
            }
            Check(vkCreateShaderModule(pRenderContext->GetDevice(), &shaderModuleInfo, nullptr, &stageInfo.module),
                  "Failed to create ray tracing shader.");
        }

        stageInfos.push_back(stageInfo);
    };

    PushRaytracingShaderStage("RayGen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);

    VkRayTracingShaderGroupCreateInfoKHR rayGenGroupInfo { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
    {
        rayGenGroupInfo.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rayGenGroupInfo.generalShader      = 0U;
        rayGenGroupInfo.closestHitShader   = VK_SHADER_UNUSED_KHR;
        rayGenGroupInfo.anyHitShader       = VK_SHADER_UNUSED_KHR;
        rayGenGroupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
    }

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineInfo { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    {
        rayTracingPipelineInfo.stageCount                   = static_cast<uint32_t>(stageInfos.size());
        rayTracingPipelineInfo.pStages                      = stageInfos.data();
        rayTracingPipelineInfo.groupCount                   = 1U;
        rayTracingPipelineInfo.pGroups                      = &rayGenGroupInfo;
        rayTracingPipelineInfo.maxPipelineRayRecursionDepth = 1;
        rayTracingPipelineInfo.layout                       = g_PipelineLayout;
    }
    Check(vkCreateRayTracingPipelinesKHR(pRenderContext->GetDevice(),
                                         VK_NULL_HANDLE,
                                         VK_NULL_HANDLE,
                                         1,
                                         &rayTracingPipelineInfo,
                                         nullptr,
                                         &g_RaytracingPipeline),
          "Failed to create ray tracing pipeline.");

    for (auto& stageInfo : stageInfos)
        vkDestroyShaderModule(pRenderContext->GetDevice(), stageInfo.module, nullptr);
}

void InitializeResources(RenderContext* pRenderContext)
{
    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, g_ColorAttachment, g_DepthAttachment), "Failed to create the rendering attachments.");

    // Create command pool for this thread.
    // ------------------------------------------------

    VkCommandPoolCreateInfo vkCommandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    {
        vkCommandPoolInfo.queueFamilyIndex = pRenderContext->GetCommandQueueIndex();
    }

    VkCommandPool vkCommandPool;
    Check(vkCreateCommandPool(pRenderContext->GetDevice(), &vkCommandPoolInfo, nullptr, &vkCommandPool), "Failed to create a Vulkan Command Pool");

    // Mesh resource loading utility.
    // ------------------------------------------------

    auto LoadMesh = [&](const char* filePath, std::vector<Vertex>& vertexData, std::vector<uint32_t>& indices) -> bool
    {
        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(filePath))
        {
            if (!reader.Error().empty())
            {
                spdlog::error("Failed to load mesh: {}", reader.Error());
                return false;
            }
        }

        auto& attrib = reader.GetAttrib();
        auto& shapes = reader.GetShapes();

        for (const auto& shape : shapes)
        {
            for (const auto& index : shape.mesh.indices)
            {
                Vertex v;

                // Positions
                v.positionOS.x = attrib.vertices[3U * index.vertex_index + 0U];
                v.positionOS.y = attrib.vertices[3U * index.vertex_index + 1U];
                v.positionOS.z = attrib.vertices[3U * index.vertex_index + 2U];

                // Normals
                v.normalOS.x = attrib.normals[3U * index.normal_index + 0U];
                v.normalOS.y = attrib.normals[3U * index.normal_index + 1U];
                v.normalOS.z = attrib.normals[3U * index.normal_index + 2U];

                vertexData.push_back(v);
                indices.push_back((uint32_t)indices.size());
            }
        }

        spdlog::info("Loaded Mesh: {}", filePath);

        return true;
    };

    // Create staging memory.
    // ------------------------------------------------

    Buffer stagingBuffer {};
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        // Staging memory is 256mb. Hope it's enough!
        bufferInfo.size = static_cast<VkDeviceSize>(256U * 1024U * 1024U);

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        Check(
            vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.buffer, &stagingBuffer.bufferAllocation, nullptr),
            "Failed to create staging buffer memory.");
    }

    // Mesh resource loading utility.
    // ------------------------------------------------

    auto CreateMeshBuffer = [&](void* pData, uint32_t dataSize, VkBufferUsageFlags usage, Buffer* pBuffer)
    {
        // Create dedicate device memory for the mesh buffer.
        // -----------------------------------------------------

        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = dataSize;
        bufferInfo.usage              = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &pBuffer->buffer, &pBuffer->bufferAllocation, nullptr),
              "Failed to create dedicated buffer memory.");

        // Copy Host -> Staging Memory.
        // -----------------------------------------------------

        void* pMappedData = nullptr;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation, &pMappedData),
              "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, dataSize);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.bufferAllocation);
        }

        // Copy Staging -> Device Memory.
        // -----------------------------------------------------

        VkCommandBuffer vkCommand = VK_NULL_HANDLE;
        SingleShotCommandBegin(pRenderContext, vkCommand, vkCommandPool);
        {
            VmaAllocationInfo allocationInfo;
            vmaGetAllocationInfo(pRenderContext->GetAllocator(), pBuffer->bufferAllocation, &allocationInfo);

            VkBufferCopy copyInfo;
            {
                copyInfo.srcOffset = 0U;
                copyInfo.dstOffset = 0U;
                copyInfo.size      = dataSize;
            }
            vkCmdCopyBuffer(vkCommand, stagingBuffer.buffer, pBuffer->buffer, 1U, &copyInfo);
        }
        SingleShotCommandEnd(pRenderContext, vkCommand);

        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_BUFFER, (uint64_t)pBuffer->buffer, "Mesh Buffer");
    };

    // Load instance transforms.
    // ------------------------------------------------

    std::vector<Vertex>   instanceVertices;
    std::vector<uint32_t> unused;
    if (!LoadMesh("..\\Assets\\instance_transforms.obj", instanceVertices, unused))
        return;

    // Create device mesh.
    // ------------------------------------------------

    std::vector<Vertex>   meshVertices;
    std::vector<uint32_t> meshIndices;
    if (!LoadMesh("..\\Assets\\bunny.obj", meshVertices, meshIndices))
        return;

    // Create dedicate device memory for the mesh buffer.
    // -----------------------------------------------------

    CreateMeshBuffer(meshVertices.data(),
                     sizeof(Vertex) * (uint32_t)meshVertices.size(),
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     &g_MeshVertexBuffer);

    CreateMeshBuffer(meshIndices.data(),
                     sizeof(uint32_t) * (uint32_t)meshIndices.size(),
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     &g_MeshIndexBuffer);

    // Create acceleration structure.
    // -----------------------------------------------------

    BuildBLAS(pRenderContext, vkCommandPool, (uint32_t)meshVertices.size(), (uint32_t)meshIndices.size());
    BuildTLAS(pRenderContext, vkCommandPool, (uint32_t)meshIndices.size());

    // Configure Descriptor Set Layout
    // --------------------------------------

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetBindingInfos;

    auto PushDescriptorBinding = [&](VkDescriptorType descriptorType)
    {
        VkDescriptorSetLayoutBinding bindingInfo {};
        {
            bindingInfo.binding         = (uint32_t)descriptorSetBindingInfos.size();
            bindingInfo.descriptorType  = descriptorType;
            bindingInfo.descriptorCount = 1U;
            bindingInfo.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        }
        descriptorSetBindingInfos.push_back(bindingInfo);
    };

    PushDescriptorBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    PushDescriptorBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    VkDescriptorSetLayoutCreateInfo descriptorSetLayout = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        descriptorSetLayout.bindingCount = (uint32_t)descriptorSetBindingInfos.size();
        descriptorSetLayout.pBindings    = descriptorSetBindingInfos.data();
    }
    vkCreateDescriptorSetLayout(pRenderContext->GetDevice(), &descriptorSetLayout, nullptr, &g_DescriptorSetLayout);

    // Configure Push Constants
    // --------------------------------------

    VkPushConstantRange vkPushConstants;
    {
        vkPushConstants.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        vkPushConstants.offset     = 0U;
        vkPushConstants.size       = sizeof(RaytracingPushConstants);
    }

    // Configure Pipeline Layouts
    // --------------------------------------

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    {
        pipelineLayoutInfo.setLayoutCount         = 1U;
        pipelineLayoutInfo.pSetLayouts            = &g_DescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges    = &vkPushConstants;
    }
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &pipelineLayoutInfo, nullptr, &g_PipelineLayout),
          "Failed to create the default Vulkan Pipeline Layout");

    // Create ray tracing pipeline.
    // -----------------------------------------------------

    CreateRaytracingPipeline(pRenderContext);

    // Create descriptor pool.
    // -----------------------------------------------------

    std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes = {
        { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U } }
    };

    VkDescriptorPoolCreateInfo descriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    {
        descriptorPoolInfo.poolSizeCount = (uint32_t)descriptorPoolSizes.size();
        descriptorPoolInfo.pPoolSizes    = descriptorPoolSizes.data();
        descriptorPoolInfo.maxSets       = 1U;
        descriptorPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    }
    Check(vkCreateDescriptorPool(pRenderContext->GetDevice(), &descriptorPoolInfo, VK_NULL_HANDLE, &g_DescriptorPool),
          "Failed to create Raytracing Descriptor Pool.");

    // Create descriptors.
    // -----------------------------------------------------

    VkDescriptorSetAllocateInfo descriptorSetAllocInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    {
        descriptorSetAllocInfo.descriptorSetCount = 1U;
        descriptorSetAllocInfo.descriptorPool     = g_DescriptorPool;
        descriptorSetAllocInfo.pSetLayouts        = &g_DescriptorSetLayout;
    }
    Check(vkAllocateDescriptorSets(pRenderContext->GetDevice(), &descriptorSetAllocInfo, &g_DescriptorSet),
          "Failed to allocate raytracing descriptors.");

    std::vector<VkWriteDescriptorSet> descriptorWrites;

    // Descriptor #0

    VkWriteDescriptorSetAccelerationStructureKHR descriptorWriteTLASInfo { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    {
        descriptorWriteTLASInfo.accelerationStructureCount = 1U;
        descriptorWriteTLASInfo.pAccelerationStructures    = &g_TLAS;
    }

    VkWriteDescriptorSet descriptorWriteInfo0 = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    {
        descriptorWriteInfo0.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        descriptorWriteInfo0.descriptorCount = 1U;
        descriptorWriteInfo0.dstBinding      = 0U;
        descriptorWriteInfo0.dstSet          = g_DescriptorSet;
        descriptorWriteInfo0.pNext           = &descriptorWriteTLASInfo;
    }

    // Descriptor #1

    VkDescriptorImageInfo descriptorWriteColorInfo(VK_NULL_HANDLE, g_ColorAttachment.imageView, VK_IMAGE_LAYOUT_GENERAL);

    VkWriteDescriptorSet descriptorWriteInfo1 = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    {
        descriptorWriteInfo1.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWriteInfo1.descriptorCount = 1U;
        descriptorWriteInfo1.dstBinding      = 1U;
        descriptorWriteInfo1.dstSet          = g_DescriptorSet;
        descriptorWriteInfo1.pImageInfo      = &descriptorWriteColorInfo;
    }

    descriptorWrites.push_back(descriptorWriteInfo0);
    descriptorWrites.push_back(descriptorWriteInfo1);

    vkUpdateDescriptorSets(pRenderContext->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0U, nullptr);

    // Release staging memory.
    // ------------------------------------------------

    vmaDestroyBuffer(pRenderContext->GetAllocator(), stagingBuffer.buffer, stagingBuffer.bufferAllocation);

    // Release thread's command pool.
    // ------------------------------------------------

    vkDestroyCommandPool(pRenderContext->GetDevice(), vkCommandPool, nullptr);

    // Done.
    // ------------------------------------------------

    g_ResourcesReadyFence.store(true);
}

void FreeResources(RenderContext* pRenderContext)
{
    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyPipelineLayout(pRenderContext->GetDevice(), g_PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(pRenderContext->GetDevice(), g_DescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(pRenderContext->GetDevice(), g_DescriptorPool, nullptr);

    vkDestroyPipeline(pRenderContext->GetDevice(), g_RaytracingPipeline, nullptr);

    vkDestroyAccelerationStructureKHR(pRenderContext->GetDevice(), g_BLAS, nullptr);
    vkDestroyAccelerationStructureKHR(pRenderContext->GetDevice(), g_TLAS, nullptr);

    vmaDestroyBuffer(pRenderContext->GetAllocator(), g_BLASBackingMemory.buffer, g_BLASBackingMemory.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), g_TLASBackingMemory.buffer, g_TLASBackingMemory.bufferAllocation);

    vkDestroyImageView(pRenderContext->GetDevice(), g_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), g_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), g_ColorAttachment.image, g_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), g_DepthAttachment.image, g_DepthAttachment.imageAllocation);

    vmaDestroyBuffer(pRenderContext->GetAllocator(), g_MeshVertexBuffer.buffer, g_MeshVertexBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), g_MeshIndexBuffer.buffer, g_MeshIndexBuffer.bufferAllocation);
}
