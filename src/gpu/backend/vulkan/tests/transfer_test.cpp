// Host test of the Vulkan device of the native renderer through the pinned
// Plume. Never enters a guest function, the SDK GPU or the Xenos plugin. One
// submission clears an RGBA8 surface (whole and a region), uploads a known
// pattern, copies a region between textures and reads the three images back
// into a buffer with padded rows. Every byte, padding included, is compared
// with the host expectation once a bounded wait sees the fence complete. The
// bounded wait is first run against a fence with no signal pending: it must
// time out within its budget instead of blocking.
// The parts the Plume interface does not carry are native and local: the
// image to buffer copy, the barrier to the host, the bounded fence wait and
// the flush/invalidate of the mapped memory.
// Usage: native_vulkan_transfer_test <new output.jsonl> [--require-extension <name>]... [--provoke-validation]
//   --require-extension adds a device extension to the capabilities the test requires (a missing one stops the
//   test before any GPU work, exit 5); --provoke-validation makes one invalid call and nothing else, to show
//   that the validation messages of the run are captured.
#include <plume_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace plume {
std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

namespace {

constexpr uint32_t kWidth = 61;
constexpr uint32_t kHeight = 37;
constexpr uint32_t kReadbackRowTexels = 64;  // Padded rows, as the D3D12 readback pitch.
constexpr uint64_t kReadbackImageBytes = uint64_t(kReadbackRowTexels) * 4 * kHeight;
constexpr uint8_t kReadbackFill = 0xCD;
constexpr uint8_t kClearWhole[4] = {51, 102, 204, 255};
constexpr uint8_t kClearRegion[4] = {255, 0, 153, 0};
constexpr uint8_t kClearCopyTarget[4] = {0, 255, 51, 102};
const plume::RenderRect kClearRect(8, 5, 40, 29);
const plume::RenderBox kCopyBox(3, 2, 35, 22);
constexpr uint32_t kCopyX = 25;
constexpr uint32_t kCopyY = 14;

std::string Escape(const std::string& text) {
  std::string out;
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char code[8];
      std::snprintf(code, sizeof(code), "\\u%04x", c);
      out += code;
    } else {
      out += c;
    }
  }
  return "\"" + out + "\"";
}

std::string Version(uint32_t version) {
  return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
         std::to_string(VK_API_VERSION_PATCH(version));
}

std::string Hex(uint64_t value) {
  char text[24];
  std::snprintf(text, sizeof(text), "\"0x%llx\"", static_cast<unsigned long long>(value));
  return text;
}

uint8_t Pattern(uint32_t x, uint32_t y, uint32_t c) { return uint8_t((x * 7 + y * 31 + c * 67 + 13) & 0xFF); }

float Unorm(uint8_t value) { return float(value) / 255.0f; }

uint64_t Fnv1a(const std::vector<uint8_t>& bytes) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (const uint8_t b : bytes) hash = (hash ^ b) * 0x100000001b3ull;
  return hash;
}

// The bounded wait of the adapter: short slices, each result kept, until the
// fence completes, a slice fails or the budget runs out. A timeout of a slice
// is not a failure; the caller decides what an exhausted budget means.
struct FenceWait {
  VkResult result = VK_TIMEOUT;
  uint32_t attempts = 0;
  uint32_t timeouts = 0;
  uint32_t early_timeouts = 0;
  double elapsed_ms = 0;
  std::string Json() const {
    std::ostringstream out;
    out << "{\"result\":" << int(result) << ",\"attempts\":" << attempts << ",\"timeouts\":" << timeouts
        << ",\"early_timeouts\":" << early_timeouts << ",\"elapsed_ms\":" << elapsed_ms << "}";
    return out.str();
  }
};

FenceWait WaitBounded(VkDevice device, VkFence fence, std::chrono::milliseconds budget,
                      std::chrono::milliseconds slice) {
  using Clock = std::chrono::steady_clock;
  FenceWait wait;
  const auto start = Clock::now();
  for (;;) {
    const auto attempt = Clock::now();
    const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(budget - (attempt - start));
    const auto step = std::min<std::chrono::nanoseconds>(std::max<std::chrono::nanoseconds>(left, {}), slice);
    wait.result = vkWaitForFences(device, 1, &fence, VK_TRUE, uint64_t(step.count()));
    ++wait.attempts;
    if (wait.result != VK_TIMEOUT) break;
    ++wait.timeouts;
    // A driver may answer a fence with no signal pending at once (the AMD one
    // does): the rest of the slice is slept, so the wait never spins.
    if (const auto rest = step - (Clock::now() - attempt); rest.count() > 0) {
      ++wait.early_timeouts;
      std::this_thread::sleep_for(rest);
    }
    if (Clock::now() - start >= budget) break;
  }
  wait.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return wait;
}

// One image of the readback: the bytes of each row (padding excluded) against
// the expectation, and the padding that the copy must not touch.
struct ImageCheck {
  uint32_t mismatches = 0;
  uint32_t padding_written = 0;
  std::string first_mismatch = "null";
  std::vector<uint8_t> tight;
  std::string Json(const char* name) const {
    std::ostringstream out;
    out << "{\"image\":\"" << name << "\",\"width\":" << kWidth << ",\"height\":" << kHeight
        << ",\"row_bytes_read\":" << kReadbackRowTexels * 4 << ",\"mismatches\":" << mismatches
        << ",\"padding_written\":" << padding_written << ",\"first_mismatch\":" << first_mismatch
        << ",\"fnv1a64\":" << Hex(Fnv1a(tight)) << "}";
    return out.str();
  }
};

template <typename Expected>
ImageCheck Check(const uint8_t* image, Expected expected) {
  ImageCheck check;
  check.tight.resize(size_t(kWidth) * kHeight * 4);
  for (uint32_t y = 0; y < kHeight; ++y) {
    const uint8_t* row = image + size_t(y) * kReadbackRowTexels * 4;
    for (uint32_t x = 0; x < kReadbackRowTexels; ++x) {
      for (uint32_t c = 0; c < 4; ++c) {
        const uint8_t actual = row[x * 4 + c];
        if (x >= kWidth) {
          check.padding_written += actual != kReadbackFill;
          continue;
        }
        check.tight[(size_t(y) * kWidth + x) * 4 + c] = actual;
        const uint8_t want = expected(x, y, c);
        if (actual != want && check.mismatches++ == 0) {
          check.first_mismatch = "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"c\":" +
                                 std::to_string(c) + ",\"expected\":" + std::to_string(want) + ",\"actual\":" +
                                 std::to_string(actual) + "}";
        }
      }
    }
  }
  return check;
}

bool Inside(const plume::RenderRect& rect, uint32_t x, uint32_t y) {
  return int32_t(x) >= rect.left && int32_t(x) < rect.right && int32_t(y) >= rect.top && int32_t(y) < rect.bottom;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::filesystem::exists(argv[1])) return 2;
  std::vector<std::string> required_extensions;
  bool provoke_validation = false;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--require-extension" && i + 1 < argc) {
      required_extensions.push_back(argv[++i]);
    } else if (arg == "--provoke-validation") {
      provoke_validation = true;
    } else {
      return 2;
    }
  }
  const std::string output_path = argv[1];
  std::ofstream output(output_path);
  if (!output) return 3;
  auto line = [&](const char* kind, bool ok, const std::string& evidence) {
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false") << ",\"evidence\":"
           << (evidence.empty() ? "null" : evidence) << "}\n";
    output.flush();
  };
  auto verdict = [&](bool passed, const char* reason) {
    output << "{\"kind\":\"verdict\",\"passed\":" << (passed ? "true" : "false") << ",\"reason\":" << Escape(reason)
           << ",\"guest_executed\":false}\n";
  };

  // The loader and the instance, as Plume creates them (Vulkan 1.2, no layer
  // of its own in a release build: the run enables the validation layer).
  std::unique_ptr<plume::RenderInterface> render = plume::CreateVulkanInterface();
  if (!render) {
    line("interface", false, "{\"error\":\"plume-vulkan-interface-not-created\"}");
    verdict(false, "interface");
    return 4;
  }
  auto* vulkan = static_cast<plume::VulkanInterface*>(render.get());
  {
    uint32_t loader = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != nullptr) vkEnumerateInstanceVersion(&loader);
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    std::ostringstream evidence;
    evidence << "{\"loader_version\":\"" << Version(loader) << "\",\"instance_api_version\":\""
             << Version(vulkan->appInfo.apiVersion) << "\",\"available_layers\":[";
    for (uint32_t i = 0; i < count; ++i) {
      evidence << (i ? "," : "") << "{\"name\":" << Escape(layers[i].layerName) << ",\"spec\":\""
               << Version(layers[i].specVersion) << "\",\"implementation\":" << layers[i].implementationVersion << "}";
    }
    evidence << "],\"environment\":{";
    const char* names[] = {"VK_LOADER_LAYERS_ENABLE", "VK_LOADER_LAYERS_DISABLE", "VK_ADD_LAYER_PATH",
                           "VK_INSTANCE_LAYERS", "VK_KHRONOS_VALIDATION_LOG_FILENAME"};
    for (size_t i = 0; i < std::size(names); ++i) {
      const char* value = std::getenv(names[i]);
      evidence << (i ? "," : "") << "\"" << names[i] << "\":" << (value ? Escape(value) : "null");
    }
    evidence << "},\"device_names\":[";
    const auto& device_names = render->getDeviceNames();
    for (size_t i = 0; i < device_names.size(); ++i) evidence << (i ? "," : "") << Escape(device_names[i]);
    evidence << "]}";
    line("interface", true, evidence.str());
  }

  std::unique_ptr<plume::RenderDevice> device = render->createDevice("");
  if (!device) {
    line("device", false, "{\"error\":\"plume-vulkan-device-not-created\"}");
    verdict(false, "device");
    return 4;
  }
  auto* vk_device = static_cast<plume::VulkanDevice*>(device.get());
  const VkPhysicalDevice physical = vk_device->physicalDevice;
  const VkPhysicalDeviceProperties& properties = vk_device->physicalDeviceProperties;
  uint32_t extension_count = 0;
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> extensions(extension_count);
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, extensions.data());
  auto has_extension = [&](const std::string& name) {
    for (const auto& e : extensions) {
      if (name == e.extensionName) return true;
    }
    return false;
  };
  uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
  const uint32_t direct_family = vk_device->queueFamilyIndices[0];  // Plume's DIRECT queue.
  const VkQueueFlags direct_flags = direct_family < family_count ? families[direct_family].queueFlags : 0;
  {
    VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
    vkGetPhysicalDeviceProperties2(physical, &properties2);
    const plume::RenderDeviceCapabilities& caps = device->getCapabilities();
    std::ostringstream evidence;
    evidence << std::boolalpha << "{\"name\":" << Escape(properties.deviceName) << ",\"vendor_id\":" << Hex(properties.vendorID)
             << ",\"device_id\":" << Hex(properties.deviceID) << ",\"device_api_version\":\""
             << Version(properties.apiVersion) << "\",\"driver_version_raw\":" << Hex(properties.driverVersion)
             << ",\"driver_id\":" << int(driver.driverID) << ",\"driver_name\":" << Escape(driver.driverName)
             << ",\"driver_info\":" << Escape(driver.driverInfo) << ",\"conformance\":\""
             << int(driver.conformanceVersion.major) << "." << int(driver.conformanceVersion.minor) << "."
             << int(driver.conformanceVersion.subminor) << "." << int(driver.conformanceVersion.patch)
             << "\",\"direct_queue_family\":" << direct_family << ",\"direct_queue_flags\":" << Hex(direct_flags)
             // What Plume enabled on the device (its feature chain follows these).
             << ",\"plume_enabled\":{\"descriptor_indexing\":" << caps.descriptorIndexing
             << ",\"scalar_block_layout\":" << caps.scalarBlockLayout << ",\"buffer_device_address\":"
             << caps.bufferDeviceAddress << ",\"null_descriptor\":" << vk_device->nullDescriptorSupported
             << ",\"present_wait\":" << caps.presentWait << ",\"load_store_op_none\":"
             << vk_device->loadStoreOpNoneSupported << ",\"geometry_shader\":" << caps.geometryShader
             << ",\"sample_locations\":" << caps.sampleLocations << ",\"raytracing\":" << caps.raytracing
             << ",\"max_texture_size\":" << caps.maxTextureSize << ",\"uma\":" << caps.uma << "},\"extensions\":{";
    const char* listed[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                            VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
                            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
                            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                            VK_KHR_PRESENT_ID_EXTENSION_NAME,
                            VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
                            VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                            VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
                            VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,
                            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME};
    for (size_t i = 0; i < std::size(listed); ++i) {
      evidence << (i ? "," : "") << "\"" << listed[i] << "\":" << (has_extension(listed[i]) ? "true" : "false");
    }
    evidence << "},\"available_extension_count\":" << extension_count << "}";
    line("device", true, evidence.str());
  }

  if (provoke_validation) {
    // Invalid on purpose (VUID-VkFenceCreateInfo-flags-parameter): the run must
    // find the message in the validation log. Drivers ignore the unknown bit.
    VkFenceCreateInfo info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    info.flags = 0x80000000u;
    VkFence fence = VK_NULL_HANDLE;
    const VkResult created = vkCreateFence(vk_device->vk, &info, nullptr, &fence);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(vk_device->vk, fence, nullptr);
    line("provoke-validation", true, "{\"call\":\"vkCreateFence\",\"flags\":\"0x80000000\",\"result\":" +
                                         std::to_string(int(created)) + "}");
    verdict(true, "provoked");
    return 0;
  }

  // The capabilities this test requires, checked before any GPU work.
  {
    std::vector<std::string> missing;
    const VkQueueFlags direct_needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    if ((direct_flags & direct_needed) != direct_needed) missing.push_back("queue:graphics+compute+transfer");
    VkFormatProperties format = {};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
    const VkFormatFeatureFlags format_needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                               VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((format.optimalTilingFeatures & format_needed) != format_needed) {
      missing.push_back("format:R8G8B8A8_UNORM:color-attachment+transfer");
    }
    VkPhysicalDeviceMemoryProperties memory = {};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    bool host_visible = false;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
      host_visible |= (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }
    if (!host_visible) missing.push_back("memory:host-visible");
    for (const auto& name : required_extensions) {
      if (!has_extension(name)) missing.push_back("extension:" + name);
    }
    std::ostringstream evidence;
    evidence << "{\"required\":[\"queue:graphics+compute+transfer\",\"format:R8G8B8A8_UNORM:color-attachment+"
                "transfer\",\"memory:host-visible\"";
    for (const auto& name : required_extensions) evidence << "," << Escape("extension:" + name);
    evidence << "],\"missing\":[";
    for (size_t i = 0; i < missing.size(); ++i) evidence << (i ? "," : "") << Escape(missing[i]);
    evidence << "]}";
    line("capabilities", missing.empty(), evidence.str());
    if (!missing.empty()) {
      verdict(false, "capability-missing");
      return 5;
    }
  }

  const VkDevice vk = vk_device->vk;
  const VmaAllocator allocator = vk_device->allocator;
  const auto format = plume::RenderFormat::R8G8B8A8_UNORM;
  auto queue = device->createCommandQueue(plume::RenderCommandListType::DIRECT);
  auto list = queue->createCommandList();
  auto fence = device->createCommandFence();
  auto cleared = device->createTexture(plume::RenderTextureDesc::ColorTarget(kWidth, kHeight, format));
  auto uploaded = device->createTexture(plume::RenderTextureDesc::Texture2D(kWidth, kHeight, 1, format));
  auto copied = device->createTexture(plume::RenderTextureDesc::ColorTarget(kWidth, kHeight, format));
  auto upload = device->createBuffer(plume::RenderBufferDesc::UploadBuffer(uint64_t(kWidth) * kHeight * 4));
  auto readback = device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(kReadbackImageBytes * 3));
  const plume::RenderTexture* cleared_attachment[] = {cleared.get()};
  const plume::RenderTexture* copied_attachment[] = {copied.get()};
  auto cleared_target = device->createFramebuffer(plume::RenderFramebufferDesc(cleared_attachment, 1));
  auto copied_target = device->createFramebuffer(plume::RenderFramebufferDesc(copied_attachment, 1));
  auto* upload_buffer = static_cast<plume::VulkanBuffer*>(upload.get());
  auto* readback_buffer = static_cast<plume::VulkanBuffer*>(readback.get());
  const bool resources = cleared && uploaded && copied && upload_buffer->vk && readback_buffer->vk;
  line("resources", resources, "null");
  if (!resources) {
    verdict(false, "resources");
    return 1;
  }

  // Host writes: the pattern into the upload buffer and a fill into the
  // readback buffer, flushed while mapped when the memory is not coherent.
  VkMemoryPropertyFlags upload_memory = 0, readback_memory = 0;
  vmaGetAllocationMemoryProperties(allocator, upload_buffer->allocation, &upload_memory);
  vmaGetAllocationMemoryProperties(allocator, readback_buffer->allocation, &readback_memory);
  auto* pattern = static_cast<uint8_t*>(upload->map());
  auto* fill = static_cast<uint8_t*>(readback->map());
  if (pattern == nullptr || fill == nullptr) {
    line("map", false, "null");
    verdict(false, "map");
    return 1;
  }
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      for (uint32_t c = 0; c < 4; ++c) pattern[(size_t(y) * kWidth + x) * 4 + c] = Pattern(x, y, c);
    }
  }
  std::fill(fill, fill + kReadbackImageBytes * 3, kReadbackFill);
  const VkResult upload_flush = vmaFlushAllocation(allocator, upload_buffer->allocation, 0, VK_WHOLE_SIZE);
  const VkResult fill_flush = vmaFlushAllocation(allocator, readback_buffer->allocation, 0, VK_WHOLE_SIZE);
  upload->unmap();
  readback->unmap();
  {
    std::ostringstream evidence;
    evidence << std::boolalpha << "{\"upload_memory\":" << Hex(upload_memory) << ",\"upload_coherent\":"
             << ((upload_memory & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) << ",\"upload_flush\":"
             << int(upload_flush) << ",\"readback_memory\":" << Hex(readback_memory) << ",\"readback_coherent\":"
             << ((readback_memory & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) << ",\"readback_cached\":"
             << ((readback_memory & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0) << ",\"fill_flush\":"
             << int(fill_flush) << "}";
    line("host-writes", upload_flush == VK_SUCCESS && fill_flush == VK_SUCCESS, evidence.str());
  }

  // The bounded wait against a fence with no signal pending: every slice times
  // out, no slice is shorter than its length, and the wait returns once its
  // budget is spent.
  auto idle = device->createCommandFence();
  const FenceWait idle_wait = WaitBounded(vk, static_cast<plume::VulkanCommandFence*>(idle.get())->vk,
                                          std::chrono::milliseconds(150), std::chrono::milliseconds(25));
  const bool bounded_ok = idle_wait.result == VK_TIMEOUT && idle_wait.timeouts >= 2 && idle_wait.attempts <= 7 &&
                          idle_wait.elapsed_ms >= 150 && idle_wait.elapsed_ms < 400;
  line("bounded-wait", bounded_ok, "{\"budget_ms\":150,\"slice_ms\":25,\"wait\":" + idle_wait.Json() + "}");

  list->begin();
  auto* commands = static_cast<plume::VulkanCommandList*>(list.get())->vk;
  list->barriers(plume::RenderBarrierStage::GRAPHICS,
                 {plume::RenderTextureBarrier(cleared.get(), plume::RenderTextureLayout::COLOR_WRITE),
                  plume::RenderTextureBarrier(copied.get(), plume::RenderTextureLayout::COLOR_WRITE)});
  list->barriers(plume::RenderBarrierStage::COPY,
                 plume::RenderTextureBarrier(uploaded.get(), plume::RenderTextureLayout::COPY_DEST));
  list->setFramebuffer(cleared_target.get());
  list->clearColor(0, plume::RenderColor(Unorm(kClearWhole[0]), Unorm(kClearWhole[1]), Unorm(kClearWhole[2]),
                                         Unorm(kClearWhole[3])));
  list->clearColor(0,
                   plume::RenderColor(Unorm(kClearRegion[0]), Unorm(kClearRegion[1]), Unorm(kClearRegion[2]),
                                      Unorm(kClearRegion[3])),
                   &kClearRect, 1);
  list->setFramebuffer(copied_target.get());
  list->clearColor(0, plume::RenderColor(Unorm(kClearCopyTarget[0]), Unorm(kClearCopyTarget[1]),
                                         Unorm(kClearCopyTarget[2]), Unorm(kClearCopyTarget[3])));
  list->setFramebuffer(nullptr);
  list->copyTextureRegion(plume::RenderTextureCopyLocation::Subresource(uploaded.get()),
                          plume::RenderTextureCopyLocation::PlacedFootprint(upload.get(), format, kWidth, kHeight, 1,
                                                                            kWidth));
  list->barriers(plume::RenderBarrierStage::COPY,
                 {plume::RenderTextureBarrier(cleared.get(), plume::RenderTextureLayout::COPY_SOURCE),
                  plume::RenderTextureBarrier(uploaded.get(), plume::RenderTextureLayout::COPY_SOURCE),
                  plume::RenderTextureBarrier(copied.get(), plume::RenderTextureLayout::COPY_DEST)});
  list->copyTextureRegion(plume::RenderTextureCopyLocation::Subresource(copied.get()),
                          plume::RenderTextureCopyLocation::Subresource(uploaded.get()), kCopyX, kCopyY, 0, &kCopyBox);
  list->barriers(plume::RenderBarrierStage::COPY,
                 plume::RenderTextureBarrier(copied.get(), plume::RenderTextureLayout::COPY_SOURCE));
  // The readback, native: Plume's copyTextureRegion has no image to buffer path.
  const VkImage images[] = {static_cast<plume::VulkanTexture*>(cleared.get())->vk,
                            static_cast<plume::VulkanTexture*>(uploaded.get())->vk,
                            static_cast<plume::VulkanTexture*>(copied.get())->vk};
  for (uint32_t i = 0; i < 3; ++i) {
    VkBufferImageCopy region = {};
    region.bufferOffset = kReadbackImageBytes * i;
    region.bufferRowLength = kReadbackRowTexels;
    region.bufferImageHeight = kHeight;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kWidth, kHeight, 1};
    vkCmdCopyImageToBuffer(commands, images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer->vk, 1, &region);
  }
  VkMemoryBarrier to_host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, nullptr,
                       0, nullptr);
  list->end();
  queue->executeCommandLists(list.get(), fence.get());
  const VkFence vk_fence = static_cast<plume::VulkanCommandFence*>(fence.get())->vk;

  // The completion the GPU signals, seen by the bounded wait and confirmed by
  // the fence status; the status right after the submission is informative.
  const VkResult status_submitted = vkGetFenceStatus(vk, vk_fence);
  const FenceWait done = WaitBounded(vk, vk_fence, std::chrono::milliseconds(5000), std::chrono::milliseconds(100));
  const VkResult status_done = vkGetFenceStatus(vk, vk_fence);
  const bool completed = done.result == VK_SUCCESS && status_done == VK_SUCCESS;
  line("completion", completed,
       "{\"status_after_submit\":" + std::to_string(int(status_submitted)) + ",\"wait\":" + done.Json() +
           ",\"status_after_wait\":" + std::to_string(int(status_done)) + "}");
  if (!completed) {
    // Never recycle what the GPU may still use: the resources are left alive.
    line("resources-retained", true, "{\"reason\":\"completion-not-observed\"}");
    const auto retain = [](auto&... owners) { (static_cast<void>(owners.release()), ...); };
    retain(cleared, uploaded, copied, upload, readback, cleared_target, copied_target, list, fence, queue, device,
           render);
    verdict(false, "completion");
    return 1;
  }

  // Invalidated while mapped, as vkInvalidateMappedMemoryRanges requires.
  const auto* bytes = static_cast<const uint8_t*>(readback->map());
  if (bytes == nullptr) {
    line("map", false, "null");
    verdict(false, "map");
    return 1;
  }
  const VkResult invalidate = vmaInvalidateAllocation(allocator, readback_buffer->allocation, 0, VK_WHOLE_SIZE);
  const ImageCheck clear_check = Check(bytes, [](uint32_t x, uint32_t y, uint32_t c) {
    return Inside(kClearRect, x, y) ? kClearRegion[c] : kClearWhole[c];
  });
  const ImageCheck upload_check = Check(bytes + kReadbackImageBytes, Pattern);
  const plume::RenderRect copy_rect(kCopyX, kCopyY, kCopyX + (kCopyBox.right - kCopyBox.left),
                                    kCopyY + (kCopyBox.bottom - kCopyBox.top));
  const ImageCheck copy_check = Check(bytes + kReadbackImageBytes * 2, [&](uint32_t x, uint32_t y, uint32_t c) {
    return Inside(copy_rect, x, y) ? Pattern(x - kCopyX + kCopyBox.left, y - kCopyY + kCopyBox.top, c)
                                   : kClearCopyTarget[c];
  });
  readback->unmap();
  const struct {
    const char* name;
    const ImageCheck& check;
  } checks[] = {{"clear", clear_check}, {"upload", upload_check}, {"copy", copy_check}};
  bool images_ok = invalidate == VK_SUCCESS;
  for (const auto& entry : checks) {
    const bool ok = entry.check.mismatches == 0 && entry.check.padding_written == 0;
    images_ok &= ok;
    line("readback", ok, entry.check.Json(entry.name));
    const std::string image_path =
        output_path + "." + entry.name + "-" + std::to_string(kWidth) + "x" + std::to_string(kHeight) + ".rgba";
    std::ofstream image(image_path, std::ios::binary);
    image.write(reinterpret_cast<const char*>(entry.check.tight.data()), std::streamsize(entry.check.tight.size()));
  }
  line("invalidate", invalidate == VK_SUCCESS, "{\"result\":" + std::to_string(int(invalidate)) + "}");

  const bool passed = resources && bounded_ok && completed && images_ok;
  verdict(passed, passed ? "ok" : "mismatch");
  return passed && output.good() ? 0 : 1;
}
