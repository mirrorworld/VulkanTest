
VULKAN_SDK_PATH = /home/ben/dev/vulkan/1.2.131.2/x86_64
STB_INCLUDE_PATH = /home/ben/dev/stb
TINYOBJ_INCLUDE_PATH = /home/ben/dev/tinyobj

LD_LIBRARY_PATH=$(VULKAN_SDK_PATH)/lib
VK_LAYER_PATH=$(VULKAN_SDK_PATH)/etc/vulkan/explicit_layer.d
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation

CFLAGS = -std=c++17 -I$(VULKAN_SDK_PATH)/include -I$(STB_INCLUDE_PATH) -I$(TINYOBJ_INCLUDE_PATH) -I./header
LDFLAGS = -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan

SRC = ./source/*.cpp

VulkanTest:
	./shaders/compile.sh
	g++ -g $(CFLAGS) -o build/VulkanTest $(SRC) $(LDFLAGS) -lpthread

.PHONY:
	test clean

run:
	LD_LIBRARY_PATH=$(VULKAN_SDK_PATH)/lib
	VK_LAYER_PATH=$(VULKAN_SDK_PATH)/etc/vulkan/explicit_layer.d
	VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
	./build/VulkanTest

debug:
	LD_LIBRARY_PATH=$(VULKAN_SDK_PATH)/lib
	VK_LAYER_PATH=$(VULKAN_SDK_PATH)/etc/vulkan/explicit_layer.d
	VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
	gdb build/VulkanTest

test:
	VulkanTest run

clean:
	rm -f VulkanTest
