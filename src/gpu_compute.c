#include "qn_gpu_compute.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QN_VK_SUCCESS 0
#define QN_VK_STRUCTURE_TYPE_APPLICATION_INFO 0u
#define QN_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1u
#define QN_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2u
#define QN_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3u
#define QN_VK_STRUCTURE_TYPE_SUBMIT_INFO 4u
#define QN_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5u
#define QN_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12u
#define QN_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16u
#define QN_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18u
#define QN_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 29u
#define QN_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30u
#define QN_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32u
#define QN_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33u
#define QN_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34u
#define QN_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35u
#define QN_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39u
#define QN_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40u
#define QN_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42u

#define QN_VK_QUEUE_COMPUTE_BIT 0x00000002u
#define QN_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1u
#define QN_VK_VENDOR_BROADCOM 0x14e4u
#define QN_VK_API_VERSION_1_0 (1u << 22)
#define QN_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x00000020u
#define QN_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x00000002u
#define QN_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004u
#define QN_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7u
#define QN_VK_SHADER_STAGE_COMPUTE_BIT 0x00000020u
#define QN_VK_PIPELINE_BIND_POINT_COMPUTE 1u
#define QN_VK_COMMAND_BUFFER_LEVEL_PRIMARY 0u
#define QN_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x00000001u
#define QN_VK_SHARING_MODE_EXCLUSIVE 0u

#define QN_GPU_DATA_WORDS (QN_GPU_COMPUTE_ELEMENT_COUNT * 3u)
#define QN_GPU_DATA_BYTES ((uint64_t)QN_GPU_DATA_WORDS * sizeof(uint32_t))

typedef int32_t QNVkResult;
typedef uint32_t QNVkFlags;
typedef uint32_t QNVkBool32;
typedef uint64_t QNVkDeviceSize;
typedef struct QNVkInstance_T *QNVkInstance;
typedef struct QNVkPhysicalDevice_T *QNVkPhysicalDevice;
typedef struct QNVkDevice_T *QNVkDevice;
typedef struct QNVkQueue_T *QNVkQueue;
typedef struct QNVkCommandBuffer_T *QNVkCommandBuffer;
typedef uint64_t QNVkBuffer;
typedef uint64_t QNVkDeviceMemory;
typedef uint64_t QNVkDescriptorSetLayout;
typedef uint64_t QNVkPipelineLayout;
typedef uint64_t QNVkShaderModule;
typedef uint64_t QNVkPipeline;
typedef uint64_t QNVkDescriptorPool;
typedef uint64_t QNVkDescriptorSet;
typedef uint64_t QNVkCommandPool;
typedef uint64_t QNVkPipelineCache;
typedef uint64_t QNVkSemaphore;

typedef struct {
    uint32_t s_type;
    const void *next;
    const char *application_name;
    uint32_t application_version;
    const char *engine_name;
    uint32_t engine_version;
    uint32_t api_version;
} QNVkApplicationInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    const QNVkApplicationInfo *application_info;
    uint32_t enabled_layer_count;
    const char *const *enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *enabled_extension_names;
} QNVkInstanceCreateInfo;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} QNVkExtent3D;

typedef struct {
    QNVkFlags queue_flags;
    uint32_t queue_count;
    uint32_t timestamp_valid_bits;
    QNVkExtent3D min_image_transfer_granularity;
} QNVkQueueFamilyProperties;

typedef struct {
    uint32_t api_version;
    uint32_t driver_version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t device_type;
    char device_name[QN_GPU_DEVICE_NAME_CAP];
    uint8_t pipeline_cache_uuid[16];
    uint8_t remaining_properties[4096];
} QNVkPhysicalDeviceProperties;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t queue_family_index;
    uint32_t queue_count;
    const float *queue_priorities;
} QNVkDeviceQueueCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t queue_create_info_count;
    const QNVkDeviceQueueCreateInfo *queue_create_infos;
    uint32_t enabled_layer_count;
    const char *const *enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *enabled_extension_names;
    const void *enabled_features;
} QNVkDeviceCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    QNVkDeviceSize size;
    QNVkFlags usage;
    uint32_t sharing_mode;
    uint32_t queue_family_index_count;
    const uint32_t *queue_family_indices;
} QNVkBufferCreateInfo;

typedef struct {
    QNVkDeviceSize size;
    QNVkDeviceSize alignment;
    uint32_t memory_type_bits;
} QNVkMemoryRequirements;

typedef struct {
    QNVkFlags property_flags;
    uint32_t heap_index;
} QNVkMemoryType;

typedef struct {
    QNVkDeviceSize size;
    QNVkFlags flags;
} QNVkMemoryHeap;

typedef struct {
    uint32_t memory_type_count;
    QNVkMemoryType memory_types[32];
    uint32_t memory_heap_count;
    QNVkMemoryHeap memory_heaps[16];
} QNVkPhysicalDeviceMemoryProperties;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkDeviceSize allocation_size;
    uint32_t memory_type_index;
} QNVkMemoryAllocateInfo;

typedef struct {
    uint32_t binding;
    uint32_t descriptor_type;
    uint32_t descriptor_count;
    QNVkFlags stage_flags;
    const void *immutable_samplers;
} QNVkDescriptorSetLayoutBinding;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t binding_count;
    const QNVkDescriptorSetLayoutBinding *bindings;
} QNVkDescriptorSetLayoutCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t set_layout_count;
    const QNVkDescriptorSetLayout *set_layouts;
    uint32_t push_constant_range_count;
    const void *push_constant_ranges;
} QNVkPipelineLayoutCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    size_t code_size;
    const uint32_t *code;
} QNVkShaderModuleCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t stage;
    QNVkShaderModule module;
    const char *name;
    const void *specialization_info;
} QNVkPipelineShaderStageCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    QNVkPipelineShaderStageCreateInfo stage;
    QNVkPipelineLayout layout;
    QNVkPipeline base_pipeline_handle;
    int32_t base_pipeline_index;
} QNVkComputePipelineCreateInfo;

typedef struct {
    uint32_t type;
    uint32_t descriptor_count;
} QNVkDescriptorPoolSize;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t max_sets;
    uint32_t pool_size_count;
    const QNVkDescriptorPoolSize *pool_sizes;
} QNVkDescriptorPoolCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkDescriptorPool descriptor_pool;
    uint32_t descriptor_set_count;
    const QNVkDescriptorSetLayout *set_layouts;
} QNVkDescriptorSetAllocateInfo;

typedef struct {
    QNVkBuffer buffer;
    QNVkDeviceSize offset;
    QNVkDeviceSize range;
} QNVkDescriptorBufferInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkDescriptorSet destination_set;
    uint32_t destination_binding;
    uint32_t destination_array_element;
    uint32_t descriptor_count;
    uint32_t descriptor_type;
    const void *image_info;
    const QNVkDescriptorBufferInfo *buffer_info;
    const void *texel_buffer_view;
} QNVkWriteDescriptorSet;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    uint32_t queue_family_index;
} QNVkCommandPoolCreateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkCommandPool command_pool;
    uint32_t level;
    uint32_t command_buffer_count;
} QNVkCommandBufferAllocateInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    QNVkFlags flags;
    const void *inheritance_info;
} QNVkCommandBufferBeginInfo;

typedef struct {
    uint32_t s_type;
    const void *next;
    uint32_t wait_semaphore_count;
    const QNVkSemaphore *wait_semaphores;
    const QNVkFlags *wait_destination_stage_mask;
    uint32_t command_buffer_count;
    const QNVkCommandBuffer *command_buffers;
    uint32_t signal_semaphore_count;
    const QNVkSemaphore *signal_semaphores;
} QNVkSubmitInfo;

typedef QNVkResult (*QNVkCreateInstanceFn)(
    const QNVkInstanceCreateInfo *, const void *, QNVkInstance *);
typedef void (*QNVkDestroyInstanceFn)(QNVkInstance, const void *);
typedef QNVkResult (*QNVkEnumeratePhysicalDevicesFn)(
    QNVkInstance, uint32_t *, QNVkPhysicalDevice *);
typedef void (*QNVkGetPhysicalDevicePropertiesFn)(
    QNVkPhysicalDevice, QNVkPhysicalDeviceProperties *);
typedef void (*QNVkGetPhysicalDeviceQueueFamilyPropertiesFn)(
    QNVkPhysicalDevice, uint32_t *, QNVkQueueFamilyProperties *);
typedef void (*QNVkGetPhysicalDeviceMemoryPropertiesFn)(
    QNVkPhysicalDevice, QNVkPhysicalDeviceMemoryProperties *);
typedef QNVkResult (*QNVkCreateDeviceFn)(
    QNVkPhysicalDevice, const QNVkDeviceCreateInfo *, const void *, QNVkDevice *);
typedef void (*QNVkDestroyDeviceFn)(QNVkDevice, const void *);
typedef void (*QNVkGetDeviceQueueFn)(
    QNVkDevice, uint32_t, uint32_t, QNVkQueue *);
typedef QNVkResult (*QNVkCreateBufferFn)(
    QNVkDevice, const QNVkBufferCreateInfo *, const void *, QNVkBuffer *);
typedef void (*QNVkDestroyBufferFn)(QNVkDevice, QNVkBuffer, const void *);
typedef void (*QNVkGetBufferMemoryRequirementsFn)(
    QNVkDevice, QNVkBuffer, QNVkMemoryRequirements *);
typedef QNVkResult (*QNVkAllocateMemoryFn)(
    QNVkDevice, const QNVkMemoryAllocateInfo *, const void *, QNVkDeviceMemory *);
typedef void (*QNVkFreeMemoryFn)(QNVkDevice, QNVkDeviceMemory, const void *);
typedef QNVkResult (*QNVkBindBufferMemoryFn)(
    QNVkDevice, QNVkBuffer, QNVkDeviceMemory, QNVkDeviceSize);
typedef QNVkResult (*QNVkMapMemoryFn)(
    QNVkDevice, QNVkDeviceMemory, QNVkDeviceSize, QNVkDeviceSize,
    QNVkFlags, void **);
typedef void (*QNVkUnmapMemoryFn)(QNVkDevice, QNVkDeviceMemory);
typedef QNVkResult (*QNVkCreateDescriptorSetLayoutFn)(
    QNVkDevice, const QNVkDescriptorSetLayoutCreateInfo *, const void *,
    QNVkDescriptorSetLayout *);
typedef void (*QNVkDestroyDescriptorSetLayoutFn)(
    QNVkDevice, QNVkDescriptorSetLayout, const void *);
typedef QNVkResult (*QNVkCreatePipelineLayoutFn)(
    QNVkDevice, const QNVkPipelineLayoutCreateInfo *, const void *,
    QNVkPipelineLayout *);
typedef void (*QNVkDestroyPipelineLayoutFn)(
    QNVkDevice, QNVkPipelineLayout, const void *);
typedef QNVkResult (*QNVkCreateShaderModuleFn)(
    QNVkDevice, const QNVkShaderModuleCreateInfo *, const void *,
    QNVkShaderModule *);
typedef void (*QNVkDestroyShaderModuleFn)(
    QNVkDevice, QNVkShaderModule, const void *);
typedef QNVkResult (*QNVkCreateComputePipelinesFn)(
    QNVkDevice, QNVkPipelineCache, uint32_t,
    const QNVkComputePipelineCreateInfo *, const void *, QNVkPipeline *);
typedef void (*QNVkDestroyPipelineFn)(QNVkDevice, QNVkPipeline, const void *);
typedef QNVkResult (*QNVkCreateDescriptorPoolFn)(
    QNVkDevice, const QNVkDescriptorPoolCreateInfo *, const void *,
    QNVkDescriptorPool *);
typedef void (*QNVkDestroyDescriptorPoolFn)(
    QNVkDevice, QNVkDescriptorPool, const void *);
typedef QNVkResult (*QNVkAllocateDescriptorSetsFn)(
    QNVkDevice, const QNVkDescriptorSetAllocateInfo *, QNVkDescriptorSet *);
typedef void (*QNVkUpdateDescriptorSetsFn)(
    QNVkDevice, uint32_t, const QNVkWriteDescriptorSet *,
    uint32_t, const void *);
typedef QNVkResult (*QNVkCreateCommandPoolFn)(
    QNVkDevice, const QNVkCommandPoolCreateInfo *, const void *,
    QNVkCommandPool *);
typedef void (*QNVkDestroyCommandPoolFn)(
    QNVkDevice, QNVkCommandPool, const void *);
typedef QNVkResult (*QNVkAllocateCommandBuffersFn)(
    QNVkDevice, const QNVkCommandBufferAllocateInfo *, QNVkCommandBuffer *);
typedef QNVkResult (*QNVkBeginCommandBufferFn)(
    QNVkCommandBuffer, const QNVkCommandBufferBeginInfo *);
typedef QNVkResult (*QNVkEndCommandBufferFn)(QNVkCommandBuffer);
typedef void (*QNVkCmdBindPipelineFn)(
    QNVkCommandBuffer, uint32_t, QNVkPipeline);
typedef void (*QNVkCmdBindDescriptorSetsFn)(
    QNVkCommandBuffer, uint32_t, QNVkPipelineLayout,
    uint32_t, uint32_t, const QNVkDescriptorSet *,
    uint32_t, const uint32_t *);
typedef void (*QNVkCmdDispatchFn)(
    QNVkCommandBuffer, uint32_t, uint32_t, uint32_t);
typedef QNVkResult (*QNVkQueueSubmitFn)(
    QNVkQueue, uint32_t, const QNVkSubmitInfo *, uint64_t);
typedef QNVkResult (*QNVkQueueWaitIdleFn)(QNVkQueue);

typedef struct {
    void *library;
    QNVkCreateInstanceFn create_instance;
    QNVkDestroyInstanceFn destroy_instance;
    QNVkEnumeratePhysicalDevicesFn enumerate_physical_devices;
    QNVkGetPhysicalDevicePropertiesFn get_physical_device_properties;
    QNVkGetPhysicalDeviceQueueFamilyPropertiesFn
        get_physical_device_queue_family_properties;
    QNVkGetPhysicalDeviceMemoryPropertiesFn
        get_physical_device_memory_properties;
    QNVkCreateDeviceFn create_device;
    QNVkDestroyDeviceFn destroy_device;
    QNVkGetDeviceQueueFn get_device_queue;
    QNVkCreateBufferFn create_buffer;
    QNVkDestroyBufferFn destroy_buffer;
    QNVkGetBufferMemoryRequirementsFn get_buffer_memory_requirements;
    QNVkAllocateMemoryFn allocate_memory;
    QNVkFreeMemoryFn free_memory;
    QNVkBindBufferMemoryFn bind_buffer_memory;
    QNVkMapMemoryFn map_memory;
    QNVkUnmapMemoryFn unmap_memory;
    QNVkCreateDescriptorSetLayoutFn create_descriptor_set_layout;
    QNVkDestroyDescriptorSetLayoutFn destroy_descriptor_set_layout;
    QNVkCreatePipelineLayoutFn create_pipeline_layout;
    QNVkDestroyPipelineLayoutFn destroy_pipeline_layout;
    QNVkCreateShaderModuleFn create_shader_module;
    QNVkDestroyShaderModuleFn destroy_shader_module;
    QNVkCreateComputePipelinesFn create_compute_pipelines;
    QNVkDestroyPipelineFn destroy_pipeline;
    QNVkCreateDescriptorPoolFn create_descriptor_pool;
    QNVkDestroyDescriptorPoolFn destroy_descriptor_pool;
    QNVkAllocateDescriptorSetsFn allocate_descriptor_sets;
    QNVkUpdateDescriptorSetsFn update_descriptor_sets;
    QNVkCreateCommandPoolFn create_command_pool;
    QNVkDestroyCommandPoolFn destroy_command_pool;
    QNVkAllocateCommandBuffersFn allocate_command_buffers;
    QNVkBeginCommandBufferFn begin_command_buffer;
    QNVkEndCommandBufferFn end_command_buffer;
    QNVkCmdBindPipelineFn cmd_bind_pipeline;
    QNVkCmdBindDescriptorSetsFn cmd_bind_descriptor_sets;
    QNVkCmdDispatchFn cmd_dispatch;
    QNVkQueueSubmitFn queue_submit;
    QNVkQueueWaitIdleFn queue_wait_idle;
} QNVulkanComputeApi;

typedef struct {
    QNVkInstance instance;
    QNVkPhysicalDevice physical_device;
    QNVkDevice device;
    QNVkQueue queue;
    QNVkBuffer buffer;
    QNVkDeviceMemory memory;
    QNVkDescriptorSetLayout descriptor_set_layout;
    QNVkPipelineLayout pipeline_layout;
    QNVkShaderModule shader_module;
    QNVkPipeline pipeline;
    QNVkDescriptorPool descriptor_pool;
    QNVkDescriptorSet descriptor_set;
    QNVkCommandPool command_pool;
    QNVkCommandBuffer command_buffer;
    uint32_t queue_family;
    void *mapped;
} QNVulkanComputeContext;

static const uint32_t qn_gpu_vector_add_spirv[] = {
    0x07230203u, 0x00010300u, 0x00000000u, 0x0000001bu, 0x00000000u, 0x00020011u,
    0x00000001u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0006000fu, 0x00000005u,
    0x0000000fu, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00060010u, 0x0000000fu,
    0x00000011u, 0x00000040u, 0x00000001u, 0x00000001u, 0x00030003u, 0x00000002u,
    0x000001c2u, 0x00040047u, 0x00000006u, 0x00000006u, 0x00000004u, 0x00050048u,
    0x00000007u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u, 0x00000007u,
    0x00000002u, 0x00040047u, 0x0000000du, 0x0000000bu, 0x0000001cu, 0x00040047u,
    0x0000000eu, 0x00000022u, 0x00000000u, 0x00040047u, 0x0000000eu, 0x00000021u,
    0x00000000u, 0x00020013u, 0x00000001u, 0x00030021u, 0x00000002u, 0x00000001u,
    0x00040015u, 0x00000003u, 0x00000020u, 0x00000000u, 0x00040017u, 0x00000004u,
    0x00000003u, 0x00000003u, 0x00040020u, 0x00000005u, 0x00000001u, 0x00000004u,
    0x0003001du, 0x00000006u, 0x00000003u, 0x0003001eu, 0x00000007u, 0x00000006u,
    0x00040020u, 0x00000008u, 0x0000000cu, 0x00000007u, 0x00040020u, 0x00000009u,
    0x0000000cu, 0x00000003u, 0x0004002bu, 0x00000003u, 0x0000000au, 0x00000000u,
    0x0004002bu, 0x00000003u, 0x0000000bu, 0x00000100u, 0x0004002bu, 0x00000003u,
    0x0000000cu, 0x00000200u, 0x0004003bu, 0x00000005u, 0x0000000du, 0x00000001u,
    0x0004003bu, 0x00000008u, 0x0000000eu, 0x0000000cu, 0x00050036u, 0x00000001u,
    0x0000000fu, 0x00000000u, 0x00000002u, 0x000200f8u, 0x00000010u, 0x0004003du,
    0x00000004u, 0x00000011u, 0x0000000du, 0x00050051u, 0x00000003u, 0x00000012u,
    0x00000011u, 0x00000000u, 0x00050080u, 0x00000003u, 0x00000013u, 0x00000012u,
    0x0000000bu, 0x00050080u, 0x00000003u, 0x00000014u, 0x00000012u, 0x0000000cu,
    0x00060041u, 0x00000009u, 0x00000015u, 0x0000000eu, 0x0000000au, 0x00000012u,
    0x00060041u, 0x00000009u, 0x00000016u, 0x0000000eu, 0x0000000au, 0x00000013u,
    0x00060041u, 0x00000009u, 0x00000017u, 0x0000000eu, 0x0000000au, 0x00000014u,
    0x0004003du, 0x00000003u, 0x00000018u, 0x00000015u, 0x0004003du, 0x00000003u,
    0x00000019u, 0x00000016u, 0x00050080u, 0x00000003u, 0x0000001au, 0x00000018u,
    0x00000019u, 0x0003003eu, 0x00000017u, 0x0000001au, 0x000100fdu, 0x00010038u
};

static bool qn_gpu_load_symbol(void *library,
                               const char *name,
                               void *out,
                               size_t out_size) {
    void *symbol = dlsym(library, name);
    if (!symbol || out_size != sizeof(symbol)) {
        return false;
    }
    memcpy(out, &symbol, sizeof(symbol));
    return true;
}

#define QN_GPU_LOAD(api, field, name) \
    qn_gpu_load_symbol((api)->library, (name), \
                       &(api)->field, sizeof((api)->field))

static bool qn_gpu_compute_api_load(QNVulkanComputeApi *api) {
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!api->library) {
        return false;
    }

    if (!QN_GPU_LOAD(api, create_instance, "vkCreateInstance") ||
        !QN_GPU_LOAD(api, destroy_instance, "vkDestroyInstance") ||
        !QN_GPU_LOAD(api, enumerate_physical_devices,
                     "vkEnumeratePhysicalDevices") ||
        !QN_GPU_LOAD(api, get_physical_device_properties,
                     "vkGetPhysicalDeviceProperties") ||
        !QN_GPU_LOAD(api, get_physical_device_queue_family_properties,
                     "vkGetPhysicalDeviceQueueFamilyProperties") ||
        !QN_GPU_LOAD(api, get_physical_device_memory_properties,
                     "vkGetPhysicalDeviceMemoryProperties") ||
        !QN_GPU_LOAD(api, create_device, "vkCreateDevice") ||
        !QN_GPU_LOAD(api, destroy_device, "vkDestroyDevice") ||
        !QN_GPU_LOAD(api, get_device_queue, "vkGetDeviceQueue") ||
        !QN_GPU_LOAD(api, create_buffer, "vkCreateBuffer") ||
        !QN_GPU_LOAD(api, destroy_buffer, "vkDestroyBuffer") ||
        !QN_GPU_LOAD(api, get_buffer_memory_requirements,
                     "vkGetBufferMemoryRequirements") ||
        !QN_GPU_LOAD(api, allocate_memory, "vkAllocateMemory") ||
        !QN_GPU_LOAD(api, free_memory, "vkFreeMemory") ||
        !QN_GPU_LOAD(api, bind_buffer_memory, "vkBindBufferMemory") ||
        !QN_GPU_LOAD(api, map_memory, "vkMapMemory") ||
        !QN_GPU_LOAD(api, unmap_memory, "vkUnmapMemory") ||
        !QN_GPU_LOAD(api, create_descriptor_set_layout,
                     "vkCreateDescriptorSetLayout") ||
        !QN_GPU_LOAD(api, destroy_descriptor_set_layout,
                     "vkDestroyDescriptorSetLayout") ||
        !QN_GPU_LOAD(api, create_pipeline_layout,
                     "vkCreatePipelineLayout") ||
        !QN_GPU_LOAD(api, destroy_pipeline_layout,
                     "vkDestroyPipelineLayout") ||
        !QN_GPU_LOAD(api, create_shader_module,
                     "vkCreateShaderModule") ||
        !QN_GPU_LOAD(api, destroy_shader_module,
                     "vkDestroyShaderModule") ||
        !QN_GPU_LOAD(api, create_compute_pipelines,
                     "vkCreateComputePipelines") ||
        !QN_GPU_LOAD(api, destroy_pipeline, "vkDestroyPipeline") ||
        !QN_GPU_LOAD(api, create_descriptor_pool,
                     "vkCreateDescriptorPool") ||
        !QN_GPU_LOAD(api, destroy_descriptor_pool,
                     "vkDestroyDescriptorPool") ||
        !QN_GPU_LOAD(api, allocate_descriptor_sets,
                     "vkAllocateDescriptorSets") ||
        !QN_GPU_LOAD(api, update_descriptor_sets,
                     "vkUpdateDescriptorSets") ||
        !QN_GPU_LOAD(api, create_command_pool, "vkCreateCommandPool") ||
        !QN_GPU_LOAD(api, destroy_command_pool, "vkDestroyCommandPool") ||
        !QN_GPU_LOAD(api, allocate_command_buffers,
                     "vkAllocateCommandBuffers") ||
        !QN_GPU_LOAD(api, begin_command_buffer, "vkBeginCommandBuffer") ||
        !QN_GPU_LOAD(api, end_command_buffer, "vkEndCommandBuffer") ||
        !QN_GPU_LOAD(api, cmd_bind_pipeline, "vkCmdBindPipeline") ||
        !QN_GPU_LOAD(api, cmd_bind_descriptor_sets,
                     "vkCmdBindDescriptorSets") ||
        !QN_GPU_LOAD(api, cmd_dispatch, "vkCmdDispatch") ||
        !QN_GPU_LOAD(api, queue_submit, "vkQueueSubmit") ||
        !QN_GPU_LOAD(api, queue_wait_idle, "vkQueueWaitIdle")) {
        dlclose(api->library);
        memset(api, 0, sizeof(*api));
        return false;
    }
    return true;
}

static void qn_gpu_compute_api_unload(QNVulkanComputeApi *api) {
    if (api->library) {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

static void qn_gpu_compute_cleanup(QNVulkanComputeApi *api,
                                   QNVulkanComputeContext *ctx) {
    if (ctx->mapped && ctx->device && ctx->memory) {
        api->unmap_memory(ctx->device, ctx->memory);
    }
    if (ctx->command_pool && ctx->device) {
        api->destroy_command_pool(ctx->device, ctx->command_pool, NULL);
    }
    if (ctx->descriptor_pool && ctx->device) {
        api->destroy_descriptor_pool(ctx->device,
                                     ctx->descriptor_pool,
                                     NULL);
    }
    if (ctx->pipeline && ctx->device) {
        api->destroy_pipeline(ctx->device, ctx->pipeline, NULL);
    }
    if (ctx->shader_module && ctx->device) {
        api->destroy_shader_module(ctx->device, ctx->shader_module, NULL);
    }
    if (ctx->pipeline_layout && ctx->device) {
        api->destroy_pipeline_layout(ctx->device,
                                     ctx->pipeline_layout,
                                     NULL);
    }
    if (ctx->descriptor_set_layout && ctx->device) {
        api->destroy_descriptor_set_layout(
            ctx->device,
            ctx->descriptor_set_layout,
            NULL
        );
    }
    if (ctx->buffer && ctx->device) {
        api->destroy_buffer(ctx->device, ctx->buffer, NULL);
    }
    if (ctx->memory && ctx->device) {
        api->free_memory(ctx->device, ctx->memory, NULL);
    }
    if (ctx->device) {
        api->destroy_device(ctx->device, NULL);
    }
    if (ctx->instance) {
        api->destroy_instance(ctx->instance, NULL);
    }
    memset(ctx, 0, sizeof(*ctx));
}

static bool qn_gpu_name_supported(const char *name) {
    return name && strncmp(name, "V3D ", 4u) == 0 &&
           strstr(name, "llvmpipe") == NULL;
}

static bool qn_gpu_select_device(QNVulkanComputeApi *api,
                                 QNVulkanComputeContext *ctx,
                                 char device_name[QN_GPU_DEVICE_NAME_CAP],
                                 uint32_t *vendor_id,
                                 QNDiagnostic *diag) {
    QNVkApplicationInfo app;
    memset(&app, 0, sizeof(app));
    app.s_type = QN_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.application_name = "QBIT NOVA GPU compute proof";
    app.application_version = 1u;
    app.engine_name = "QBIT NOVA";
    app.engine_version = 1u;
    app.api_version = QN_VK_API_VERSION_1_0;

    QNVkInstanceCreateInfo create;
    memset(&create, 0, sizeof(create));
    create.s_type = QN_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.application_info = &app;

    QNVkResult result = api->create_instance(&create, NULL, &ctx->instance);
    if (result != QN_VK_SUCCESS || !ctx->instance) {
        qn_diag_set_code(diag, "QN-E7102", 0, 0,
                         "cannot create Vulkan instance: %d", result);
        return false;
    }

    uint32_t count = 0u;
    result = api->enumerate_physical_devices(ctx->instance, &count, NULL);
    if (result != QN_VK_SUCCESS || count == 0u || count > 64u) {
        qn_diag_set_code(diag, "QN-E7103", 0, 0,
                         "no bounded Vulkan device set: %d", result);
        return false;
    }

    QNVkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) {
        qn_diag_set_code(diag, "QN-E7104", 0, 0,
                         "out of memory enumerating Vulkan devices");
        return false;
    }

    result = api->enumerate_physical_devices(ctx->instance, &count, devices);
    if (result != QN_VK_SUCCESS) {
        free(devices);
        qn_diag_set_code(diag, "QN-E7103", 0, 0,
                         "cannot enumerate Vulkan devices: %d", result);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0u; i < count && !found; ++i) {
        _Alignas(8) QNVkPhysicalDeviceProperties props;
        memset(&props, 0, sizeof(props));
        api->get_physical_device_properties(devices[i], &props);
        props.device_name[QN_GPU_DEVICE_NAME_CAP - 1u] = '\0';

        if (props.vendor_id != QN_VK_VENDOR_BROADCOM ||
            props.device_type !=
                QN_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
            !qn_gpu_name_supported(props.device_name)) {
            continue;
        }

        uint32_t queue_count = 0u;
        api->get_physical_device_queue_family_properties(
            devices[i], &queue_count, NULL
        );
        if (queue_count == 0u || queue_count > 256u) {
            continue;
        }

        QNVkQueueFamilyProperties *queues =
            calloc(queue_count, sizeof(*queues));
        if (!queues) {
            free(devices);
            qn_diag_set_code(diag, "QN-E7104", 0, 0,
                             "out of memory enumerating Vulkan queues");
            return false;
        }

        api->get_physical_device_queue_family_properties(
            devices[i], &queue_count, queues
        );
        for (uint32_t q = 0u; q < queue_count; ++q) {
            if (queues[q].queue_count > 0u &&
                (queues[q].queue_flags &
                 QN_VK_QUEUE_COMPUTE_BIT) != 0u) {
                ctx->physical_device = devices[i];
                ctx->queue_family = q;
                snprintf(device_name,
                         QN_GPU_DEVICE_NAME_CAP,
                         "%s",
                         props.device_name);
                *vendor_id = props.vendor_id;
                found = true;
                break;
            }
        }
        free(queues);
    }

    free(devices);

    if (!found) {
        qn_diag_set_code(diag, "QN-E7101", 0, 0,
                         "supported V3D Vulkan compute device unavailable");
    }
    return found;
}

static bool qn_gpu_find_memory_type(
    const QNVkPhysicalDeviceMemoryProperties *properties,
    uint32_t type_bits,
    uint32_t *index_out
) {
    const QNVkFlags required =
        QN_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        QN_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t i = 0u; i < properties->memory_type_count; ++i) {
        if ((type_bits & (1u << i)) != 0u &&
            (properties->memory_types[i].property_flags & required) ==
                required) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static void qn_gpu_fill_inputs(uint32_t data[QN_GPU_DATA_WORDS],
                               uint32_t expected[
                                   QN_GPU_COMPUTE_ELEMENT_COUNT]) {
    for (uint32_t i = 0u; i < QN_GPU_COMPUTE_ELEMENT_COUNT; ++i) {
        const uint32_t a = 0x10000000u + i * 3u;
        const uint32_t b = 0x01000000u + i * 5u;
        data[i] = a;
        data[QN_GPU_COMPUTE_ELEMENT_COUNT + i] = b;
        data[2u * QN_GPU_COMPUTE_ELEMENT_COUNT + i] = 0xdeadbeefu;
        expected[i] = a + b;
    }
}

static QNStatus qn_gpu_run_vulkan(QNGpuComputeProof *proof,
                                  QNDiagnostic *diag) {
    if (access(QN_GPU_RENDER_NODE, R_OK | W_OK) != 0) {
        qn_diag_set_code(diag, "QN-E7101", 0, 0,
                         "render node is not read/write accessible: %s",
                         QN_GPU_RENDER_NODE);
        return QN_ERR_RUNTIME;
    }

    QNVulkanComputeApi api;
    if (!qn_gpu_compute_api_load(&api)) {
        qn_diag_set_code(diag, "QN-E7101", 0, 0,
                         "Vulkan loader or required symbols unavailable");
        return QN_ERR_RUNTIME;
    }

    QNVulkanComputeContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    QNStatus status = QN_ERR_RUNTIME;
    QNVkResult result = QN_VK_SUCCESS;
    uint32_t expected[QN_GPU_COMPUTE_ELEMENT_COUNT];

    if (!qn_gpu_select_device(&api,
                              &ctx,
                              proof->hardware_device,
                              &proof->hardware_vendor_id,
                              diag)) {
        goto cleanup;
    }

    float priority = 1.0f;
    QNVkDeviceQueueCreateInfo queue_info;
    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.s_type =
        QN_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queue_family_index = ctx.queue_family;
    queue_info.queue_count = 1u;
    queue_info.queue_priorities = &priority;

    QNVkDeviceCreateInfo device_info;
    memset(&device_info, 0, sizeof(device_info));
    device_info.s_type = QN_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queue_create_info_count = 1u;
    device_info.queue_create_infos = &queue_info;

    result = api.create_device(ctx.physical_device,
                               &device_info,
                               NULL,
                               &ctx.device);
    if (result != QN_VK_SUCCESS || !ctx.device) {
        qn_diag_set_code(diag, "QN-E7105", 0, 0,
                         "cannot create V3D logical device: %d", result);
        goto cleanup;
    }
    api.get_device_queue(ctx.device, ctx.queue_family, 0u, &ctx.queue);
    if (!ctx.queue) {
        qn_diag_set_code(diag, "QN-E7105", 0, 0,
                         "V3D compute queue handle unavailable");
        goto cleanup;
    }

    QNVkBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.s_type = QN_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = QN_GPU_DATA_BYTES;
    buffer_info.usage = QN_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharing_mode = QN_VK_SHARING_MODE_EXCLUSIVE;

    result = api.create_buffer(ctx.device,
                               &buffer_info,
                               NULL,
                               &ctx.buffer);
    if (result != QN_VK_SUCCESS || !ctx.buffer) {
        qn_diag_set_code(diag, "QN-E7106", 0, 0,
                         "cannot create bounded storage buffer: %d", result);
        goto cleanup;
    }

    QNVkMemoryRequirements requirements;
    memset(&requirements, 0, sizeof(requirements));
    api.get_buffer_memory_requirements(
        ctx.device, ctx.buffer, &requirements
    );

    QNVkPhysicalDeviceMemoryProperties memory_properties;
    memset(&memory_properties, 0, sizeof(memory_properties));
    api.get_physical_device_memory_properties(
        ctx.physical_device, &memory_properties
    );

    uint32_t memory_type = 0u;
    if (!qn_gpu_find_memory_type(&memory_properties,
                                 requirements.memory_type_bits,
                                 &memory_type)) {
        qn_diag_set_code(
            diag,
            "QN-E7107",
            0,
            0,
            "host-visible coherent V3D storage memory unavailable"
        );
        goto cleanup;
    }

    QNVkMemoryAllocateInfo allocation;
    memset(&allocation, 0, sizeof(allocation));
    allocation.s_type = QN_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocation_size = requirements.size;
    allocation.memory_type_index = memory_type;

    result = api.allocate_memory(ctx.device,
                                 &allocation,
                                 NULL,
                                 &ctx.memory);
    if (result != QN_VK_SUCCESS || !ctx.memory) {
        qn_diag_set_code(diag, "QN-E7108", 0, 0,
                         "cannot allocate bounded V3D memory: %d", result);
        goto cleanup;
    }

    result = api.bind_buffer_memory(
        ctx.device, ctx.buffer, ctx.memory, 0u
    );
    if (result != QN_VK_SUCCESS) {
        qn_diag_set_code(diag, "QN-E7108", 0, 0,
                         "cannot bind V3D storage memory: %d", result);
        goto cleanup;
    }

    result = api.map_memory(ctx.device,
                            ctx.memory,
                            0u,
                            requirements.size,
                            0u,
                            &ctx.mapped);
    if (result != QN_VK_SUCCESS || !ctx.mapped) {
        qn_diag_set_code(diag, "QN-E7109", 0, 0,
                         "cannot map V3D storage memory: %d", result);
        goto cleanup;
    }
    qn_gpu_fill_inputs((uint32_t *)ctx.mapped, expected);

    QNVkDescriptorSetLayoutBinding binding;
    memset(&binding, 0, sizeof(binding));
    binding.binding = 0u;
    binding.descriptor_type = QN_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptor_count = 1u;
    binding.stage_flags = QN_VK_SHADER_STAGE_COMPUTE_BIT;

    QNVkDescriptorSetLayoutCreateInfo descriptor_layout_info;
    memset(&descriptor_layout_info, 0, sizeof(descriptor_layout_info));
    descriptor_layout_info.s_type =
        QN_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.binding_count = 1u;
    descriptor_layout_info.bindings = &binding;

    result = api.create_descriptor_set_layout(
        ctx.device,
        &descriptor_layout_info,
        NULL,
        &ctx.descriptor_set_layout
    );
    if (result != QN_VK_SUCCESS || !ctx.descriptor_set_layout) {
        qn_diag_set_code(diag, "QN-E7110", 0, 0,
                         "cannot create compute descriptor layout: %d",
                         result);
        goto cleanup;
    }

    QNVkPipelineLayoutCreateInfo pipeline_layout_info;
    memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
    pipeline_layout_info.s_type =
        QN_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.set_layout_count = 1u;
    pipeline_layout_info.set_layouts = &ctx.descriptor_set_layout;

    result = api.create_pipeline_layout(
        ctx.device,
        &pipeline_layout_info,
        NULL,
        &ctx.pipeline_layout
    );
    if (result != QN_VK_SUCCESS || !ctx.pipeline_layout) {
        qn_diag_set_code(diag, "QN-E7111", 0, 0,
                         "cannot create compute pipeline layout: %d",
                         result);
        goto cleanup;
    }

    QNVkShaderModuleCreateInfo shader_info;
    memset(&shader_info, 0, sizeof(shader_info));
    shader_info.s_type =
        QN_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.code_size = sizeof(qn_gpu_vector_add_spirv);
    shader_info.code = qn_gpu_vector_add_spirv;

    result = api.create_shader_module(
        ctx.device, &shader_info, NULL, &ctx.shader_module
    );
    if (result != QN_VK_SUCCESS || !ctx.shader_module) {
        qn_diag_set_code(diag, "QN-E7112", 0, 0,
                         "embedded SPIR-V rejected by V3D: %d", result);
        goto cleanup;
    }

    QNVkComputePipelineCreateInfo pipeline_info;
    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.s_type =
        QN_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.s_type =
        QN_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = QN_VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = ctx.shader_module;
    pipeline_info.stage.name = "main";
    pipeline_info.layout = ctx.pipeline_layout;
    pipeline_info.base_pipeline_index = -1;

    result = api.create_compute_pipelines(
        ctx.device, 0u, 1u, &pipeline_info, NULL, &ctx.pipeline
    );
    if (result != QN_VK_SUCCESS || !ctx.pipeline) {
        qn_diag_set_code(diag, "QN-E7113", 0, 0,
                         "cannot create bounded V3D compute pipeline: %d",
                         result);
        goto cleanup;
    }

    QNVkDescriptorPoolSize pool_size;
    pool_size.type = QN_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptor_count = 1u;

    QNVkDescriptorPoolCreateInfo pool_info;
    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.s_type =
        QN_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.max_sets = 1u;
    pool_info.pool_size_count = 1u;
    pool_info.pool_sizes = &pool_size;

    result = api.create_descriptor_pool(
        ctx.device, &pool_info, NULL, &ctx.descriptor_pool
    );
    if (result != QN_VK_SUCCESS || !ctx.descriptor_pool) {
        qn_diag_set_code(diag, "QN-E7114", 0, 0,
                         "cannot create bounded descriptor pool: %d",
                         result);
        goto cleanup;
    }

    QNVkDescriptorSetAllocateInfo set_info;
    memset(&set_info, 0, sizeof(set_info));
    set_info.s_type =
        QN_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptor_pool = ctx.descriptor_pool;
    set_info.descriptor_set_count = 1u;
    set_info.set_layouts = &ctx.descriptor_set_layout;

    result = api.allocate_descriptor_sets(
        ctx.device, &set_info, &ctx.descriptor_set
    );
    if (result != QN_VK_SUCCESS || !ctx.descriptor_set) {
        qn_diag_set_code(diag, "QN-E7114", 0, 0,
                         "cannot allocate bounded descriptor set: %d",
                         result);
        goto cleanup;
    }

    QNVkDescriptorBufferInfo descriptor_buffer;
    descriptor_buffer.buffer = ctx.buffer;
    descriptor_buffer.offset = 0u;
    descriptor_buffer.range = QN_GPU_DATA_BYTES;

    QNVkWriteDescriptorSet write;
    memset(&write, 0, sizeof(write));
    write.s_type = QN_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.destination_set = ctx.descriptor_set;
    write.destination_binding = 0u;
    write.descriptor_count = 1u;
    write.descriptor_type = QN_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer_info = &descriptor_buffer;

    api.update_descriptor_sets(ctx.device, 1u, &write, 0u, NULL);

    QNVkCommandPoolCreateInfo command_pool_info;
    memset(&command_pool_info, 0, sizeof(command_pool_info));
    command_pool_info.s_type =
        QN_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_info.queue_family_index = ctx.queue_family;

    result = api.create_command_pool(
        ctx.device, &command_pool_info, NULL, &ctx.command_pool
    );
    if (result != QN_VK_SUCCESS || !ctx.command_pool) {
        qn_diag_set_code(diag, "QN-E7115", 0, 0,
                         "cannot create bounded command pool: %d",
                         result);
        goto cleanup;
    }

    QNVkCommandBufferAllocateInfo command_allocate;
    memset(&command_allocate, 0, sizeof(command_allocate));
    command_allocate.s_type =
        QN_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_allocate.command_pool = ctx.command_pool;
    command_allocate.level = QN_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.command_buffer_count = 1u;

    result = api.allocate_command_buffers(
        ctx.device, &command_allocate, &ctx.command_buffer
    );
    if (result != QN_VK_SUCCESS || !ctx.command_buffer) {
        qn_diag_set_code(diag, "QN-E7115", 0, 0,
                         "cannot allocate bounded command buffer: %d",
                         result);
        goto cleanup;
    }

    QNVkCommandBufferBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.s_type = QN_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = QN_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = api.begin_command_buffer(ctx.command_buffer, &begin);
    if (result != QN_VK_SUCCESS) {
        qn_diag_set_code(diag, "QN-E7116", 0, 0,
                         "cannot begin compute command buffer: %d", result);
        goto cleanup;
    }

    api.cmd_bind_pipeline(
        ctx.command_buffer,
        QN_VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.pipeline
    );
    api.cmd_bind_descriptor_sets(
        ctx.command_buffer,
        QN_VK_PIPELINE_BIND_POINT_COMPUTE,
        ctx.pipeline_layout,
        0u,
        1u,
        &ctx.descriptor_set,
        0u,
        NULL
    );
    api.cmd_dispatch(
        ctx.command_buffer,
        QN_GPU_COMPUTE_DISPATCH_X,
        1u,
        1u
    );

    result = api.end_command_buffer(ctx.command_buffer);
    if (result != QN_VK_SUCCESS) {
        qn_diag_set_code(diag, "QN-E7116", 0, 0,
                         "cannot finalize compute command buffer: %d",
                         result);
        goto cleanup;
    }

    QNVkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.s_type = QN_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &ctx.command_buffer;

    proof->gpu_execution_attempted = true;
    result = api.queue_submit(ctx.queue, 1u, &submit, 0u);
    if (result != QN_VK_SUCCESS) {
        qn_diag_set_code(diag, "QN-E7117", 0, 0,
                         "V3D compute submission failed: %d", result);
        goto cleanup;
    }

    result = api.queue_wait_idle(ctx.queue);
    if (result != QN_VK_SUCCESS) {
        qn_diag_set_code(diag, "QN-E7117", 0, 0,
                         "V3D compute completion failed: %d", result);
        goto cleanup;
    }
    proof->gpu_execution_completed = true;

    const uint32_t *gpu_output =
        (const uint32_t *)ctx.mapped +
        2u * QN_GPU_COMPUTE_ELEMENT_COUNT;

    proof->cpu_reference_validated = true;
    proof->result_match =
        memcmp(gpu_output, expected, sizeof(expected)) == 0;

    if (!proof->result_match) {
        qn_diag_set_code(
            diag,
            "QN-E7118",
            0,
            0,
            "V3D output mismatch against deterministic CPU reference"
        );
        goto cleanup;
    }

    qn_sha256((const uint8_t *)gpu_output,
              sizeof(expected),
              proof->output_digest);
    status = QN_OK;

cleanup:
    qn_gpu_compute_cleanup(&api, &ctx);
    qn_gpu_compute_api_unload(&api);
    return status;
}

QNStatus qn_gpu_compute_proof(QNGpuBackendRequest requested,
                              QNGpuComputeProof *out,
                              QNDiagnostic *diag) {
    if (!out) {
        qn_diag_set_code(diag, "QN-E7100", 0, 0,
                         "invalid GPU compute proof output");
        return QN_ERR_RUNTIME;
    }

    memset(out, 0, sizeof(*out));
    out->requested = requested;
    out->element_count = QN_GPU_COMPUTE_ELEMENT_COUNT;
    out->local_size_x = QN_GPU_COMPUTE_LOCAL_SIZE_X;
    out->dispatch_x = QN_GPU_COMPUTE_DISPATCH_X;
    qn_sha256((const uint8_t *)qn_gpu_vector_add_spirv,
              sizeof(qn_gpu_vector_add_spirv),
              out->shader_digest);

    uint32_t data[QN_GPU_DATA_WORDS];
    uint32_t expected[QN_GPU_COMPUTE_ELEMENT_COUNT];
    qn_gpu_fill_inputs(data, expected);

    if (requested == QN_GPU_BACKEND_CPU) {
        out->selected_backend = "cpu";
        out->selection_reason = "explicit-cpu-reference";
        out->cpu_reference_validated = true;
        out->result_match = true;
        out->cpu_fallback = true;
        qn_sha256((const uint8_t *)expected,
                  sizeof(expected),
                  out->output_digest);
        return QN_OK;
    }

    QNGpuProbe probe;
    QNStatus status = qn_gpu_probe(&probe, diag);
    if (status != QN_OK) {
        return status;
    }

    if (!probe.hardware_candidate_available) {
        if (requested == QN_GPU_BACKEND_VULKAN) {
            qn_diag_set_code(diag, "QN-E7101", 0, 0,
                             "supported V3D hardware unavailable");
            return QN_ERR_RUNTIME;
        }

        out->selected_backend = "cpu";
        out->selection_reason = "hardware-unavailable-cpu-fallback";
        out->cpu_reference_validated = true;
        out->result_match = true;
        out->cpu_fallback = true;
        qn_sha256((const uint8_t *)expected,
                  sizeof(expected),
                  out->output_digest);
        return QN_OK;
    }

    out->selected_backend = "vulkan";
    out->selection_reason =
        requested == QN_GPU_BACKEND_AUTO
            ? "verified-v3d-auto"
            : "explicit-verified-v3d";
    out->cpu_fallback = false;

    status = qn_gpu_run_vulkan(out, diag);
    if (status != QN_OK) {
        return status;
    }
    return QN_OK;
}

static const char *qn_gpu_bool(bool value) {
    return value ? "true" : "false";
}

void qn_gpu_compute_print(const QNGpuComputeProof *proof, FILE *stream) {
    if (!proof || !stream) {
        return;
    }
    char shader_hex[65];
    char output_hex[65];
    qn_hex32(proof->shader_digest, shader_hex);
    qn_hex32(proof->output_digest, output_hex);

    fprintf(stream, "QBIT_NOVA_GPU_COMPUTE_PROOF_V06\n");
    fprintf(stream, "proof_version=1\n");
    fprintf(stream, "operation=uint32-vector-add\n");
    fprintf(stream, "requested_backend=%s\n",
            qn_gpu_backend_name(proof->requested));
    fprintf(stream, "selected_backend=%s\n",
            proof->selected_backend);
    fprintf(stream, "selection_reason=%s\n",
            proof->selection_reason);
    fprintf(stream, "hardware_device=%s\n",
            proof->hardware_device[0]
                ? proof->hardware_device
                : "none");
    fprintf(stream, "hardware_vendor_id=0x%04x\n",
            proof->hardware_vendor_id);
    fprintf(stream, "element_count=%u\n", proof->element_count);
    fprintf(stream, "local_size_x=%u\n", proof->local_size_x);
    fprintf(stream, "dispatch_x=%u\n", proof->dispatch_x);
    fprintf(stream, "gpu_execution_attempted=%s\n",
            qn_gpu_bool(proof->gpu_execution_attempted));
    fprintf(stream, "gpu_execution_completed=%s\n",
            qn_gpu_bool(proof->gpu_execution_completed));
    fprintf(stream, "cpu_reference_validated=%s\n",
            qn_gpu_bool(proof->cpu_reference_validated));
    fprintf(stream, "result_match=%s\n",
            qn_gpu_bool(proof->result_match));
    fprintf(stream, "cpu_fallback=%s\n",
            qn_gpu_bool(proof->cpu_fallback));
    fprintf(stream, "shader_sha256=%s\n", shader_hex);
    fprintf(stream, "output_sha256=%s\n", output_hex);
}

static void qn_gpu_json_string(FILE *stream, const char *value) {
    fputc('"', stream);
    for (const unsigned char *p =
             (const unsigned char *)(value ? value : "");
         *p;
         ++p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', stream);
            fputc((int)*p, stream);
        } else if (*p == '\n') {
            fputs("\\n", stream);
        } else if (*p < 0x20u) {
            fprintf(stream, "\\u%04x", (unsigned int)*p);
        } else {
            fputc((int)*p, stream);
        }
    }
    fputc('"', stream);
}

QNStatus qn_gpu_compute_write_receipt(const char *path,
                                      const QNGpuComputeProof *proof,
                                      QNDiagnostic *diag) {
    if (!path || !proof) {
        qn_diag_set_code(diag, "QN-E7119", 0, 0,
                         "invalid GPU compute receipt arguments");
        return QN_ERR_RUNTIME;
    }

    FILE *stream = fopen(path, "wb");
    if (!stream) {
        qn_diag_set_code(diag, "QN-E7119", 0, 0,
                         "cannot open GPU compute receipt: %s", path);
        return QN_ERR_IO;
    }

    char shader_hex[65];
    char output_hex[65];
    qn_hex32(proof->shader_digest, shader_hex);
    qn_hex32(proof->output_digest, output_hex);

    fputs("{\n  \"schema\": "
          "\"QBIT_NOVA_GPU_COMPUTE_PROOF_V06\",\n", stream);
    fputs("  \"proof_version\": 1,\n", stream);
    fputs("  \"operation\": \"uint32-vector-add\",\n", stream);
    fputs("  \"requested_backend\": ", stream);
    qn_gpu_json_string(stream, qn_gpu_backend_name(proof->requested));
    fputs(",\n  \"selected_backend\": ", stream);
    qn_gpu_json_string(stream, proof->selected_backend);
    fputs(",\n  \"selection_reason\": ", stream);
    qn_gpu_json_string(stream, proof->selection_reason);
    fputs(",\n  \"hardware_device\": ", stream);
    qn_gpu_json_string(
        stream,
        proof->hardware_device[0] ? proof->hardware_device : "none"
    );
    fprintf(stream,
            ",\n  \"hardware_vendor_id\": \"0x%04x\",\n",
            proof->hardware_vendor_id);
    fprintf(stream, "  \"element_count\": %u,\n",
            proof->element_count);
    fprintf(stream, "  \"local_size_x\": %u,\n",
            proof->local_size_x);
    fprintf(stream, "  \"dispatch_x\": %u,\n",
            proof->dispatch_x);
    fprintf(stream, "  \"gpu_execution_attempted\": %s,\n",
            qn_gpu_bool(proof->gpu_execution_attempted));
    fprintf(stream, "  \"gpu_execution_completed\": %s,\n",
            qn_gpu_bool(proof->gpu_execution_completed));
    fprintf(stream, "  \"cpu_reference_validated\": %s,\n",
            qn_gpu_bool(proof->cpu_reference_validated));
    fprintf(stream, "  \"result_match\": %s,\n",
            qn_gpu_bool(proof->result_match));
    fprintf(stream, "  \"cpu_fallback\": %s,\n",
            qn_gpu_bool(proof->cpu_fallback));
    fprintf(stream, "  \"shader_sha256\": \"%s\",\n",
            shader_hex);
    fprintf(stream, "  \"output_sha256\": \"%s\"\n}\n",
            output_hex);

    if (fclose(stream) != 0) {
        qn_diag_set_code(diag, "QN-E7119", 0, 0,
                         "cannot finalize GPU compute receipt: %s", path);
        return QN_ERR_IO;
    }
    return QN_OK;
}
