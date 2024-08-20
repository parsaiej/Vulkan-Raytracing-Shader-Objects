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

    InitializeResources(pRenderContext.get());

    // UI
    // ------------------------------------------------

    auto RecordInterface = [&]()
    {
        ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0, 0));

        if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            if (ImGui::BeginChild("LogSubWindow", ImVec2(0, 300), 1, ImGuiWindowFlags_HorizontalScrollbar))
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

void InitializeResources(RenderContext* pRenderContext)
{
    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, g_ColorAttachment, g_DepthAttachment), "Failed to create the rendering attachments.");
}

void FreeResources(RenderContext* pRenderContext)
{
    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), g_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), g_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), g_ColorAttachment.image, g_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), g_DepthAttachment.image, g_DepthAttachment.imageAllocation);
}
