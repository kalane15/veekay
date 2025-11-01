//
// Created by Makar on 12.10.2025.
//
#include <cstdint>
#include <climits>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

#include <veekay/veekay.hpp>

#include <imgui.h>
#include <vulkan/vulkan_core.h>
#ifndef VEEKAY_UTILITY_INNER_HPP
#define VEEKAY_UTILITY_INNER_HPP

const float M_PI = 3.1415926535;

constexpr float camera_fov = 70.0f;
constexpr float camera_near_plane = 0.01f;
constexpr float camera_far_plane = 100.0f;

struct Matrix {
    float m[4][4];
};

struct Vector {
    float x, y, z;
};

const Vector ivector = {0.0, 0.0, 0.0};

struct Vertex {
    Vector position;
    Vector color;
    // NOTE: You can add more attributes
};

struct ShaderConstants {
    Matrix projection;
    Matrix transform;
};

struct VulkanBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};
Matrix identity() {
    Matrix result{};

    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;

    return result;
}

Matrix projection(float fov, float aspect_ratio, float near, float far) {
    Matrix result{};

    const float radians = fov * M_PI / 180.0f;
    const float cot = 1.0f / tanf(radians / 2.0f);

    result.m[0][0] = cot / aspect_ratio;
    result.m[1][1] = cot;
    result.m[2][3] = 1.0f;

    result.m[2][2] = far / (far - near);
    result.m[3][2] = (-near * far) / (far - near);

    return result;
}

Matrix translation(Vector vector) {
    Matrix result = identity();

    result.m[3][0] = vector.x;
    result.m[3][1] = vector.y;
    result.m[3][2] = vector.z;

    return result;
}

Matrix rotation(Vector axis, float angle) {
    Matrix result{};

    float length = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

    axis.x /= length;
    axis.y /= length;
    axis.z /= length;

    float sina = sinf(angle);
    float cosa = cosf(angle);
    float cosv = 1.0f - cosa;

    result.m[0][0] = (axis.x * axis.x * cosv) + cosa;
    result.m[0][1] = (axis.x * axis.y * cosv) + (axis.z * sina);
    result.m[0][2] = (axis.x * axis.z * cosv) - (axis.y * sina);

    result.m[1][0] = (axis.y * axis.x * cosv) - (axis.z * sina);
    result.m[1][1] = (axis.y * axis.y * cosv) + cosa;
    result.m[1][2] = (axis.y * axis.z * cosv) + (axis.x * sina);

    result.m[2][0] = (axis.z * axis.x * cosv) + (axis.y * sina);
    result.m[2][1] = (axis.z * axis.y * cosv) - (axis.x * sina);
    result.m[2][2] = (axis.z * axis.z * cosv) + cosa;

    result.m[3][3] = 1.0f;

    return result;
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};

    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            for (int k = 0; k < 4; k++) {
                result.m[j][i] += a.m[j][k] * b.m[k][i];
            }
        }
    }

    return result;
}

VulkanBuffer createBuffer(size_t size, void *data, VkBufferUsageFlags usage) {
    VkDevice& device = veekay::app.vk_device;
    VkPhysicalDevice& physical_device = veekay::app.vk_physical_device;

    VulkanBuffer result{};

    {
        // NOTE: We create a buffer of specific usage with specified size
        VkBufferCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if (vkCreateBuffer(device, &info, nullptr, &result.buffer) != VK_SUCCESS) {
            std::cerr << "Failed to create Vulkan buffer\n";
            return {};
        }
    }

    // NOTE: Creating a buffer does not allocate memory,
    //       only a buffer **object** was created.
    //       So, we allocate memory for the buffer

    {
        // NOTE: Ask buffer about its memory requirements
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, result.buffer, &requirements);

        // NOTE: Ask GPU about types of memory it supports
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

        // NOTE: We want type of memory which is visible to both CPU and GPU
        // NOTE: HOST is CPU, DEVICE is GPU; we are interested in "CPU" visible memory
        // NOTE: COHERENT means that CPU cache will be invalidated upon mapping memory region
        const VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        // NOTE: Linear search through types of memory until
        //       one type matches the requirements, thats the index of memory type
        uint32_t index = UINT_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            const VkMemoryType& type = properties.memoryTypes[i];

            if ((requirements.memoryTypeBits & (1 << i)) &&
                (type.propertyFlags & flags) == flags) {
                index = i;
                break;
            }
        }

        if (index == UINT_MAX) {
            std::cerr << "Failed to find required memory type to allocate Vulkan buffer\n";
            return {};
        }

        // NOTE: Allocate required memory amount in appropriate memory type
        VkMemoryAllocateInfo info{
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = requirements.size,
                .memoryTypeIndex = index,
        };

        if (vkAllocateMemory(device, &info, nullptr, &result.memory) != VK_SUCCESS) {
            std::cerr << "Failed to allocate Vulkan buffer memory\n";
            return {};
        }

        // NOTE: Link allocated memory with a buffer
        if (vkBindBufferMemory(device, result.buffer, result.memory, 0) != VK_SUCCESS) {
            std::cerr << "Failed to bind Vulkan  buffer memory\n";
            return {};
        }

        // NOTE: Get pointer to allocated memory
        void* device_data;
        vkMapMemory(device, result.memory, 0, requirements.size, 0, &device_data);

        memcpy(device_data, data, size);

        vkUnmapMemory(device, result.memory);
    }

    return result;
}

void destroyBuffer(const VulkanBuffer& buffer) {
    VkDevice& device = veekay::app.vk_device;

    vkFreeMemory(device, buffer.memory, nullptr);
    vkDestroyBuffer(device, buffer.buffer, nullptr);
}
#endif //VEEKAY_UTILITY_INNER_HPP
