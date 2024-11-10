/*
 * Vulkan device class
 *
 * Encapsulates a physical Vulkan device and its logical representation
 *
 * Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include "VulkanBuffer.h"
#include "VulkanTools.h"
#include "vulkan/vulkan.h"
#include <algorithm>
#include <assert.h>
#include <exception>

#include <ffx_api/vk/ffx_api_vk.hpp>

namespace vks
{
struct VulkanDevice
{
	/** @brief Physical device representation */
	VkPhysicalDevice physicalDevice;
	/** @brief Logical device representation (application's view of the device) */
	VkDevice logicalDevice;
	/** @brief Properties of the physical device including limits that the application can check against */
	VkPhysicalDeviceProperties properties;
	/** @brief Features of the physical device that an application can use to check if a feature is supported */
	VkPhysicalDeviceFeatures features;
	/** @brief Features that have been enabled for use on the physical device */
	VkPhysicalDeviceFeatures enabledFeatures;
	/** @brief Memory types and heaps of the physical device */
	VkPhysicalDeviceMemoryProperties memoryProperties;
	/** @brief Queue family properties of the physical device */
	std::vector<VkQueueFamilyProperties> queueFamilyProperties;
	/** @brief List of extensions supported by the device */
	std::vector<std::string> supportedExtensions;
	/** @brief Default command pool for the graphics queue family index */
	VkCommandPool commandPool = VK_NULL_HANDLE;
	/** @brief Contains queue family indices */
	struct
	{
		uint32_t graphics;
		uint32_t compute;
		uint32_t transfer;
	} queueFamilyIndices;
	operator VkDevice() const
	{
		return logicalDevice;
	};
	explicit VulkanDevice(VkPhysicalDevice physicalDevice);
	~VulkanDevice();
	uint32_t        getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr) const;
	uint32_t        getQueueFamilyIndex(VkQueueFlags queueFlags) const;
	VkResult        createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char *> enabledExtensions, void *pNextChain, bool useSwapChain = true, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
	VkResult        createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data = nullptr);
	VkResult        createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer *buffer, VkDeviceSize size, void *data = nullptr);
	void            copyBuffer(vks::Buffer *src, vks::Buffer *dst, VkQueue queue, VkBufferCopy *copyRegion = nullptr);
	VkCommandPool   createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin = false);
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin = false);
	void            flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free = true);
	void            flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true);
	bool            extensionSupported(std::string extension);
	VkFormat        getSupportedDepthFormat(bool checkSamplingSupport);

	// TEST FSR
	void            setSwapchainMethodsAndContext(PFN_vkGetSwapchainImagesKHR    getSwapchainImagesKHR,
													PFN_vkAcquireNextImageKHR    acquireNextImageKHR,
													PFN_vkQueuePresentKHR        queuePresentKHR,
													PFN_vkCreateSwapchainFFXAPI  createSwapchainFFXAPI,
													PFN_vkDestroySwapchainFFXAPI destroySwapchainFFXAPI,
													PFN_getLastPresentCountFFXAPI      getLastPresentCountFFXAPI,
													void*                        pSwapchainContext);
	// swapchain related functions
	PFN_vkCreateSwapchainFFXAPI   m_vkCreateSwapchainFFXAPI = nullptr;  // When using FFX API
	PFN_vkDestroySwapchainFFXAPI  m_vkDestroySwapchainFFXAPI = nullptr;  // When using FFX API
	PFN_vkGetSwapchainImagesKHR   m_vkGetSwapchainImagesKHR = nullptr;
	PFN_vkAcquireNextImageKHR     m_vkAcquireNextImageKHR = nullptr;
	PFN_vkQueuePresentKHR         m_vkQueuePresentKHR = nullptr;
	PFN_getLastPresentCountFFXAPI m_getLastPresentCountFFXAPI = nullptr;  // When using FFX API
	void* m_pSwapchainContext = nullptr;

	VkResult createSwapchainKHR(const VkSwapchainCreateInfoKHR* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkSwapchainKHR* pSwapchain) const {
		if (m_vkCreateSwapchainFFXAPI != nullptr) {
			return m_vkCreateSwapchainFFXAPI(logicalDevice, pCreateInfo, pAllocator, pSwapchain, m_pSwapchainContext);
		}
		return vkCreateSwapchainKHR(logicalDevice, pCreateInfo, pAllocator, pSwapchain);
	}

	void destroySwapchainKHR(VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) const {
		if (m_vkDestroySwapchainFFXAPI != nullptr) {
			m_vkDestroySwapchainFFXAPI(logicalDevice, swapchain, pAllocator, m_pSwapchainContext);
		}
		return vkDestroySwapchainKHR(logicalDevice, swapchain, pAllocator);
	}

	VkResult getSwapchainImagesKHR(VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) const
	{
		if (m_vkGetSwapchainImagesKHR != nullptr)
			return m_vkGetSwapchainImagesKHR(logicalDevice, swapchain, pSwapchainImageCount, pSwapchainImages);
		return vkGetSwapchainImagesKHR(logicalDevice, swapchain, pSwapchainImageCount, pSwapchainImages);
	}
	VkResult acquireNextImageKHR(VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) const
	{
		if (m_vkAcquireNextImageKHR != nullptr)
			return m_vkAcquireNextImageKHR(logicalDevice, swapchain, timeout, semaphore, fence, pImageIndex);
		return vkAcquireNextImageKHR(logicalDevice, swapchain, timeout, semaphore, fence, pImageIndex);
	}
	VkResult queuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) const
	{
		if (m_vkQueuePresentKHR != nullptr)
			return m_vkQueuePresentKHR(queue, pPresentInfo);
		return vkQueuePresentKHR(queue, pPresentInfo);
	}

};
}        // namespace vks
