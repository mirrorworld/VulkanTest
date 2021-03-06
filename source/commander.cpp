#include "commander.h"
#include "descriptor.h"
#include "device.h"
#include "include.h"
#include "instance.h"
#include "model.h"
#include "renderer.h"
#include "surface.h"
#include "sync.h"
#include "texture.h"
#include "util.h"

void Commander::createPool(Instance* instance) {
	QueueFamilyIndices queueFamilyIndices =
	    findQueueFamilies(instance, instance->device->physical);
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
	poolInfo.flags = 0; // Optional
	if (vkCreateCommandPool(instance->device->logical, &poolInfo, nullptr,
	                        &pool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}
}

void Commander::createBuffers(Instance* instance) {
	buffers.resize(instance->surface->getSwapChainSize());
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)buffers.size();
	if (vkAllocateCommandBuffers(instance->device->logical, &allocInfo,
	                             buffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffers!");
	}
	for (size_t i = 0; i < buffers.size(); i++) {
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		if (vkBeginCommandBuffer(buffers[i], &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error(
			    "failed to begin recording command buffer!");
		}
		VkRenderPassBeginInfo renderPassInfo =
		    instance->renderer->getRenderPassInfo(instance, i);
		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
		clearValues[1].depthStencil = {1.0f, 0};
		renderPassInfo.clearValueCount =
		    static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();
		vkCmdBeginRenderPass(buffers[i], &renderPassInfo,
		                     VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
		                  instance->renderer->graphicsPipeline);
		VkBuffer vertexBuffers[] = {instance->descriptor->vertexBuffer};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(buffers[i], 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(buffers[i], instance->descriptor->indexBuffer, 0,
		                     VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        instance->renderer->pipelineLayout, 0, 1,
		                        &instance->descriptor->descriptorSets[i], 0,
		                        nullptr);
		vkCmdDrawIndexed(buffers[i], instance->descriptor->nIndices, 1, 0, 0,
		                 0);
		vkCmdEndRenderPass(buffers[i]);
		if (vkEndCommandBuffer(buffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}
	}
}

VkCommandBuffer Commander::beginSingleTimeCommands(Device* device) {
	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = pool;
	allocInfo.commandBufferCount = 1;
	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device->logical, &allocInfo, &commandBuffer);
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	return commandBuffer;
}

void Commander::endSingleTimeCommands(Device* device,
                                      VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vkQueueSubmit(device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(device->graphicsQueue);
	vkFreeCommandBuffers(device->logical, pool, 1, &commandBuffer);
}

void Commander::destroyPool(Device* device) {
	vkDestroyCommandPool(device->logical, pool, nullptr);
}

void Commander::destroyBuffers(Device* device) {
	vkFreeCommandBuffers(device->logical, pool,
	                     static_cast<uint32_t>(buffers.size()), buffers.data());
}

void Commander::transitionImageLayout(Device* device, VkImage image,
                                      VkFormat format, VkImageLayout oldLayout,
                                      VkImageLayout newLayout,
                                      uint32_t mipLevels) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device);
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (hasStencilComponent(format)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	} else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
	    newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	           newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
	           newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	} else {
		throw std::invalid_argument("unsupported layout transition!");
	}
	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
	                     nullptr, 0, nullptr, 1, &barrier);
	endSingleTimeCommands(device, commandBuffer);
}

void Commander::pushConstants(Instance* instance) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(instance->device);
    vkCmdPushConstants(commandBuffer, instance->renderer->pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UniformBufferObject),
        (void*)&instance->descriptor->ubo);
	endSingleTimeCommands(instance->device, commandBuffer);
}

void Commander::copyBuffer(Device* device, VkBuffer srcBuffer,
                           VkBuffer dstBuffer, VkDeviceSize size) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device);
	VkBufferCopy copyRegion = {};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
	endSingleTimeCommands(device, commandBuffer);
}

void Commander::copyBufferToImage(Device* device, VkBuffer buffer,
                                  VkImage image, uint32_t width,
                                  uint32_t height) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device);
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = {0, 0, 0};
	region.imageExtent = {width, height, 1};
	vkCmdCopyBufferToImage(commandBuffer, buffer, image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	endSingleTimeCommands(device, commandBuffer);
}

void Commander::generateMipmaps(Device* device, VkImage image,
                                VkFormat imageFormat, int32_t texWidth,
                                int32_t texHeight, uint32_t mipLevels) {
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(device->physical, imageFormat,
	                                    &formatProperties);
	if (!(formatProperties.optimalTilingFeatures &
	      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		throw std::runtime_error(
		    "texture image format does not support linear blitting!");
	}
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device);
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;
	for (uint32_t i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
		                     nullptr, 1, &barrier);
		VkImageBlit blit = {};
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
		                      mipHeight > 1 ? mipHeight / 2 : 1, 1};
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		vkCmdBlitImage(
		    commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &barrier);
		if (mipWidth > 1)
			mipWidth /= 2;
		if (mipHeight > 1)
			mipHeight /= 2;
	}
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
	                     0, nullptr, 1, &barrier);
	endSingleTimeCommands(device, commandBuffer);
}