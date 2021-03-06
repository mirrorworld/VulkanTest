#include "surface.h"
#include "commander.h"
#include "descriptor.h"
#include "device.h"
#include "include.h"
#include "instance.h"
#include "model.h"
#include "renderer.h"
#include "sync.h"
#include "texture.h"
#include "util.h"

static void framebufferResizeCallback(GLFWwindow* window, int width,
                                      int height) {
	// What in fresh hell
	auto app = reinterpret_cast<Instance*>(glfwGetWindowUserPointer(window));
	app->framebufferResized = true;
}

const VkFormat Surface::getFormat() const { return swapChainImageFormat; }

const VkExtent2D Surface::getExtents() const { return swapChainExtent; }

const uint32_t Surface::getSwapChainSize() const {
	return swapChainImageViews.size();
}

void Surface::createWindow(Instance* instance) {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
	glfwSetWindowUserPointer(window, instance);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Surface::createSurface(Instance* instance) {
	if (glfwCreateWindowSurface(instance->instance, window, nullptr,
	                            &surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface!");
	}
}

void Surface::createSwapChain(Instance* instance) {
	SwapChainSupportDetails swapChainSupport =
	    querySwapChainSupport(instance, instance->device->physical);
	VkSurfaceFormatKHR surfaceFormat =
	    chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode =
	    chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
	    imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}
	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	QueueFamilyIndices indices =
	    findQueueFamilies(instance, instance->device->physical);
	uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
	                                 indices.presentFamily.value()};
	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}
	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	if (vkCreateSwapchainKHR(instance->device->logical, &createInfo, nullptr,
	                         &swapChain) != VK_SUCCESS) {
		throw std::runtime_error("failed to create swap chain!");
	}
	vkGetSwapchainImagesKHR(instance->device->logical, swapChain, &imageCount,
	                        nullptr);
	swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(instance->device->logical, swapChain, &imageCount,
	                        swapChainImages.data());
	swapChainImageFormat = surfaceFormat.format;
	swapChainExtent = extent;
}

void Surface::createImageViews(Device* device) {
	swapChainImageViews.resize(swapChainImages.size());
	for (uint32_t i = 0; i < swapChainImages.size(); i++) {
		swapChainImageViews[i] =
		    createImageView(device, swapChainImages[i], swapChainImageFormat,
		                    VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}

void Surface::destroyWindow() { glfwDestroyWindow(window); }

void Surface::destroySurface(Instance* instance) {
	vkDestroySurfaceKHR(instance->instance, surface, nullptr);
}

void Surface::destroySwapChain(Device* device) {
	vkDestroySwapchainKHR(device->logical, swapChain, nullptr);
}

void Surface::destroyImageViews(Device* device) {
	uint32_t swapChainSize = swapChainImages.size();
	for (size_t i = 0; i < swapChainSize; i++) {
		vkDestroyImageView(device->logical, swapChainImageViews[i], nullptr);
	}
}

VkSurfaceFormatKHR Surface::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
		    availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}
	return availableFormats[0];
}

VkPresentModeKHR Surface::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D
Surface::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != UINT32_MAX) {
		return capabilities.currentExtent;
	} else {
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		VkExtent2D actualExtent = {static_cast<uint32_t>(width),
		                           static_cast<uint32_t>(height)};
		actualExtent.width = std::max(
		    capabilities.minImageExtent.width,
		    std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(
		    capabilities.minImageExtent.height,
		    std::min(capabilities.maxImageExtent.height, actualExtent.height));
		return actualExtent;
	}
}
