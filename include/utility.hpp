#include <cstdint>
#include <climits>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include "utility_inner.hpp"
#ifndef VEEKAY_UTILITY_HPP
#define VEEKAY_UTILITY_HPP



class Figure {
public:
    std::vector<Vertex> vertexes = std::vector<Vertex>();
    std::vector<uint32_t> indexes = std::vector<uint32_t>();
    VulkanBuffer vertex_buffer;
    VulkanBuffer index_buffer;

    Figure* parent = nullptr;
    std::vector<Figure*> children;

    Vector local_position = {0.0f, 0.0f, 0.0f};
    Vector local_rotation = {0.0f, 0.0f, 0.0f};

    explicit Figure(std::vector<Vertex>& vertexes, std::vector<uint32_t>& indexes);
    explicit Figure();
    explicit Figure(Vertex vertexes[], size_t size, uint32_t indexes[], size_t size2);
    void Draw(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout pipeline_layout);
    void SetColorRGB255ForAll(Vector color);
    ~Figure();
    Figure& operator=(Figure&& other) noexcept;
    void DestroyBuffers();
    void SetColorVertex(size_t, Vector);

    void SetParent(Figure* new_parent);
    void AddChild(Figure* child);
    void RemoveChild(Figure* child);

    void SetLocalPosition(Vector position);
    void SetLocalRotation(Vector rotation);
    void Translate(Vector translation);
    void Rotate(Vector rotation);

private:
    void BuildBuffers();
    void UpdateVertexBuffer();
    void NormalizeColor(Vector& color);
    [[nodiscard]] Matrix CalculateLocalTransform() const;
    [[nodiscard]] Matrix CalculateWorldTransform() const;
};


Figure::Figure(std::vector<Vertex>& _vertexes, std::vector<uint32_t>& _indexes) {
    this->vertexes = _vertexes;
    this->indexes = _indexes;
    BuildBuffers();
}

Figure::Figure(Vertex* _vertexes, size_t size, uint32_t* _indexes, size_t size2) {
    for (size_t i = 0; i < size; i++) {
        this->vertexes.push_back(_vertexes[i]);
    }
    for (size_t i = 0; i < size2; i++) {
        this->indexes.push_back(_indexes[i]);
    }
    BuildBuffers();
}

Figure::Figure() {}

Figure::~Figure() {
    if (parent) {
        parent->RemoveChild(this);
    }

    for (auto child : children) {
        child->parent = nullptr;
    }
    children.clear();
}

Figure& Figure::operator=(Figure&& other) noexcept {
    indexes = std::move(other.indexes);
    vertexes = std::move(other.vertexes);
    BuildBuffers();

    parent = other.parent;
    children = std::move(other.children);
    local_position = other.local_position;
    local_rotation = other.local_rotation;

    for (auto child : children) {
        child->parent = this;
    }

    other.parent = nullptr;
    other.children.clear();
    other.DestroyBuffers();
    return *this;
}

void Figure::SetParent(Figure* new_parent) {
    if (parent == new_parent) return;

    if (parent) {
        parent->RemoveChild(this);
    }

    parent = new_parent;

    if (parent) {
        parent->AddChild(this);
    }
}

void Figure::AddChild(Figure* child) {
    if (child && std::find(children.begin(), children.end(), child) == children.end()) {
        children.push_back(child);
        if (child->parent != this) {
            child->SetParent(this);
        }
    }
}

void Figure::RemoveChild(Figure* child) {
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) {
        children.erase(it);
        if (child->parent == this) {
            child->parent = nullptr;
        }
    }
}

Matrix Figure::CalculateLocalTransform() const {
    Matrix rot_x = rotation({1.0f, 0.0f, 0.0f}, local_rotation.x);
    Matrix rot_y = rotation({0.0f, 1.0f, 0.0f}, local_rotation.y);
    Matrix rot_z = rotation({0.0f, 0.0f, 1.0f}, local_rotation.z);
    Matrix rotation_matrix = multiply(rot_z, multiply(rot_y, rot_x));

    Matrix translation_matrix = translation(local_position);

    return multiply(rotation_matrix, translation_matrix);
}

Matrix Figure::CalculateWorldTransform() const {
    Matrix local_transform = CalculateLocalTransform();
    if (parent) {
        return multiply(local_transform, parent->CalculateWorldTransform());
    }

    return local_transform;
}

// Local space transform methods
void Figure::SetLocalPosition(Vector position) {
    local_position = position;
}

void Figure::SetLocalRotation(Vector rotation) {
    local_rotation = rotation;
}

void Figure::Translate(Vector translation) {
    local_position.x += translation.x;
    local_position.y += translation.y;
    local_position.z += translation.z;
}

void Figure::Rotate(Vector rotation) {
    local_rotation.x += rotation.x;
    local_rotation.y += rotation.y;
    local_rotation.z += rotation.z;
}

void Figure::Draw(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout pipeline_layout) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, index_buffer.buffer, offset, VK_INDEX_TYPE_UINT32);

    ShaderConstants constants{
            .projection = projection(
                    camera_fov,
                    float(veekay::app.window_width) / float(veekay::app.window_height),
                    camera_near_plane, camera_far_plane),
            .transform = CalculateWorldTransform(),
    };

    vkCmdPushConstants(cmd, pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ShaderConstants), &constants);

    vkCmdDrawIndexed(cmd, indexes.size(), 1, 0, 0, 0);
}

void Figure::BuildBuffers() {
    vertex_buffer = createBuffer(vertexes.size() * sizeof(Vertex), (void*)vertexes.data(),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    index_buffer = createBuffer(indexes.size() * sizeof(uint32_t), (void*)indexes.data(),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Figure::DestroyBuffers() {
    destroyBuffer(index_buffer);
    destroyBuffer(vertex_buffer);
}

void Figure::UpdateVertexBuffer() {
    void* device_data;
    vkMapMemory(veekay::app.vk_device, vertex_buffer.memory, 0,
                sizeof(Vertex) * vertexes.size(), 0, &device_data);
    memcpy(device_data, vertexes.data(), sizeof(Vertex) * vertexes.size());
    vkUnmapMemory(veekay::app.vk_device, vertex_buffer.memory);
}

void Figure::SetColorRGB255ForAll(Vector color) {
    NormalizeColor(color);
    bool changed = false;

    for (Vertex& v : vertexes) {
        if (v.color.x != color.x || v.color.y != color.y || v.color.z != color.z) {
            v.color = color;
            changed = true;
        }
    }
    if (changed) {
        UpdateVertexBuffer();
    }
}

void Figure::SetColorVertex(size_t ind, Vector color) {
    NormalizeColor(color);
    vertexes[ind].color = color;
    UpdateVertexBuffer();
}

void Figure::NormalizeColor(Vector& color) {
    if (color.x > 1.0 || color.y > 1.0 || color.z > 1.0) {
        color.x /= 255;
        color.y /= 255;
        color.z /= 255;
    }
}

#endif //VEEKAY_UTILITY_HPP
