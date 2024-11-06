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

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_framegeneration.h"
#include "ffx_api/vk/ffx_api_vk.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Vertex layout for this example
struct Vertex {
	float pos[3];
	float uv[2];
	float normal[3];
};

class VulkanExample : public VulkanExampleBase
{
public:
	//
	ffxContext m_FrameGenContext;
	ffxConfigureDescFrameGeneration m_FrameGenerationConfig{};
	uint64_t m_FrameID = 0;

	vks::Texture2D loadColorTexture;
	vks::Texture2D loadDepthTexture;
	vks::Texture2D loadMotionVectors;

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount{ 0 };

	//
	struct UniformData {
		glm::mat4 viewProjection;
		glm::mat4 prevViewProjection;
	} uniformData;
	vks::Buffer uniformBuffer;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };

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

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height,	0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			// The sample uses a combined image + sampler descriptor to sample the texture in the fragment shader
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Set
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		
		// Setup a descriptor image info for the current texture to be used as a combined image sampler
		VkDescriptorImageInfo textureDescriptor;
		// The image's view (images are never directly accessed by the shader, but rather through views defining subresources)
		textureDescriptor.imageView = loadColorTexture.view;
		// The sampler (Telling the pipeline how to sample the texture, including repeat, border, etc.)
		textureDescriptor.sampler = loadColorTexture.sampler;
		// The current layout of the image(Note: Should always fit the actual use, e.g.shader read)
		textureDescriptor.imageLayout = loadColorTexture.imageLayout;

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor),
			// Binding 1 : Fragment shader texture sampler
			//	Fragment shader: layout (binding = 1) uniform sampler2D samplerColor;
			vks::initializers::writeDescriptorSet(descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,		// The descriptor set will use a combined image sampler (as opposed to splitting image and sampler)
				1,												// Shader binding point 1
				&textureDescriptor)								// Pointer to the descriptor image for our texture
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
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
			loadShader(getShadersPath() + "texture/texture.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "texture/texture.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};

		// Vertex input state
		std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)),
		};
		VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

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
		pipelineCI.pVertexInputState = &vertexInputState;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(UniformData));
		VK_CHECK_RESULT(uniformBuffer.map());
	}

	void updateUniformBuffers()
	{
		// TODO: Set the vp matrix from file
		uniformData.viewProjection = camera.matrices.perspective;
		uniformData.prevViewProjection = camera.matrices.perspective;
		uniformBuffer.copyTo(&uniformData, sizeof(UniformData));
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

		stbi_set_flip_vertically_on_load(true);
		unsigned char* imageData = stbi_load_from_memory(fileData.data(), fileData.size(), &width, &height, &channels, STBI_rgb_alpha);
		if (!imageData) {
			printf("Failed to load image: %s\n", filename.c_str());
			return;
		}
		loadColorTexture.fromBuffer(imageData, width * height * channels, VK_FORMAT_R8G8B8A8_UNORM, width, height, vulkanDevice, queue);
		printf("loadColorTextureFromPNG: width = %d, height = %d, channels = %d, texture = %d\n", width, height, channels, loadColorTexture.image);

		stbi_image_free(imageData);
	}

	void loadDepthTextureFromPNG(const std::string& filename) {
		int width, height, channels;
		std::vector<unsigned char> fileData = readFile(filename);
		if (fileData.empty()) {
			return;
		}

		stbi_set_flip_vertically_on_load(true);
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

		stbi_set_flip_vertically_on_load(true);
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

		loadMotionVectors.fromBuffer(mvData.data(), mvData.size() * sizeof(uint16_t), VK_FORMAT_R16G16_UINT, width, height, vulkanDevice, queue);
		printf("loadMVTextureFromPNG: width = %d, height = %d, channels = %d, texture = %d\n", width, height, channels, loadMotionVectors.image);

		stbi_image_free(imageData);
	}

	void loadResource(int frameIndex) {
		std::string filePath = R"(E:\dwarping\dwarping_1011_30fps)";
		std::string fileName;
		int index = 101 + frameIndex;
		fileName = filePath + "/color/color_frame" + std::to_string(index) + ".png";
		loadColorTextureFromPNG(fileName);
		fileName = filePath + "/depth/depth_frame" + std::to_string(index) + ".png";
		loadDepthTextureFromPNG(fileName);
		fileName = filePath + "/mvBackward/mvBackward_frame" + std::to_string(index) + ".png";
		loadMVTextureFromPNG(fileName);
		// TODO: Load vp matrix
	}

	// Creates a vertex and index buffer for a quad made of two triangles
	// This is used to display the texture on
	void generateQuad()
	{
		// Setup vertices for a single uv-mapped quad made from two triangles
		std::vector<Vertex> vertices =
		{
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f },{ 0.0f, 0.0f, 1.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f },{ 0.0f, 0.0f, 1.0f } },
			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } },
			{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } }
		};

		// Setup indices
		std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
		indexCount = static_cast<uint32_t>(indices.size());

		// Create buffers and upload data to the GPU
		struct StagingBuffers {
			vks::Buffer vertices;
			vks::Buffer indices;
		} stagingBuffers;

		// Host visible source buffers (staging)
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffers.vertices, vertices.size() * sizeof(Vertex), vertices.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffers.indices, indices.size() * sizeof(uint32_t), indices.data()));

		// Device local destination buffers
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertexBuffer, vertices.size() * sizeof(Vertex)));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indexBuffer, indices.size() * sizeof(uint32_t)));

		// Copy from host do device
		vulkanDevice->copyBuffer(&stagingBuffers.vertices, &vertexBuffer, queue);
		vulkanDevice->copyBuffer(&stagingBuffers.indices, &indexBuffer, queue);

		// Clean up
		stagingBuffers.vertices.destroy();
		stagingBuffers.indices.destroy();
	}

	void prepareFSRContext() {
		ffxCreateBackendVKDesc backendDesc{};
		backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
		backendDesc.vkDevice = device;
		backendDesc.vkPhysicalDevice = physicalDevice;
		backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
		
		// Create the FrameGen context
		ffxCreateContextDescFrameGeneration createFg{};
		createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
		createFg.displaySize = { width, height };
		createFg.maxRenderSize = { width, height };
		// The flags is combination of FfxApiCreateContextFramegenerationFlags
		bool depthInverted = false;
		bool enableAsyncCompute = false;
		if (depthInverted) {
			createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
		}
		if (enableAsyncCompute) {
			createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
		}
		// FIXME: Whether need to enable HDR?
		createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
		// Surface format: one of the values from FfxApiSurfaceFormat
		createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;  // Keep this same with type of swapchain backbuffer or create a new one

		// FIXME: how to set backendDesc, now crash here
		//createFg.header.pNext = &backendDesc.header;
		ffxReturnCode_t retCode = ffxCreateContext(&m_FrameGenContext, &createFg.header, nullptr);
		// Check if retCode == ffx::ReturnCode::OK

		m_FrameGenerationConfig.frameGenerationEnabled = false;
		m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxDispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};
		m_FrameGenerationConfig.frameGenerationCallbackUserContext = &m_FrameGenContext;
		m_FrameGenerationConfig.presentCallback = nullptr;
		m_FrameGenerationConfig.presentCallbackUserContext = nullptr;
		m_FrameGenerationConfig.swapChain = &swapChain;
		m_FrameGenerationConfig.frameID = m_FrameID;

		retCode = ffxConfigure(&m_FrameGenContext, &m_FrameGenerationConfig.header);
	}

	void executeFSR(VkCommandBuffer prepCmd, VkCommandBuffer fgCmd) {
		//
		ffxDispatchDescFrameGenerationPrepare dispatchFgPrep{};
		dispatchFgPrep.commandList = prepCmd;
		dispatchFgPrep.depth = {&loadDepthTexture};
		dispatchFgPrep.motionVectors = {&loadMotionVectors};
		dispatchFgPrep.flags = 0;
		dispatchFgPrep.jitterOffset.x = 0;
		dispatchFgPrep.jitterOffset.y = 0;
		dispatchFgPrep.motionVectorScale.x = width;
		dispatchFgPrep.motionVectorScale.y = height;
		dispatchFgPrep.frameTimeDelta = 33.3;
		dispatchFgPrep.renderSize.width = width;
		dispatchFgPrep.renderSize.height = height;
		dispatchFgPrep.cameraFovAngleVertical = 45;
		dispatchFgPrep.cameraFar = 0.0;
		dispatchFgPrep.cameraNear = 2097152.0;
		dispatchFgPrep.viewSpaceToMetersFactor = 0.f;
		dispatchFgPrep.frameID = m_FrameID;

		bool presentInterpolatedOnly = false;
		//
		m_FrameGenerationConfig.frameGenerationEnabled = true;
		m_FrameGenerationConfig.flags = 0;
		dispatchFgPrep.flags = m_FrameGenerationConfig.flags;
		m_FrameGenerationConfig.generationRect.left = 0;
		m_FrameGenerationConfig.generationRect.top = 0;
		m_FrameGenerationConfig.generationRect.width = width;
		m_FrameGenerationConfig.generationRect.height = height;
		m_FrameGenerationConfig.frameGenerationCallback = nullptr;
		m_FrameGenerationConfig.frameGenerationCallbackUserContext = nullptr;
		m_FrameGenerationConfig.onlyPresentGenerated = presentInterpolatedOnly;
		m_FrameGenerationConfig.frameID = m_FrameID;

		m_FrameGenerationConfig.swapChain = &swapChain;

		ffxReturnCode_t retCode = ffxConfigure(&m_FrameGenContext, &m_FrameGenerationConfig.header);

		retCode = ffxDispatch(&m_FrameGenContext, &dispatchFgPrep.header);

		//
		bool resetFSRFG = false;
		ffxDispatchDescFrameGeneration dispatchFg{};
		dispatchFg.presentColor = {&loadColorTexture};
		dispatchFg.numGeneratedFrames = 1;
		dispatchFg.generationRect.left = 0;
		dispatchFg.generationRect.top = 0;
		dispatchFg.generationRect.width = width;
		dispatchFg.generationRect.height = height;
		dispatchFg.commandList = fgCmd;
		dispatchFg.outputs[0] = { swapChain.images[currentBuffer] };
		dispatchFg.frameID = m_FrameID;
		dispatchFg.reset = resetFSRFG;

		retCode = ffxDispatch(&m_FrameGenContext, &dispatchFg.header);
	}

	void destroyFSR() {
		ffxDestroyContext(&m_FrameGenContext, nullptr);
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
		generateQuad();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();

		// fsr create context
		//prepareFSRContext();

		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		/*VkCommandBuffer fsrCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		executeFSR(fsrCmd, drawCmdBuffers[currentBuffer]);*/

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBuffers();
		draw();
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