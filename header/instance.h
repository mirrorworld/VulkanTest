#ifndef __INSTANCE_H_INCLUDED__
#define __INSTANCE_H_INCLUDED__

#include "surface.h"
#include "renderer.h"
#include "descriptor.h"
#include "commander.h"
#include "model.h"


struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct Device {
    VkDevice logical;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkQueue graphicsQueue;
    VkQueue presentQueue;

    void pickPhysicalDevice(Instance instance);
    void createLogicalDevice(Instance instance, bool enableValidationLayers);

private:
    bool isDeviceSuitable(VkPhysicalDevice device);
};

struct Instance {
    bool validationLayersEnabled;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    Device device;
    Surface surface;
    Renderer renderer;
    Descriptor descriptor;
    Commander commander;
    std::vector<Model> models;

    void create(bool enableValidationLayers);
    void destroy();

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();

    void cleanupSwapChain();
    void recreateSwapChain();
};

#endif