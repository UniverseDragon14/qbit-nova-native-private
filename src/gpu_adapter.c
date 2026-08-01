#include "qn_gpu_adapter.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QN_VK_SUCCESS 0
#define QN_VK_STRUCTURE_TYPE_APPLICATION_INFO 0u
#define QN_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1u
#define QN_VK_QUEUE_COMPUTE_BIT 0x00000002u
#define QN_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1u
#define QN_VK_PHYSICAL_DEVICE_TYPE_CPU 4u
#define QN_VK_VENDOR_BROADCOM 0x14e4u
#define QN_VK_API_VERSION_1_0 (1u << 22)

typedef int32_t QNVkResult;
typedef uint32_t QNVkFlags;
typedef struct QNVkInstance_T *QNVkInstance;
typedef struct QNVkPhysicalDevice_T *QNVkPhysicalDevice;

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

typedef QNVkResult (*QNVkCreateInstanceFn)(
    const QNVkInstanceCreateInfo *,
    const void *,
    QNVkInstance *
);
typedef void (*QNVkDestroyInstanceFn)(QNVkInstance, const void *);
typedef QNVkResult (*QNVkEnumeratePhysicalDevicesFn)(
    QNVkInstance,
    uint32_t *,
    QNVkPhysicalDevice *
);
typedef void (*QNVkGetPhysicalDevicePropertiesFn)(
    QNVkPhysicalDevice,
    QNVkPhysicalDeviceProperties *
);
typedef void (*QNVkGetPhysicalDeviceQueueFamilyPropertiesFn)(
    QNVkPhysicalDevice,
    uint32_t *,
    QNVkQueueFamilyProperties *
);

typedef struct {
    void *library;
    QNVkCreateInstanceFn create_instance;
    QNVkDestroyInstanceFn destroy_instance;
    QNVkEnumeratePhysicalDevicesFn enumerate_physical_devices;
    QNVkGetPhysicalDevicePropertiesFn get_physical_device_properties;
    QNVkGetPhysicalDeviceQueueFamilyPropertiesFn
        get_physical_device_queue_family_properties;
} QNVulkanApi;

_Static_assert(sizeof(QNVkCreateInstanceFn) == sizeof(void *),
               "Vulkan function pointer size mismatch");
_Static_assert(sizeof(QNVkDestroyInstanceFn) == sizeof(void *),
               "Vulkan function pointer size mismatch");
_Static_assert(sizeof(QNVkEnumeratePhysicalDevicesFn) == sizeof(void *),
               "Vulkan function pointer size mismatch");
_Static_assert(sizeof(QNVkGetPhysicalDevicePropertiesFn) == sizeof(void *),
               "Vulkan function pointer size mismatch");
_Static_assert(
    sizeof(QNVkGetPhysicalDeviceQueueFamilyPropertiesFn) == sizeof(void *),
    "Vulkan function pointer size mismatch"
);

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

static bool qn_gpu_vulkan_load(QNVulkanApi *api) {
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!api->library) {
        return false;
    }

    if (!qn_gpu_load_symbol(api->library,
                            "vkCreateInstance",
                            &api->create_instance,
                            sizeof(api->create_instance)) ||
        !qn_gpu_load_symbol(api->library,
                            "vkDestroyInstance",
                            &api->destroy_instance,
                            sizeof(api->destroy_instance)) ||
        !qn_gpu_load_symbol(api->library,
                            "vkEnumeratePhysicalDevices",
                            &api->enumerate_physical_devices,
                            sizeof(api->enumerate_physical_devices)) ||
        !qn_gpu_load_symbol(api->library,
                            "vkGetPhysicalDeviceProperties",
                            &api->get_physical_device_properties,
                            sizeof(api->get_physical_device_properties)) ||
        !qn_gpu_load_symbol(
            api->library,
            "vkGetPhysicalDeviceQueueFamilyProperties",
            &api->get_physical_device_queue_family_properties,
            sizeof(api->get_physical_device_queue_family_properties))) {
        dlclose(api->library);
        memset(api, 0, sizeof(*api));
        return false;
    }

    return true;
}

static void qn_gpu_vulkan_unload(QNVulkanApi *api) {
    if (api->library) {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

static bool qn_gpu_name_is_llvmpipe(const char *name) {
    return name && strstr(name, "llvmpipe") != NULL;
}

static void qn_gpu_copy_name(char out[QN_GPU_DEVICE_NAME_CAP],
                             const char *name) {
    if (!name) {
        out[0] = '\0';
        return;
    }
    snprintf(out, QN_GPU_DEVICE_NAME_CAP, "%s", name);
}

static bool qn_gpu_candidate_better(const QNGpuDeviceCandidate *candidate,
                                    const QNGpuDeviceCandidate *current) {
    if (!current->name[0]) {
        return true;
    }
    if (candidate->device_id != current->device_id) {
        return candidate->device_id < current->device_id;
    }
    return strcmp(candidate->name, current->name) < 0;
}

bool qn_gpu_backend_parse(const char *name, QNGpuBackendRequest *out) {
    if (!name || !out) {
        return false;
    }
    if (strcmp(name, "auto") == 0) {
        *out = QN_GPU_BACKEND_AUTO;
        return true;
    }
    if (strcmp(name, "cpu") == 0) {
        *out = QN_GPU_BACKEND_CPU;
        return true;
    }
    if (strcmp(name, "vulkan") == 0) {
        *out = QN_GPU_BACKEND_VULKAN;
        return true;
    }
    return false;
}

const char *qn_gpu_backend_name(QNGpuBackendRequest backend) {
    switch (backend) {
        case QN_GPU_BACKEND_AUTO: return "auto";
        case QN_GPU_BACKEND_CPU: return "cpu";
        case QN_GPU_BACKEND_VULKAN: return "vulkan";
        default: return "invalid";
    }
}

bool qn_gpu_candidate_is_supported(const QNGpuDeviceCandidate *candidate) {
    if (!candidate) {
        return false;
    }
    return candidate->vendor_id == QN_VK_VENDOR_BROADCOM &&
           candidate->device_type ==
               QN_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
           strncmp(candidate->name, "V3D ", 4u) == 0 &&
           !qn_gpu_name_is_llvmpipe(candidate->name) &&
           candidate->compute_queue &&
           candidate->render_access;
}

QNStatus qn_gpu_probe(QNGpuProbe *out, QNDiagnostic *diag) {
    if (!out) {
        qn_diag_set_code(diag,
                         "QN-E7001",
                         0,
                         0,
                         "invalid GPU probe output");
        return QN_ERR_RUNTIME;
    }

    memset(out, 0, sizeof(*out));
    out->render_node_readable =
        access(QN_GPU_RENDER_NODE, R_OK) == 0;
    out->render_node_writable =
        access(QN_GPU_RENDER_NODE, W_OK) == 0;

    QNVulkanApi api;
    if (!qn_gpu_vulkan_load(&api)) {
        return QN_OK;
    }
    out->vulkan_loader_available = true;

    QNVkApplicationInfo application_info;
    memset(&application_info, 0, sizeof(application_info));
    application_info.s_type = QN_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.application_name = "QBIT NOVA GPU probe";
    application_info.application_version = 1u;
    application_info.engine_name = "QBIT NOVA";
    application_info.engine_version = 1u;
    application_info.api_version = QN_VK_API_VERSION_1_0;

    QNVkInstanceCreateInfo create_info;
    memset(&create_info, 0, sizeof(create_info));
    create_info.s_type = QN_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.application_info = &application_info;

    QNVkInstance instance = NULL;
    QNVkResult result = api.create_instance(&create_info, NULL, &instance);
    if (result != QN_VK_SUCCESS || !instance) {
        qn_gpu_vulkan_unload(&api);
        return QN_OK;
    }

    uint32_t device_count = 0u;
    result = api.enumerate_physical_devices(instance, &device_count, NULL);
    if (result != QN_VK_SUCCESS || device_count == 0u ||
        device_count > 64u) {
        api.destroy_instance(instance, NULL);
        qn_gpu_vulkan_unload(&api);
        return QN_OK;
    }

    QNVkPhysicalDevice *devices = calloc(
        device_count,
        sizeof(*devices)
    );
    if (!devices) {
        api.destroy_instance(instance, NULL);
        qn_gpu_vulkan_unload(&api);
        qn_diag_set_code(diag,
                         "QN-E7004",
                         0,
                         0,
                         "out of memory during GPU probe");
        return QN_ERR_RUNTIME;
    }

    result = api.enumerate_physical_devices(
        instance,
        &device_count,
        devices
    );
    if (result != QN_VK_SUCCESS) {
        free(devices);
        api.destroy_instance(instance, NULL);
        qn_gpu_vulkan_unload(&api);
        return QN_OK;
    }

    bool render_access = out->render_node_readable &&
                         out->render_node_writable;

    for (uint32_t i = 0u; i < device_count; ++i) {
        _Alignas(8) QNVkPhysicalDeviceProperties properties;
        memset(&properties, 0, sizeof(properties));
        api.get_physical_device_properties(devices[i], &properties);
        properties.device_name[QN_GPU_DEVICE_NAME_CAP - 1u] = '\0';

        if (qn_gpu_name_is_llvmpipe(properties.device_name) ||
            properties.device_type == QN_VK_PHYSICAL_DEVICE_TYPE_CPU) {
            if (qn_gpu_name_is_llvmpipe(properties.device_name)) {
                out->llvmpipe_seen = true;
            }
            continue;
        }

        uint32_t queue_count = 0u;
        api.get_physical_device_queue_family_properties(
            devices[i],
            &queue_count,
            NULL
        );
        if (queue_count == 0u || queue_count > 256u) {
            continue;
        }

        QNVkQueueFamilyProperties *queues = calloc(
            queue_count,
            sizeof(*queues)
        );
        if (!queues) {
            free(devices);
            api.destroy_instance(instance, NULL);
            qn_gpu_vulkan_unload(&api);
            qn_diag_set_code(diag,
                             "QN-E7004",
                             0,
                             0,
                             "out of memory during queue probe");
            return QN_ERR_RUNTIME;
        }

        api.get_physical_device_queue_family_properties(
            devices[i],
            &queue_count,
            queues
        );

        bool compute_queue = false;
        for (uint32_t q = 0u; q < queue_count; ++q) {
            if (queues[q].queue_count > 0u &&
                (queues[q].queue_flags & QN_VK_QUEUE_COMPUTE_BIT) != 0u) {
                compute_queue = true;
                break;
            }
        }
        free(queues);

        QNGpuDeviceCandidate candidate;
        memset(&candidate, 0, sizeof(candidate));
        qn_gpu_copy_name(candidate.name, properties.device_name);
        candidate.vendor_id = properties.vendor_id;
        candidate.device_id = properties.device_id;
        candidate.device_type = properties.device_type;
        candidate.compute_queue = compute_queue;
        candidate.render_access = render_access;

        if (qn_gpu_candidate_is_supported(&candidate) &&
            qn_gpu_candidate_better(
                &candidate,
                &out->hardware_candidate
            )) {
            out->hardware_candidate = candidate;
            out->hardware_candidate_available = true;
        }
    }

    free(devices);
    api.destroy_instance(instance, NULL);
    qn_gpu_vulkan_unload(&api);
    return QN_OK;
}

QNStatus qn_gpu_decide(QNGpuBackendRequest requested,
                       const QNGpuProbe *probe,
                       QNGpuDecision *out,
                       QNDiagnostic *diag) {
    if (!probe || !out) {
        qn_diag_set_code(diag,
                         "QN-E7001",
                         0,
                         0,
                         "invalid GPU decision arguments");
        return QN_ERR_RUNTIME;
    }

    memset(out, 0, sizeof(*out));
    out->requested = requested;
    out->selected_backend = "cpu";
    out->gpu_execution_enabled = false;
    out->cpu_fallback = true;

    if (requested == QN_GPU_BACKEND_CPU) {
        out->selection_reason = "explicit-cpu";
        return QN_OK;
    }

    if (requested == QN_GPU_BACKEND_AUTO) {
        out->selection_reason = probe->hardware_candidate_available
            ? "gpu-kernel-not-implemented"
            : "hardware-unavailable";
        return QN_OK;
    }

    if (requested == QN_GPU_BACKEND_VULKAN) {
        qn_diag_set_code(
            diag,
            "QN-E7005",
            0,
            0,
            "Vulkan execution is denied in Stage 6 Step 2: "
            "adapter contract only; no GPU kernel is enabled"
        );
        return QN_ERR_RUNTIME;
    }

    qn_diag_set_code(diag,
                     "QN-E7002",
                     0,
                     0,
                     "unknown GPU backend request");
    return QN_ERR_PARSE;
}

static const char *qn_gpu_bool(bool value) {
    return value ? "true" : "false";
}

static const char *qn_gpu_availability(bool value) {
    return value ? "available" : "unavailable";
}

static const char *qn_gpu_render_access(const QNGpuProbe *probe) {
    if (probe->render_node_readable && probe->render_node_writable) {
        return "read-write";
    }
    if (probe->render_node_readable) {
        return "read-only";
    }
    return "unavailable";
}

void qn_gpu_print_contract(const QNGpuProbe *probe,
                           const QNGpuDecision *decision,
                           FILE *stream) {
    if (!probe || !decision || !stream) {
        return;
    }

    fprintf(stream, "QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06\n");
    fprintf(stream, "contract_version=1\n");
    fprintf(stream,
            "requested_backend=%s\n",
            qn_gpu_backend_name(decision->requested));
    fprintf(stream,
            "selected_backend=%s\n",
            decision->selected_backend);
    fprintf(stream,
            "selection_reason=%s\n",
            decision->selection_reason);
    fprintf(stream,
            "vulkan_loader=%s\n",
            qn_gpu_availability(probe->vulkan_loader_available));
    fprintf(stream, "render_node=%s\n", QN_GPU_RENDER_NODE);
    fprintf(stream,
            "render_node_access=%s\n",
            qn_gpu_render_access(probe));
    fprintf(stream,
            "hardware_candidate=%s\n",
            qn_gpu_availability(probe->hardware_candidate_available));
    fprintf(stream,
            "hardware_device=%s\n",
            probe->hardware_candidate_available
                ? probe->hardware_candidate.name
                : "none");
    fprintf(stream,
            "hardware_vendor_id=0x%04x\n",
            probe->hardware_candidate_available
                ? probe->hardware_candidate.vendor_id
                : 0u);
    fprintf(stream,
            "hardware_compute_queue=%s\n",
            qn_gpu_bool(
                probe->hardware_candidate_available &&
                probe->hardware_candidate.compute_queue
            ));
    fprintf(stream,
            "llvmpipe_seen=%s\n",
            qn_gpu_bool(probe->llvmpipe_seen));
    fprintf(stream, "llvmpipe_hardware_gpu=false\n");
    fprintf(stream,
            "gpu_execution_enabled=%s\n",
            qn_gpu_bool(decision->gpu_execution_enabled));
    fprintf(stream,
            "cpu_fallback=%s\n",
            qn_gpu_bool(decision->cpu_fallback));
    fprintf(stream,
            "profile_max_workgroup_invocations=%u\n",
            QN_GPU_PROFILE_MAX_WORKGROUP_INVOCATIONS);
    fprintf(stream,
            "profile_max_compute_shared_memory=%u\n",
            QN_GPU_PROFILE_MAX_COMPUTE_SHARED_MEMORY);
    fprintf(stream,
            "profile_subgroup_size=%u\n",
            QN_GPU_PROFILE_SUBGROUP_SIZE);
    fprintf(stream, "profile_shader_int64=false\n");
    fprintf(stream, "profile_shader_float64=false\n");
}

static void qn_gpu_json_string(FILE *stream, const char *value) {
    fputc('"', stream);
    for (const unsigned char *p =
             (const unsigned char *)(value ? value : "");
         *p;
         ++p) {
        switch (*p) {
            case '"': fputs("\\\"", stream); break;
            case '\\': fputs("\\\\", stream); break;
            case '\n': fputs("\\n", stream); break;
            case '\r': fputs("\\r", stream); break;
            case '\t': fputs("\\t", stream); break;
            default:
                if (*p < 0x20u) {
                    fprintf(stream, "\\u%04x", (unsigned int)*p);
                } else {
                    fputc((int)*p, stream);
                }
                break;
        }
    }
    fputc('"', stream);
}

QNStatus qn_gpu_write_receipt(const char *path,
                              const QNGpuProbe *probe,
                              const QNGpuDecision *decision,
                              QNDiagnostic *diag) {
    if (!path || !probe || !decision) {
        qn_diag_set_code(diag,
                         "QN-E7001",
                         0,
                         0,
                         "invalid GPU receipt arguments");
        return QN_ERR_RUNTIME;
    }

    FILE *stream = fopen(path, "wb");
    if (!stream) {
        qn_diag_set_code(diag,
                         "QN-E7003",
                         0,
                         0,
                         "cannot open GPU receipt: %s",
                         path);
        return QN_ERR_IO;
    }

    fputs("{\n  \"schema\": \"QBIT_NOVA_GPU_ADAPTER_CONTRACT_V06\",\n",
          stream);
    fputs("  \"contract_version\": 1,\n", stream);
    fputs("  \"requested_backend\": ", stream);
    qn_gpu_json_string(stream, qn_gpu_backend_name(decision->requested));
    fputs(",\n  \"selected_backend\": ", stream);
    qn_gpu_json_string(stream, decision->selected_backend);
    fputs(",\n  \"selection_reason\": ", stream);
    qn_gpu_json_string(stream, decision->selection_reason);
    fprintf(stream,
            ",\n  \"vulkan_loader_available\": %s,\n",
            qn_gpu_bool(probe->vulkan_loader_available));
    fputs("  \"render_node\": ", stream);
    qn_gpu_json_string(stream, QN_GPU_RENDER_NODE);
    fputs(",\n  \"render_node_access\": ", stream);
    qn_gpu_json_string(stream, qn_gpu_render_access(probe));
    fprintf(stream,
            ",\n  \"hardware_candidate_available\": %s,\n",
            qn_gpu_bool(probe->hardware_candidate_available));
    fputs("  \"hardware_device\": ", stream);
    qn_gpu_json_string(
        stream,
        probe->hardware_candidate_available
            ? probe->hardware_candidate.name
            : "none"
    );
    fprintf(stream,
            ",\n  \"hardware_vendor_id\": \"0x%04x\",\n",
            probe->hardware_candidate_available
                ? probe->hardware_candidate.vendor_id
                : 0u);
    fprintf(stream,
            "  \"hardware_compute_queue\": %s,\n",
            qn_gpu_bool(
                probe->hardware_candidate_available &&
                probe->hardware_candidate.compute_queue
            ));
    fprintf(stream,
            "  \"llvmpipe_seen\": %s,\n",
            qn_gpu_bool(probe->llvmpipe_seen));
    fputs("  \"llvmpipe_hardware_gpu\": false,\n", stream);
    fprintf(stream,
            "  \"gpu_execution_enabled\": %s,\n",
            qn_gpu_bool(decision->gpu_execution_enabled));
    fprintf(stream,
            "  \"cpu_fallback\": %s,\n",
            qn_gpu_bool(decision->cpu_fallback));
    fprintf(stream,
            "  \"profile_max_workgroup_invocations\": %u,\n",
            QN_GPU_PROFILE_MAX_WORKGROUP_INVOCATIONS);
    fprintf(stream,
            "  \"profile_max_compute_shared_memory\": %u,\n",
            QN_GPU_PROFILE_MAX_COMPUTE_SHARED_MEMORY);
    fprintf(stream,
            "  \"profile_subgroup_size\": %u,\n",
            QN_GPU_PROFILE_SUBGROUP_SIZE);
    fputs("  \"profile_shader_int64\": false,\n", stream);
    fputs("  \"profile_shader_float64\": false\n}\n", stream);

    if (fclose(stream) != 0) {
        qn_diag_set_code(diag,
                         "QN-E7003",
                         0,
                         0,
                         "cannot finalize GPU receipt: %s",
                         path);
        return QN_ERR_IO;
    }

    return QN_OK;
}
