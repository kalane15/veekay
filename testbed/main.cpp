#include <cstdint>
#include <climits>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

#include <veekay/veekay.hpp>

#include <imgui.h>
#include <vulkan/vulkan_core.h>
#include <utility.hpp>



namespace {

// NOTE: These variable will be available to shaders through push constant uniform


VkShaderModule vertex_shader_module;
VkShaderModule fragment_shader_module;
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// NOTE: Declare buffers and other variables here
VulkanBuffer vertex_buffer;
VulkanBuffer index_buffer;

Vector model_position = {0.0f, 0.0f, 5.0f};
float model_rotation;
Vector model_color = {0.5f, 1.0f, 0.7f };
bool model_spin = true;

// NOTE: Loads shader byte code from file
// NOTE: Your shaders are compiled via CMake with this code too, look it up
VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
    file.seekg(0, std::ifstream::end);
	size_t size = file.tellg();
    int test = file.tellg();
    auto a = size / sizeof(uint32_t);
	std::vector<uint32_t> buffer(size / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);
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
void post_init();
void initialize() {
	VkDevice& device = veekay::app.vk_device;
	VkPhysicalDevice& physical_device = veekay::app.vk_physical_device;

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
				.offset = offsetof(Vertex, position), // NOTE: Offset of "position" field in a Vertex struct
			},
            {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, color),
            }
        };
			// NOTE: If you want more attributes per vertex, declare them here

		// NOTE: Bring 
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

		// NOTE: Declare constant memory region visible to vertex and fragment shaders
		VkPushConstantRange push_constants{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
			              VK_SHADER_STAGE_FRAGMENT_BIT,
			.size = sizeof(ShaderConstants),
		};

		// NOTE: Declare external data sources, only push constants this time
		VkPipelineLayoutCreateInfo layout_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &push_constants,
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

	// TODO: You define model vertices and create buffers here
	// TODO: Index buffer has to be created here too
	// NOTE: Look for createBuffer function

	// (v0)------(v1)
	//  |  \       |
	//  |   `--,   |
	//  |       \  |
	// (v3)------(v2)
    post_init();
}

Figure square;
Figure pyramid;
Figure sphere;
    Figure cube2;
void shutdown() {
	VkDevice& device = veekay::app.vk_device;

	// NOTE: Destroy resources here, do not cause leaks in your program!
    square.DestroyBuffers();
    pyramid.DestroyBuffers();
    sphere.DestroyBuffers();
    cube2.DestroyBuffers();
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyShaderModule(device, fragment_shader_module, nullptr);
	vkDestroyShaderModule(device, vertex_shader_module, nullptr);
}


Vertex vertices[] = {
        {{-1.0f, -1.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}},
        {{-1.0f, -1.0f, 1.0f}},
        {{1.0f, -1.0f, 1.0f}},
        {{1.0f, 1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}},
};
uint32_t indices[] = {
        0, 1, 2,  2, 3, 0,
        4, 7, 6,  6, 5, 4,
        0, 3, 7,  7, 4, 0,
        1, 5, 6,  6, 2, 1,
        0, 4, 5,  5, 1, 0,
        3, 2, 6,  6, 7, 3
};

Vertex vertices2[] = {
        {{0.0f, -1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}},
        {{0.0f, 1.0f, 1.0f}},
};
uint32_t indices2[] = {
        0, 2, 1,
        3, 0, 1,
        2, 0, 3,
        2, 3, 1
};



std::pair<std::vector<Vertex>, std::vector<uint32_t >> generateSphere(float radius, int sectors) {
    std::vector<Vertex> vertexes_sphere = std::vector<Vertex>();
    std::vector<uint32_t > indexes_sphere = std::vector<uint32_t >();
    int stacks = sectors;
    for (int i = 0; i <= stacks; ++i) {
        float stackAngle = M_PI / 2 - (float)i * (M_PI / (float)stacks);
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);

        for (int j = 0; j <= sectors; ++j) {
            float sectorAngle = j * 2 * M_PI / (float)sectors;

            float x = xy * cosf(sectorAngle);
            float y = xy * sinf(sectorAngle);

            Vertex vertex = {};
            vertex.position = {x, y, z};

            vertexes_sphere.push_back(vertex);
        }
    }

    // Генерация индексов
    for (int i = 0; i < stacks; ++i) {
        int k1 = i * (sectors + 1);
        int k2 = k1 + sectors + 1;

        for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
            if (i != 0) {
                indexes_sphere.push_back(k1);
                indexes_sphere.push_back(k2);
                indexes_sphere.push_back(k1 + 1);
            }

            if (i != (stacks - 1)) {
                indexes_sphere.push_back(k1 + 1);
                indexes_sphere.push_back(k2);
                indexes_sphere.push_back(k2 + 1);
            }
        }
    }

    return {vertexes_sphere, indexes_sphere};
}


void post_init(){
    pyramid = Figure(vertices2, 4, indices2, 12);
    square = Figure(vertices, 8, indices, 36);
    auto p = generateSphere(1.0, 20);
    auto vertexes_sphere = p.first;
    auto indexes_sphere = p.second;
    sphere = Figure(vertexes_sphere, indexes_sphere);
    cube2 = Figure(vertices, 8, indices, 36);
}

Vector square_position = {0.0, 0.0, 5.0};
Vector sphere_position = {4.0, 0.0, 5.0};
Vector pyramid_position = {-4.0, 0.0, 5.0};
Vector cube_position = {0.0, 0.0, 3.0};

Vector cube_color = {255.0, 255.0, 255.0};
Vector sphere_color = {19, 115, 240};;
Vector cube2_color = {1.0, 1.0, 0.0};;
Vector pyramid_color1 = {255.0, 0.0, 0.0};
Vector pyramid_color2 = {0.0, 255.0, 0.0};
Vector pyramid_color3 = {0.0, 0.0, 0.0};
Vector pyramid_color4 = {0.0, 0.0, 255.0};

void update(double time) {
	ImGui::Begin("Controls:");
	ImGui::InputFloat3("Translation square", reinterpret_cast<float*>(&square_position));
	ImGui::InputFloat3("Translation sphere", reinterpret_cast<float*>(&sphere_position));
	ImGui::InputFloat3("Translation pyramid", reinterpret_cast<float*>(&pyramid_position));
	ImGui::InputFloat3("Translation cube2", reinterpret_cast<float*>(&cube_position));

    ImGui::InputFloat3("Color cube", reinterpret_cast<float*>(&cube_color));
    ImGui::InputFloat3("Color sphere", reinterpret_cast<float*>(&sphere_color));
    ImGui::InputFloat3("Color cube2", reinterpret_cast<float*>(&cube2_color));
    ImGui::InputFloat3("Color pyramid 1", reinterpret_cast<float*>(&pyramid_color1));
    ImGui::InputFloat3("Color pyramid 2", reinterpret_cast<float*>(&pyramid_color2));
    ImGui::InputFloat3("Color pyramid 3", reinterpret_cast<float*>(&pyramid_color3));
    ImGui::InputFloat3("Color pyramid 4", reinterpret_cast<float*>(&pyramid_color4));
	ImGui::SliderFloat("Rotation", &model_rotation, 0.0f, 2.0f * M_PI);
	ImGui::Checkbox("Spin?", &model_spin);
	// TODO: Your GUI stuff here
	ImGui::End();
	// NOTE: Animation code and other runtime variable updates go here
	if (model_spin) {
		model_rotation = float(time);
	}
    cube2.SetParent(&pyramid);
	model_rotation = fmodf(model_rotation, 2.0f * M_PI);
    square.SetLocalRotation({0.0, model_rotation, 0.0});
    pyramid.local_rotation.y = model_rotation;
    sphere.SetLocalRotation({0.0, model_rotation, 0.0});



    square.SetLocalPosition(square_position);
    sphere.SetLocalPosition(sphere_position);
    pyramid.SetLocalPosition(pyramid_position);

    square.SetColorRGB255ForAll(cube_color);
    sphere.SetColorRGB255ForAll(sphere_color);
    cube2.SetColorRGB255ForAll(cube2_color);

    pyramid.SetColorVertex(0, pyramid_color1);
    pyramid.SetColorVertex(1, pyramid_color2);
    pyramid.SetColorVertex(2, pyramid_color3);
    pyramid.SetColorVertex(3, pyramid_color4);
//    pyramid.SetColorRGB255ForAll({255.0, 0.0, 0.0});

    cube2.SetLocalPosition(cube_position);
    cube2.SetLocalRotation({0.0, model_rotation * 6, 0.0});
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

	// TODO: Vulkan rendering code here
	// NOTE: ShaderConstant updates, vkCmdXXX expected to be here

    square.Draw(cmd, pipeline, pipeline_layout);
    pyramid.Draw(cmd, pipeline, pipeline_layout);
    sphere.Draw(cmd, pipeline, pipeline_layout);
    cube2.Draw(cmd, pipeline, pipeline_layout);

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
