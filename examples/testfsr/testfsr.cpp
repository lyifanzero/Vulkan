/*
* Vulkan Example - Taking screenshots
* 
* This sample shows how to get the conents of the swapchain (render output) and store them to disk (see saveScreenshot)
*
* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#include "ffx_api/ffx_api.hpp"
#include "ffx_api/ffx_framegeneration.hpp"
#include "ffx_api/vk/ffx_api_vk.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

class VulkanExample : public VulkanExampleBase
{
public:
	bool loadCompressedResource = false;
	//
	VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures = {};

	ffxContext m_FrameGenContext;
	ffxConfigureDescFrameGeneration m_FrameGenerationConfig{};
	uint64_t m_FrameID = 0;
	bool resourceLoaded = false;
	bool descriptorSetUpdated = false;

	vks::Texture2D loadColorTexture;
	vks::Texture2D loadDepthTexture;
	vks::Texture2D loadMotionVectors;

	//
	struct UniformData {
		glm::vec4 viewProjection[5];
		glm::vec4 prevViewProjection[5];
	} uniformData;
	vks::Buffer uniformBuffer;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };

	// Framebuffers holding the attachments
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
		VkImageCreateInfo createInfo;
	};
	struct FrameBuffer {
		VkFramebuffer frameBuffer;
		FrameBufferAttachment mv;
		FrameBufferAttachment color;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
	} convertMVFrameBuf{};
	
	VkSampler mvSampler{ VK_NULL_HANDLE };

	VkCommandBuffer convertMVCmdBuffer{ VK_NULL_HANDLE };
	VkCommandBuffer fsrPrepCmd{ VK_NULL_HANDLE };

	VkSemaphore convertMVSemaphore{ VK_NULL_HANDLE };

	bool screenshotSaved{ false };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Saving framebuffer to screenshot";
		camera.type = Camera::CameraType::lookat;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-25.0f, 23.75f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -3.0f));
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			uniformBuffer.destroy();

			loadColorTexture.destroy();
			loadDepthTexture.destroy();
			loadMotionVectors.destroy();
		}
	}

	void getEnabledFeatures() override {
		timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
		timelineSemaphoreFeatures.pNext = NULL;
		timelineSemaphoreFeatures.timelineSemaphore = VK_TRUE;

		deviceCreatepNextChain = &timelineSemaphoreFeatures;
	}

	void getEnabledExtensions() override {
		// Get physical device supported extension list
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

		// Check if support VK_KHR_get_memory_requirements2
		bool extensionFound = false;
		for (const auto& extension : availableExtensions) {
			if (strcmp(extension.extensionName, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0) {
				extensionFound = true;
				break;
			}
		}

		if (!extensionFound) {
			throw std::runtime_error("VK_KHR_get_memory_requirements2 extension not supported");
		}

		extensionFound = false;
		for (const auto& extension : availableExtensions) {
			if (strcmp(extension.extensionName, VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME) == 0) {
				extensionFound = true;
				break;
			}
		}

		if (!extensionFound) {
			throw std::runtime_error("VK_KHR_get_memory_requirements2 extension not supported");
		}

		enabledDeviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
		enabledDeviceExtensions.push_back(VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
	}

	void createAttachment(VkFormat format, VkImageUsageFlagBits usage, FrameBufferAttachment* attachment) {
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (format >= VK_FORMAT_D16_UNORM_S8_UINT)
				aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

		attachment->createInfo = image;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	void prepareConvertMVFramebuffer() {
		// mv
		createAttachment(
			VK_FORMAT_R16G16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&convertMVFrameBuf.mv);

		createAttachment(
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			&convertMVFrameBuf.color);

		createAttachment(
			VK_FORMAT_D32_SFLOAT,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			&convertMVFrameBuf.depth);

		// Set up separate renderpass
		std::array<VkAttachmentDescription, 3> attachmentDescs = {};

		// Init attachment properties
		for (uint32_t i = 0; i < 3; i++) {
			attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		// Formats
		attachmentDescs[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachmentDescs[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachmentDescs[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// FinalLayout
		attachmentDescs[0].format = convertMVFrameBuf.mv.format;
		attachmentDescs[1].format = convertMVFrameBuf.color.format;
		attachmentDescs[2].format = convertMVFrameBuf.depth.format;

		std::vector<VkAttachmentReference> colorReferences;
		colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
		colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 2;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.pColorAttachments = colorReferences.data();
		subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpass.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for attachment layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &convertMVFrameBuf.renderPass));

		std::array<VkImageView, 3> attachments;
		attachments[0] = convertMVFrameBuf.mv.view;
		attachments[1] = convertMVFrameBuf.color.view;
		attachments[2] = convertMVFrameBuf.depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = {};
		fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbufCreateInfo.pNext = NULL;
		fbufCreateInfo.renderPass = convertMVFrameBuf.renderPass;
		fbufCreateInfo.pAttachments = attachments.data();
		fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbufCreateInfo.width = width;
		fbufCreateInfo.height = height;
		fbufCreateInfo.layers = 1;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &convertMVFrameBuf.frameBuffer));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &mvSampler));
		loadColorTexture.sampler = mvSampler;
		loadColorTexture.descriptor.sampler = mvSampler;
		loadMotionVectors.sampler = mvSampler;
		loadMotionVectors.descriptor.sampler = mvSampler;
		loadDepthTexture.sampler = mvSampler;
		loadDepthTexture.descriptor.sampler = mvSampler;
	}

	void buildCommandBuffers()
	{
		if (convertMVCmdBuffer == VK_NULL_HANDLE) {
			convertMVCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize convertMV and fsr
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &convertMVSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		std::array<VkClearValue, 3> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = convertMVFrameBuf.renderPass;
		renderPassBeginInfo.framebuffer = convertMVFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(convertMVCmdBuffer, &cmdBufInfo));

		vkCmdBeginRenderPass(convertMVCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(convertMVCmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height,	0, 0);
		vkCmdSetScissor(convertMVCmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(convertMVCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
		vkCmdBindPipeline(convertMVCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			
		// This is done by simply drawing a full screen quad
		// The fragment shader then combines the deferred attachments into the final image
		vkCmdDraw(convertMVCmdBuffer, 3, 1, 0, 0);

		vkCmdEndRenderPass(convertMVCmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(convertMVCmdBuffer));

	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 3);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1 : mv texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2 : color texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 2 : depth texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Set
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		updateConvertMVDescriptorSet();
	}

	void updateConvertMVDescriptorSet() {
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor),
			// Binding 1 : MV texture target
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &loadMotionVectors.descriptor),
			// Binding 2: Color texture target
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &loadColorTexture.descriptor),
			// Binding 3: Depth texture target
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &loadDepthTexture.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		descriptorSetUpdated = true;
	}

	void preparePipelines()
	{
		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
			loadShader(getShadersPath() + "testfsr/convertMV.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "testfsr/convertMV.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		// Empty vertex input state, vertices are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
	
		// Separate render pass
		pipelineCI.renderPass = convertMVFrameBuf.renderPass;

		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			//vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
		};

		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepareUniformBuffers()
	{
		// convertMV fragment shader
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(UniformData));
		VK_CHECK_RESULT(uniformBuffer.map());
	}

	void updateUniformBuffers()
	{
		// TODO: Set the vp matrix from file
		//uniformData.viewProjection = camera.matrices.perspective;
		//uniformData.prevViewProjection = camera.matrices.perspective;
		uniformBuffer.copyTo(&uniformData, sizeof(UniformData));

		if (!descriptorSetUpdated) {
			updateConvertMVDescriptorSet();
			descriptorSetUpdated = false;
		}
	}

	std::vector<unsigned char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::binary);
		if (!file) {
			printf("Failed to open file %s\n", filename.c_str());
			return {};
		}

		file.seekg(0, std::ios::end);
		size_t fileSize = file.tellg();
		file.seekg(0, std::ios::beg);

		std::vector<unsigned char> buffer(fileSize);
		file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
		return buffer;
	}

	void loadColorTextureFromPNG(const std::string& filename) {
		int width, height, channels;
		std::vector<unsigned char> fileData = readFile(filename);
		if (fileData.empty()) {
			return;
		}

		//stbi_set_flip_vertically_on_load(true);
		unsigned char* imageData = stbi_load_from_memory(fileData.data(), fileData.size(), &width, &height, &channels, STBI_rgb_alpha);
		if (!imageData) {
			printf("Failed to load image: %s\n", filename.c_str());
			return;
		}
		loadColorTexture.fromBuffer(imageData, width * height * sizeof(GLuint), VK_FORMAT_R8G8B8A8_UNORM, width, height, vulkanDevice, queue);
		printf("loadColorTextureFromPNG: width = %d, height = %d, channels = %d, texture = %d\n", width, height, channels, loadColorTexture.image);

		stbi_image_free(imageData);
	}

	void loadDepthTextureFromPNG(const std::string& filename) {
		int width, height, channels;
		std::vector<unsigned char> fileData = readFile(filename);
		if (fileData.empty()) {
			return;
		}

		//stbi_set_flip_vertically_on_load(true);
		unsigned char* imageData = stbi_load_from_memory(fileData.data(), fileData.size(), &width, &height, &channels, STBI_rgb_alpha);
		if (!imageData) {
			printf("Failed to load image: %s\n", filename.c_str());
			return;
		}

		std::vector<GLuint> depthData(width * height);
		for (int i = 0; i < width * height; ++i) {
			unsigned char r = imageData[i * 4 + 0];
			unsigned char g = imageData[i * 4 + 1];
			unsigned char b = imageData[i * 4 + 2];
			float depth = r / 255.0f + g / (255.0f * 255.0f) + b / (255.0f * 255.0f * 255.0f);

			// float to 24 bit unsigned int
			depthData[i] = static_cast<uint32_t>(depth * 16777215.0f); // 16777215 = 2^24 - 1

			// Don't need to shift 8 bit
			// depthData[i] = depthData[i] << 8;
		}

		loadDepthTexture.fromBuffer(depthData.data(), depthData.size() * sizeof(GLuint), VK_FORMAT_R32_UINT, width, height, vulkanDevice, queue);
		printf("loadDepthTextureFromPNG: width = %d, height = %d, channels = %d, texture = %d\n", width, height, channels, loadDepthTexture.image);

		stbi_image_free(imageData);
	}

	void loadMVTextureFromPNG(const std::string& filename) {
		int width, height, channels;
		std::vector<unsigned char> fileData = readFile(filename);
		if (fileData.empty()) {
			return;
		}

		//stbi_set_flip_vertically_on_load(true);
		unsigned char* imageData = stbi_load_from_memory(fileData.data(), fileData.size(), &width, &height, &channels, STBI_rgb_alpha);
		if (!imageData) {
			printf("Failed to load image: %s\n", filename.c_str());
			return;
		}

		std::vector<uint16_t> mvData(width * height * 2);
		for (int i = 0; i < width * height; ++i) {
			uint16_t r = imageData[i * 4 + 0];
			uint16_t g = imageData[i * 4 + 1];
			uint16_t b = imageData[i * 4 + 2];
			uint16_t a = imageData[i * 4 + 3];

			mvData[i * 2 + 0] = (r << 8) | g;
			mvData[i * 2 + 1] = (b << 8) | a;
		}

		loadMotionVectors.fromBuffer(mvData.data(), mvData.size() * sizeof(uint16_t), VK_FORMAT_R16G16_UINT, width, height, vulkanDevice, queue, VK_FILTER_NEAREST);
		printf("loadMVTextureFromPNG: width = %d, height = %d, channels = %d, texture = %d\n", width, height, channels, loadMotionVectors.image);

		stbi_image_free(imageData);
	}

	void loadMatrixFromFile(std::vector<float>& matrix, const std::string& filename) {
		std::ifstream inFile(filename, std::ios::binary);
		if (!inFile) {
			printf("Failed to open file %s\n", filename.c_str());
			return;
		}
		matrix.resize(20);
		inFile.read(reinterpret_cast<char*>(matrix.data()), 20 * sizeof(float));
		inFile.close();	
	}

	void loadResource(int frameIndex) {
		if (resourceLoaded) {
			return;
		}
		std::string filePath = R"(E:\dwarping\dwarping_1011_30fps)";
		std::string fileName;
		int index = 102 + frameIndex;
		std::string paddingIndex;
		if (loadCompressedResource) {
			std::stringstream ss;
			ss << std::setw(4) << std::setfill('0') << frameIndex + 2;
			paddingIndex = ss.str();
		}
		else {
			paddingIndex = std::to_string(index);
		}

		fileName = filePath + (loadCompressedResource ? "/color_decode/color_frame" : "/color/color_frame") + paddingIndex + ".png";
		loadColorTextureFromPNG(fileName);

		fileName = filePath + (loadCompressedResource ? "/depth_decode/depth_frame" : "/depth/depth_frame") + paddingIndex + ".png";
		loadDepthTextureFromPNG(fileName);

		fileName = filePath + (loadCompressedResource ? "/mvBackward_decode/mvBackward_frame" : "/mvBackward/mvBackward_frame") + paddingIndex + ".png";
		loadMVTextureFromPNG(fileName);

		fileName = filePath + "/vpMatrix/vpMatrix_frame" + std::to_string(index) + ".bin";
		std::vector<float> matrix;
		loadMatrixFromFile(matrix, fileName);
		uniformData.viewProjection[0] = glm::vec4(matrix[0], matrix[1], matrix[2], matrix[3]);
		uniformData.viewProjection[1] = glm::vec4(matrix[4], matrix[5], matrix[6], matrix[7]);
		uniformData.viewProjection[2] = glm::vec4(matrix[8], matrix[9], matrix[10], matrix[11]);
		uniformData.viewProjection[3] = glm::vec4(matrix[12], matrix[13], matrix[14], matrix[15]);
		uniformData.viewProjection[4] = glm::vec4(matrix[16], matrix[17], matrix[18], matrix[19]);

		fileName = filePath + "/vpMatrix/vpMatrix_frame" + std::to_string(index - 1) + ".bin";
		loadMatrixFromFile(matrix, fileName);
		uniformData.prevViewProjection[0] = glm::vec4(matrix[0], matrix[1], matrix[2], matrix[3]);
		uniformData.prevViewProjection[1] = glm::vec4(matrix[4], matrix[5], matrix[6], matrix[7]);
		uniformData.prevViewProjection[2] = glm::vec4(matrix[8], matrix[9], matrix[10], matrix[11]);
		uniformData.prevViewProjection[3] = glm::vec4(matrix[12], matrix[13], matrix[14], matrix[15]);
		uniformData.prevViewProjection[4] = glm::vec4(matrix[16], matrix[17], matrix[18], matrix[19]);

		vkDeviceWaitIdle(device);

		resourceLoaded = true;
	}

	void prepareFSRContext() {
		ffx::CreateBackendVKDesc backendDesc{};
		backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
		backendDesc.vkDevice = device;
		backendDesc.vkPhysicalDevice = physicalDevice;
		backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
		
		// Create the FrameGen context
		ffx::CreateContextDescFrameGeneration createFg{};
		createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
		createFg.displaySize = { width, height };
		createFg.maxRenderSize = { width, height };
		// The flags is combination of FfxApiCreateContextFramegenerationFlags
		bool depthInverted = true;
		bool enableAsyncCompute = false;
		if (depthInverted) {
			createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
		}
		if (enableAsyncCompute) {
			createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
		}
		// FIXME: Whether need to enable HDR?
		createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
		// Surface format: one of the values from FfxApiSurfaceFormat
		createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;  // Keep this same with type of swapchain backbuffer or create a new one

		ffx::ReturnCode retCode = ffx::CreateContext(m_FrameGenContext, nullptr, createFg, backendDesc);
		// Check if retCode == ffx::ReturnCode::OK

		m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		m_FrameGenerationConfig.frameGenerationEnabled = false;
		m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxDispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};
		m_FrameGenerationConfig.frameGenerationCallbackUserContext = &m_FrameGenContext;
		m_FrameGenerationConfig.presentCallback = nullptr;
		m_FrameGenerationConfig.presentCallbackUserContext = nullptr;
		m_FrameGenerationConfig.swapChain = swapChain.swapChain;
		m_FrameGenerationConfig.frameID = m_FrameID;

		retCode = ffx::Configure(m_FrameGenContext, m_FrameGenerationConfig);
	}

	void executeFSR() {
		//
		if (fsrPrepCmd == VK_NULL_HANDLE) {
			fsrPrepCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VkResult result = vkBeginCommandBuffer(fsrPrepCmd, &cmdBufInfo);
		//
		ffx::DispatchDescFrameGenerationPrepare dispatchFgPrep{};
		dispatchFgPrep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
		dispatchFgPrep.commandList = fsrPrepCmd;
		dispatchFgPrep.depth =  ffxApiGetResourceVK(
			(void*)convertMVFrameBuf.depth.image,
			ffxApiGetImageResourceDescriptionVK(convertMVFrameBuf.depth.image, convertMVFrameBuf.depth.createInfo, 0),
			FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchFgPrep.motionVectors = ffxApiGetResourceVK(
			(void*)convertMVFrameBuf.mv.image,
			ffxApiGetImageResourceDescriptionVK(convertMVFrameBuf.mv.image, convertMVFrameBuf.mv.createInfo, 0),
			FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchFgPrep.flags = 0;
		dispatchFgPrep.jitterOffset.x = 0;
		dispatchFgPrep.jitterOffset.y = 0;
		dispatchFgPrep.motionVectorScale.x = width;
		dispatchFgPrep.motionVectorScale.y = height;
		dispatchFgPrep.frameTimeDelta = 33.3;     // Fixme
		dispatchFgPrep.renderSize.width = width;
		dispatchFgPrep.renderSize.height = height;
		dispatchFgPrep.cameraFovAngleVertical = 1.28700221;
		dispatchFgPrep.cameraFar = 10.0;
		dispatchFgPrep.cameraNear = 2097152.0;
		dispatchFgPrep.viewSpaceToMetersFactor = 0.01f;
		dispatchFgPrep.frameID = m_FrameID;

		bool presentInterpolatedOnly = true;
		bool useCallback = false;
		//
		m_FrameGenerationConfig.frameGenerationEnabled = true;
		m_FrameGenerationConfig.flags = 0;
		//
		dispatchFgPrep.flags = m_FrameGenerationConfig.flags;
		m_FrameGenerationConfig.generationRect.left = 0;
		m_FrameGenerationConfig.generationRect.top = 0;
		m_FrameGenerationConfig.generationRect.width = width;
		m_FrameGenerationConfig.generationRect.height = height;
		if (useCallback) {
			m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t
				{
					return ffxDispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
				};
			m_FrameGenerationConfig.frameGenerationCallbackUserContext = &m_FrameGenContext;
		}
		else {
			m_FrameGenerationConfig.frameGenerationCallback = nullptr;
			m_FrameGenerationConfig.frameGenerationCallbackUserContext = nullptr;
		}
		
		m_FrameGenerationConfig.onlyPresentGenerated = presentInterpolatedOnly;
		m_FrameGenerationConfig.frameID = m_FrameID;

		m_FrameGenerationConfig.swapChain = swapChain.swapChain;

		ffx::ReturnCode retCode = ffx::Configure(m_FrameGenContext, m_FrameGenerationConfig);

		retCode = ffx::Dispatch(m_FrameGenContext, dispatchFgPrep);

		result = vkEndCommandBuffer(fsrPrepCmd);

		//
		if (!useCallback) {
			bool resetFSRFG = false;
			ffxDispatchDescFrameGeneration dispatchFg{};
			dispatchFg.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
			dispatchFg.presentColor = ffxApiGetResourceVK(
				(void*)loadColorTexture.image,
				ffxApiGetImageResourceDescriptionVK(loadColorTexture.image, loadColorTexture.createInfo, 0),
				FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
			dispatchFg.numGeneratedFrames = 1;
			dispatchFg.generationRect.left = 0;
			dispatchFg.generationRect.top = 0;
			dispatchFg.generationRect.width = width;
			dispatchFg.generationRect.height = height;

			ffx::QueryDescFrameGenerationSwapChainInterpolationCommandListVK queryCmdList{};
			queryCmdList.pOutCommandList = &dispatchFg.commandList;
			ffx::Query(m_SwapChainContext, queryCmdList);

			ffx::QueryDescFrameGenerationSwapChainInterpolationTextureVK queryFiTexture{};
			queryFiTexture.pOutTexture = &dispatchFg.outputs[0];
			ffx::Query(m_SwapChainContext, queryFiTexture);

			dispatchFg.frameID = m_FrameID;
			dispatchFg.reset = resetFSRFG;

			retCode = ffx::Dispatch(m_FrameGenContext, dispatchFg);
		}
	}

	void destroyFSR() {
		ffx::DestroyContext(m_FrameGenContext, nullptr);
	}

	// Take a screenshot from the current swapchain image
	// This is done using a blit from the swapchain image to a linear image whose memory content is then saved as a ppm image
	// Getting the image date directly from a swapchain image wouldn't work as they're usually stored in an implementation dependent optimal tiling format
	// Note: This requires the swapchain images to be created with the VK_IMAGE_USAGE_TRANSFER_SRC_BIT flag (see VulkanSwapChain::create)
	void saveScreenshot(const char *filename)
	{
		screenshotSaved = false;
		bool supportsBlit = true;

		// Check blit support for source and destination
		VkFormatProperties formatProps;

		// Check if the device supports blitting from optimal images (the swapchain images are in optimal format)
		vkGetPhysicalDeviceFormatProperties(physicalDevice, swapChain.colorFormat, &formatProps);
		if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
			std::cerr << "Device does not support blitting from optimal tiled images, using copy instead of blit!" << std::endl;
			supportsBlit = false;
		}

		// Check if the device supports blitting to linear images
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProps);
		if (!(formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
			std::cerr << "Device does not support blitting to linear tiled images, using copy instead of blit!" << std::endl;
			supportsBlit = false;
		}

		// Source for the copy is the last rendered swapchain image
		VkImage srcImage = swapChain.images[currentBuffer];

		// Create the linear tiled destination image to copy to and to read the memory from
		VkImageCreateInfo imageCreateCI(vks::initializers::imageCreateInfo());
		imageCreateCI.imageType = VK_IMAGE_TYPE_2D;
		// Note that vkCmdBlitImage (if supported) will also do format conversions if the swapchain color format would differ
		imageCreateCI.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCreateCI.extent.width = width;
		imageCreateCI.extent.height = height;
		imageCreateCI.extent.depth = 1;
		imageCreateCI.arrayLayers = 1;
		imageCreateCI.mipLevels = 1;
		imageCreateCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateCI.tiling = VK_IMAGE_TILING_LINEAR;
		imageCreateCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		// Create the image
		VkImage dstImage;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateCI, nullptr, &dstImage));
		// Create memory to back up the image
		VkMemoryRequirements memRequirements;
		VkMemoryAllocateInfo memAllocInfo(vks::initializers::memoryAllocateInfo());
		VkDeviceMemory dstImageMemory;
		vkGetImageMemoryRequirements(device, dstImage, &memRequirements);
		memAllocInfo.allocationSize = memRequirements.size;
		// Memory must be host visible to copy from
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &dstImageMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, dstImage, dstImageMemory, 0));

		// Do the actual blit from the swapchain image to our host visible destination image
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Transition destination image to transfer destination layout
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			dstImage,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// Transition swapchain image from present to transfer source layout
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			srcImage,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
		if (supportsBlit)
		{
			// Define the region to blit (we will blit the whole swapchain image)
			VkOffset3D blitSize;
			blitSize.x = width;
			blitSize.y = height;
			blitSize.z = 1;
			VkImageBlit imageBlitRegion{};
			imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.srcSubresource.layerCount = 1;
			imageBlitRegion.srcOffsets[1] = blitSize;
			imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlitRegion.dstSubresource.layerCount = 1;
			imageBlitRegion.dstOffsets[1] = blitSize;

			// Issue the blit command
			vkCmdBlitImage(
				copyCmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlitRegion,
				VK_FILTER_NEAREST);
		}
		else
		{
			// Otherwise use image copy (requires us to manually flip components)
			VkImageCopy imageCopyRegion{};
			imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.srcSubresource.layerCount = 1;
			imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageCopyRegion.dstSubresource.layerCount = 1;
			imageCopyRegion.extent.width = width;
			imageCopyRegion.extent.height = height;
			imageCopyRegion.extent.depth = 1;

			// Issue the copy command
			vkCmdCopyImage(
				copyCmd,
				srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageCopyRegion);
		}

		// Transition destination image to general layout, which is the required layout for mapping the image memory later on
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			dstImage,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// Transition back the swap chain image after the blit is done
		vks::tools::insertImageMemoryBarrier(
			copyCmd,
			srcImage,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		vulkanDevice->flushCommandBuffer(copyCmd, queue);

		// Get layout of the image (including row pitch)
		VkImageSubresource subResource { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
		VkSubresourceLayout subResourceLayout;
		vkGetImageSubresourceLayout(device, dstImage, &subResource, &subResourceLayout);

		// Map image memory so we can start copying from it
		const char* data;
		vkMapMemory(device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, (void**)&data);
		data += subResourceLayout.offset;

		std::ofstream file(filename, std::ios::out | std::ios::binary);

		// ppm header
		file << "P6\n" << width << "\n" << height << "\n" << 255 << "\n";

		// If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
		bool colorSwizzle = false;
		// Check if source is BGR
		// Note: Not complete, only contains most common and basic BGR surface formats for demonstration purposes
		if (!supportsBlit)
		{
			std::vector<VkFormat> formatsBGR = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM };
			colorSwizzle = (std::find(formatsBGR.begin(), formatsBGR.end(), swapChain.colorFormat) != formatsBGR.end());
		}

		// ppm binary pixel data
		for (uint32_t y = 0; y < height; y++)
		{
			unsigned int *row = (unsigned int*)data;
			for (uint32_t x = 0; x < width; x++)
			{
				if (colorSwizzle)
				{
					file.write((char*)row+2, 1);
					file.write((char*)row+1, 1);
					file.write((char*)row, 1);
				}
				else
				{
					file.write((char*)row, 3);
				}
				row++;
			}
			data += subResourceLayout.rowPitch;
		}
		file.close();

		std::cout << "Screenshot saved to disk" << std::endl;

		// Clean up resources
		vkUnmapMemory(device, dstImageMemory);
		vkFreeMemory(device, dstImageMemory, nullptr);
		vkDestroyImage(device, dstImage, nullptr);

		screenshotSaved = true;
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// load textures
		loadResource(m_FrameID);
		prepareConvertMVFramebuffer();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();

		// fsr create context
		prepareFSRContext();

		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		loadResource(m_FrameID);

		executeFSR();

		VkCommandBuffer cbf[2] = { convertMVCmdBuffer, fsrPrepCmd };

		// convert mv
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &convertMVCmdBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		submitInfo.pSignalSemaphores = &convertMVSemaphore;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		// fsr prep
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &fsrPrepCmd;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &convertMVSemaphore;
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBuffers();
		draw();
		m_FrameID++;
		resourceLoaded = false;
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Functions")) {
			if (overlay->button("Take screenshot")) {
				saveScreenshot("screenshot.ppm");
			}
			if (screenshotSaved) {
				overlay->text("Screenshot saved as screenshot.ppm");
			}
		}
	}

};

VulkanExample* vulkanExample; LRESULT __stdcall WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (vulkanExample != 0) {
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
	} 
	return (DefWindowProcA(hWnd, uMsg, wParam, lParam));
} 
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int) {
	for (int32_t i = 0; i < (*__p___argc()); i++) {
		VulkanExample::args.push_back((*__p___argv())[i]);
	}; 
	vulkanExample = new VulkanExample(); 
	vulkanExample->initVulkan(); 
	vulkanExample->setupWindow(hInstance, WndProc); 
	vulkanExample->prepare(); 
	vulkanExample->renderLoop(); 
	delete(vulkanExample); 
	return 0;
}