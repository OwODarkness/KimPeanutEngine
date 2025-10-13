#include "vulkan_pipeline_manager.h"
#include <array>
#include "log/logger.h"
#include "common/shader.h"
#include "vulkan_enum.h"

namespace kpengine::graphics
{

    PipelineHandle VulkanPipelineManager::CreatePipelineResource(VkDevice logical_device, const PipelineDesc &pipeline_desc, VkRenderPass render_pass)
    {

        PipelineHandle handle = handle_system_.Create();

        if (handle.id == resources_.size())
        {
            resources_.emplace_back();
        }
        else if (handle.id > resources_.size())
        {
            throw std::runtime_error("handle system hand out a invalid handle");
        }

        VulkanPipelineResource &pipeline_resource = resources_[handle.id];

        // shader_stage
        std::vector<VkPipelineShaderStageCreateInfo> stages{};
        VkShaderModule vert_shader_module = VK_NULL_HANDLE;
        VkShaderModule frag_shader_module = VK_NULL_HANDLE;
        VkShaderModule geom_shader_module = VK_NULL_HANDLE;

        if (pipeline_desc.vert_shader)
        {
            CreateShaderModule(logical_device, pipeline_desc.vert_shader->GetCode(), pipeline_desc.vert_shader->GetCodeSize(), vert_shader_module);

            VkPipelineShaderStageCreateInfo shader_stage_info{};
            shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.module = vert_shader_module;
            shader_stage_info.pName = "main";
            shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages.push_back(shader_stage_info);
        }

        if (pipeline_desc.frag_shader)
        {
            CreateShaderModule(logical_device, pipeline_desc.frag_shader->GetCode(), pipeline_desc.frag_shader->GetCodeSize(), frag_shader_module);

            VkPipelineShaderStageCreateInfo shader_stage_info{};
            shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.module = frag_shader_module;
            shader_stage_info.pName = "main";
            shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages.push_back(shader_stage_info);
        }

        if (pipeline_desc.geom_shader)
        {
            CreateShaderModule(logical_device, pipeline_desc.geom_shader->GetCode(), pipeline_desc.geom_shader->GetCodeSize(), geom_shader_module);

            VkPipelineShaderStageCreateInfo shader_stage_info{};
            shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.module = geom_shader_module;
            shader_stage_info.pName = "main";
            shader_stage_info.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
            stages.push_back(shader_stage_info);
        }

        try
        {
            // vertex stage
            std::vector<VkVertexInputBindingDescription> bindings{};
            for (const auto &bind_desc : pipeline_desc.binding_descs)
            {
                VkVertexInputRate input_rate = bind_desc.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
                bindings.push_back({bind_desc.binding, bind_desc.stride, input_rate});
            }

            std::vector<VkVertexInputAttributeDescription> attributes{};
            for (const auto &attri_desc : pipeline_desc.attri_descs)
            {
                attributes.push_back({attri_desc.location, attri_desc.binding, ConvertToVulkanVertexFormat(attri_desc.format), attri_desc.offset});
            }

            //set vertex state
            VkPipelineVertexInputStateCreateInfo vertex_state_create_info{};
            vertex_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_state_create_info.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
            vertex_state_create_info.pVertexBindingDescriptions = bindings.data();
            vertex_state_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            vertex_state_create_info.pVertexAttributeDescriptions = attributes.data();

            // set assembly state
            VkPipelineInputAssemblyStateCreateInfo assembly_state_create_info{};
            assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly_state_create_info.topology = ConvertToVulkanPrimitiveTopology(pipeline_desc.primitive_topology_type);
            assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

            // set viewport state
            VkPipelineViewportStateCreateInfo viewport_state_create_info{};
            viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state_create_info.viewportCount = 1;
            viewport_state_create_info.scissorCount = 1;

            // set raster state
            VkPipelineRasterizationStateCreateInfo raster_state_create_info{};
            raster_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster_state_create_info.frontFace = ConvertToVulkanFrontFace(pipeline_desc.raster_state.front_face);
            raster_state_create_info.depthClampEnable = pipeline_desc.raster_state.depth_clamp_enabled;
            raster_state_create_info.polygonMode = ConvertToVulkanPolygonMode(pipeline_desc.raster_state.polygon_mode);
            raster_state_create_info.rasterizerDiscardEnable = pipeline_desc.raster_state.rasterizer_discard_enabled;
            raster_state_create_info.lineWidth = pipeline_desc.raster_state.line_width;
            raster_state_create_info.cullMode = ConvertToVulkanCullMode(pipeline_desc.raster_state.cull_mode);
            raster_state_create_info.depthBiasEnable = pipeline_desc.raster_state.depth_bias_enabled;
            raster_state_create_info.depthBiasConstantFactor = pipeline_desc.raster_state.depth_bias_constant;
            raster_state_create_info.depthBiasClamp = 0.f;
            raster_state_create_info.depthBiasSlopeFactor = pipeline_desc.raster_state.depth_bias_slope;

            // set dynamic state
            // TODO: use pipeline desc control the dynamic
            std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state_create_info{};
            dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_state_create_info.pDynamicStates = dynamic_states.data();

            // set multi sample
            VkPipelineMultisampleStateCreateInfo multisample_state_create_info{};
            multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample_state_create_info.sampleShadingEnable = pipeline_desc.multisample_state.sample_shading_enabled;
            multisample_state_create_info.rasterizationSamples = ConvertToVulkanSampleCount(pipeline_desc.multisample_state.rasterization_samples);
            multisample_state_create_info.minSampleShading = pipeline_desc.multisample_state.min_sample_shading;
            multisample_state_create_info.pSampleMask = nullptr;
            multisample_state_create_info.alphaToCoverageEnable = pipeline_desc.multisample_state.alpha_to_coverage_enable;
            multisample_state_create_info.alphaToOneEnable = pipeline_desc.multisample_state.alpha_to_one_enable;

            //set color blend attachment
            VkPipelineColorBlendAttachmentState colorblend_attachment{};
            colorblend_attachment.colorWriteMask = pipeline_desc.blend_attachment_state.color_write_mask;
            colorblend_attachment.blendEnable = pipeline_desc.blend_attachment_state.blend_enabled;
            colorblend_attachment.srcColorBlendFactor = ConvertToVulkanBlendFactor(pipeline_desc.blend_attachment_state.src_color_blend_factor);
            colorblend_attachment.dstColorBlendFactor = ConvertToVulkanBlendFactor(pipeline_desc.blend_attachment_state.dst_color_blend_factor);
            colorblend_attachment.colorBlendOp = ConvertToVulkanBlendOp(pipeline_desc.blend_attachment_state.color_blend_op);
            colorblend_attachment.srcAlphaBlendFactor = ConvertToVulkanBlendFactor(pipeline_desc.blend_attachment_state.src_alpha_blend_factor);
            colorblend_attachment.dstAlphaBlendFactor = ConvertToVulkanBlendFactor(pipeline_desc.blend_attachment_state.dst_alpha_blend_factor);
            colorblend_attachment.alphaBlendOp = ConvertToVulkanBlendOp(pipeline_desc.blend_attachment_state.alpha_blend_op);

            //set color blend state
            VkPipelineColorBlendStateCreateInfo colorblend_state_create_info{};
            colorblend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorblend_state_create_info.attachmentCount = 1;
            colorblend_state_create_info.pAttachments = &colorblend_attachment;
            colorblend_state_create_info.logicOpEnable = VK_FALSE;
            colorblend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
            colorblend_state_create_info.blendConstants[0] = 0.f;
            colorblend_state_create_info.blendConstants[1] = 0.f;
            colorblend_state_create_info.blendConstants[2] = 0.f;
            colorblend_state_create_info.blendConstants[3] = 0.f;

            //set depth and stencil state
            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = VK_TRUE;
            depth_stencil.depthWriteEnable = VK_TRUE;
            depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            depth_stencil.minDepthBounds = 0.f;
            depth_stencil.maxDepthBounds = 1.f;
            depth_stencil.stencilTestEnable = VK_FALSE;
            depth_stencil.front = {};
            depth_stencil.back = {};

            // set descriptor sets layout
            pipeline_resource.descriptor_set_layouts.resize(pipeline_desc.descriptor_binding_descs.size());
            for (size_t i = 0; i < pipeline_resource.descriptor_set_layouts.size(); i++)
            {
                std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings(pipeline_desc.descriptor_binding_descs[i].size());
                for (size_t j = 0; j < pipeline_desc.descriptor_binding_descs[i].size(); j++)
                {
                    descriptor_set_layout_bindings[j].binding = pipeline_desc.descriptor_binding_descs[i][j].binding;
                    descriptor_set_layout_bindings[j].descriptorCount = pipeline_desc.descriptor_binding_descs[i][j].descriptor_count;
                    descriptor_set_layout_bindings[j].descriptorType = ConvertToVulkanDescriptorType(pipeline_desc.descriptor_binding_descs[i][j].descriptor_type);
                    descriptor_set_layout_bindings[j].stageFlags = ConvertToVulkanShaderStageFlags(pipeline_desc.descriptor_binding_descs[i][j].stage_flag);
                    descriptor_set_layout_bindings[j].pImmutableSamplers = nullptr;
                }
                VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{};
                descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptor_set_layout_create_info.bindingCount = static_cast<uint32_t>(descriptor_set_layout_bindings.size());
                descriptor_set_layout_create_info.pBindings = descriptor_set_layout_bindings.data();

                if (vkCreateDescriptorSetLayout(logical_device, &descriptor_set_layout_create_info, nullptr, &pipeline_resource.descriptor_set_layouts[i]) != VK_SUCCESS)
                {
                    KP_LOG("VulkanPipelineManagerLog", LOG_LEVEL_ERROR, "Failed to create DescriptorSetLayout");
                    throw std::runtime_error("Failed to create DescriptorSetLayout");
                }
            }
            // set pipeline_layout
            VkPipelineLayoutCreateInfo layout_create_info{};
            layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_create_info.setLayoutCount = static_cast<uint32_t>(pipeline_resource.descriptor_set_layouts.size());
            layout_create_info.pSetLayouts = pipeline_resource.descriptor_set_layouts.data();
            layout_create_info.pushConstantRangeCount = 0;
            layout_create_info.pPushConstantRanges = nullptr;

            if (vkCreatePipelineLayout(logical_device, &layout_create_info, nullptr, &pipeline_resource.layout) != VK_SUCCESS)
            {
                KP_LOG("VulkanPipelineManagerLog", LOG_LEVEL_ERROR, "Failed to create pipeline layout");
                throw std::runtime_error("Failed to create pipeline layout");
            }

            //create graphics pipeline
            VkGraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_create_info.stageCount = static_cast<uint32_t>(stages.size());
            pipeline_create_info.pStages = stages.data();
            pipeline_create_info.pVertexInputState = &vertex_state_create_info;
            pipeline_create_info.pInputAssemblyState = &assembly_state_create_info;
            pipeline_create_info.pViewportState = &viewport_state_create_info;
            pipeline_create_info.pRasterizationState = &raster_state_create_info;
            pipeline_create_info.pMultisampleState = &multisample_state_create_info;
            pipeline_create_info.pDepthStencilState = nullptr;
            pipeline_create_info.pColorBlendState = &colorblend_state_create_info;
            pipeline_create_info.pDynamicState = &dynamic_state_create_info;
            pipeline_create_info.layout = pipeline_resource.layout;
            pipeline_create_info.renderPass = render_pass;
            pipeline_create_info.subpass = 0;
            pipeline_create_info.pDepthStencilState = &depth_stencil;
            pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
            pipeline_create_info.basePipelineIndex = -1;

            if (vkCreateGraphicsPipelines(logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline_resource.pipeline) != VK_SUCCESS)
            {
                KP_LOG("VulkanPipelineManagerLog", LOG_LEVEL_ERROR, "Failed to create pipeline");
                throw std::runtime_error("Failed to create pipeline");
            }
        }
        catch (std::exception e)
        {
            if (vert_shader_module != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(logical_device, vert_shader_module, nullptr);
                vert_shader_module = VK_NULL_HANDLE; // optional, avoid dangling handle
            }

            if (frag_shader_module != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(logical_device, frag_shader_module, nullptr);
                frag_shader_module = VK_NULL_HANDLE;
            }

            if (geom_shader_module != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(logical_device, geom_shader_module, nullptr);
                geom_shader_module = VK_NULL_HANDLE;
            }
            throw std::runtime_error("Failed to create pipeline");
        }

        if (vert_shader_module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(logical_device, vert_shader_module, nullptr);
            vert_shader_module = VK_NULL_HANDLE; // optional, avoid dangling handle
        }

        if (frag_shader_module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(logical_device, frag_shader_module, nullptr);
            frag_shader_module = VK_NULL_HANDLE;
        }

        if (geom_shader_module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(logical_device, geom_shader_module, nullptr);
            geom_shader_module = VK_NULL_HANDLE;
        }

        return handle;
    }

    void VulkanPipelineManager::DestroyPipelineResource(VkDevice logical_device, PipelineHandle handle)
    {
        VulkanPipelineResource *pipeline_resource = GetPipelineResource(handle);

        for (size_t i = 0; i < pipeline_resource->descriptor_set_layouts.size(); i++)
        {
            vkDestroyDescriptorSetLayout(logical_device, pipeline_resource->descriptor_set_layouts[i], nullptr);
        }
        vkDestroyPipeline(logical_device, pipeline_resource->pipeline, nullptr);
        vkDestroyPipelineLayout(logical_device, pipeline_resource->layout, nullptr);

        pipeline_resource->pipeline = VK_NULL_HANDLE;
        pipeline_resource->layout = VK_NULL_HANDLE;
        handle_system_.Destroy(handle);
    }

    VulkanPipelineResource *VulkanPipelineManager::GetPipelineResource(PipelineHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);

        if (index >= resources_.size())
        {
            KP_LOG("VulkanPipelineManagerLog", LOG_LEVEL_ERROR, "Failed to find pipeline resource");
            throw std::runtime_error("Failed to find pipeline resource");
        }

        return &resources_[index];
    }

    void VulkanPipelineManager::CreateShaderModule(VkDevice logicial_device, const void *data, size_t size, VkShaderModule &shader_module)
    {
        VkShaderModuleCreateInfo shader_module_create_info{};
        shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_module_create_info.codeSize = static_cast<uint32_t>(size);
        shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(data);

        if (vkCreateShaderModule(logicial_device, &shader_module_create_info, nullptr, &shader_module) != VK_SUCCESS)
        {
            KP_LOG("VulkanPipelineManagerLog", LOG_LEVEL_ERROR, "Failed to create shader module");
            throw std::runtime_error("Failed to create shader module");
        }
    }
}