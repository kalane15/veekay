#include <cstdint>
#include <climits>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

#include <veekay/veekay.hpp>

#include <vulkan/vulkan_core.h>
#include <imgui.h>
#include <lodepng.h>

namespace {

    constexpr uint32_t max_models = 1024;
    constexpr uint32_t max_lights = 256;

    struct Vertex {
        veekay::vec3 position;
        veekay::vec3 normal;
        veekay::vec2 uv;
        // NOTE: You can add more attributes
    };

    struct SceneUniforms {
        veekay::mat4 view_projection;
        veekay::vec3 view_position;
        float _pad0;
        veekay::vec3 ambient_light_intensity;
        float _pad1;
        veekay::vec3 sun_light_direction;
        float _pad2;
        veekay::vec3 sun_light_color;
        uint32_t point_lights_count;
        uint32_t spot_lights_count;
        float _pad4[3];
    };

    struct ModelUniforms {
        veekay::mat4 model;
        veekay::vec3 albedo_color;
        float _pad0;
        veekay::vec3 specular_color;
        float shininess;
    };

    struct Mesh {
        veekay::graphics::Buffer *vertex_buffer;
        veekay::graphics::Buffer *index_buffer;
        uint32_t indices;
    };

    struct Transform {
        veekay::vec3 position = {};
        veekay::vec3 scale = {1.0f, 1.0f, 1.0f};
        veekay::vec3 rotation = {};

        // NOTE: Model matrix (translation, rotation and scaling)
        veekay::mat4 matrix() const;
    };

    struct Model {
        Mesh mesh;
        Transform transform;
        veekay::vec3 albedo_color;
        veekay::vec3 specular_color;
        float shininess;
        std::string texture_path_albedo = "./assets/lenna.png";
        std::string texture_path_specular = "./assets/white.png";
        std::string texture_path_emissive = "./assets/black.png";
        VkDescriptorSet texture_descriptors_set;
    };

    struct Camera {
        constexpr static float default_fov = 60.0f;
        constexpr static float default_near_plane = 0.01f;
        constexpr static float default_far_plane = 100.0f;

        veekay::vec3 position = {0, -0.5, -3.0};
        veekay::vec3 rotation = {0, 0.0f, 0.0};

        float fov = default_fov;
        float near_plane = default_near_plane;
        float far_plane = default_far_plane;

        veekay::vec3 forward;
        veekay::vec3 right;
        veekay::vec3 up;

        bool useLookAt = false;

        // NOTE: View matrix of camera (inverse of a transform)
        veekay::mat4 view() const;

        // NOTE: View and projection composition
        veekay::mat4 view_projection(float aspect_ratio) const;
    };


    struct PointLight {
        veekay::vec3 position;
        float _pad1;
        veekay::vec3 color;
        float intensity;
    };

    struct SpotLight {
        veekay::vec3 position;
        float intensity;
        veekay::vec3 direction;
        float angle; // Косинус угла
        veekay::vec3 color;
        float _pad0;
    };

// NOTE: Scene objects
    inline namespace {
        Camera camera{
        };

        std::vector<Model> models;
        std::vector<PointLight> point_lights;
        std::vector<SpotLight> spot_lights;
    }

// NOTE: Vulkan objects
    inline namespace {
        VkShaderModule vertex_shader_module;
        VkShaderModule fragment_shader_module;

        VkDescriptorPool descriptor_pool;
        VkDescriptorSetLayout descriptor_set_layout;

        std::vector<VkSampler> samplers;
        std::vector<veekay::graphics::Texture *> textures;

        VkPipelineLayout pipeline_layout;
        VkPipeline pipeline;

        veekay::graphics::Buffer *scene_uniforms_buffer;
        veekay::graphics::Buffer *model_uniforms_buffer;
        veekay::graphics::Buffer *point_light_buffer;
        veekay::graphics::Buffer *spotlight_buffer;

        Mesh plane_mesh;
        Mesh cube_mesh;

        VkSampler missing_texture_sampler;


    }

    float toRadians(float degrees) {
        return degrees * float(M_PI) / 180.0f;
    }

    veekay::mat4 Transform::matrix() const {
        // TODO: Scaling and rotation

        auto t = veekay::mat4::translation(position);

        return t;
    }

    veekay::mat4 Camera::view() const {

        veekay::mat4 view = veekay::mat4::identity();

        view[0] = {right.x, up.x, forward.x, 0};
        view[1] = {right.y, up.y, forward.y, 0};
        view[2] = {right.z, up.z, forward.z, 0};

        auto lookAt = veekay::mat4::translation(-position) * view;

        if (camera.useLookAt) {
            return lookAt;
        }

        auto tr = veekay::mat4::translation(-position);
        auto rot_x = veekay::mat4::rotation({1, 0, 0}, -rotation.x);
        auto rot_y = veekay::mat4::rotation({0, 1, 0}, -rotation.y);
        auto rot_z = veekay::mat4::rotation({0, 0, 1}, -rotation.z);
        auto res = tr * rot_z * rot_y * rot_x;

        return res;
    }


    veekay::mat4 Camera::view_projection(float aspect_ratio) const {
        auto projection = veekay::mat4::projection(fov, aspect_ratio, near_plane, far_plane);

        return view() * projection;
    }

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
    VkShaderModule loadShaderModule(const char *path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        size_t size = file.tellg();
        std::vector<uint32_t> buffer(size / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), size);
        file.close();

        VkShaderModuleCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = size,
                .pCode = buffer.data(),
        };

        VkShaderModule result;
        if (vkCreateShaderModule(veekay::app.vk_device, &
                info, nullptr, &result) != VK_SUCCESS) {
            return nullptr;
        }

        return result;
    }

    void initialize(VkCommandBuffer cmd) {
        // NOTE: Plane mesh initialization
        {
// (v0)------(v1)
//  |  \       |
//  |   `--,   |
//  |       \  |
// (v3)------(v2)
            std::vector<Vertex> vertices = {
                    {{-5.0f, 0.0f, 5.0f},  {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
                    {{15.0f, 0.0f, 5.0f},  {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                    {{15.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
                    {{-5.0f, 0.0f, -5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
            };

            std::vector<uint32_t> indices = {
                    0, 3, 2, 2, 1, 0,
                    0, 1, 2, 2, 3, 0
            };

            plane_mesh.
                    vertex_buffer = new veekay::graphics::Buffer(
                    vertices.size() * sizeof(Vertex), vertices.data(),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

            plane_mesh.
                    index_buffer = new veekay::graphics::Buffer(
                    indices.size() * sizeof(uint32_t), indices.data(),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            plane_mesh.
                    indices = uint32_t(indices.size());
        }

// NOTE: Cube mesh initialization
        {
            std::vector<Vertex> vertices = {
                    {{-0.5f, -0.5f, -0.5f}, {0.0f,  0.0f,  -1.0f}, {0.0f, 0.0f}},
                    {{+0.5f, -0.5f, -0.5f}, {0.0f,  0.0f,  -1.0f}, {1.0f, 0.0f}},
                    {{+0.5f, +0.5f, -0.5f}, {0.0f,  0.0f,  -1.0f}, {1.0f, 1.0f}},
                    {{-0.5f, +0.5f, -0.5f}, {0.0f,  0.0f,  -1.0f}, {0.0f, 1.0f}},

                    {{+0.5f, -0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0.0f, 0.0f}},
                    {{+0.5f, -0.5f, +0.5f}, {1.0f,  0.0f,  0.0f},  {1.0f, 0.0f}},
                    {{+0.5f, +0.5f, +0.5f}, {1.0f,  0.0f,  0.0f},  {1.0f, 1.0f}},
                    {{+0.5f, +0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0.0f, 1.0f}},

                    {{+0.5f, -0.5f, +0.5f}, {0.0f,  0.0f,  1.0f},  {0.0f, 0.0f}},
                    {{-0.5f, -0.5f, +0.5f}, {0.0f,  0.0f,  1.0f},  {1.0f, 0.0f}},
                    {{-0.5f, +0.5f, +0.5f}, {0.0f,  0.0f,  1.0f},  {1.0f, 1.0f}},
                    {{+0.5f, +0.5f, +0.5f}, {0.0f,  0.0f,  1.0f},  {0.0f, 1.0f}},

                    {{-0.5f, -0.5f, +0.5f}, {-1.0f, 0.0f,  0.0f},  {0.0f, 0.0f}},
                    {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f,  0.0f},  {1.0f, 0.0f}},
                    {{-0.5f, +0.5f, -0.5f}, {-1.0f, 0.0f,  0.0f},  {1.0f, 1.0f}},
                    {{-0.5f, +0.5f, +0.5f}, {-1.0f, 0.0f,  0.0f},  {0.0f, 1.0f}},

                    {{-0.5f, -0.5f, +0.5f}, {0.0f,  -1.0f, 0.0f},  {0.0f, 0.0f}},
                    {{+0.5f, -0.5f, +0.5f}, {0.0f,  -1.0f, 0.0f},  {1.0f, 0.0f}},
                    {{+0.5f, -0.5f, -0.5f}, {0.0f,  -1.0f, 0.0f},  {1.0f, 1.0f}},
                    {{-0.5f, -0.5f, -0.5f}, {0.0f,  -1.0f, 0.0f},  {0.0f, 1.0f}},

                    {{-0.5f, +0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {0.0f, 0.0f}},
                    {{+0.5f, +0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {1.0f, 0.0f}},
                    {{+0.5f, +0.5f, +0.5f}, {0.0f,  1.0f,  0.0f},  {1.0f, 1.0f}},
                    {{-0.5f, +0.5f, +0.5f}, {0.0f,  1.0f,  0.0f},  {0.0f, 1.0f}},
            };

            std::vector<uint32_t> indices = {
                    0, 1, 2, 2, 3, 0,
                    4, 5, 6, 6, 7, 4,
                    8, 9, 10, 10, 11, 8,
                    12, 13, 14, 14, 15, 12,
                    16, 17, 18, 18, 19, 16,
                    20, 21, 22, 22, 23, 20,
            };

            cube_mesh.
                    vertex_buffer = new veekay::graphics::Buffer(
                    vertices.size() * sizeof(Vertex), vertices.data(),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

            cube_mesh.
                    index_buffer = new veekay::graphics::Buffer(
                    indices.size() * sizeof(uint32_t), indices.data(),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            cube_mesh.
                    indices = uint32_t(indices.size());
            models.
                    emplace_back(Model{
                                         .mesh = plane_mesh,
                                         .transform = Transform{
                                                 .position = {0.0f, 0.0f, 0.0}
                                         },
                                         .albedo_color = veekay::vec3{0.8f, 0.6f, 0.2f},
                                         .specular_color = veekay::vec3{1.0f, 1.0f, 1.0f},
                                         .shininess= 0.5f,
                                         .texture_path_albedo = "./assets/sw.png",
                                 }
            );

            models.
                    emplace_back(Model{
                                         .mesh = cube_mesh,
                                         .transform = Transform{
                                                 .position = {-2.0f, -0.6f, -1.5f},
                                         },
                                         .albedo_color = veekay::vec3{1.0f, 0.0f, 0.0f},
                                         .texture_path_specular = "./assets/white_circle.png"
                                 }
            );

            models.
                    emplace_back(Model{
                                         .mesh = cube_mesh,
                                         .transform = Transform{
                                                 .position = {1.5f, -0.6f, -0.5f},
                                         },
                                         .albedo_color = veekay::vec3{0.0f, 1.0f, 0.0f},
                                         .specular_color = veekay::vec3{1.0f, 1.0f, 1.0f},
                                         .shininess= 0.5f,
                                 }
            );

            models.
                    emplace_back(Model{
                                         .mesh = cube_mesh,
                                         .transform = Transform{
                                                 .position = {0.0f, -3.6f, 1.0f},
                                         },
                                         .albedo_color = veekay::vec3{0.0f, 0.0f, 1.0f},
                                 }
            );

            models.
                    emplace_back(Model{
                                         .mesh = cube_mesh,
                                         .transform = Transform{
                                                 .position = {8.5f, -0.6f, -0.5f},
                                         },
                                         .albedo_color = veekay::vec3{0.0f, 1.0f, 0.0f},
                                 }
            );

            models.
                    emplace_back(Model{
                                         .mesh = cube_mesh,
                                         .transform = Transform{
                                                 .position = {8.0f, -3.6f, 1.0f},
                                         },
                                         .albedo_color = veekay::vec3{0.0f, 0.0f, 1.0f},
                                         .texture_path_emissive = "./assets/points.png"
                                 }
            );


            point_lights.
                    emplace_back(PointLight{
                                         .position = {-2.0, -3.0, -3.0},
                                         .color = {1.0, 1.0, 1.0},
                                         .intensity = 10.0f,
                                 }
            );

            spot_lights.
                    emplace_back(SpotLight{
                                         .position = {1.0, 1.0, 1.0},
                                         .intensity = 20.0f,
                                         .direction = {0.0, 0.0, 1.0},
                                         .angle = 0.8660254,
                                         .color = {1.0, 1.0, 1.0},
                                 }
            );

            VkDevice &device = veekay::app.vk_device;
            VkPhysicalDevice &physical_device = veekay::app.vk_physical_device;

            { // NOTE: Build graphics pipeline
                vertex_shader_module = loadShaderModule("./shaders/shader.vert.spv");
                if (!vertex_shader_module) {
                    std::cerr << "Failed to load Vulkan vertex shader from file\n";
                    veekay::app.running = false;
                    return;
                }

                fragment_shader_module = loadShaderModule("./shaders/shader.frag.spv");
                if (!fragment_shader_module) {
                    std::cerr << "Failed to load Vulkan fragment shader from file\n";
                    veekay::app.running = false;
                    return;
                }

                VkPipelineShaderStageCreateInfo stage_infos[2];

                // NOTE: Vertex shader stage
                stage_infos[0] = VkPipelineShaderStageCreateInfo{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = vertex_shader_module,
                        .pName = "main",
                };

                // NOTE: Fragment shader stage
                stage_infos[1] = VkPipelineShaderStageCreateInfo{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = fragment_shader_module,
                        .pName = "main",
                };

                // NOTE: How many bytes does a vertex take?
                VkVertexInputBindingDescription buffer_binding{
                        .binding = 0,
                        .stride = sizeof(Vertex),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                };

                // NOTE: Declare vertex attributes
                VkVertexInputAttributeDescription attributes[] = {
                        {
                                .location = 0, // NOTE: First attribute
                                .binding = 0, // NOTE: First vertex buffer
                                .format = VK_FORMAT_R32G32B32_SFLOAT, // NOTE: 3-component vector of floats
                                .offset = offsetof(Vertex,
                                                   position), // NOTE: Offset of "position" field in a Vertex struct
                        },
                        {
                                .location = 1,
                                .binding = 0,
                                .format = VK_FORMAT_R32G32B32_SFLOAT,
                                .offset = offsetof(Vertex, normal),
                        },
                        {
                                .location = 2,
                                .binding = 0,
                                .format = VK_FORMAT_R32G32_SFLOAT,
                                .offset = offsetof(Vertex, uv),
                        }
                };

                // NOTE: Describe inputs
                VkPipelineVertexInputStateCreateInfo input_state_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                        .vertexBindingDescriptionCount = 1,
                        .pVertexBindingDescriptions = &buffer_binding,
                        .vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
                        .pVertexAttributeDescriptions = attributes,
                };

                // NOTE: Every three vertices make up a triangle,
                //       so our vertex buffer contains a "list of triangles"
                VkPipelineInputAssemblyStateCreateInfo assembly_state_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                };

                // NOTE: Declare clockwise triangle order as front-facing
                //       Discard triangles that are facing away
                //       Fill triangles, don't draw lines instaed
                VkPipelineRasterizationStateCreateInfo raster_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                        .polygonMode = VK_POLYGON_MODE_FILL,
                        .cullMode = VK_CULL_MODE_BACK_BIT,
                        .frontFace = VK_FRONT_FACE_CLOCKWISE,
                        .lineWidth = 1.0f,
                };

                // NOTE: Use 1 sample per pixel
                VkPipelineMultisampleStateCreateInfo sample_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                        .sampleShadingEnable = false,
                        .minSampleShading = 1.0f,
                };

                VkViewport viewport{
                        .x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(veekay::app.window_width),
                        .height = static_cast<float>(veekay::app.window_height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f,
                };

                VkRect2D scissor{
                        .offset = {0, 0},
                        .extent = {veekay::app.window_width, veekay::app.window_height},
                };

                // NOTE: Let rasterizer draw on the entire window
                VkPipelineViewportStateCreateInfo viewport_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,

                        .viewportCount = 1,
                        .pViewports = &viewport,

                        .scissorCount = 1,
                        .pScissors = &scissor,
                };

                // NOTE: Let rasterizer perform depth-testing and overwrite depth values on condition pass
                VkPipelineDepthStencilStateCreateInfo depth_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                        .depthTestEnable = true,
                        .depthWriteEnable = true,
                        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
                };

                // NOTE: Let fragment shader write all the color channels
                VkPipelineColorBlendAttachmentState attachment_info{
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT,
                };

                // NOTE: Let rasterizer just copy resulting pixels onto a buffer, don't blend
                VkPipelineColorBlendStateCreateInfo blend_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,

                        .logicOpEnable = false,
                        .logicOp = VK_LOGIC_OP_COPY,

                        .attachmentCount = 1,
                        .pAttachments = &attachment_info
                };

                {
                    VkDescriptorPoolSize pools[] = {
                            {
                                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    .descriptorCount = 8,
                            },
                            {
                                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                    .descriptorCount = 8,
                            },
                            {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .descriptorCount = 8,
                            },
                            {
                                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .descriptorCount = 32,
                            }
                    };

                    VkDescriptorPoolCreateInfo info{
                            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                            .maxSets = 10,
                            .poolSizeCount = sizeof(pools) / sizeof(pools[0]),
                            .pPoolSizes = pools,
                    };

                    if (vkCreateDescriptorPool(device, &info, nullptr,
                                               &descriptor_pool) != VK_SUCCESS) {
                        std::cerr << "Failed to create Vulkan descriptor pool\n";
                        veekay::app.running = false;
                        return;
                    }
                }


                // NOTE: Descriptor set layout specification
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                            {
                                    .binding = 0,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 2,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 3,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 4,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 5,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            {
                                    .binding = 6,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .descriptorCount = 1,
                                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            }
                    };


                    VkDescriptorSetLayoutCreateInfo info{
                            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                            .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
                            .pBindings = bindings,
                    };

                    if (vkCreateDescriptorSetLayout(device, &info, nullptr,
                                                    &descriptor_set_layout) != VK_SUCCESS) {
                        std::cerr << "Failed to create Vulkan descriptor set layout\n";
                        veekay::app.running = false;
                        return;
                    }

                    for (uint32_t i = 0; i < models.size(); i++) {

                        VkDescriptorSetAllocateInfo all_info{
                                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                .descriptorPool = descriptor_pool,
                                .descriptorSetCount = 1,
                                .pSetLayouts = &descriptor_set_layout,
                        };

                        if (vkAllocateDescriptorSets(device, &all_info, &models[i].texture_descriptors_set) !=
                            VK_SUCCESS) {
                            std::cout << i << '\n';
                            std::cerr << "Failed to create Vulkan descriptor set\n";
                            veekay::app.running = false;
                            return;
                        }

                    }
                }

                VkPipelineLayoutCreateInfo layout_info{
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                        .setLayoutCount = 1,
                        .pSetLayouts = &descriptor_set_layout,
                };

                // NOTE: Create pipeline layout
                if (vkCreatePipelineLayout(device, &layout_info,
                                           nullptr, &pipeline_layout) != VK_SUCCESS) {
                    std::cerr << "Failed to create Vulkan pipeline layout\n";
                    veekay::app.running = false;
                    return;
                }

                VkGraphicsPipelineCreateInfo info{
                        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                        .stageCount = 2,
                        .pStages = stage_infos,
                        .pVertexInputState = &input_state_info,
                        .pInputAssemblyState = &assembly_state_info,
                        .pViewportState = &viewport_info,
                        .pRasterizationState = &raster_info,
                        .pMultisampleState = &sample_info,
                        .pDepthStencilState = &depth_info,
                        .pColorBlendState = &blend_info,
                        .layout = pipeline_layout,
                        .renderPass = veekay::app.vk_render_pass,
                };

                // NOTE: Create graphics pipeline
                if (vkCreateGraphicsPipelines(device, nullptr,
                                              1, &info, nullptr, &pipeline) != VK_SUCCESS) {
                    std::cerr << "Failed to create Vulkan pipeline\n";
                    veekay::app.running = false;
                    return;
                }
            }

            scene_uniforms_buffer = new veekay::graphics::Buffer(
                    veekay::graphics::Buffer::structureAlignment(sizeof(SceneUniforms)),
                    nullptr,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            model_uniforms_buffer = new veekay::graphics::Buffer(
                    max_models * veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms)),
                    nullptr,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

            point_light_buffer = new veekay::graphics::Buffer(
                    max_lights * veekay::graphics::Buffer::structureAlignment(sizeof(PointLight)),
                    nullptr,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

            spotlight_buffer = new veekay::graphics::Buffer(
                    max_lights * veekay::graphics::Buffer::structureAlignment(sizeof(SpotLight)),
                    nullptr,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);


            // NOTE: This texture and sampler is used when texture could not be loaded

            VkSamplerCreateInfo info{
                    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                    .magFilter = VK_FILTER_LINEAR, // Фильтрация если плотность текселей меньше
                    .minFilter = VK_FILTER_LINEAR, // Фильтрация если плотность больше
                    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, // Фильтрация мип-мапов
                    // Что делать, если по какой-то из осей вышли за границы текстурных коорд-т
                    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    .anisotropyEnable = true, // Включить анизотропную фильтрацию?
                    .maxAnisotropy = 16.0f,   // Кол-во сэмплов анизотропной фильтрации
                    .minLod = 0.0f, // Минимальный уровень мипа
                    .maxLod = VK_LOD_CLAMP_NONE, // Максимальный уровень мипа (тут бескоченость)

            };

            {
                VkDescriptorBufferInfo buffer_infos[] = {
                        {
                                .buffer = scene_uniforms_buffer->buffer,
                                .offset = 0,
                                .range = sizeof(SceneUniforms),
                        },
                        {
                                .buffer = model_uniforms_buffer->buffer,
                                .offset = 0,
                                .range = sizeof(ModelUniforms),
                        },
                        {
                                .buffer = point_light_buffer->buffer,
                                .offset = 0,
                                .range = sizeof(PointLight),
                        },
                        {
                                .buffer = spotlight_buffer->buffer,
                                .offset = 0,
                                .range = sizeof(SpotLight),
                        },
                };


                for (uint32_t i = 0; i < models.size(); ++i) {
                    VkDescriptorImageInfo image_albedo;
                    VkDescriptorImageInfo image_specular;
                    VkDescriptorImageInfo image_emissive;
                    Model &m = models[i];

                    VkSampler texture_sampler;
                    if (vkCreateSampler(device, &info, nullptr, &texture_sampler) != VK_SUCCESS) {
                        std::cerr << "Failed to create Vulkan texture sampler\n";
                        veekay::app.running = false;
                        return;
                    }
                    samplers.push_back(texture_sampler);

                    {
                        uint32_t width, height;
                        std::vector<uint8_t> pixels_albedo;
                        lodepng::decode(pixels_albedo, width, height, m.texture_path_albedo);

                        veekay::graphics::Texture *texture_albedo =
                                new veekay::graphics::Texture(cmd, width, height, VK_FORMAT_R8G8B8A8_UNORM, pixels_albedo.data());
                        textures.push_back(texture_albedo);

                        image_albedo = {
                                .sampler = texture_sampler,
                                .imageView = texture_albedo->view,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        };
                    }

                    {
                        uint32_t width, height;
                        std::vector<uint8_t> pixels_specular;
                        lodepng::decode(pixels_specular, width, height, m.texture_path_specular);

                        veekay::graphics::Texture *texture_specular =
                                new veekay::graphics::Texture(cmd, width, height, VK_FORMAT_R8G8B8A8_UNORM, pixels_specular.data());
                        textures.push_back(texture_specular);

                        image_specular = {
                                .sampler = texture_sampler,
                                .imageView = texture_specular->view,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        };
                    }

                    {
                        uint32_t width, height;
                        std::vector<uint8_t> pixels_emissive;
                        lodepng::decode(pixels_emissive, width, height, m.texture_path_emissive);

                        veekay::graphics::Texture *texture_emissive =
                                new veekay::graphics::Texture(cmd, width, height, VK_FORMAT_R8G8B8A8_UNORM, pixels_emissive.data());
                        textures.push_back(texture_emissive);

                        image_emissive = {
                                .sampler = texture_sampler,
                                .imageView = texture_emissive->view,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        };
                    }

                    VkWriteDescriptorSet write_infos[] = {
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    .pBufferInfo = &buffer_infos[0],
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 1,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                    .pBufferInfo = &buffer_infos[1],
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 2,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .pBufferInfo = &buffer_infos[2],
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 3,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    .pBufferInfo = &buffer_infos[3],
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 4,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo = &image_albedo,
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 5,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo = &image_specular,
                            },
                            {
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet = m.texture_descriptors_set,
                                    .dstBinding = 6,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo = &image_emissive,
                            },
                    };

                    vkUpdateDescriptorSets(device, sizeof(write_infos) / sizeof(write_infos[0]), write_infos, 0, nullptr);
                }

            }


        }

    }

// NOTE: Destroy resources here, do not cause leaks in your program!
    void shutdown() {
        VkDevice &device = veekay::app.vk_device;

        vkDestroySampler(device, missing_texture_sampler, nullptr);

        delete cube_mesh.index_buffer;
        delete cube_mesh.vertex_buffer;

        delete plane_mesh.index_buffer;
        delete plane_mesh.vertex_buffer;

        for (auto& texture: textures) {
            texture->~Texture();
        }

        for (auto & sampler : samplers){
            vkDestroySampler(device, sampler, nullptr);
        }

        delete model_uniforms_buffer;
        delete scene_uniforms_buffer;
        delete point_light_buffer;
        delete spotlight_buffer;

        vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        vkDestroyShaderModule(device, fragment_shader_module, nullptr);
        vkDestroyShaderModule(device, vertex_shader_module, nullptr);

    }

    veekay::vec3 sun_dir = {0.0, -1.0, 0};
    veekay::vec3 test_point_light_position = {-2.0, -3.0, -3.0f};

    veekay::vec3 spot_light_pos = {10, -1, -5};
    veekay::vec3 spot_light_dir = {0.0, 0.0, -1.0};
    float spot_light_angle = 0.8660254;

    veekay::vec3 front = {0.0, 0.0, 1.0};

    void update(double time) {
        ImGui::Begin("General lightning:");
        ImGui::InputFloat3("Sun direction", reinterpret_cast<float *>(&sun_dir));

        ImGui::InputFloat3("Point light pos", reinterpret_cast<float *>(&test_point_light_position));

        ImGui::InputFloat3("Camera rot", reinterpret_cast<float *>(&camera.rotation));
        ImGui::InputFloat3("Camera pos", reinterpret_cast<float *>(&camera.position));
        ImGui::Checkbox("Use LookAt?", &camera.useLookAt);

        ImGui::InputFloat3("Spot light pos", reinterpret_cast<float *>(&spot_light_pos));
        ImGui::InputFloat("Angle", reinterpret_cast<float *>(&spot_light_angle));
        ImGui::InputFloat3("Spot light direction", reinterpret_cast<float *>(&spot_light_dir));
        ImGui::InputFloat("Shiness", reinterpret_cast<float *>(&models[0].shininess));
        ImGui::End();


        camera.forward.x = sin(camera.rotation.y) * cos(camera.rotation.x);
        camera.forward.y = -sin(camera.rotation.x);
        camera.forward.z = cos(camera.rotation.y) * cos(camera.rotation.x);
        camera.forward = veekay::vec3::normalized(camera.forward);

        camera.right = veekay::vec3::normalized(veekay::vec3::cross(camera.forward, {0.0, -1.0, 0.0}));
        camera.up = -veekay::vec3::normalized(veekay::vec3::cross(camera.right, camera.forward));

        if (!ImGui::IsWindowHovered()) {
            using namespace veekay::input;

            if (mouse::isButtonDown(mouse::Button::left)) {
                auto move_delta = mouse::cursorDelta();

                auto deltaX = move_delta.x * 0.001f;
                auto deltaY = move_delta.y * 0.001f;
                camera.rotation += {-deltaY, deltaX, 0};

                // TODO: Calculate right, up and front from view matrix



                if (keyboard::isKeyDown(keyboard::Key::w))
                    camera.position += camera.forward * 0.1f;

                if (keyboard::isKeyDown(keyboard::Key::s))
                    camera.position -= camera.forward * 0.1f;

                if (keyboard::isKeyDown(keyboard::Key::d))
                    camera.position += camera.right * 0.1f;

                if (keyboard::isKeyDown(keyboard::Key::a))
                    camera.position -= camera.right * 0.1f;

                if (keyboard::isKeyDown(keyboard::Key::q))
                    camera.position -= camera.up * 0.1f;

                if (keyboard::isKeyDown(keyboard::Key::e))
                    camera.position += camera.up * 0.1f;
            }
        }

        float aspect_ratio = float(veekay::app.window_width) / float(veekay::app.window_height);
        SceneUniforms scene_uniforms{
                .view_projection = camera.view_projection(aspect_ratio),
                .view_position = camera.position,
                .ambient_light_intensity = {0.00, 0.0, 0.00},
                .sun_light_direction = sun_dir,
                .sun_light_color = {0.5, 0.5, 0.5},
        };

        scene_uniforms.point_lights_count = point_lights.size();
        scene_uniforms.spot_lights_count = spot_lights.size();

        point_lights[0].position = test_point_light_position;

        spot_lights[0].position = spot_light_pos;
        spot_lights[0].angle = spot_light_angle;
        spot_lights[0].direction = spot_light_dir;

        std::vector<ModelUniforms> model_uniforms(models.size());
        for (size_t i = 0, n = models.size(); i < n; ++i) {
            const Model &model = models[i];
            ModelUniforms &uniforms = model_uniforms[i];

            uniforms.model = model.transform.matrix();
            uniforms.albedo_color = model.albedo_color;
            uniforms.specular_color = model.specular_color;
            uniforms.shininess = model.shininess;
        }

        *(SceneUniforms *) scene_uniforms_buffer->mapped_region = scene_uniforms;

        {
            const size_t alignment =
                    veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

            for (size_t i = 0, n = model_uniforms.size(); i < n; ++i) {
                const ModelUniforms &uniforms = model_uniforms[i];

                char *const pointer = static_cast<char *>(model_uniforms_buffer->mapped_region) + i * alignment;
                *reinterpret_cast<ModelUniforms *>(pointer) = uniforms;
            }
        }

        {
            const size_t alignment =
                    veekay::graphics::Buffer::structureAlignment(sizeof(PointLight));

            for (size_t i = 0; i < point_lights.size(); ++i) {
                const PointLight &light = point_lights[i];
                char *const pointer = static_cast<char *>(point_light_buffer->mapped_region) + i * alignment;
                *reinterpret_cast<PointLight *>(pointer) = light;
            }
        }

        {
            const size_t alignment =
                    veekay::graphics::Buffer::structureAlignment(sizeof(SpotLight));

            for (size_t i = 0; i < spot_lights.size(); ++i) {
                const auto &light = spot_lights[i];
                char *const pointer = static_cast<char *>(spotlight_buffer->mapped_region) + i * alignment;
                *reinterpret_cast<SpotLight *>(pointer) = light;
            }
        }
    }

    void render(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
        vkResetCommandBuffer(cmd, 0);

        { // NOTE: Start recording rendering commands
            VkCommandBufferBeginInfo info{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };

            vkBeginCommandBuffer(cmd, &info);
        }

        { // NOTE: Use current swapchain framebuffer and clear it
            VkClearValue clear_color{.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
            VkClearValue clear_depth{.depthStencil = {1.0f, 0}};

            VkClearValue clear_values[] = {clear_color, clear_depth};

            VkRenderPassBeginInfo info{
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    .renderPass = veekay::app.vk_render_pass,
                    .framebuffer = framebuffer,
                    .renderArea = {
                            .extent = {
                                    veekay::app.window_width,
                                    veekay::app.window_height
                            },
                    },
                    .clearValueCount = 2,
                    .pClearValues = clear_values,
            };

            vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize zero_offset = 0;

        VkBuffer current_vertex_buffer = VK_NULL_HANDLE;
        VkBuffer current_index_buffer = VK_NULL_HANDLE;

        const size_t model_uniorms_alignment =
                veekay::graphics::Buffer::structureAlignment(sizeof(ModelUniforms));

        for (size_t i = 0, n = models.size(); i < n; ++i) {
            const Model &model = models[i];
            const Mesh &mesh = model.mesh;

            if (current_vertex_buffer != mesh.vertex_buffer->buffer) {
                current_vertex_buffer = mesh.vertex_buffer->buffer;
                vkCmdBindVertexBuffers(cmd, 0, 1, &current_vertex_buffer, &zero_offset);
            }

            if (current_index_buffer != mesh.index_buffer->buffer) {
                current_index_buffer = mesh.index_buffer->buffer;
                vkCmdBindIndexBuffer(cmd, current_index_buffer, zero_offset, VK_INDEX_TYPE_UINT32);
            }

            uint32_t dynamicOffset = i * model_uniorms_alignment;
            vkCmdBindDescriptorSets(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_layout,
                    0,                    // set = 0 → UBO + SSBO
                    1,                    // count = 1
                    &models[i].texture_descriptors_set,      // descriptor set #0
                    1,                    // dynamicOffsetCount = 1
                    &dynamicOffset        // pointer to 1 offset
            );

            vkCmdDrawIndexed(cmd, mesh.indices, 1, 0, 0, 0);
        }


        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

} // namespace

int main() {
    return veekay::run({
                               .init = initialize,
                               .shutdown = shutdown,
                               .update = update,
                               .render = render,
                       });
}
