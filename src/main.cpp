#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined (__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class Application {

public:
	void run()
	{
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

private:
	GLFWwindow* window = nullptr;

	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
	vk::raii::SurfaceKHR surface = nullptr;
	vk::raii::PhysicalDevice physicalDevice = nullptr;
	vk::raii::Device device = nullptr;
	uint32_t queueIndex = ~0u;
	vk::raii::Queue queue = nullptr;
	vk::raii::SwapchainKHR swapChain = nullptr;
	std::vector<vk::Image> swapChainImages;
	vk::SurfaceFormatKHR swapChainSurfaceFormat;
	vk::Extent2D swapChainExtent;
	std::vector<vk::raii::ImageView> swapChainImageViews;

	vk::raii::PipelineLayout pipelineLayout = nullptr;
	vk::raii::Pipeline graphicsPipeline = nullptr;

	vk::raii::CommandPool commandPool = nullptr;
	vk::raii::CommandBuffer commandBuffer = nullptr;

	vk::raii::Semaphore presentCompleteSemaphore = nullptr;
	vk::raii::Semaphore renderFinishedSemaphore = nullptr;
	vk::raii::Fence drawFence = nullptr;

	std::vector<const char*> requiredDeviceExtension = {
		vk::KHRSwapchainExtensionName };

	void initWindow()
	{
		if (!glfwInit())
		{
			throw std::runtime_error("failed to initialize GLFW!");
		}
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		window = glfwCreateWindow(WIDTH, HEIGHT, "VulkanEngine", nullptr, nullptr);
		if (!window)
		{
			// cleanup() is not reached when run() throws, so GLFW is terminated here.
			glfwTerminate();
			throw std::runtime_error("failed to create the window!");
		}
	}

	void initVulkan()
	{
		// Each step prints a short diagnostic line to stdout once it completes.
		createInstance();
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();
		createGraphicsPipeline();
		createCommandPool();
		createCommandBuffer();
		createSyncObjects();
	}

	void mainLoop()
	{
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			drawFrame();
		}
		device.waitIdle(); // Ensure all device operations have completed before destroying resources.
	}

	void cleanup()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void createInstance()
	{
		auto extensions = context.enumerateInstanceExtensionProperties();
		std::cout << "available extensions:\n";
		for (const auto &extension : extensions) {
			std::cout << '\t' << extension.extensionName << '\n';
		}

		constexpr vk::ApplicationInfo appInfo {
			.pApplicationName = "cpp-vulkan-engine",
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = "VulkanEngine",
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = vk::ApiVersion14
		};

		// Retrieve the required layers.
		std::vector<char const*> requiredLayers;
		if (enableValidationLayers)
		{
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// Verify that the Vulkan implementation supports all required layers.
		auto layerProperties = context.enumerateInstanceLayerProperties();
		auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
			[&layerProperties](auto const& requiredLayer) {
				return std::ranges::none_of(layerProperties,
					[requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
			});
		if (unsupportedLayerIt != requiredLayers.end())
		{
			throw std::runtime_error("Unsupported required layer: " + std::string(*unsupportedLayerIt));
		}

		// Retrieve the required instance extensions.
		auto requiredExtensions = getRequiredInstanceExtensions();

		// Verify that the Vulkan implementation supports all required extensions.
		auto extensionProperties = context.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt =
			std::ranges::find_if(requiredExtensions,
				[&extensionProperties](auto const& requiredExtension) {
					return std::ranges::none_of(extensionProperties,
						[requiredExtension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
				});

		if (unsupportedPropertyIt != requiredExtensions.end())
		{
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

		vk::InstanceCreateInfo createInfo {
			.pApplicationInfo = &appInfo,
			.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
			.ppEnabledLayerNames = requiredLayers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
			.ppEnabledExtensionNames = requiredExtensions.data()
		};
		instance = vk::raii::Instance(context, createInfo);
	}

	void setupDebugMessenger()
	{
		if (!enableValidationLayers)
			return;

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
															vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{.messageSeverity = severityFlags,
																			  .messageType = messageTypeFlags,
																			  .pfnUserCallback = &debugCallback };

		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void createSurface()
	{
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != VK_SUCCESS)
		{
			throw std::runtime_error("window surface creation failed!");
		}
		surface = vk::raii::SurfaceKHR(instance, _surface);
	}

	// Determine whether a physicalDevice meets all the requirements for this application.
	bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice)
	{
		// Verify that the physicalDevice supports the Vulkan 1.3 API version.
		bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;

		// Verify whether any queue family supports both graphics and presentation to the surface.
		auto queueFamilies = physicalDevice.getQueueFamilyProperties();
		uint32_t qfpIndex = 0;
		bool supportsGraphicsAndPresent =
			std::ranges::any_of(queueFamilies,
				[&physicalDevice, &surface = this->surface, &qfpIndex](auto const &qfp) {
					bool const suitable = (qfp.queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface);
					qfpIndex++;
					return suitable;
			});

		// Verify that all required physicalDevice extensions are available.
		auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
		bool supportsAllRequiredExtensions =
			std::ranges::all_of(requiredDeviceExtension,
				[&availableDeviceExtensions](auto const &requiredDeviceExtension) {
					return std::ranges::any_of(availableDeviceExtensions,
						[requiredDeviceExtension](auto const& availableDeviceExtension) { return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; });
				});

		// Verify that the physicalDevice supports all required features.
		auto features = physicalDevice.template getFeatures2< vk::PhysicalDeviceFeatures2,
															  vk::PhysicalDeviceVulkan11Features,
															  vk::PhysicalDeviceVulkan13Features,
															  vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
		bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
										features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
										features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
										features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

		// Return true when the physicalDevice satisfies all required criteria.
		return supportsVulkan1_3 && supportsGraphicsAndPresent && supportsAllRequiredExtensions && supportsRequiredFeatures;
	}

	void pickPhysicalDevice()
	{
		std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
		auto const devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return isDeviceSuitable(physicalDevice); });
		if (devIter == physicalDevices.end())
		{
			throw std::runtime_error("failed to find GPUs with Vulkan support!");
		}

		physicalDevice = *devIter;
		std::cout << "selected GPU: " << physicalDevice.getProperties().deviceName << '\n';
	}

	void createLogicalDevice()
	{
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		// Locate the first queue family that supports both graphics and presentation.
		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
				physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
			{
				// Found a queue family supporting both graphics and presentation.
				queueIndex = qfpIndex;
				break;
			}
		}
		if (queueIndex == ~0u)
		{
			throw std::runtime_error("No queue family supports both graphics and presentation; terminating.");
		}

		// Query the available Vulkan 1.3 features.
		vk::StructureChain<vk::PhysicalDeviceFeatures2,
						   vk::PhysicalDeviceVulkan11Features,
						   vk::PhysicalDeviceVulkan13Features,
						   vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
			featureChain = {
				{}, // PhysicalDeviceFeatures2 left empty; only used to chain the structs below
				{.shaderDrawParameters = true},
				{.synchronization2 = true, .dynamicRendering = true},
				{.extendedDynamicState = true}
			};

		// Create the logical device.
		float queuePriority = 0.5f;
		vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = queueIndex, .queueCount = 1, .pQueuePriorities = &queuePriority };
		vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
			.ppEnabledExtensionNames = requiredDeviceExtension.data()};

		device = vk::raii::Device(physicalDevice, deviceCreateInfo);
		queue = vk::raii::Queue(device, queueIndex, 0);
		std::cout << "graphics queue family index: " << queueIndex << '\n';
	}

	void createSwapChain()
	{
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
		swapChainExtent = chooseSwapExtent(surfaceCapabilities);
		uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
		swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);
		vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{
			.surface = *surface,
			.minImageCount = minImageCount,
			.imageFormat = swapChainSurfaceFormat.format,
			.imageColorSpace = swapChainSurfaceFormat.colorSpace,
			.imageExtent = swapChainExtent,
			.imageArrayLayers = 1,
			.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
			.imageSharingMode = vk::SharingMode::eExclusive,
			.preTransform = surfaceCapabilities.currentTransform,
			.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
			.presentMode = presentMode,
			.clipped = true
		};

		swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
		swapChainImages = swapChain.getImages();
		std::cout << "swap chain images: " << swapChainImages.size()
				  << ", format: " << to_string(swapChainSurfaceFormat.format)
			      << ", extent: " << swapChainExtent.width << 'x' << swapChainExtent.height << '\n';
	}

	void createImageViews()
	{
		assert(swapChainImageViews.empty());

		vk::ImageViewCreateInfo imageViewCreateInfo{
			.viewType = vk::ImageViewType::e2D,
			.format = swapChainSurfaceFormat.format,
			// Color aspect, no mipmaps (1 level) and single layer (not an array texture).
			.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
		};

		for (auto &image : swapChainImages)
		{
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo);
		}

		std::cout << "image views created: " << swapChainImageViews.size() << '\n';
	}

	void createGraphicsPipeline()
	{
		// The compiled .spv contains both the vertex and fragment entry points,
		// so a single shader module is loaded and reused for both stages.
		vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/slang.spv"));
		vk::PipelineShaderStageCreateInfo vertShaderStageInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain" };
		vk::PipelineShaderStageCreateInfo fragShaderStageInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };
		vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		std::cout << "shader module loaded, stages: " << std::size(shaderStages) << '\n';

		// No vertex buffers yet: the vertices are hardcoded in the vertex shader.
		vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
		vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = vk::PrimitiveTopology::eTriangleList};
		// Only the counts are set; viewport and scissor are dynamic and set at draw time.
		vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1 };

		vk::PipelineRasterizationStateCreateInfo rasterizer{
			.depthClampEnable = vk::False,
			.rasterizerDiscardEnable = vk::False,
			.polygonMode = vk::PolygonMode::eFill,
			.cullMode = vk::CullModeFlagBits::eBack,
			// Clockwise because Vulkan's Y axis points down.
			.frontFace = vk::FrontFace::eClockwise,
			.depthBiasEnable = vk::False,
			.lineWidth = 1.0f
		};

		vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{
			.blendEnable = vk::False,
			.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
		};

		vk::PipelineColorBlendStateCreateInfo colorBlending{
			.logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1, .pAttachments = &colorBlendAttachment
		};

		// Viewport and scissor can change without recreating the pipeline (e.g. on resize).
		std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data()};

		// Empty layout (no descriptor sets, no push constants), but still required by the pipeline.
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 0, .pushConstantRangeCount = 0 };
		pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);
		std::cout << "pipeline layout created: " << (*pipelineLayout != VK_NULL_HANDLE ? "yes" : "no") << '\n';

		// Chained PipelineRenderingCreateInfo describes the attachment formats (dynamic rendering),
		// so no render pass is needed.
		vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
			{.stageCount = 2,
			 .pStages = shaderStages,
			 .pVertexInputState = &vertexInputInfo,
			 .pInputAssemblyState = &inputAssembly,
			 .pViewportState = &viewportState,
			 .pRasterizationState = &rasterizer,
			 .pMultisampleState = &multisampling,
			 .pColorBlendState = &colorBlending,
			 .pDynamicState = &dynamicState,
			 .layout = pipelineLayout,
			 .renderPass = nullptr},
			{.colorAttachmentCount = 1, .pColorAttachmentFormats = &swapChainSurfaceFormat.format}
		};

		// No pipeline cache (nullptr).
		graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
		std::cout << "graphics pipeline created: " << (*graphicsPipeline != VK_NULL_HANDLE ? "yes" : "no") << '\n';
	}

	void createCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{
			// eResetCommandBuffer lets the command buffer be re-recorded every frame.
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = queueIndex
		};

		commandPool = vk::raii::CommandPool(device, poolInfo);
		std::cout << "command pool created: " << (*commandPool != VK_NULL_HANDLE ? "yes" : "no") << '\n';
	}

	void createCommandBuffer()
	{
		vk::CommandBufferAllocateInfo allocInfo{
			.commandPool = commandPool,
			// Primary buffers can be submitted to a queue directly.
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = 1
		};

		commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
		std::cout << "command buffer allocated: " << (*commandBuffer != VK_NULL_HANDLE ? "yes" : "no") << '\n';
	}

	void recordCommandBuffer(uint32_t imageIndex)
	{
		commandBuffer.begin({});

		// Make the image writable as a color attachment.
		transition_image_layout(
			imageIndex,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			{},
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput
		);
		// Clear values are linear; the SRGB swap chain converts them, so 0.01 displays as dark gray.
		vk::ClearValue clearColor = vk::ClearColorValue(0.01f, 0.01f, 0.01f, 1.0f);

		vk::RenderingAttachmentInfo attachmentInfo = {
			.imageView = swapChainImageViews[imageIndex],
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = clearColor
		};

		vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &attachmentInfo
		};

		commandBuffer.beginRendering(renderingInfo);
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
		commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
		commandBuffer.draw(3, 1, 0, 0);
		commandBuffer.endRendering();

		// Make the image presentable.
		transition_image_layout(
			imageIndex,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			{},
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eBottomOfPipe
		);
		commandBuffer.end();
	}

	void transition_image_layout(
		uint32_t imageIndex,
		vk::ImageLayout old_layout,
		vk::ImageLayout new_layout,
		vk::AccessFlags2 src_access_mask,
		vk::AccessFlags2 dst_access_mask,
		vk::PipelineStageFlags2 src_stage_mask,
		vk::PipelineStageFlags2 dst_stage_mask
	)
	{
		vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = src_stage_mask,
			.srcAccessMask = src_access_mask,
			.dstStageMask = dst_stage_mask,
			.dstAccessMask = dst_access_mask,
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = swapChainImages[imageIndex],
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		vk::DependencyInfo dependency_info = {
			.dependencyFlags = {},
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier
		};

		commandBuffer.pipelineBarrier2(dependency_info);
	}

	void createSyncObjects()
	{
		presentCompleteSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
		renderFinishedSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
		// Created signaled so the first drawFrame() does not wait forever.
		drawFence = vk::raii::Fence(device, { .flags = vk::FenceCreateFlagBits::eSignaled });
		std::cout << "sync objects created: " << (*drawFence != VK_NULL_HANDLE ? "yes" : "no") << '\n';
	}

	// Frame: wait for the previous frame, acquire an image, recrod, submit, present.
	void drawFrame()
	{
		auto fenceResult = device.waitForFences(*drawFence, vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("Unable to wait for the fence!");
		}

		device.resetFences(*drawFence);

		auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphore, nullptr);

		recordCommandBuffer(imageIndex);

		// Temporary: wait for the previous frame to fully finish; to be replaced by frames in flight.
		queue.waitIdle();

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*presentCompleteSemaphore,
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*commandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*renderFinishedSemaphore
		};

		queue.submit(submitInfo, *drawFence);

		const vk::PresentInfoKHR presentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*renderFinishedSemaphore,
			.swapchainCount = 1,
			.pSwapchains = &*swapChain,
			.pImageIndices = &imageIndex
		};

		result = queue.presentKHR(presentInfoKHR);
		switch (result)
		{
			case vk::Result::eSuccess:
				break;
			case vk::Result::eSuboptimalKHR:
				std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
				break;
			default:
				break;
		}
	}

	[[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const
	{
		vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const uint32_t *>(code.data())};
		vk::raii::ShaderModule shaderModule{ device, createInfo };

		return shaderModule;
	}

	// Pick at least 3 swap chain images (triple buffering), clamped to what the surface allows.
	static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	// Prefer sRGB 8-bit color, falling back to the first available format.
	static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats)
	{
		assert(!availableFormats.empty());
		const auto formatIt = std::ranges::find_if(
			availableFormats,
			[](const auto &format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	// Prefer Mailbox for lower latency, falling back to Fifo (always guaranteed by Vulkan).
	static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	// Determine the swap chain image size, in pixels, clamped to the surface's limits.
	vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	// Combine GLFW's required extensions with the debug extension, if validation layers are enabled.
	std::vector<const char*> getRequiredInstanceExtensions() const
	{
		uint32_t glfwExtensionCount = 0;
		auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
		if (enableValidationLayers)
		{
			extensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		return extensions;
	}

	// Called by Vulkan whenever a validation layer has a message to report.
	static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
	{
		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		{
			std::cerr << "validation layer: type: " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
		}

		return vk::False;
	}

	// Read an entire binary file into memory. Opening with std::ios::ate
	// positions the cursor at the end, so tellg() gives the file size.
	static std::vector<char> readFile(const std::string &filename)
	{
		std::ifstream file(filename, std::ios::ate | std::ios::binary);
		if (!file.is_open())
		{
			throw std::runtime_error("failed to open the specified file!");
		}
		std::vector<char> buffer(file.tellg());
		file.seekg(0, std::ios::beg);
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		file.close();
		return buffer;
	}
};

int main()
{
	try
	{
		Application app;
		app.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
