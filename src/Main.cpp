#include <stdio.h>

#include <vector>

#include "vulkan/vulkan.h"
#include "SDL.h"
#include "SDL_vulkan.h"
#include "glm/glm.hpp"

#include "ShaderBytecode/Triangle.vert.h"
#include "ShaderBytecode/Triangle.frag.h"
#include "Main.h"

struct Result
{
  static Result Application(int value)
  {
    Result res;
    res.category = Result::CATEGORY_APPLICATION;
    res.appResult = value;
    return res;
  }

  static Result Vulkan(VkResult value)
  {
    Result res;
    res.category = Result::CATEGORY_VULKAN;
    res.vkResult = value;
    return res;
  }

  bool Success() const
  {
    switch (category)
    {
      case CATEGORY_APPLICATION:
      return appResult == 0;
      case CATEGORY_VULKAN:
      return vkResult >= 0;
    }
    return false;
  }

  enum Category
  {
    CATEGORY_APPLICATION = 0,
    CATEGORY_VULKAN = 1
  } category;
  union
  {
    int appResult;
    VkResult vkResult;
  };
};

void HandleResult(Result res, const char* message)
{
  if (!message) message = "";
  if (res.category == Result::CATEGORY_APPLICATION && res.appResult != 0)
  {
    fprintf(stderr, "Application failure [%d]: %s\n", res.appResult, message);
  }
  else if (res.category == Result::CATEGORY_VULKAN && res.vkResult != VK_SUCCESS)
  {
    fprintf(stderr, "Vulkan failure: [%d]: %s\n", res.vkResult, message);
  }
}

// Macro used inside functions that return Result
// for early exit from function (instead of exceptions, yeah)
#define RETURN_IF_FAILURE(result, message)      \
{                                               \
  Result resCopy = result;                      \
  if (!resCopy.Success())                       \
  {                                             \
    HandleResult(resCopy, message);             \
    return resCopy;                             \
  }                                             \
}

static SDL_Window* g_Window;
static uint32_t g_WindowWidth = 600;
static uint32_t g_WindowHeight = 600;
static uint32_t g_DrawableWidth;
static uint32_t g_DrawableHeight;
static bool g_DrawableChanged = false;

Result InitWindow()
{
  RETURN_IF_FAILURE(Result::Application(
    SDL_Init(SDL_INIT_VIDEO) != 0 ? 1 : 0),
    "SDL_Init");

  g_Window = SDL_CreateWindow(
    "",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    (int)g_WindowWidth, (int)g_WindowHeight,
    SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

  RETURN_IF_FAILURE(Result::Application(
    g_Window == nullptr ? 2 : 0),
    "SDL_CreateWindow");

  int w, h;
  SDL_Vulkan_GetDrawableSize(g_Window, &w, &h);
  g_DrawableWidth = (uint32_t)w;
  g_DrawableHeight = (uint32_t)h;

  return Result::Application(0);
}

void DestroyWindow()
{
  SDL_DestroyWindow(g_Window);
  g_Window = nullptr;
}

static VkInstance g_Instance = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT g_DebugMessenger = VK_NULL_HANDLE;
static VkSurfaceKHR g_Surface = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
static uint32_t g_GraphicsQueueFamily = (uint32_t)-1;
static uint32_t g_TransferQueueFamily = (uint32_t)-1;
static uint32_t g_ComputeQueueFamily = (uint32_t)-1;
static uint32_t g_PresentQueueFamily = (uint32_t)-1;
static VkDevice g_Device = VK_NULL_HANDLE;
static VkQueue g_GraphicsQueue = VK_NULL_HANDLE;
static VkQueue g_TransferQueue = VK_NULL_HANDLE;
static VkQueue g_PresentQueue = VK_NULL_HANDLE;
static VkSwapchainKHR g_Swapchain = VK_NULL_HANDLE;
static VkFormat g_SwapchainFormat;
static VkExtent2D g_SwapchainExtent;
static std::vector<VkImage> g_SwapchainImages;
static std::vector<VkImageView> g_SwapchainImageViews;
static VkShaderModule g_TriangleShaderVert;
static VkShaderModule g_TriangleShaderFrag;
static VkPipelineCache g_PipelineCache;
static VkPipelineLayout g_PipelineLayout;
static VkRenderPass g_RenderPass;
static VkPipeline g_GraphicsPipeline;
static std::vector<VkFramebuffer> g_SwapchainFramebuffers;
static VkCommandPool g_GraphicsCommandPool;
static VkCommandPool g_TransferCommandPool;
static std::vector<VkCommandBuffer> g_GraphicsCommandBuffers;
static VkCommandBuffer g_TransferCommandBuffer;
static VkDeviceMemory g_TriangleBufferMemory;
static VkBuffer g_TriangleBuffer;
static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
static uint32_t g_CurrentFrame = 0;
static std::vector<VkSemaphore> g_ImageAvailableSemaphores;
static std::vector<VkSemaphore> g_RenderFinishedSemaphores;
static std::vector<VkFence> g_GraphicsCommandBufferIsUsedFences;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData)
{
  const char* severity = "";
  switch (messageSeverity)
  {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: severity = "INFO"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: severity = "WARNING"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: severity = "ERROR"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: severity = "VERBOSE"; break;
  }
  const char* type = "";
  switch (messageType)
  {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: type = "GENERAL"; break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT: type = "VALIDATION"; break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: type = "PERFORMANCE"; break;
  }
  fprintf(stderr, "Vk Validation [%s, %s]: %s\n", severity, type, pCallbackData->pMessage);
  return VK_FALSE;
}

Result InitVkInstance()
{
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
  appInfo.pEngineName = "";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  std::vector<const char*> instanceLayers;
  std::vector<const char*> instanceExtensions;

  unsigned int numExtensions;
  if (!SDL_Vulkan_GetInstanceExtensions(g_Window, &numExtensions, nullptr))
  {
    return Result::Application(1);
  }
  instanceExtensions.resize(numExtensions);
  if (!SDL_Vulkan_GetInstanceExtensions(g_Window, &numExtensions, instanceExtensions.data()))
  {
    return Result::Application(1);
  }

#ifndef NDEBUG
  instanceExtensions.push_back("VK_EXT_debug_utils");
  instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  uint32_t numInstanceLayers = (uint32_t)instanceLayers.size();
  uint32_t numInstanceExtensions = (uint32_t)instanceExtensions.size();

  VkInstanceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &appInfo;
  ci.enabledLayerCount = numInstanceLayers;
  ci.ppEnabledLayerNames = numInstanceLayers > 0 ? instanceLayers.data() : nullptr;
  ci.enabledExtensionCount = numInstanceExtensions;
  ci.ppEnabledExtensionNames = numInstanceExtensions > 0 ? instanceExtensions.data() : nullptr;

  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreateInstance(&ci, nullptr, &g_Instance)),
    "vkCreateInstance");

  return Result::Application(0);
}

void DestroyVkInstance()
{
  if (g_Instance != VK_NULL_HANDLE)
  {
    vkDestroyInstance(g_Instance, nullptr);
    g_Instance = VK_NULL_HANDLE;
  }
}

Result InitVkDebugMessenger()
{
  VkDebugUtilsMessengerCreateInfoEXT ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  ci.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  ci.messageType =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  ci.pfnUserCallback = DebugCallback;
  ci.pUserData = nullptr;

  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
    vkGetInstanceProcAddr(g_Instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr)
  {
    RETURN_IF_FAILURE(Result::Vulkan(func(g_Instance, &ci, nullptr, &g_DebugMessenger)),
                      "vkCreateDebugUtilsMessengerEXT");
  }
  else
  {
    return Result::Vulkan(VK_ERROR_EXTENSION_NOT_PRESENT);
  }

  return Result::Application(0);
}

void DestroyVkDebugMessenger()
{
  if (g_DebugMessenger != VK_NULL_HANDLE)
  {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      g_Instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func)
    {
      func(g_Instance, g_DebugMessenger, nullptr);
    }

    g_DebugMessenger = VK_NULL_HANDLE;
  }
}

Result InitVkSurface()
{
  return Result::Application(
    SDL_Vulkan_CreateSurface(g_Window, g_Instance, &g_Surface) == SDL_TRUE ? 0 : 1);
}

void DestroyVkSurface()
{
  if (g_Surface != VK_NULL_HANDLE)
  {
    vkDestroySurfaceKHR(g_Instance, g_Surface, nullptr);
    g_Surface = VK_NULL_HANDLE;
  }
}

Result InitVkPhysicalDevice()
{
  uint32_t numPhysicalDevices;
  RETURN_IF_FAILURE(Result::Vulkan(
    vkEnumeratePhysicalDevices(g_Instance, &numPhysicalDevices, nullptr)), "");
  std::vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  RETURN_IF_FAILURE(Result::Vulkan(
    vkEnumeratePhysicalDevices(g_Instance, &numPhysicalDevices, physicalDevices.data())), "");

  const char* requiredExtensions[] = {
    "VK_KHR_swapchain"
  };

  for (VkPhysicalDevice physicalDevice : physicalDevices)
  {
    g_GraphicsQueueFamily = (uint32_t)-1;
    g_TransferQueueFamily = (uint32_t)-1;
    g_ComputeQueueFamily = (uint32_t)-1;
    g_PresentQueueFamily = (uint32_t)-1;

    std::vector<VkQueueFamilyProperties> queueFamilies;
    {
      uint32_t numQueueFamilies;
      vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numQueueFamilies, nullptr);
      queueFamilies.resize(numQueueFamilies);
      vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numQueueFamilies, queueFamilies.data());
    }

    {
      uint32_t queueFamilyIdx = 0;

      for (auto const& queueFamilyProperties : queueFamilies)
      {
        if (queueFamilyProperties.queueCount > 0)
        {
          if (g_GraphicsQueueFamily == (uint32_t)-1
              && queueFamilyProperties.queueFlags | VK_QUEUE_GRAPHICS_BIT)
          {
            g_GraphicsQueueFamily = queueFamilyIdx;
          }
          if (g_TransferQueueFamily == (uint32_t)-1
              && queueFamilyProperties.queueFlags | VK_QUEUE_TRANSFER_BIT)
          {
            g_TransferQueueFamily = queueFamilyIdx;
          }
          if (g_ComputeQueueFamily == (uint32_t)-1
              && queueFamilyProperties.queueFlags | VK_QUEUE_COMPUTE_BIT)
          {
            g_ComputeQueueFamily = queueFamilyIdx;
          }
          VkBool32 presentSupported;
          if (g_PresentQueueFamily == (uint32_t)-1
              && vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIdx, g_Surface, &presentSupported) == VK_SUCCESS
              && presentSupported)
          {
            g_PresentQueueFamily = queueFamilyIdx;
          }
        }

        queueFamilyIdx++;
      }

      if (g_GraphicsQueueFamily == (uint32_t)-1
          || g_TransferQueueFamily == (uint32_t)-1
          || g_ComputeQueueFamily == (uint32_t)-1
          || g_PresentQueueFamily == (uint32_t)-1)
      {
        continue;
      }
    }

    bool requiredExtensionsSupported = true;
    {
      {
        uint32_t numExtensions;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numExtensions, nullptr) != VK_SUCCESS)
        {
          continue;
        }
        std::vector<VkExtensionProperties> extensions(numExtensions);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &numExtensions, extensions.data()) != VK_SUCCESS)
        {
          continue;
        }
        for (const char* requiredExtension : requiredExtensions)
        {
          bool found = false;
          for (auto const& extension : extensions)
          {
            if (strcmp(extension.extensionName, requiredExtension) == 0)
            {
              found = true;
              break;
            }
          }
          if (!found)
          {
            requiredExtensionsSupported = false;
            break;
          }
        }
      }
    }
    if (!requiredExtensionsSupported)
    {
      continue;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
      continue;
    }

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);

    uint32_t numSurfaceFormats;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, g_Surface, &numSurfaceFormats, nullptr) != VK_SUCCESS)
    {
      continue;
    }
    if (numSurfaceFormats == 0)
    {
      continue;
    }


    uint32_t numSurfacePresentModes;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, g_Surface, &numSurfacePresentModes, nullptr) != VK_SUCCESS)
    {
      continue;
    }
    if (numSurfacePresentModes == 0)
    {
      continue;
    }

    g_PhysicalDevice = physicalDevice;
    break;
  }

  return Result::Application(g_PhysicalDevice == VK_NULL_HANDLE ? 1 : 0);
}

void DestroyVkPhysicalDevice()
{
  g_PresentQueueFamily = (uint32_t)-1;
  g_GraphicsQueueFamily = (uint32_t)-1;
  g_TransferQueueFamily = (uint32_t)-1;
  g_ComputeQueueFamily = (uint32_t)-1;
  g_PhysicalDevice = VK_NULL_HANDLE;
}

Result InitVkDevice()
{
  std::vector<uint32_t> queueFamilies = {
    g_GraphicsQueueFamily, g_PresentQueueFamily, g_TransferQueueFamily };

  std::vector<uint32_t> usedQueueFamilies;
  std::vector<VkDeviceQueueCreateInfo> queueCIs;

  for (uint32_t queueFamily : queueFamilies)
  {
    bool used = false;
    for (uint32_t usedQueueFamily : usedQueueFamilies)
    {
      if (queueFamily == usedQueueFamily)
      {
        used = true;
        break;
      }
    }
    if (used)
    {
      continue;
    }
    usedQueueFamilies.push_back(queueFamily);

    float queuePriorities[] = { 1.0f };

    VkDeviceQueueCreateInfo queueCI = {};
    queueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = queueFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = queuePriorities;
    queueCIs.push_back(queueCI);
  }

  VkPhysicalDeviceFeatures features = {};

  std::vector<const char*> layers;
  std::vector<const char*> extensions = {
    "VK_KHR_swapchain" };

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.queueCreateInfoCount = (uint32_t)queueCIs.size();
  ci.pQueueCreateInfos = queueCIs.data();
  ci.pEnabledFeatures = &features;
  ci.enabledLayerCount = (uint32_t)layers.size();
  if (layers.size() > 0)
  {
    ci.ppEnabledLayerNames = layers.data();
  }
  ci.enabledExtensionCount = (uint32_t)extensions.size();
  if (extensions.size() > 0)
  {
    ci.ppEnabledExtensionNames = extensions.data();
  }

  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreateDevice(g_PhysicalDevice, &ci, nullptr, &g_Device)),
    "vkCreateDevice");

  vkGetDeviceQueue(g_Device, g_GraphicsQueueFamily, 0, &g_GraphicsQueue);
  vkGetDeviceQueue(g_Device, g_TransferQueueFamily, 0, &g_TransferQueue);
  vkGetDeviceQueue(g_Device, g_PresentQueueFamily, 0, &g_PresentQueue);

  return Result::Application(0);
}

void DestroyVkDevice()
{
  g_GraphicsQueue = VK_NULL_HANDLE;
  g_PresentQueue = VK_NULL_HANDLE;

  if (g_Device != VK_NULL_HANDLE)
  {
    vkDestroyDevice(g_Device, nullptr);
    g_Device = VK_NULL_HANDLE;
  }
}

Result InitVkSwapchain()
{
  std::vector<VkSurfaceFormatKHR> availableFormats;
  {
    uint32_t numFormats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_Surface, &numFormats, nullptr);
    availableFormats.resize(numFormats);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_Surface, &numFormats, availableFormats.data());
  }

  std::vector<VkPresentModeKHR> availablePresentModes;
  {
    uint32_t numPresentModes;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_Surface, &numPresentModes, nullptr);
    availablePresentModes.resize(numPresentModes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_Surface, &numPresentModes, availablePresentModes.data());
  }

  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PhysicalDevice, g_Surface, &capabilities);

  VkSurfaceFormatKHR format;
  {
    if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
    {
      format.format = VK_FORMAT_B8G8R8A8_UNORM;
      format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    }
    else
    {
      bool desiredFormatFound = false;
      for (auto const& availableFormat : availableFormats)
      {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM
            && availableFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
        {
          format = availableFormat;
          desiredFormatFound = true;
          break;
        }
      }
      if (!desiredFormatFound)
      {
        format = availableFormats[0];
      }
    }
  }

  VkPresentModeKHR presentMode;
  {
    presentMode = VK_PRESENT_MODE_FIFO_KHR;

    for (auto availablePresentMode : availablePresentModes)
    {
      if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
      {
        presentMode = availablePresentMode;
        break;
      }
      else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
      {
        presentMode = availablePresentMode;
        break;
      };
    }
  }

  VkExtent2D extent;
  {
    if (capabilities.currentExtent.width != (uint32_t)-1)
    {
      extent = capabilities.currentExtent;
    }
    else
    {
      extent.width = g_WindowWidth;
      extent.height = g_WindowHeight;

      extent.width = glm::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
      extent.height = glm::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0
      && imageCount > capabilities.maxImageCount)
  {
    imageCount = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR swapchainCI = {};
  swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCI.surface = g_Surface;
  swapchainCI.minImageCount = imageCount;
  swapchainCI.imageFormat = format.format;
  swapchainCI.imageColorSpace = format.colorSpace;
  swapchainCI.imageExtent = extent;
  swapchainCI.imageArrayLayers = 1;
  swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  uint32_t indices[] = { g_GraphicsQueueFamily, g_PresentQueueFamily };
  if (g_GraphicsQueueFamily != g_PresentQueueFamily)
  {
    swapchainCI.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchainCI.queueFamilyIndexCount = 2;
    swapchainCI.pQueueFamilyIndices = indices;
  }
  else
  {
    swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  swapchainCI.preTransform = capabilities.currentTransform;
  swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCI.presentMode = presentMode;
  swapchainCI.clipped = VK_TRUE;
  swapchainCI.oldSwapchain = VK_NULL_HANDLE;

  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreateSwapchainKHR(g_Device, &swapchainCI, nullptr, &g_Swapchain)),
    "vkCreateSwapchainKHR");

  uint32_t numImages;
  RETURN_IF_FAILURE(Result::Vulkan(
    vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &numImages, nullptr)),
    "vkGetSwapchainImagesKHR");
  g_SwapchainImages.resize(numImages);
  RETURN_IF_FAILURE(Result::Vulkan(
    vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &numImages, g_SwapchainImages.data())),
    "vkGetSwapchainImagesKHR");

  g_SwapchainFormat = format.format;
  g_SwapchainExtent = extent;

  g_SwapchainImageViews.resize(numImages);
  for (uint32_t i = 0; i < numImages; i++)
  {
    VkImageViewCreateInfo imageViewCI = {};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.image = g_SwapchainImages[i];
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.format = g_SwapchainFormat;
    imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;

    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateImageView(g_Device, &imageViewCI, nullptr, &g_SwapchainImageViews[i])),
      "vkCreateImageView");
  }

  return Result::Application(0);
}

void DestroyVkSwapchain()
{
  for (VkImageView imageView : g_SwapchainImageViews)
  {
    if (imageView != VK_NULL_HANDLE)
    {
      vkDestroyImageView(g_Device, imageView, nullptr);
    }
  }
  g_SwapchainImageViews.clear();

  g_SwapchainImages.clear();

  if (g_Swapchain != VK_NULL_HANDLE)
  {
    vkDestroySwapchainKHR(g_Device, g_Swapchain, nullptr);
    g_Swapchain = VK_NULL_HANDLE;
  }
}

Result InitVkGraphicsPipeline()
{
  {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pCode = Triangle_frag_bytecode;
    ci.codeSize = sizeof(Triangle_frag_bytecode);
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateShaderModule(g_Device, &ci, nullptr, &g_TriangleShaderFrag)),
      "vkCreateShaderModule");
  }

  {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.pCode = Triangle_vert_bytecode;
    ci.codeSize = sizeof(Triangle_vert_bytecode);
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateShaderModule(g_Device, &ci, nullptr, &g_TriangleShaderVert)),
      "vkCreateShaderModule");
  }

  VkPipelineCacheCreateInfo pipelineCacheCI = {};
  pipelineCacheCI.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreatePipelineCache(g_Device, &pipelineCacheCI, nullptr, &g_PipelineCache)),
    "vkCreatePipelineCache");

  VkPipelineShaderStageCreateInfo shaderStageCIs[2];
  shaderStageCIs[0] = {};
  shaderStageCIs[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageCIs[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStageCIs[0].module = g_TriangleShaderVert;
  shaderStageCIs[0].pName = "main";
  shaderStageCIs[1] = {};
  shaderStageCIs[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageCIs[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStageCIs[1].module = g_TriangleShaderFrag;
  shaderStageCIs[1].pName = "main";

  VkVertexInputAttributeDescription vertexAttributeDescs[2];
  vertexAttributeDescs[0] = {};
  vertexAttributeDescs[0].binding = 0;
  vertexAttributeDescs[0].location = 0;
  vertexAttributeDescs[0].offset = 0;
  vertexAttributeDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
  vertexAttributeDescs[1] = {};
  vertexAttributeDescs[1].binding = 0;
  vertexAttributeDescs[1].location = 1;
  vertexAttributeDescs[1].offset = 2 * sizeof(float);
  vertexAttributeDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;

  VkVertexInputBindingDescription vertexBindingDesc = {};
  vertexBindingDesc.binding = 0;
  vertexBindingDesc.stride = 5 * sizeof(float);
  vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {};
  vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexAttributeDescriptionCount = 2;
  vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescs;
  vertexInputStateCI.vertexBindingDescriptionCount = 1;
  vertexInputStateCI.pVertexBindingDescriptions = &vertexBindingDesc;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport = {};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = (float)g_SwapchainExtent.width;
  viewport.height = (float)g_SwapchainExtent.height;

  VkRect2D scissor = {};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent = g_SwapchainExtent;

  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.pViewports = &viewport;
  viewportStateCI.scissorCount = 1;
  viewportStateCI.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationStateCI.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationStateCI.depthBiasEnable = VK_FALSE;
  rasterizationStateCI.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCI.depthTestEnable = VK_FALSE;
  depthStencilStateCI.stencilTestEnable = VK_FALSE;
  depthStencilStateCI.minDepthBounds = 0.0f;
  depthStencilStateCI.maxDepthBounds = 1.0f;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {};
  colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.logicOpEnable = VK_FALSE;
  colorBlendStateCI.attachmentCount = 1;
  colorBlendStateCI.pAttachments = &colorBlendAttachment;

  VkDynamicState dynamicState = VK_DYNAMIC_STATE_VIEWPORT;

  VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.dynamicStateCount = 1;
  dynamicStateCI.pDynamicStates = &dynamicState;

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.size = sizeof(float);
  pushConstantRange.offset = 0;
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreatePipelineLayout(g_Device, &pipelineLayoutCI, nullptr, &g_PipelineLayout)),
    "vkCreatePipelineLayout");

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = g_SwapchainFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

  VkRenderPassCreateInfo renderPassCI = {};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = 1;
  renderPassCI.pAttachments = &colorAttachment;
  renderPassCI.subpassCount = 1;
  renderPassCI.pSubpasses = &subpass;
  renderPassCI.dependencyCount = 1;
  renderPassCI.pDependencies = &dependency;
  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreateRenderPass(g_Device, &renderPassCI, nullptr, &g_RenderPass)),
    "vkCreateRenderPass");

  VkGraphicsPipelineCreateInfo pipelineCI = {};
  pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCI.stageCount = 2;
  pipelineCI.pStages = shaderStageCIs;
  pipelineCI.pVertexInputState = &vertexInputStateCI;
  pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  pipelineCI.pTessellationState = nullptr;
  pipelineCI.pViewportState = &viewportStateCI;
  pipelineCI.pRasterizationState = &rasterizationStateCI;
  pipelineCI.pMultisampleState = &multisampleStateCI;
  pipelineCI.pDepthStencilState = &depthStencilStateCI;
  pipelineCI.pColorBlendState = &colorBlendStateCI;
  pipelineCI.pDynamicState = &dynamicStateCI;
  pipelineCI.layout = g_PipelineLayout;
  pipelineCI.renderPass = g_RenderPass;
  pipelineCI.subpass = 0;

  RETURN_IF_FAILURE(Result::Vulkan(
    vkCreateGraphicsPipelines(g_Device, g_PipelineCache, 1, &pipelineCI, nullptr, &g_GraphicsPipeline)),
    "vkCreateGraphicsPipelines");

  return Result::Application(0);
}

void DestroyVkGraphicsPipeline()
{
  if (g_GraphicsPipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(g_Device, g_GraphicsPipeline, nullptr);
    g_GraphicsPipeline = VK_NULL_HANDLE;
  }

  if (g_RenderPass != VK_NULL_HANDLE)
  {
    vkDestroyRenderPass(g_Device, g_RenderPass, nullptr);
    g_RenderPass = VK_NULL_HANDLE;
  }

  if (g_PipelineLayout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(g_Device, g_PipelineLayout, nullptr);
    g_PipelineLayout = VK_NULL_HANDLE;
  }

  if (g_PipelineCache != VK_NULL_HANDLE)
  {
    vkDestroyPipelineCache(g_Device, g_PipelineCache, nullptr);
    g_PipelineCache = VK_NULL_HANDLE;
  }

  if (g_TriangleShaderVert != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(g_Device, g_TriangleShaderVert, nullptr);
    g_TriangleShaderVert = VK_NULL_HANDLE;
  }

  if (g_TriangleShaderFrag != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(g_Device, g_TriangleShaderFrag, nullptr);
    g_TriangleShaderFrag = VK_NULL_HANDLE;
  }
}

Result InitVkSwapchainFramebuffers()
{
  g_SwapchainFramebuffers.resize(g_SwapchainImageViews.size());

  for (uint32_t i = 0; i < (uint32_t)g_SwapchainImageViews.size(); i++)
  {
    VkFramebufferCreateInfo framebufferCI = {};
    framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferCI.renderPass = g_RenderPass;
    framebufferCI.attachmentCount = 1;
    framebufferCI.pAttachments = &g_SwapchainImageViews[i];
    framebufferCI.width = g_SwapchainExtent.width;
    framebufferCI.height = g_SwapchainExtent.height;
    framebufferCI.layers = 1;
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateFramebuffer(g_Device, &framebufferCI, nullptr, &g_SwapchainFramebuffers[i])),
      "vkCreateFramebuffer");
  }

  return Result::Application(0);
}

void DestroyVkSwapchainFramebuffers()
{
  for (auto framebuffer : g_SwapchainFramebuffers)
  {
    if (framebuffer != VK_NULL_HANDLE)
    {
      vkDestroyFramebuffer(g_Device, framebuffer, nullptr);
    }
  }
  g_SwapchainFramebuffers.clear();
}

Result InitVkCommandPools()
{
  {
    VkCommandPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCI.queueFamilyIndex = g_GraphicsQueueFamily;
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateCommandPool(g_Device, &poolCI, nullptr, &g_GraphicsCommandPool)),
      "vkCreateCommandPool");

    poolCI.queueFamilyIndex = g_TransferQueueFamily;
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateCommandPool(g_Device, &poolCI, nullptr, &g_TransferCommandPool)),
      "vkCreateCommandPool");
  }

  return Result::Application(0);
}

void DestroyVkCommandPools()
{
  if (g_TransferCommandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(g_Device, g_TransferCommandPool, nullptr);
    g_TransferCommandPool = VK_NULL_HANDLE;
  }

  if (g_GraphicsCommandPool != VK_NULL_HANDLE)
  {
    vkDestroyCommandPool(g_Device, g_GraphicsCommandPool, nullptr);
    g_GraphicsCommandPool = VK_NULL_HANDLE;
  }
}

Result InitVkCommandBuffers()
{
  g_GraphicsCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_GraphicsCommandPool;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RETURN_IF_FAILURE(Result::Vulkan(
      vkAllocateCommandBuffers(g_Device, &allocInfo, g_GraphicsCommandBuffers.data())),
      "vkAllocateCommandBuffers");
  }

  {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_TransferCommandPool;
    allocInfo.commandBufferCount = 1;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RETURN_IF_FAILURE(Result::Vulkan(
      vkAllocateCommandBuffers(g_Device, &allocInfo, &g_TransferCommandBuffer)),
      "vkAllocateCommandBuffers");
  }

  return Result::Application(0);
}

void DestroyVkCommandBuffers()
{
  g_TransferCommandBuffer = VK_NULL_HANDLE;
  g_GraphicsCommandBuffers.clear();
}

Result InitVkTriangleBuffer()
{
  std::vector<float> vertexData = {
    // x                  y       r     g     b
    0.0f,                 -0.5f,  1.0f, 0.0f, 0.0f,
    sqrtf(3.0f) * 0.25f,  0.25f,  0.0f, 1.0f, 0.0f,
    -sqrtf(3.0f) * 0.25f, 0.25f,  0.0f, 0.0f, 1.0f,
    -0.75f,               0.0f,   0.5f, 0.5f, 0.5f,
    0.75f,                0.0f,   0.5f, 0.5f, 0.5f, 
  };

  std::vector<uint32_t> indexData = {
    0, 1, 2, 0, 2, 3, 0, 4, 1
  };

  uint32_t bufferSize = (uint32_t)(sizeof(float) * vertexData.size() + sizeof(uint32_t) * indexData.size());
  std::vector<char> bufferData(bufferSize);
  memcpy(bufferData.data(), (void*)vertexData.data(), sizeof(float) * vertexData.size());
  memcpy(bufferData.data() + sizeof(float) * vertexData.size(), (void*)indexData.data(), sizeof(uint32_t) * indexData.size());

  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memoryProperties);

  auto FindMemoryType = [&memoryProperties](VkMemoryPropertyFlags propertyFlags, VkMemoryHeapFlags heapFlags) -> uint32_t
  {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
      uint32_t heap = memoryProperties.memoryTypes[i].heapIndex;
      if (((memoryProperties.memoryHeaps[heap].flags & heapFlags) == heapFlags)
          && ((memoryProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags))
      {
        return i;
        break;
      }
    }
    return (uint32_t)-1;
  };

  // Create device-local buffer
  {
    VkBufferCreateInfo bufferCI = {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = bufferSize;
    bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g_Device, &bufferCI, nullptr, &g_TriangleBuffer);

    VkMemoryRequirements memoryReqs;
    vkGetBufferMemoryRequirements(g_Device, g_TriangleBuffer, &memoryReqs);

    // Find device-local memory type
    uint32_t memoryType = FindMemoryType(
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.memoryTypeIndex = memoryType;
    allocateInfo.allocationSize = memoryReqs.size;
    vkAllocateMemory(g_Device, &allocateInfo, nullptr, &g_TriangleBufferMemory);

    vkBindBufferMemory(g_Device, g_TriangleBuffer, g_TriangleBufferMemory, 0);
  }

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  // Create staging buffer
  {
    VkBufferCreateInfo bufferCI = {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = bufferSize;
    bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g_Device, &bufferCI, nullptr, &stagingBuffer);

    VkMemoryRequirements memoryReqs;
    vkGetBufferMemoryRequirements(g_Device, stagingBuffer, &memoryReqs);

    // Find device-local memory type
    uint32_t memoryType = FindMemoryType(
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      0);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.memoryTypeIndex = memoryType;
    allocateInfo.allocationSize = memoryReqs.size;
    vkAllocateMemory(g_Device, &allocateInfo, nullptr, &stagingBufferMemory);

    vkBindBufferMemory(g_Device, stagingBuffer, stagingBufferMemory, 0);
  }

  // Write to staging buffer
  {
    void* data;
    vkMapMemory(g_Device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, bufferData.data(), bufferSize);
    vkUnmapMemory(g_Device, stagingBufferMemory);
  }

  // Write from staging buffer to device-local buffer
  {
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = VkDeviceSize{ 0 };
    copyRegion.dstOffset = VkDeviceSize{ 0 };
    copyRegion.size = bufferSize;

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_TransferCommandBuffer, &beginInfo);
    vkCmdCopyBuffer(g_TransferCommandBuffer, stagingBuffer, g_TriangleBuffer, 1, &copyRegion);
    vkEndCommandBuffer(g_TransferCommandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &g_TransferCommandBuffer;
    vkQueueSubmit(g_TransferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_TransferQueue);
  }

  {
    vkFreeCommandBuffers(g_Device, g_TransferCommandPool, 1, &g_TransferCommandBuffer);
    g_TransferCommandBuffer = VK_NULL_HANDLE;
    vkDestroyBuffer(g_Device, stagingBuffer, nullptr);
    vkFreeMemory(g_Device, stagingBufferMemory, nullptr);
  }

  return Result::Application(0);
}

void DestroyVkTriangleBuffer()
{
  if (g_TriangleBuffer != VK_NULL_HANDLE)
  {
    vkDestroyBuffer(g_Device, g_TriangleBuffer, nullptr);
    g_TriangleBuffer = VK_NULL_HANDLE;
  }

  if (g_TriangleBufferMemory != VK_NULL_HANDLE)
  {
    vkFreeMemory(g_Device, g_TriangleBufferMemory, nullptr);
    g_TriangleBufferMemory = VK_NULL_HANDLE;
  }
}

Result InitVkSemaphoresAndFences()
{
  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  g_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  g_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &g_ImageAvailableSemaphores[i])),
      "");
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateSemaphore(g_Device, &semaphoreCI, nullptr, &g_RenderFinishedSemaphores[i])),
      "");
  }

  g_GraphicsCommandBufferIsUsedFences.resize(MAX_FRAMES_IN_FLIGHT);

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    RETURN_IF_FAILURE(Result::Vulkan(
      vkCreateFence(g_Device, &fenceCI, nullptr, &g_GraphicsCommandBufferIsUsedFences[i])),
      "vkCreateFence");
  }

  return Result::Application(0);
}

void DestroyVkSemaphoresAndFences()
{
  for (VkFence fence : g_GraphicsCommandBufferIsUsedFences)
  {
    if (fence != VK_NULL_HANDLE)
    {
      vkDestroyFence(g_Device, fence, nullptr);
    }
  }
  g_GraphicsCommandBufferIsUsedFences.clear();

  for (VkSemaphore semaphore : g_ImageAvailableSemaphores)
  {
    if (semaphore != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(g_Device, semaphore, nullptr);
    }
  }
  g_GraphicsCommandBufferIsUsedFences.clear();

  for (VkSemaphore semaphore : g_RenderFinishedSemaphores)
  {
    if (semaphore != VK_NULL_HANDLE)
    {
      vkDestroySemaphore(g_Device, semaphore, nullptr);
    }
  }
  g_RenderFinishedSemaphores.clear();
}

Result InitVulkan()
{
  RETURN_IF_FAILURE(InitVkInstance(), "InitVkInstance");
#ifndef NDEBUG
  RETURN_IF_FAILURE(InitVkDebugMessenger(), "InitVkDebugMessenger");
#endif
  RETURN_IF_FAILURE(InitVkSurface(), "InitVkSurface");
  RETURN_IF_FAILURE(InitVkPhysicalDevice(), "InitVkPhysicalDevice");
  RETURN_IF_FAILURE(InitVkDevice(), "InitVkDevice");
  RETURN_IF_FAILURE(InitVkSwapchain(), "InitVkSwapchain");
  RETURN_IF_FAILURE(InitVkGraphicsPipeline(), "InitVkGraphicsPipeline");
  RETURN_IF_FAILURE(InitVkSwapchainFramebuffers(), "InitVkSwapchainFramebuffers");
  RETURN_IF_FAILURE(InitVkCommandPools(), "InitVkCommandPools");
  RETURN_IF_FAILURE(InitVkCommandBuffers(), "InitVkCommandBuffers");
  RETURN_IF_FAILURE(InitVkTriangleBuffer(), "InitVkTriangleBuffer");
  RETURN_IF_FAILURE(InitVkSemaphoresAndFences(), "InitVkSemaphoresAndFences");

  return Result::Application(0);
}

Result Init()
{
  RETURN_IF_FAILURE(InitWindow(), "InitWindow");
  RETURN_IF_FAILURE(InitVulkan(), "InitVulkan");
  SDL_ShowWindow(g_Window);

  return Result::Application(0);
}

void Shutdown()
{
  vkDeviceWaitIdle(g_Device);

  DestroyVkSemaphoresAndFences();
  DestroyVkTriangleBuffer();
  DestroyVkCommandBuffers();
  DestroyVkCommandPools();
  DestroyVkSwapchainFramebuffers();
  DestroyVkGraphicsPipeline();
  DestroyVkSwapchain();
  DestroyVkDevice();
  DestroyVkPhysicalDevice();
  DestroyVkSurface();
  DestroyVkDebugMessenger();
  DestroyVkInstance();
  DestroyWindow();

  SDL_Quit();
}

Result RecreateSwapchain()
{
  vkDeviceWaitIdle(g_Device);

  DestroyVkSwapchainFramebuffers();
  DestroyVkGraphicsPipeline();
  DestroyVkSwapchain();

  RETURN_IF_FAILURE(InitVkSwapchain(), "InitVkSwapchain");
  RETURN_IF_FAILURE(InitVkGraphicsPipeline(), "InitVkGraphicsPipeline");
  RETURN_IF_FAILURE(InitVkSwapchainFramebuffers(), "InitVkSwapchainFramebuffers");

  return Result::Application(0);
}

float g_WorldTime = 0.0f;

Result WriteCommandBuffers(uint32_t swapchainImageIndex)
{
  VkCommandBuffer commandBuffer = g_GraphicsCommandBuffers[g_CurrentFrame];

  RETURN_IF_FAILURE(Result::Vulkan(
    vkResetCommandBuffer(commandBuffer, 0)),
    "vkResetCommandBuffer");

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  RETURN_IF_FAILURE(Result::Vulkan(
    vkBeginCommandBuffer(commandBuffer, &beginInfo)),
    "vkBeginCommandBuffer");

  VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
  VkRenderPassBeginInfo renderPassBeginInfo = {};
  renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBeginInfo.renderPass = g_RenderPass;
  renderPassBeginInfo.clearValueCount = 1;
  renderPassBeginInfo.pClearValues = &clearColor;
  renderPassBeginInfo.framebuffer = g_SwapchainFramebuffers[swapchainImageIndex];
  renderPassBeginInfo.renderArea.offset = { 0, 0 };
  renderPassBeginInfo.renderArea.extent = g_SwapchainExtent;
  vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkDeviceSize vertexBufferOffset = 0;
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &g_TriangleBuffer, &vertexBufferOffset);

  VkDeviceSize indexBufferOffset = 5 * 5 * sizeof(float);
  vkCmdBindIndexBuffer(commandBuffer, g_TriangleBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);

  VkViewport viewport = {};
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = (float)g_SwapchainExtent.width;
  viewport.height = (float)g_SwapchainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GraphicsPipeline);
  vkCmdPushConstants(commandBuffer, g_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &g_WorldTime);

  vkCmdDrawIndexed(commandBuffer, 9, 1, 0, 0, 0);

  vkCmdEndRenderPass(commandBuffer);

  RETURN_IF_FAILURE(Result::Vulkan(
    vkEndCommandBuffer(commandBuffer)),
    "vkEndCommandBuffer");

  return Result::Application(0);
}

Result Render(float normalizedDelay)
{
  uint32_t imageIndex;
  Result acquireNextImage = Result::Vulkan(
    vkAcquireNextImageKHR(g_Device, g_Swapchain, (uint64_t)-1, g_ImageAvailableSemaphores[g_CurrentFrame], VK_NULL_HANDLE, &imageIndex));
  if (acquireNextImage.vkResult == VK_ERROR_OUT_OF_DATE_KHR)
  {
    acquireNextImage = RecreateSwapchain();
    g_DrawableChanged = false;
  }
  RETURN_IF_FAILURE(acquireNextImage, "vkAcquireNextImageKHR");

  RETURN_IF_FAILURE(Result::Vulkan(
    vkWaitForFences(g_Device, 1, &g_GraphicsCommandBufferIsUsedFences[g_CurrentFrame], VK_TRUE, (uint64_t)-1)),
    "vkWaitForFences");
  RETURN_IF_FAILURE(Result::Vulkan(
    vkResetFences(g_Device, 1, &g_GraphicsCommandBufferIsUsedFences[g_CurrentFrame])),
    "vkResetFences");
  RETURN_IF_FAILURE(WriteCommandBuffers(imageIndex),
                    "VkWriteCommandBuffers");

  VkPipelineStageFlags waitStages[] = {
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &g_GraphicsCommandBuffers[g_CurrentFrame];
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &g_ImageAvailableSemaphores[g_CurrentFrame];
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &g_RenderFinishedSemaphores[g_CurrentFrame];

  RETURN_IF_FAILURE(Result::Vulkan(
    vkQueueSubmit(g_GraphicsQueue, 1, &submitInfo, g_GraphicsCommandBufferIsUsedFences[g_CurrentFrame])),
    "vkQueueSubmit");

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &g_Swapchain;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &g_RenderFinishedSemaphores[g_CurrentFrame];
  presentInfo.pImageIndices = &imageIndex;

  Result queuePresent = Result::Vulkan(
    vkQueuePresentKHR(g_PresentQueue, &presentInfo));
  if (g_DrawableChanged
      || queuePresent.vkResult == VK_ERROR_OUT_OF_DATE_KHR
      || queuePresent.vkResult == VK_SUBOPTIMAL_KHR)
  {
    queuePresent = RecreateSwapchain();
    g_DrawableChanged = false;
  }
  RETURN_IF_FAILURE(queuePresent, "vkQueuePresentKHR");

  g_CurrentFrame = (g_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

  return Result::Application(0);
}

void Loop()
{
  // adaptive time per loop iteration?
  const float MS_PER_LOOP_ITERATION = 1.0f / 60.0f * 1000.0f;
  const float MS_PER_UPDATE = 1.0f / 60.0f * 1000.0f;
  const int MAX_UPDATES_PER_FRAME = 4;

  bool windowVisible = true;

  uint64_t previous = SDL_GetPerformanceCounter();
  float lag = 0.0f;
  while (1)
  {
    uint64_t loopIterationStart = SDL_GetPerformanceCounter();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
        case SDL_WINDOWEVENT:
        switch (event.window.event)
        {
          case SDL_WINDOWEVENT_CLOSE:
          {
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
          }
          break;
          case SDL_WINDOWEVENT_RESIZED:
          {
            int w = event.window.data1;
            int h = event.window.data2;
            g_WindowWidth = (uint32_t)w;
            g_WindowHeight = (uint32_t)h;
            SDL_Vulkan_GetDrawableSize(g_Window, &w, &h);
            g_DrawableWidth = (uint32_t)w;
            g_DrawableHeight = (uint32_t)h;
            g_DrawableChanged = true;
          }
          break;
          case SDL_WINDOWEVENT_MINIMIZED:
          case SDL_WINDOWEVENT_HIDDEN:
          {
            windowVisible = false;
          }
          break;
          case SDL_WINDOWEVENT_SHOWN:
          case SDL_WINDOWEVENT_RESTORED:
          {
            windowVisible = true;
          }
          break;
        }
        break;

        case SDL_QUIT:
        return;
      }
    }

    // Process input here

    uint64_t current = SDL_GetPerformanceCounter();
    float elapsed = (float)((current - previous) * 1000) / (float)SDL_GetPerformanceFrequency();
    previous = current;
    lag += elapsed;

    int numUpdates = 0;
    while (lag >= MS_PER_UPDATE)
    {
      if (numUpdates >= MAX_UPDATES_PER_FRAME)
      {
        // omit missing updates
        while (lag > MS_PER_UPDATE)
        {
          lag -= MS_PER_UPDATE;
        }
        break;
      }

      // Update world
      g_WorldTime += MS_PER_UPDATE / 1000.0f;

      lag -= MS_PER_UPDATE;
      numUpdates++;
    }

    if (windowVisible && g_WindowWidth > 0 && g_WindowHeight > 0)
    {
      float renderDelay = lag / MS_PER_UPDATE; // normalized in range [0, 1)
      Result renderResult = Render(renderDelay);
      if (!renderResult.Success())
      {
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
      }
    }

    uint64_t loopIterationEnd = SDL_GetPerformanceCounter();
    float loopIterationTime = (float)((loopIterationEnd - loopIterationStart) * 1000)
      / (float)SDL_GetPerformanceFrequency();
    if (loopIterationTime < MS_PER_LOOP_ITERATION)
    {
      uint32_t sleepMs = (uint32_t)(MS_PER_LOOP_ITERATION - loopIterationTime);
      if (sleepMs > 0)
      {
        SDL_Delay(sleepMs);
      }
    }
  }
}

int main(int argc, char** argv)
{
  Result initResult = Init();
  HandleResult(initResult, "Init");
  if (initResult.Success())
  {
    Loop();
  }

  Shutdown();

  return 0;
}