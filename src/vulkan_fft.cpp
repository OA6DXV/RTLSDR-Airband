#include "vulkan_fft.h"

#include <fftw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "vulkan_fft_spv.h"

namespace {

const double PI = 3.141592653589793238462643383279502884;

std::string lower_string(const std::string& value) {
    std::string result(value);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    return result;
}

std::string vk_error(const char* operation, VkResult result) {
    std::ostringstream stream;
    stream << operation << " failed with VkResult " << static_cast<int>(result);
    return stream.str();
}

uint32_t power_of_two_floor(uint32_t value) {
    uint32_t result = 1;
    while (result <= value / 2)
        result <<= 1;
    return result;
}

}  // namespace

struct VulkanFFT::Impl {
    enum Backend { BACKEND_NONE, BACKEND_VULKAN, BACKEND_FFTW };

    struct Buffer {
        VkBuffer handle;
        VkDeviceMemory memory;
        void* mapped;
        VkDeviceSize size;
        bool coherent;

        Buffer() : handle(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), mapped(NULL), size(0), coherent(false) {}
    };

    struct PushConstants {
        uint32_t fft_size;
        uint32_t batch_size;
        uint32_t stage;
        uint32_t mode;
    };

    size_t fft_size;
    size_t batch_size;
    uint32_t fft_log2;
    Backend backend;
    std::string device_name;
    std::string fallback_reason;
    std::string last_error;
    uint32_t workgroup_size;

    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[2];
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    Buffer buffer_a;
    Buffer buffer_b;
    Buffer twiddle_buffer;
    Buffer* final_buffer;

    fftwf_plan fftw_plan;
    VulkanComplex* fftw_input;
    VulkanComplex* fftw_output;

    Impl(size_t transform_size, size_t transforms)
        : fft_size(transform_size),
          batch_size(transforms),
          fft_log2(0),
          backend(BACKEND_NONE),
          workgroup_size(0),
          instance(VK_NULL_HANDLE),
          physical_device(VK_NULL_HANDLE),
          device(VK_NULL_HANDLE),
          queue_family(0),
          queue(VK_NULL_HANDLE),
          descriptor_set_layout(VK_NULL_HANDLE),
          descriptor_pool(VK_NULL_HANDLE),
          pipeline_layout(VK_NULL_HANDLE),
          pipeline(VK_NULL_HANDLE),
          command_pool(VK_NULL_HANDLE),
          command_buffer(VK_NULL_HANDLE),
          fence(VK_NULL_HANDLE),
          final_buffer(NULL),
          fftw_plan(NULL),
          fftw_input(NULL),
          fftw_output(NULL) {
        descriptor_sets[0] = VK_NULL_HANDLE;
        descriptor_sets[1] = VK_NULL_HANDLE;
        std::memset(&physical_properties, 0, sizeof(physical_properties));
        std::memset(&memory_properties, 0, sizeof(memory_properties));

        size_t value = fft_size;
        while (value > 1 && (value & 1u) == 0) {
            ++fft_log2;
            value >>= 1;
        }
        if (value != 1)
            fft_log2 = 0;
    }

    ~Impl() {
        destroy_vulkan();
        destroy_fftw();
    }

    void destroy_buffer(Buffer& buffer) {
        if (device == VK_NULL_HANDLE)
            return;
        if (buffer.mapped != NULL && buffer.memory != VK_NULL_HANDLE)
            vkUnmapMemory(device, buffer.memory);
        if (buffer.handle != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer.handle, NULL);
        if (buffer.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, buffer.memory, NULL);
        buffer = Buffer();
    }

    void destroy_vulkan() {
        if (device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device);

        if (device != VK_NULL_HANDLE && fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, command_pool, NULL);
        if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, NULL);
        if (device != VK_NULL_HANDLE && pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (device != VK_NULL_HANDLE && descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, NULL);
        if (device != VK_NULL_HANDLE && descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);

        destroy_buffer(twiddle_buffer);
        destroy_buffer(buffer_b);
        destroy_buffer(buffer_a);

        if (device != VK_NULL_HANDLE)
            vkDestroyDevice(device, NULL);
        if (instance != VK_NULL_HANDLE)
            vkDestroyInstance(instance, NULL);

        instance = VK_NULL_HANDLE;
        physical_device = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        descriptor_sets[0] = VK_NULL_HANDLE;
        descriptor_sets[1] = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        command_pool = VK_NULL_HANDLE;
        command_buffer = VK_NULL_HANDLE;
        fence = VK_NULL_HANDLE;
        final_buffer = NULL;
    }

    void destroy_fftw() {
        if (fftw_plan != NULL)
            fftwf_destroy_plan(fftw_plan);
        if (fftw_input != NULL)
            fftwf_free(fftw_input);
        if (fftw_output != NULL)
            fftwf_free(fftw_output);
        fftw_plan = NULL;
        fftw_input = NULL;
        fftw_output = NULL;
    }

    bool create_buffer(Buffer& buffer, VkDeviceSize size) {
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(device, &buffer_info, NULL, &buffer.handle);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateBuffer", result);
            return false;
        }

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);

        int selected_type = -1;
        int selected_score = -1;
        VkMemoryPropertyFlags selected_flags = 0;
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) == 0)
                continue;
            VkMemoryPropertyFlags flags = memory_properties.memoryTypes[i].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
                continue;

            int score = 0;
            if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
                score += 4;
            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
                score += 2;
            if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0)
                score += 1;
            if (score > selected_score) {
                selected_type = static_cast<int>(i);
                selected_score = score;
                selected_flags = flags;
            }
        }

        if (selected_type < 0) {
            last_error = "No host-visible Vulkan memory type is available for FFT buffers";
            return false;
        }

        VkMemoryAllocateInfo allocation_info = {};
        allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation_info.allocationSize = requirements.size;
        allocation_info.memoryTypeIndex = static_cast<uint32_t>(selected_type);
        result = vkAllocateMemory(device, &allocation_info, NULL, &buffer.memory);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkAllocateMemory", result);
            return false;
        }

        result = vkBindBufferMemory(device, buffer.handle, buffer.memory, 0);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkBindBufferMemory", result);
            return false;
        }

        result = vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkMapMemory", result);
            return false;
        }

        buffer.size = size;
        buffer.coherent = (selected_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        return true;
    }

    bool flush_buffer(const Buffer& buffer) {
        if (buffer.coherent)
            return true;
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = buffer.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        VkResult result = vkFlushMappedMemoryRanges(device, 1, &range);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkFlushMappedMemoryRanges", result);
            return false;
        }
        return true;
    }

    bool invalidate_buffer(const Buffer& buffer) {
        if (buffer.coherent)
            return true;
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = buffer.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        VkResult result = vkInvalidateMappedMemoryRanges(device, 1, &range);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkInvalidateMappedMemoryRanges", result);
            return false;
        }
        return true;
    }

    bool select_physical_device() {
        uint32_t count = 0;
        VkResult result = vkEnumeratePhysicalDevices(instance, &count, NULL);
        if (result != VK_SUCCESS || count == 0) {
            last_error = result == VK_SUCCESS ? "No Vulkan physical devices found" : vk_error("vkEnumeratePhysicalDevices", result);
            return false;
        }

        std::vector<VkPhysicalDevice> candidates(count);
        result = vkEnumeratePhysicalDevices(instance, &count, &candidates[0]);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkEnumeratePhysicalDevices", result);
            return false;
        }

        const char* requested_env = std::getenv("RTL_AIRBAND_VULKAN_DEVICE");
        const std::string requested = requested_env == NULL ? std::string() : lower_string(requested_env);
        int best_score = -1;
        VkPhysicalDevice best_device = VK_NULL_HANDLE;
        uint32_t best_queue_family = 0;
        VkPhysicalDeviceProperties best_properties;
        std::memset(&best_properties, 0, sizeof(best_properties));

        for (uint32_t device_index = 0; device_index < count; ++device_index) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(candidates[device_index], &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                continue;
            if (!requested.empty() && lower_string(properties.deviceName).find(requested) == std::string::npos)
                continue;

            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidates[device_index], &queue_count, NULL);
            if (queue_count == 0)
                continue;
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidates[device_index], &queue_count, &queues[0]);

            int queue_score = -1;
            uint32_t candidate_queue_family = 0;
            for (uint32_t queue_index = 0; queue_index < queue_count; ++queue_index) {
                if ((queues[queue_index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
                    continue;
                int score = (queues[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 ? 200 : 100;
                if (score > queue_score) {
                    queue_score = score;
                    candidate_queue_family = queue_index;
                }
            }
            if (queue_score < 0)
                continue;

            int device_score = queue_score;
            switch (properties.deviceType) {
                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                    device_score += 4000;
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                    device_score += 3000;
                    break;
                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                    device_score += 2000;
                    break;
                default:
                    device_score += 1000;
                    break;
            }
            if (!requested.empty())
                device_score += 10000;

            if (device_score > best_score) {
                best_score = device_score;
                best_device = candidates[device_index];
                best_queue_family = candidate_queue_family;
                best_properties = properties;
            }
        }

        if (best_device == VK_NULL_HANDLE) {
            if (requested.empty())
                last_error = "No non-CPU Vulkan device with a compute queue was found";
            else
                last_error = "No Vulkan GPU matching RTL_AIRBAND_VULKAN_DEVICE has a compute queue";
            return false;
        }

        physical_device = best_device;
        queue_family = best_queue_family;
        physical_properties = best_properties;
        device_name = physical_properties.deviceName;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

        uint32_t limit = physical_properties.limits.maxComputeWorkGroupInvocations;
        limit = std::min(limit, physical_properties.limits.maxComputeWorkGroupSize[0]);
        limit = std::min(limit, static_cast<uint32_t>(256));
        if (limit == 0) {
            last_error = "Selected Vulkan device reports no usable compute workgroup size";
            return false;
        }
        workgroup_size = power_of_two_floor(limit);
        return true;
    }

    bool create_descriptors() {
        VkDescriptorSetLayoutBinding bindings[3] = {};
        for (uint32_t i = 0; i < 3; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 3;
        layout_info.pBindings = bindings;
        VkResult result = vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_set_layout);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateDescriptorSetLayout", result);
            return false;
        }

        VkDescriptorPoolSize pool_size = {};
        pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 6;
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 2;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        result = vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateDescriptorPool", result);
            return false;
        }

        VkDescriptorSetLayout layouts[2] = {descriptor_set_layout, descriptor_set_layout};
        VkDescriptorSetAllocateInfo allocate_info = {};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = descriptor_pool;
        allocate_info.descriptorSetCount = 2;
        allocate_info.pSetLayouts = layouts;
        result = vkAllocateDescriptorSets(device, &allocate_info, descriptor_sets);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkAllocateDescriptorSets", result);
            return false;
        }

        Buffer* inputs[2] = {&buffer_a, &buffer_b};
        Buffer* outputs[2] = {&buffer_b, &buffer_a};
        for (uint32_t set_index = 0; set_index < 2; ++set_index) {
            VkDescriptorBufferInfo buffer_infos[3] = {};
            buffer_infos[0].buffer = inputs[set_index]->handle;
            buffer_infos[0].range = inputs[set_index]->size;
            buffer_infos[1].buffer = outputs[set_index]->handle;
            buffer_infos[1].range = outputs[set_index]->size;
            buffer_infos[2].buffer = twiddle_buffer.handle;
            buffer_infos[2].range = twiddle_buffer.size;

            VkWriteDescriptorSet writes[3] = {};
            for (uint32_t binding = 0; binding < 3; ++binding) {
                writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[binding].dstSet = descriptor_sets[set_index];
                writes[binding].dstBinding = binding;
                writes[binding].descriptorCount = 1;
                writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[binding].pBufferInfo = &buffer_infos[binding];
            }
            vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
        }
        return true;
    }

    bool create_pipeline() {
        if ((vulkan_fft_spv_size & 3u) != 0) {
            last_error = "Embedded Vulkan FFT shader has an invalid SPIR-V size";
            return false;
        }

        VkShaderModuleCreateInfo shader_info = {};
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = vulkan_fft_spv_size;
        shader_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_fft_spv);
        VkShaderModule shader_module = VK_NULL_HANDLE;
        VkResult result = vkCreateShaderModule(device, &shader_info, NULL, &shader_module);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateShaderModule", result);
            return false;
        }

        VkPushConstantRange push_range = {};
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
        result = vkCreatePipelineLayout(device, &layout_info, NULL, &pipeline_layout);
        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(device, shader_module, NULL);
            last_error = vk_error("vkCreatePipelineLayout", result);
            return false;
        }

        VkSpecializationMapEntry map_entry = {};
        map_entry.constantID = 0;
        map_entry.offset = 0;
        map_entry.size = sizeof(workgroup_size);
        VkSpecializationInfo specialization = {};
        specialization.mapEntryCount = 1;
        specialization.pMapEntries = &map_entry;
        specialization.dataSize = sizeof(workgroup_size);
        specialization.pData = &workgroup_size;

        VkPipelineShaderStageCreateInfo stage_info = {};
        stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage_info.module = shader_module;
        stage_info.pName = "main";
        stage_info.pSpecializationInfo = &specialization;

        VkComputePipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = stage_info;
        pipeline_info.layout = pipeline_layout;
        result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline);
        vkDestroyShaderModule(device, shader_module, NULL);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateComputePipelines", result);
            return false;
        }
        return true;
    }

    void shader_barrier(VkCommandBuffer command) {
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    }

    uint32_t dispatch_count(size_t item_count) const {
        return static_cast<uint32_t>((item_count + workgroup_size - 1) / workgroup_size);
    }

    bool record_command_buffer() {
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queue_family;
        VkResult result = vkCreateCommandPool(device, &pool_info, NULL, &command_pool);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateCommandPool", result);
            return false;
        }

        VkCommandBufferAllocateInfo allocate_info = {};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers(device, &allocate_info, &command_buffer);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkAllocateCommandBuffers", result);
            return false;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        result = vkBeginCommandBuffer(command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkBeginCommandBuffer", result);
            return false;
        }

        VkMemoryBarrier host_barrier = {};
        host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

        PushConstants constants;
        constants.fft_size = static_cast<uint32_t>(fft_size);
        constants.batch_size = static_cast<uint32_t>(batch_size);
        constants.stage = 0;
        constants.mode = 0;
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_sets[0], 0, NULL);
        vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(command_buffer, dispatch_count(fft_size * batch_size), 1, 1);

        for (uint32_t stage = 1; stage <= fft_log2; ++stage) {
            shader_barrier(command_buffer);
            uint32_t set_index = (stage & 1u) != 0 ? 1u : 0u;
            constants.stage = stage;
            constants.mode = 1;
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_sets[set_index], 0, NULL);
            vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            vkCmdDispatch(command_buffer, dispatch_count((fft_size >> 1u) * batch_size), 1, 1);
        }

        VkMemoryBarrier host_read_barrier = {};
        host_read_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host_read_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        host_read_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_read_barrier, 0, NULL, 0, NULL);

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkEndCommandBuffer", result);
            return false;
        }

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = vkCreateFence(device, &fence_info, NULL, &fence);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateFence", result);
            return false;
        }

        final_buffer = (fft_log2 & 1u) != 0 ? &buffer_a : &buffer_b;
        return true;
    }

    bool init_vulkan() {
        if (fft_size == 0 || batch_size == 0 || fft_log2 == 0) {
            last_error = "Vulkan FFT requires a non-zero power-of-two transform size and batch";
            return false;
        }

        VkApplicationInfo app_info = {};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "RTLSDR-Airband";
        app_info.applicationVersion = 1;
        app_info.pEngineName = "RTLSDR-Airband Vulkan FFT";
        app_info.engineVersion = 1;
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instance_info = {};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        VkResult result = vkCreateInstance(&instance_info, NULL, &instance);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateInstance", result);
            return false;
        }

        if (!select_physical_device())
            return false;

        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info = {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &queue_priority;

        VkDeviceCreateInfo device_info = {};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        result = vkCreateDevice(physical_device, &device_info, NULL, &device);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkCreateDevice", result);
            return false;
        }
        vkGetDeviceQueue(device, queue_family, 0, &queue);

        VkDeviceSize data_size = static_cast<VkDeviceSize>(fft_size * batch_size * sizeof(VulkanComplex));
        VkDeviceSize twiddle_size = static_cast<VkDeviceSize>((fft_size >> 1u) * sizeof(VulkanComplex));
        if (!create_buffer(buffer_a, data_size) || !create_buffer(buffer_b, data_size) || !create_buffer(twiddle_buffer, twiddle_size))
            return false;

        VulkanComplex* twiddles = static_cast<VulkanComplex*>(twiddle_buffer.mapped);
        for (size_t i = 0; i < fft_size / 2; ++i) {
            double angle = -2.0 * PI * static_cast<double>(i) / static_cast<double>(fft_size);
            twiddles[i].re = static_cast<float>(std::cos(angle));
            twiddles[i].im = static_cast<float>(std::sin(angle));
        }
        if (!flush_buffer(twiddle_buffer))
            return false;

        if (!create_descriptors() || !create_pipeline() || !record_command_buffer())
            return false;

        backend = BACKEND_VULKAN;
        return true;
    }

    bool init_fftw() {
        static_assert(sizeof(VulkanComplex) == sizeof(fftwf_complex), "VulkanComplex must match fftwf_complex layout");
        const size_t count = fft_size * batch_size;
        fftw_input = static_cast<VulkanComplex*>(fftwf_malloc(count * sizeof(VulkanComplex)));
        fftw_output = static_cast<VulkanComplex*>(fftwf_malloc(count * sizeof(VulkanComplex)));
        if (fftw_input == NULL || fftw_output == NULL) {
            last_error = "Unable to allocate FFTW fallback buffers";
            return false;
        }

        int transform_size = static_cast<int>(fft_size);
        fftw_plan = fftwf_plan_many_dft(1, &transform_size, static_cast<int>(batch_size), reinterpret_cast<fftwf_complex*>(fftw_input), NULL, 1, transform_size,
                                        reinterpret_cast<fftwf_complex*>(fftw_output), NULL, 1, transform_size, FFTW_FORWARD, FFTW_MEASURE);
        if (fftw_plan == NULL) {
            last_error = "Unable to create FFTW fallback plan";
            return false;
        }
        device_name = "FFTW3F CPU fallback";
        workgroup_size = 0;
        backend = BACKEND_FFTW;
        return true;
    }

    bool initialize() {
        const char* disabled = std::getenv("RTL_AIRBAND_VULKAN_DISABLE");
        if (disabled == NULL || std::strcmp(disabled, "1") != 0) {
            if (init_vulkan())
                return true;
            fallback_reason = last_error;
            destroy_vulkan();
        } else {
            fallback_reason = "Vulkan disabled by RTL_AIRBAND_VULKAN_DISABLE=1";
        }

        if (init_fftw())
            return true;
        return false;
    }

    bool execute() {
        if (backend == BACKEND_FFTW) {
            fftwf_execute(fftw_plan);
            return true;
        }
        if (backend != BACKEND_VULKAN) {
            last_error = "FFT backend is not initialized";
            return false;
        }

        if (!flush_buffer(buffer_a))
            return false;

        VkResult result = vkResetFences(device, 1, &fence);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkResetFences", result);
            return false;
        }

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        result = vkQueueSubmit(queue, 1, &submit_info, fence);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkQueueSubmit", result);
            return false;
        }

        result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            last_error = vk_error("vkWaitForFences", result);
            return false;
        }

        return invalidate_buffer(*final_buffer);
    }

    VulkanComplex* input() {
        if (backend == BACKEND_VULKAN)
            return static_cast<VulkanComplex*>(buffer_a.mapped);
        return fftw_input;
    }

    const VulkanComplex* output() const {
        if (backend == BACKEND_VULKAN && final_buffer != NULL)
            return static_cast<const VulkanComplex*>(final_buffer->mapped);
        return fftw_output;
    }
};

VulkanFFT::VulkanFFT(size_t fft_size, size_t batch_size) : impl_(new Impl(fft_size, batch_size)) {}

VulkanFFT::~VulkanFFT() {}

bool VulkanFFT::initialize() {
    return impl_->initialize();
}

bool VulkanFFT::execute() {
    return impl_->execute();
}

VulkanComplex* VulkanFFT::input() {
    return impl_->input();
}

const VulkanComplex* VulkanFFT::output() const {
    return impl_->output();
}

bool VulkanFFT::using_vulkan() const {
    return impl_->backend == Impl::BACKEND_VULKAN;
}

const std::string& VulkanFFT::device_name() const {
    return impl_->device_name;
}

const std::string& VulkanFFT::fallback_reason() const {
    return impl_->fallback_reason;
}

const std::string& VulkanFFT::last_error() const {
    return impl_->last_error;
}

size_t VulkanFFT::workgroup_size() const {
    return impl_->workgroup_size;
}
