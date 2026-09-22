#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>


#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_VULKAN_VERSION 1004000 // Vulkan 1.4
#include <vk_mem_alloc.h>