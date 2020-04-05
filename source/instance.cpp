
#include "instance.h"


void Device::pickPhysicalDevice(Instance instance) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance.instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> candidateDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance.instance, &deviceCount, candidateDevices.data());
    for (const auto& candidateDevice : candidateDevices) {
        if (isDeviceSuitable(candidateDevice)) {
            physical = candidateDevice;
            break;
        }
    }
    if (physical == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void Device::createLogicalDevice(Instance instance, bool enableValidationLayers) {
    QueueFamilyIndices indices = findQueueFamilies(instance);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    if (vkCreateDevice(physical, &createInfo, nullptr, &logical) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }
    vkGetDeviceQueue(logical, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(logical, indices.presentFamily.value(), 0, &presentQueue);
}

void Device::destroyLogicalDevice() {
    vkDestroyDevice(logical, nullptr);
}

bool Device::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(this);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}


void Instance::create(bool enableValidationLayers) {
    surface.createWindow(this);
    validationLayersEnabled = enableValidationLayers;
    currentFrame = 0;
    createInstance();
    setupDebugMessenger();
    createSurface();
    device.pickPhysicalDevice(*this);
    device.createLogicalDevice(*this, validationLayersEnabled);
    surface.createSwapChain(*this);
    surface.createImageViews(device);
    renderer.createRenderPass(*this);
    descriptor.createDescriptorSetLayout(*this);
    renderer.createGraphicsPipeline(*this);
    commander.createPool(device);
    renderer.createColourResources(*this);
    renderer.createDepthResources(*this);
    renderer.createFramebuffers(*this);
    models = std::vector<Model>();
    models.push_back(Model());
    models[0].create(*this, "models/chalet.obj", "textures/chalet.jpg");
    descriptor.createVertexBuffer(*this, models[0].vertices);
    descriptor.createIndexBuffer(*this, models[0].indices);
    descriptor.createUniformBuffers(*this);
    descriptor.createDescriptorPool(*this);
    descriptor.createDescriptorSets(*this);
    commander.createBuffers(*this);
    sync.createSyncObjects(*this);
}

void Instance::destroy() {
    cleanupSwapChain();
    models[0].texture.destroy(device);
    descriptor.destroyDescriptorSetLayout(device);
    descriptor.destroyIndexBuffer(device);
    descriptor.destroyVertexBuffer(device);
    sync.destroySyncObjects(device);
    commander.destroyPool(device);
    device.destroyLogicalDevice();
    if (validationLayersEnabled) {
        destroyDebugMessenger();
    }
    surface.destroySurface(*this);
    vkDestroyInstance(instance, nullptr);
    surface.destroyWindow();
    glfwTerminate();
}

bool Instance::shouldClose() {
    return glfwWindowShouldClose(surface.window);
}

void Instance::waitIdle() {
    vkDeviceWaitIdle(device.logical);
}

void Instance::drawFrame() {
    vkWaitForFences(device.logical, 1, &sync.inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device.logical, surface.swapChain, UINT64_MAX, sync.imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }
    if (sync.imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device.logical, 1, &sync.imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    sync.imagesInFlight[imageIndex] = sync.inFlightFences[currentFrame];
    descriptor.updateUniformBuffer(*this, imageIndex);
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {sync.imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commander.buffers[imageIndex];
    VkSemaphore signalSemaphores[] = {sync.renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    vkResetFences(device.logical, 1, &sync.inFlightFences[currentFrame]);
    if (vkQueueSubmit(device.graphicsQueue, 1, &submitInfo, sync.inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {surface.swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(device.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Instance::createInstance() {
    if (validationLayersEnabled && !checkValidationLayerSupport()) {
        throw std::runtime_error(
            "validation layers requested, but not available! Compiled for debug?");
    }
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    auto extensions = getRequiredExtensions(validationLayersEnabled);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    if (validationLayersEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

void Instance::setupDebugMessenger() {
    if (!validationLayersEnabled) return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);
    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void Instance::destroyDebugMessenger() {
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
}

void Instance::cleanupSwapChain() {
    renderer.destroyColourResources(device);
    renderer.destroyDepthResources(device);
    renderer.destroyFramebuffers(*this);
    commander.destroyBuffers(device);
    renderer.destroyGraphicsPipeline(device);
    renderer.destroyRenderPass(device);
    surface.destroyImageViews(device);
    surface.destroySwapChain(device);
    descriptor.destroyUniformBuffers(*this);
    descriptor.destroyDescriptorPool(device);
}

void Instance::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(surface.window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(surface.window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device.logical);
    cleanupSwapChain();
    surface.createSwapChain(*this);
    surface.createImageViews(device);
    renderer.createRenderPass(*this);
    renderer.createColourResources(*this);
    renderer.createDepthResources(*this);
    renderer.createFramebuffers(*this);
    descriptor.createUniformBuffers(*this);
    descriptor.createDescriptorPool(*this);
    descriptor.createDescriptorSets(*this);
    commander.createBuffers(*this);
}
