/* Wine Vulkan ICD implementation
 *
 * Copyright 2017 Roderick Colenbrander
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <poll.h>
#include <sys/eventfd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winnt.h"
#include "winioctl.h"
#include "wine/server.h"
#include "wine/list.h"

#include "vulkan_private.h"
#include "wine/vulkan_driver.h"
#include "ntuser.h"

WINE_DEFAULT_DEBUG_CHANNEL(vulkan);


static BOOL is_wow64(void)
{
    return sizeof(void *) == sizeof(UINT64) && NtCurrentTeb()->WowTebOffset;
}

static BOOL use_external_memory(void)
{
    return is_wow64();
}

static ULONG_PTR zero_bits(void)
{
    return is_wow64() ? 0x7fffffff : 0;
}

#define wine_vk_count_struct(s, t) wine_vk_count_struct_((void *)s, VK_STRUCTURE_TYPE_##t)
static uint32_t wine_vk_count_struct_(void *s, VkStructureType t)
{
    const VkBaseInStructure *header;
    uint32_t result = 0;

    for (header = s; header; header = header->pNext)
    {
        if (header->sType == t)
            result++;
    }

    return result;
}

static const struct vulkan_funcs *vk_funcs;

#define WINE_VK_ADD_DISPATCHABLE_MAPPING(instance, client_handle, native_handle, object) \
    wine_vk_add_handle_mapping((instance), (uintptr_t)(client_handle), (uintptr_t)(native_handle), &(object)->mapping)
#define WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(instance, client_handle, native_handle, object) \
    wine_vk_add_handle_mapping((instance), (uintptr_t)(client_handle), (native_handle), &(object)->mapping)
static void  wine_vk_add_handle_mapping(struct wine_instance *instance, uint64_t wrapped_handle,
        uint64_t native_handle, struct wine_vk_mapping *mapping)
{
    if (instance->enable_wrapper_list)
    {
        mapping->native_handle = native_handle;
        mapping->wine_wrapped_handle = wrapped_handle;
        pthread_rwlock_wrlock(&instance->wrapper_lock);
        list_add_tail(&instance->wrappers, &mapping->link);
        pthread_rwlock_unlock(&instance->wrapper_lock);
    }
}

#define WINE_VK_REMOVE_HANDLE_MAPPING(instance, object) \
    wine_vk_remove_handle_mapping((instance), &(object)->mapping)
static void wine_vk_remove_handle_mapping(struct wine_instance *instance, struct wine_vk_mapping *mapping)
{
    if (instance->enable_wrapper_list)
    {
        pthread_rwlock_wrlock(&instance->wrapper_lock);
        list_remove(&mapping->link);
        pthread_rwlock_unlock(&instance->wrapper_lock);
    }
}

static uint64_t wine_vk_get_wrapper(struct wine_instance *instance, uint64_t native_handle)
{
    struct wine_vk_mapping *mapping;
    uint64_t result = 0;

    pthread_rwlock_rdlock(&instance->wrapper_lock);
    LIST_FOR_EACH_ENTRY(mapping, &instance->wrappers, struct wine_vk_mapping, link)
    {
        if (mapping->native_handle == native_handle)
        {
            result = mapping->wine_wrapped_handle;
            break;
        }
    }
    pthread_rwlock_unlock(&instance->wrapper_lock);
    return result;
}

static VkBool32 debug_utils_callback_conversion(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data)
{
    struct wine_vk_debug_utils_params params;
    VkDebugUtilsObjectNameInfoEXT *object_name_infos;
    struct wine_debug_utils_messenger *object;
    void *ret_ptr;
    ULONG ret_len;
    VkBool32 result;
    unsigned int i;

    TRACE("%i, %u, %p, %p\n", severity, message_types, callback_data, user_data);

    object = user_data;

    if (!object->instance->instance)
    {
        /* instance wasn't yet created, this is a message from the native loader */
        return VK_FALSE;
    }

    /* FIXME: we should pack all referenced structs instead of passing pointers */
    params.user_callback = object->user_callback;
    params.user_data = object->user_data;
    params.severity = severity;
    params.message_types = message_types;
    params.data = *((VkDebugUtilsMessengerCallbackDataEXT *) callback_data);

    object_name_infos = calloc(params.data.objectCount, sizeof(*object_name_infos));

    for (i = 0; i < params.data.objectCount; i++)
    {
        object_name_infos[i].sType = callback_data->pObjects[i].sType;
        object_name_infos[i].pNext = callback_data->pObjects[i].pNext;
        object_name_infos[i].objectType = callback_data->pObjects[i].objectType;
        object_name_infos[i].pObjectName = callback_data->pObjects[i].pObjectName;

        if (wine_vk_is_type_wrapped(callback_data->pObjects[i].objectType))
        {
            object_name_infos[i].objectHandle = wine_vk_get_wrapper(object->instance, callback_data->pObjects[i].objectHandle);
            if (!object_name_infos[i].objectHandle)
            {
                WARN("handle conversion failed 0x%s\n", wine_dbgstr_longlong(callback_data->pObjects[i].objectHandle));
                free(object_name_infos);
                return VK_FALSE;
            }
        }
        else
        {
            object_name_infos[i].objectHandle = callback_data->pObjects[i].objectHandle;
        }
    }

    params.data.pObjects = object_name_infos;

    /* applications should always return VK_FALSE */
    result = KeUserModeCallback( NtUserCallVulkanDebugUtilsCallback, &params, sizeof(params),
                                 &ret_ptr, &ret_len );

    free(object_name_infos);

    return result;
}

static VkBool32 debug_report_callback_conversion(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT object_type,
    uint64_t object_handle, size_t location, int32_t code, const char *layer_prefix, const char *message, void *user_data)
{
    struct wine_vk_debug_report_params params;
    struct wine_debug_report_callback *object;
    void *ret_ptr;
    ULONG ret_len;

    TRACE("%#x, %#x, 0x%s, 0x%s, %d, %p, %p, %p\n", flags, object_type, wine_dbgstr_longlong(object_handle),
        wine_dbgstr_longlong(location), code, layer_prefix, message, user_data);

    object = user_data;

    if (!object->instance->instance)
    {
        /* instance wasn't yet created, this is a message from the native loader */
        return VK_FALSE;
    }

    /* FIXME: we should pack all referenced structs instead of passing pointers */
    params.user_callback = object->user_callback;
    params.user_data = object->user_data;
    params.flags = flags;
    params.object_type = object_type;
    params.location = location;
    params.code = code;
    params.layer_prefix = layer_prefix;
    params.message = message;

    params.object_handle = wine_vk_get_wrapper(object->instance, object_handle);
    if (!params.object_handle)
        params.object_type = VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT;

    return KeUserModeCallback( NtUserCallVulkanDebugReportCallback, &params, sizeof(params),
                               &ret_ptr, &ret_len );
}

static void wine_vk_physical_device_free(struct wine_phys_dev *phys_dev)
{
    if (!phys_dev)
        return;

    WINE_VK_REMOVE_HANDLE_MAPPING(phys_dev->instance, phys_dev);
    free(phys_dev->extensions);
    free(phys_dev);
}

static struct wine_phys_dev *wine_vk_physical_device_alloc(struct wine_instance *instance,
        VkPhysicalDevice phys_dev, VkPhysicalDevice handle)
{
    struct wine_phys_dev *object;
    uint32_t num_host_properties, num_properties = 0;
    VkExtensionProperties *host_properties = NULL;
    VkPhysicalDeviceProperties physdev_properties;
    BOOL have_external_memory_host = FALSE;
    VkResult res;
    unsigned int i, j;

    if (!(object = calloc(1, sizeof(*object))))
        return NULL;

    object->instance = instance;
    object->handle = handle;
    object->phys_dev = phys_dev;

    instance->funcs.p_vkGetPhysicalDeviceProperties(phys_dev, &physdev_properties);
    object->api_version = physdev_properties.apiVersion;

    handle->base.unix_handle = (uintptr_t)object;
    WINE_VK_ADD_DISPATCHABLE_MAPPING(instance, handle, phys_dev, object);

    instance->funcs.p_vkGetPhysicalDeviceMemoryProperties(phys_dev, &object->memory_properties);

    res = instance->funcs.p_vkEnumerateDeviceExtensionProperties(phys_dev,
            NULL, &num_host_properties, NULL);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to enumerate device extensions, res=%d\n", res);
        goto err;
    }

    host_properties = calloc(num_host_properties, sizeof(*host_properties));
    if (!host_properties)
    {
        ERR("Failed to allocate memory for device properties!\n");
        goto err;
    }

    res = instance->funcs.p_vkEnumerateDeviceExtensionProperties(phys_dev,
            NULL, &num_host_properties, host_properties);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to enumerate device extensions, res=%d\n", res);
        goto err;
    }

    /* Count list of extensions for which we have an implementation.
     * TODO: perform translation for platform specific extensions.
     */
    for (i = 0; i < num_host_properties; i++)
    {
        if (!strcmp(host_properties[i].extensionName, "VK_KHR_external_memory_fd"))
        {
            TRACE("Substituting VK_KHR_external_memory_fd for VK_KHR_external_memory_win32\n");

            snprintf(host_properties[i].extensionName, sizeof(host_properties[i].extensionName),
                    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
            host_properties[i].specVersion = VK_KHR_EXTERNAL_MEMORY_WIN32_SPEC_VERSION;
        }
        if (!strcmp(host_properties[i].extensionName, "VK_KHR_external_semaphore_fd"))
        {
            TRACE("Substituting VK_KHR_external_semaphore_fd for VK_KHR_external_semaphore_win32\n");

            snprintf(host_properties[i].extensionName, sizeof(host_properties[i].extensionName),
                    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
            host_properties[i].specVersion = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_SPEC_VERSION;
        }

        if (wine_vk_device_extension_supported(host_properties[i].extensionName))
        {
            TRACE("Enabling extension '%s' for physical device %p\n", host_properties[i].extensionName, object);
            num_properties++;
        }
        else
        {
            TRACE("Skipping extension '%s', no implementation found in winevulkan.\n", host_properties[i].extensionName);
        }
        if (!strcmp(host_properties[i].extensionName, "VK_EXT_external_memory_host"))
            have_external_memory_host = TRUE;
    }

    TRACE("Host supported extensions %u, Wine supported extensions %u\n", num_host_properties, num_properties);

    if (!(object->extensions = calloc(num_properties, sizeof(*object->extensions))))
    {
        ERR("Failed to allocate memory for device extensions!\n");
        goto err;
    }

    for (i = 0, j = 0; i < num_host_properties; i++)
    {
        if (wine_vk_device_extension_supported(host_properties[i].extensionName))
        {
            object->extensions[j] = host_properties[i];
            j++;
        }
    }
    object->extension_count = num_properties;

    if (use_external_memory() && have_external_memory_host)
    {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_mem_props =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
        };
        VkPhysicalDeviceProperties2 props =
        {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &host_mem_props,
        };
        instance->funcs.p_vkGetPhysicalDeviceProperties2KHR(phys_dev, &props);
        object->external_memory_align = host_mem_props.minImportedHostPointerAlignment;
        if (object->external_memory_align)
            TRACE("Using VK_EXT_external_memory_host for memory mapping with alignment: %u\n",
                  object->external_memory_align);
    }

    free(host_properties);
    return object;

err:
    wine_vk_physical_device_free(object);
    free(host_properties);
    return NULL;
}

static void wine_vk_free_command_buffers(struct wine_device *device,
        struct wine_cmd_pool *pool, uint32_t count, const VkCommandBuffer *buffers)
{
    unsigned int i;

    for (i = 0; i < count; i++)
    {
        struct wine_cmd_buffer *buffer = wine_cmd_buffer_from_handle(buffers[i]);

        if (!buffer)
            continue;

        device->funcs.p_vkFreeCommandBuffers(device->device, pool->command_pool, 1, &buffer->command_buffer);
        WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, buffer);
        buffer->handle->base.unix_handle = 0;
        free(buffer);
    }
}

static void wine_vk_device_get_queues(struct wine_device *device,
        uint32_t family_index, uint32_t queue_count, VkDeviceQueueCreateFlags flags,
        struct wine_queue *queues, VkQueue *handles)
{
    VkDeviceQueueInfo2 queue_info;
    unsigned int i;

    for (i = 0; i < queue_count; i++)
    {
        struct wine_queue *queue = &queues[i];

        queue->device = device;
        queue->handle = (*handles)++;
        queue->family_index = family_index;
        queue->queue_index = i;
        queue->flags = flags;

        pthread_mutex_init(&queue->submissions_mutex, NULL);
        pthread_cond_init(&queue->submissions_cond, NULL);
        list_init(&queue->submissions);

        pthread_mutex_init(&queue->signaller_mutex, NULL);
        pthread_cond_init(&queue->signaller_cond, NULL);
        list_init(&queue->signal_ops);

        /* The Vulkan spec says:
         *
         * "vkGetDeviceQueue must only be used to get queues that were created
         * with the flags parameter of VkDeviceQueueCreateInfo set to zero."
         */
        if (flags && device->funcs.p_vkGetDeviceQueue2)
        {
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
            queue_info.pNext = NULL;
            queue_info.flags = flags;
            queue_info.queueFamilyIndex = family_index;
            queue_info.queueIndex = i;
            device->funcs.p_vkGetDeviceQueue2(device->device, &queue_info, &queue->queue);
        }
        else
        {
            device->funcs.p_vkGetDeviceQueue(device->device, family_index, i, &queue->queue);
        }

        queue->handle->base.unix_handle = (uintptr_t)queue;
        WINE_VK_ADD_DISPATCHABLE_MAPPING(device->phys_dev->instance, queue->handle, queue->queue, queue);
    }
}

static char **parse_xr_extensions(unsigned int *len)
{
    char *xr_str, *iter, *start, **list;
    unsigned int extension_count = 0, o = 0;

    xr_str = getenv("__WINE_OPENXR_VK_DEVICE_EXTENSIONS");
    if (!xr_str)
    {
        *len = 0;
        return NULL;
    }
    xr_str = strdup(xr_str);

    TRACE("got var: %s\n", xr_str);

    iter = xr_str;
    while(*iter){
        if(*iter++ == ' ')
            extension_count++;
    }
    /* count the one ending in NUL */
    if(iter != xr_str)
        extension_count++;
    if(!extension_count){
        *len = 0;
        return NULL;
    }

    TRACE("counted %u extensions\n", extension_count);

    list = malloc(extension_count * sizeof(char *));

    start = iter = xr_str;
    do{
        if(*iter == ' '){
            *iter = 0;
            list[o++] = strdup(start);
            TRACE("added %s to list\n", list[o-1]);
            iter++;
            start = iter;
        }else if(*iter == 0){
            list[o++] = strdup(start);
            TRACE("added %s to list\n", list[o-1]);
            break;
        }else{
            iter++;
        }
    }while(1);

    free(xr_str);

    *len = extension_count;

    return list;
}

static VkResult wine_vk_device_convert_create_info(struct wine_phys_dev *phys_dev,
        struct conversion_context *ctx, const VkDeviceCreateInfo *src, VkDeviceCreateInfo *dst)
{
    static const char *wine_xr_extension_name = "VK_WINE_openxr_device_extensions";
    unsigned int i, append_xr = 0, replace_win32 = 0, append_timeline = 1;
    VkBaseOutStructure *header;
    char **xr_extensions_list;

    *dst = *src;
    if ((header = (VkBaseOutStructure *)dst->pNext) && header->sType == VK_STRUCTURE_TYPE_CREATE_INFO_WINE_DEVICE_CALLBACK)
        dst->pNext = header->pNext;

    /* Should be filtered out by loader as ICDs don't support layers. */
    dst->enabledLayerCount = 0;
    dst->ppEnabledLayerNames = NULL;

    TRACE("Enabled %u extensions.\n", dst->enabledExtensionCount);
    for (i = 0; i < dst->enabledExtensionCount; i++)
    {
        const char *extension_name = dst->ppEnabledExtensionNames[i];
        TRACE("Extension %u: %s.\n", i, debugstr_a(extension_name));

        if (!strcmp(extension_name, wine_xr_extension_name))
            append_xr = 1;
        else if (!strcmp(src->ppEnabledExtensionNames[i], "VK_KHR_external_memory_win32") || !strcmp(src->ppEnabledExtensionNames[i], "VK_KHR_external_semaphore_win32"))
            replace_win32 = 1;
        else if (!strcmp(extension_name, "VK_KHR_timeline_semaphore"))
            append_timeline = 0;
    }
    if (append_timeline)
         append_timeline = phys_dev->api_version < VK_API_VERSION_1_2 || phys_dev->instance->api_version < VK_API_VERSION_1_2;
    if (append_timeline)
    {
        append_timeline = 0;
        for (i = 0; i < phys_dev->extension_count; ++i)
        {
            if (!strcmp(phys_dev->extensions[i].extensionName, "VK_KHR_timeline_semaphore"))
            {
                append_timeline = 1;
                break;
            }
        }
    }

    if (append_xr)
        xr_extensions_list = parse_xr_extensions(&append_xr);

    if (phys_dev->external_memory_align || append_xr || replace_win32 || append_timeline)
    {
        const char **new_extensions;
        unsigned int o = 0, count;

        count = dst->enabledExtensionCount;
        if (phys_dev->external_memory_align)
            count += 2;
        if (append_xr)
            count += append_xr - 1;
        if (append_timeline)
            ++count;

        new_extensions = conversion_context_alloc(ctx, count * sizeof(*dst->ppEnabledExtensionNames));
        for (i = 0; i < dst->enabledExtensionCount; ++i)
        {
            if (append_xr && !strcmp(src->ppEnabledExtensionNames[i], wine_xr_extension_name))
                continue;
            if (replace_win32 && !strcmp(src->ppEnabledExtensionNames[i], "VK_KHR_external_memory_win32"))
                new_extensions[o++] = "VK_KHR_external_memory_fd";
            else if (replace_win32 && !strcmp(src->ppEnabledExtensionNames[i], "VK_KHR_external_semaphore_win32"))
                new_extensions[o++] = "VK_KHR_external_semaphore_fd";
            else
                new_extensions[o++] = src->ppEnabledExtensionNames[i];
        }
        if (phys_dev->external_memory_align)
        {
            new_extensions[o++] = "VK_KHR_external_memory";
            new_extensions[o++] = "VK_EXT_external_memory_host";
        }
        for (i = 0; i < append_xr; ++i)
        {
            TRACE("\t%s\n", xr_extensions_list[i]);
            new_extensions[o++] = xr_extensions_list[i];
        }
        if (append_timeline)
            new_extensions[o++] = "VK_KHR_timeline_semaphore";
        dst->enabledExtensionCount = count;
        dst->ppEnabledExtensionNames = new_extensions;
    }

    return VK_SUCCESS;
}

static bool is_virtual_queue(struct wine_queue *queue)
{
    return __atomic_load_n(&queue->virtual_queue, __ATOMIC_ACQUIRE);
}

/* Helper function used for freeing a device structure. This function supports full
 * and partial object cleanups and can thus be used for vkCreateDevice failures.
 */
static void wine_vk_device_free(struct wine_device *device)
{
    struct wine_queue *queue;

    if (!device)
        return;

    if (device->queues)
    {
        unsigned int i;
        for (i = 0; i < device->queue_count; i++)
        {
            queue = &device->queues[i];

            if (is_virtual_queue(queue))
            {
                pthread_mutex_lock(&queue->submissions_mutex);
                pthread_mutex_lock(&queue->signaller_mutex);
                queue->stop = 1;
                pthread_mutex_unlock(&queue->submissions_mutex);
                pthread_mutex_unlock(&queue->signaller_mutex);

                pthread_cond_signal(&queue->submissions_cond);
                pthread_cond_signal(&queue->signaller_cond);

                pthread_join(queue->virtual_queue_thread, NULL);
                pthread_join(queue->signal_thread, NULL);
            }

            pthread_mutex_destroy(&queue->submissions_mutex);
            pthread_mutex_destroy(&queue->signaller_mutex);

            pthread_cond_destroy(&queue->submissions_cond);
            pthread_cond_destroy(&queue->signaller_cond);

            if (queue && queue->queue)
                WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, queue);
        }
        free(device->queues);
        device->queues = NULL;
    }

    if (device->device && device->funcs.p_vkDestroyDevice)
    {
        WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, device);
        device->funcs.p_vkDestroyDevice(device->device, NULL /* pAllocator */);
    }

    free(device);
}

NTSTATUS init_vulkan(void *args)
{
    vk_funcs = __wine_get_vulkan_driver(WINE_VULKAN_DRIVER_VERSION);
    if (!vk_funcs)
    {
        ERR("Failed to load Wine graphics driver supporting Vulkan.\n");
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/* Helper function for converting between win32 and host compatible VkInstanceCreateInfo.
 * This function takes care of extensions handled at winevulkan layer, a Wine graphics
 * driver is responsible for handling e.g. surface extensions.
 */
static VkResult wine_vk_instance_convert_create_info(struct conversion_context *ctx,
        const VkInstanceCreateInfo *src, VkInstanceCreateInfo *dst, struct wine_instance *object)
{
    VkDebugUtilsMessengerCreateInfoEXT *debug_utils_messenger;
    VkDebugReportCallbackCreateInfoEXT *debug_report_callback;
    VkBaseInStructure *header;
    unsigned int i;

    *dst = *src;

    object->utils_messenger_count = wine_vk_count_struct(dst, DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    object->utils_messengers =  calloc(object->utils_messenger_count, sizeof(*object->utils_messengers));
    header = (VkBaseInStructure *) dst;
    for (i = 0; i < object->utils_messenger_count; i++)
    {
        header = find_next_struct(header->pNext, VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        debug_utils_messenger = (VkDebugUtilsMessengerCreateInfoEXT *) header;

        object->utils_messengers[i].instance = object;
        object->utils_messengers[i].debug_messenger = VK_NULL_HANDLE;
        object->utils_messengers[i].user_callback = debug_utils_messenger->pfnUserCallback;
        object->utils_messengers[i].user_data = debug_utils_messenger->pUserData;

        /* convert_VkInstanceCreateInfo_* already copied the chain, so we can modify it in-place. */
        debug_utils_messenger->pfnUserCallback = (void *) &debug_utils_callback_conversion;
        debug_utils_messenger->pUserData = &object->utils_messengers[i];
    }

    debug_report_callback = find_next_struct(header->pNext,
                                             VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT);
    if (debug_report_callback)
    {
        object->default_callback.instance = object;
        object->default_callback.debug_callback = VK_NULL_HANDLE;
        object->default_callback.user_callback = debug_report_callback->pfnCallback;
        object->default_callback.user_data = debug_report_callback->pUserData;

        debug_report_callback->pfnCallback = (void *) &debug_report_callback_conversion;
        debug_report_callback->pUserData = &object->default_callback;
    }

    /* ICDs don't support any layers, so nothing to copy. Modern versions of the loader
     * filter this data out as well.
     */
    if (object->quirks & WINEVULKAN_QUIRK_IGNORE_EXPLICIT_LAYERS) {
        dst->enabledLayerCount = 0;
        dst->ppEnabledLayerNames = NULL;
        WARN("Ignoring explicit layers!\n");
    } else if (dst->enabledLayerCount) {
        FIXME("Loading explicit layers is not supported by winevulkan!\n");
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    TRACE("Enabled %u instance extensions.\n", dst->enabledExtensionCount);
    for (i = 0; i < dst->enabledExtensionCount; i++)
    {
        const char *extension_name = dst->ppEnabledExtensionNames[i];
        TRACE("Extension %u: %s.\n", i, debugstr_a(extension_name));
        if (!strcmp(extension_name, "VK_EXT_debug_utils") || !strcmp(extension_name, "VK_EXT_debug_report"))
        {
            object->enable_wrapper_list = VK_TRUE;
        }
    }

    if (use_external_memory())
    {
        const char **new_extensions;

        new_extensions = conversion_context_alloc(ctx, (dst->enabledExtensionCount + 2) *
                                                  sizeof(*dst->ppEnabledExtensionNames));
        memcpy(new_extensions, src->ppEnabledExtensionNames,
               dst->enabledExtensionCount * sizeof(*dst->ppEnabledExtensionNames));
        new_extensions[dst->enabledExtensionCount++] = "VK_KHR_get_physical_device_properties2";
        new_extensions[dst->enabledExtensionCount++] = "VK_KHR_external_memory_capabilities";
        dst->ppEnabledExtensionNames = new_extensions;
    }

    return VK_SUCCESS;
}

/* Helper function which stores wrapped physical devices in the instance object. */
static VkResult wine_vk_instance_load_physical_devices(struct wine_instance *instance)
{
    VkPhysicalDevice *tmp_phys_devs;
    uint32_t phys_dev_count;
    unsigned int i;
    VkResult res;

    res = instance->funcs.p_vkEnumeratePhysicalDevices(instance->instance, &phys_dev_count, NULL);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to enumerate physical devices, res=%d\n", res);
        return res;
    }
    if (!phys_dev_count)
        return res;

    if (phys_dev_count > instance->handle->phys_dev_count)
    {
        instance->handle->phys_dev_count = phys_dev_count;
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    instance->handle->phys_dev_count = phys_dev_count;

    if (!(tmp_phys_devs = calloc(phys_dev_count, sizeof(*tmp_phys_devs))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = instance->funcs.p_vkEnumeratePhysicalDevices(instance->instance, &phys_dev_count, tmp_phys_devs);
    if (res != VK_SUCCESS)
    {
        free(tmp_phys_devs);
        return res;
    }

    instance->phys_devs = calloc(phys_dev_count, sizeof(*instance->phys_devs));
    if (!instance->phys_devs)
    {
        free(tmp_phys_devs);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /* Wrap each native physical device handle into a dispatchable object for the ICD loader. */
    for (i = 0; i < phys_dev_count; i++)
    {
        struct wine_phys_dev *phys_dev = wine_vk_physical_device_alloc(instance, tmp_phys_devs[i],
                                                                       &instance->handle->phys_devs[i]);
        if (!phys_dev)
        {
            ERR("Unable to allocate memory for physical device!\n");
            free(tmp_phys_devs);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        instance->phys_devs[i] = phys_dev;
        instance->phys_dev_count = i + 1;
    }
    instance->phys_dev_count = phys_dev_count;

    free(tmp_phys_devs);
    return VK_SUCCESS;
}

static struct wine_phys_dev *wine_vk_instance_wrap_physical_device(struct wine_instance *instance,
        VkPhysicalDevice physical_device)
{
    unsigned int i;

    for (i = 0; i < instance->phys_dev_count; ++i)
    {
        struct wine_phys_dev *current = instance->phys_devs[i];
        if (current->phys_dev == physical_device)
            return current;
    }

    ERR("Unrecognized physical device %p.\n", physical_device);
    return NULL;
}

/* Helper function used for freeing an instance structure. This function supports full
 * and partial object cleanups and can thus be used for vkCreateInstance failures.
 */
static void wine_vk_instance_free(struct wine_instance *instance)
{
    if (!instance)
        return;

    if (instance->phys_devs)
    {
        unsigned int i;

        for (i = 0; i < instance->phys_dev_count; i++)
        {
            wine_vk_physical_device_free(instance->phys_devs[i]);
        }
        free(instance->phys_devs);
    }

    if (instance->instance)
    {
        vk_funcs->p_vkDestroyInstance(instance->instance, NULL /* allocator */);
        WINE_VK_REMOVE_HANDLE_MAPPING(instance, instance);
    }

    pthread_rwlock_destroy(&instance->wrapper_lock);
    free(instance->utils_messengers);

    free(instance);
}

VkResult wine_vkAllocateCommandBuffers(VkDevice handle, const VkCommandBufferAllocateInfo *allocate_info,
                                       VkCommandBuffer *buffers )
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_cmd_buffer *buffer;
    struct wine_cmd_pool *pool;
    VkResult res = VK_SUCCESS;
    unsigned int i;

    pool = wine_cmd_pool_from_handle(allocate_info->commandPool);

    for (i = 0; i < allocate_info->commandBufferCount; i++)
    {
        VkCommandBufferAllocateInfo allocate_info_host;

        /* TODO: future extensions (none yet) may require pNext conversion. */
        allocate_info_host.pNext = allocate_info->pNext;
        allocate_info_host.sType = allocate_info->sType;
        allocate_info_host.commandPool = pool->command_pool;
        allocate_info_host.level = allocate_info->level;
        allocate_info_host.commandBufferCount = 1;

        TRACE("Allocating command buffer %u from pool 0x%s.\n",
                i, wine_dbgstr_longlong(allocate_info_host.commandPool));

        if (!(buffer = calloc(1, sizeof(*buffer))))
        {
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            break;
        }

        buffer->handle = buffers[i];
        buffer->device = device;
        res = device->funcs.p_vkAllocateCommandBuffers(device->device,
                &allocate_info_host, &buffer->command_buffer);
        buffer->handle->base.unix_handle = (uintptr_t)buffer;
        WINE_VK_ADD_DISPATCHABLE_MAPPING(device->phys_dev->instance, buffer->handle,
                                         buffer->command_buffer, buffer);
        if (res != VK_SUCCESS)
        {
            ERR("Failed to allocate command buffer, res=%d.\n", res);
            buffer->command_buffer = VK_NULL_HANDLE;
            break;
        }
    }

    if (res != VK_SUCCESS)
        wine_vk_free_command_buffers(device, pool, i + 1, buffers);

    return res;
}

VkResult wine_vkCreateDevice(VkPhysicalDevice phys_dev_handle, const VkDeviceCreateInfo *create_info,
                             const VkAllocationCallbacks *allocator, VkDevice *ret_device,
                             void *client_ptr)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);
    PFN_native_vkCreateDevice native_create_device = NULL;
    void *native_create_device_context = NULL;
    VkCreateInfoWineDeviceCallback *callback;
    VkDevice device_handle = client_ptr;
    VkDeviceCreateInfo create_info_host;
    struct VkQueue_T *queue_handles;
    struct wine_queue *next_queue;
    struct conversion_context ctx;
    struct wine_device *object;
    unsigned int i;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (TRACE_ON(vulkan))
    {
        VkPhysicalDeviceProperties properties;

        phys_dev->instance->funcs.p_vkGetPhysicalDeviceProperties(phys_dev->phys_dev, &properties);

        TRACE("Device name: %s.\n", debugstr_a(properties.deviceName));
        TRACE("Vendor ID: %#x, Device ID: %#x.\n", properties.vendorID, properties.deviceID);
        TRACE("Driver version: %#x.\n", properties.driverVersion);
    }

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    object->phys_dev = phys_dev;

    if ((callback = (VkCreateInfoWineDeviceCallback *)create_info->pNext)
            && callback->sType == VK_STRUCTURE_TYPE_CREATE_INFO_WINE_DEVICE_CALLBACK)
    {
        native_create_device = callback->native_create_callback;
        native_create_device_context = callback->context;
    }

    init_conversion_context(&ctx);
    res = wine_vk_device_convert_create_info(phys_dev, &ctx, create_info, &create_info_host);
    if (res == VK_SUCCESS)
    {
        VkPhysicalDeviceFeatures features = {0};
        VkPhysicalDeviceFeatures2 *features2;

        /* Enable shaderStorageImageWriteWithoutFormat for fshack
         * This is available on all hardware and driver combinations we care about.
         */
        if (create_info_host.pEnabledFeatures)
        {
            features = *create_info_host.pEnabledFeatures;
            features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            create_info_host.pEnabledFeatures = &features;
        }
        if ((features2 = wine_vk_find_struct(&create_info_host, PHYSICAL_DEVICE_FEATURES_2)))
        {
            features2->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        }
        else if (!create_info_host.pEnabledFeatures)
        {
            features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            create_info_host.pEnabledFeatures = &features;
        }

        if (native_create_device)
            res = native_create_device(phys_dev->phys_dev,
                    &create_info_host, NULL /* allocator */, &object->device,
                    vk_funcs->p_vkGetInstanceProcAddr, native_create_device_context);
        else
            res = phys_dev->instance->funcs.p_vkCreateDevice(phys_dev->phys_dev,
                    &create_info_host, NULL /* allocator */, &object->device);
    }
    free_conversion_context(&ctx);
    WINE_VK_ADD_DISPATCHABLE_MAPPING(phys_dev->instance, device_handle, object->device, object);
    if (res != VK_SUCCESS)
    {
        WARN("Failed to create device, res=%d.\n", res);
        goto fail;
    }

    /* Just load all function pointers we are aware off. The loader takes care of filtering.
     * We use vkGetDeviceProcAddr as opposed to vkGetInstanceProcAddr for efficiency reasons
     * as functions pass through fewer dispatch tables within the loader.
     */
#define USE_VK_FUNC(name) \
    object->funcs.p_##name = (void *)vk_funcs->p_vkGetDeviceProcAddr(object->device, #name); \
    if (object->funcs.p_##name == NULL) \
        TRACE("Not found '%s'.\n", #name);
    ALL_VK_DEVICE_FUNCS()
#undef USE_VK_FUNC

    /* We need to cache all queues within the device as each requires wrapping since queues are
     * dispatchable objects.
     */
    for (i = 0; i < create_info_host.queueCreateInfoCount; i++)
    {
        object->queue_count += create_info_host.pQueueCreateInfos[i].queueCount;
    }

    if (!(object->queues = calloc(object->queue_count, sizeof(*object->queues))))
    {
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto fail;
    }

    next_queue = object->queues;
    queue_handles = device_handle->queues;
    for (i = 0; i < create_info_host.queueCreateInfoCount; i++)
    {
        uint32_t flags = create_info_host.pQueueCreateInfos[i].flags;
        uint32_t family_index = create_info_host.pQueueCreateInfos[i].queueFamilyIndex;
        uint32_t queue_count = create_info_host.pQueueCreateInfos[i].queueCount;

        TRACE("Queue family index %u, queue count %u.\n", family_index, queue_count);

        wine_vk_device_get_queues(object, family_index, queue_count, flags, next_queue, &queue_handles);
        next_queue += queue_count;
    }

    device_handle->quirks = phys_dev->instance->quirks;
    device_handle->base.unix_handle = (uintptr_t)object;
    *ret_device = device_handle;
    TRACE("Created device %p (native device %p).\n", object, object->device);
    return VK_SUCCESS;

fail:
    wine_vk_device_free(object);
    return res;
}

VkResult wine_vkCreateInstance(const VkInstanceCreateInfo *create_info,
                               const VkAllocationCallbacks *allocator, VkInstance *instance,
                               void *client_ptr)
{
    VkInstance client_instance = client_ptr;
    VkInstanceCreateInfo create_info_host;
    const VkApplicationInfo *app_info;
    struct conversion_context ctx;
    struct wine_instance *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
    {
        ERR("Failed to allocate memory for instance\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    list_init(&object->wrappers);
    pthread_rwlock_init(&object->wrapper_lock, NULL);

    init_conversion_context(&ctx);
    res = wine_vk_instance_convert_create_info(&ctx, create_info, &create_info_host, object);
    if (res == VK_SUCCESS)
        res = vk_funcs->p_vkCreateInstance(&create_info_host, NULL /* allocator */, &object->instance);
    free_conversion_context(&ctx);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to create instance, res=%d\n", res);
        wine_vk_instance_free(object);
        return res;
    }

    object->handle = client_instance;
    WINE_VK_ADD_DISPATCHABLE_MAPPING(object, object->handle, object->instance, object);

    /* Load all instance functions we are aware of. Note the loader takes care
     * of any filtering for extensions which were not requested, but which the
     * ICD may support.
     */
#define USE_VK_FUNC(name) \
    object->funcs.p_##name = (void *)vk_funcs->p_vkGetInstanceProcAddr(object->instance, #name);
    ALL_VK_INSTANCE_FUNCS()
#undef USE_VK_FUNC

    /* Cache physical devices for vkEnumeratePhysicalDevices within the instance as
     * each vkPhysicalDevice is a dispatchable object, which means we need to wrap
     * the native physical devices and present those to the application.
     * Cleanup happens as part of wine_vkDestroyInstance.
     */
    res = wine_vk_instance_load_physical_devices(object);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to load physical devices, res=%d\n", res);
        wine_vk_instance_free(object);
        return res;
    }

    if ((app_info = create_info->pApplicationInfo))
    {
        TRACE("Application name %s, application version %#x.\n",
                debugstr_a(app_info->pApplicationName), app_info->applicationVersion);
        TRACE("Engine name %s, engine version %#x.\n", debugstr_a(app_info->pEngineName),
                app_info->engineVersion);
        TRACE("API version %#x.\n", app_info->apiVersion);

        object->api_version = app_info->apiVersion;

        if (app_info->pEngineName && !strcmp(app_info->pEngineName, "idTech"))
            object->quirks |= WINEVULKAN_QUIRK_GET_DEVICE_PROC_ADDR;
    }

    object->quirks |= WINEVULKAN_QUIRK_ADJUST_MAX_IMAGE_COUNT;

    client_instance->base.unix_handle = (uintptr_t)object;
    *instance = client_instance;
    TRACE("Created instance %p (native instance %p).\n", object, object->instance);
    return VK_SUCCESS;
}

void wine_vkDestroyDevice(VkDevice handle, const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(handle);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    wine_vk_device_free(device);
}

void wine_vkDestroyInstance(VkInstance handle, const VkAllocationCallbacks *allocator)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);

    if (allocator)
        FIXME("Support allocation allocators\n");

    wine_vk_instance_free(instance);
}

VkResult wine_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice phys_dev_handle, const char *layer_name,
                                                   uint32_t *count, VkExtensionProperties *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);

    /* This shouldn't get called with layer_name set, the ICD loader prevents it. */
    if (layer_name)
    {
        ERR("Layer enumeration not supported from ICD.\n");
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!properties)
    {
        *count = phys_dev->extension_count;
        return VK_SUCCESS;
    }

    *count = min(*count, phys_dev->extension_count);
    memcpy(properties, phys_dev->extensions, *count * sizeof(*properties));

    TRACE("Returning %u extensions.\n", *count);
    return *count < phys_dev->extension_count ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult wine_vkEnumerateInstanceExtensionProperties(const char *name, uint32_t *count,
                                                     VkExtensionProperties *properties)
{
    uint32_t num_properties = 0, num_host_properties;
    VkExtensionProperties *host_properties;
    unsigned int i, j;
    VkResult res;

    res = vk_funcs->p_vkEnumerateInstanceExtensionProperties(NULL, &num_host_properties, NULL);
    if (res != VK_SUCCESS)
        return res;

    if (!(host_properties = calloc(num_host_properties, sizeof(*host_properties))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = vk_funcs->p_vkEnumerateInstanceExtensionProperties(NULL, &num_host_properties, host_properties);
    if (res != VK_SUCCESS)
    {
        ERR("Failed to retrieve host properties, res=%d.\n", res);
        free(host_properties);
        return res;
    }

    /* The Wine graphics driver provides us with all extensions supported by the host side
     * including extension fixup (e.g. VK_KHR_xlib_surface -> VK_KHR_win32_surface). It is
     * up to us here to filter the list down to extensions for which we have thunks.
     */
    for (i = 0; i < num_host_properties; i++)
    {
        if (wine_vk_instance_extension_supported(host_properties[i].extensionName))
            num_properties++;
        else
            TRACE("Instance extension '%s' is not supported.\n", host_properties[i].extensionName);
    }

    if (!properties)
    {
        TRACE("Returning %u extensions.\n", num_properties);
        *count = num_properties;
        free(host_properties);
        return VK_SUCCESS;
    }

    for (i = 0, j = 0; i < num_host_properties && j < *count; i++)
    {
        if (wine_vk_instance_extension_supported(host_properties[i].extensionName))
        {
            TRACE("Enabling extension '%s'.\n", host_properties[i].extensionName);
            properties[j++] = host_properties[i];
        }
    }
    *count = min(*count, num_properties);

    free(host_properties);
    return *count < num_properties ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult wine_vkEnumerateDeviceLayerProperties(VkPhysicalDevice phys_dev, uint32_t *count,
                                               VkLayerProperties *properties)
{
    *count = 0;
    return VK_SUCCESS;
}

VkResult wine_vkEnumerateInstanceVersion(uint32_t *version)
{
    VkResult res;

    static VkResult (*p_vkEnumerateInstanceVersion)(uint32_t *version);
    if (!p_vkEnumerateInstanceVersion)
        p_vkEnumerateInstanceVersion = vk_funcs->p_vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");

    if (p_vkEnumerateInstanceVersion)
    {
        res = p_vkEnumerateInstanceVersion(version);
    }
    else
    {
        *version = VK_API_VERSION_1_0;
        res = VK_SUCCESS;
    }

    TRACE("API version %u.%u.%u.\n",
            VK_VERSION_MAJOR(*version), VK_VERSION_MINOR(*version), VK_VERSION_PATCH(*version));
    *version = min(WINE_VK_VERSION, *version);
    return res;
}

VkResult wine_vkEnumeratePhysicalDevices(VkInstance handle, uint32_t *count, VkPhysicalDevice *devices)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    unsigned int i;

    if (!devices)
    {
        *count = instance->phys_dev_count;
        return VK_SUCCESS;
    }

    *count = min(*count, instance->phys_dev_count);
    for (i = 0; i < *count; i++)
    {
        devices[i] = instance->phys_devs[i]->handle;
    }

    TRACE("Returning %u devices.\n", *count);
    return *count < instance->phys_dev_count ? VK_INCOMPLETE : VK_SUCCESS;
}

void wine_vkFreeCommandBuffers(VkDevice handle, VkCommandPool command_pool, uint32_t count,
                               const VkCommandBuffer *buffers)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_cmd_pool *pool = wine_cmd_pool_from_handle(command_pool);

    wine_vk_free_command_buffers(device, pool, count, buffers);
}

static VkQueue wine_vk_device_find_queue(VkDevice handle, const VkDeviceQueueInfo2 *info)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_queue *queue;
    uint32_t i;

    for (i = 0; i < device->queue_count; i++)
    {
        queue = &device->queues[i];
        if (queue->family_index == info->queueFamilyIndex
                && queue->queue_index == info->queueIndex
                && queue->flags == info->flags)
        {
            return queue->handle;
        }
    }

    return VK_NULL_HANDLE;
}

void wine_vkGetDeviceQueue(VkDevice device, uint32_t family_index, uint32_t queue_index, VkQueue *queue)
{
    VkDeviceQueueInfo2 queue_info;

    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
    queue_info.pNext = NULL;
    queue_info.flags = 0;
    queue_info.queueFamilyIndex = family_index;
    queue_info.queueIndex = queue_index;

    *queue = wine_vk_device_find_queue(device, &queue_info);
}

void wine_vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *info, VkQueue *queue)
{
    const VkBaseInStructure *chain;

    if ((chain = info->pNext))
        FIXME("Ignoring a linked structure of type %u.\n", chain->sType);

    *queue = wine_vk_device_find_queue(device, info);
}

VkResult wine_vkCreateCommandPool(VkDevice device_handle, const VkCommandPoolCreateInfo *info,
                                  const VkAllocationCallbacks *allocator, VkCommandPool *command_pool,
                                  void *client_ptr)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct vk_command_pool *handle = client_ptr;
    struct wine_cmd_pool *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = device->funcs.p_vkCreateCommandPool(device->device, info, NULL, &object->command_pool);

    if (res == VK_SUCCESS)
    {
        object->handle = (uintptr_t)handle;
        handle->unix_handle = (uintptr_t)object;
        WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, object->handle,
                                             object->command_pool, object);
        *command_pool = object->handle;
    }
    else
    {
        free(object);
    }

    return res;
}

void wine_vkDestroyCommandPool(VkDevice device_handle, VkCommandPool handle,
                               const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_cmd_pool *pool = wine_cmd_pool_from_handle(handle);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, pool);

    device->funcs.p_vkDestroyCommandPool(device->device, pool->command_pool, NULL);
    free(pool);
}

static VkResult wine_vk_enumerate_physical_device_groups(struct wine_instance *instance,
        VkResult (*p_vkEnumeratePhysicalDeviceGroups)(VkInstance, uint32_t *, VkPhysicalDeviceGroupProperties *),
        uint32_t *count, VkPhysicalDeviceGroupProperties *properties)
{
    unsigned int i, j;
    VkResult res;

    res = p_vkEnumeratePhysicalDeviceGroups(instance->instance, count, properties);
    if (res < 0 || !properties)
        return res;

    for (i = 0; i < *count; ++i)
    {
        VkPhysicalDeviceGroupProperties *current = &properties[i];
        for (j = 0; j < current->physicalDeviceCount; ++j)
        {
            VkPhysicalDevice dev = current->physicalDevices[j];
            struct wine_phys_dev *phys_dev = wine_vk_instance_wrap_physical_device(instance, dev);
            if (!phys_dev)
                return VK_ERROR_INITIALIZATION_FAILED;
            current->physicalDevices[j] = phys_dev->handle;
        }
    }

    return res;
}

VkResult wine_vkEnumeratePhysicalDeviceGroups(VkInstance handle, uint32_t *count,
                                              VkPhysicalDeviceGroupProperties *properties)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);

    return wine_vk_enumerate_physical_device_groups(instance,
            instance->funcs.p_vkEnumeratePhysicalDeviceGroups, count, properties);
}

VkResult wine_vkEnumeratePhysicalDeviceGroupsKHR(VkInstance handle, uint32_t *count,
                                                 VkPhysicalDeviceGroupProperties *properties)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);

    return wine_vk_enumerate_physical_device_groups(instance,
            instance->funcs.p_vkEnumeratePhysicalDeviceGroupsKHR, count, properties);
}

void wine_vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice phys_dev,
                                                     const VkPhysicalDeviceExternalFenceInfo *fence_info,
                                                     VkExternalFenceProperties *properties)
{
    properties->exportFromImportedHandleTypes = 0;
    properties->compatibleHandleTypes = 0;
    properties->externalFenceFeatures = 0;
}

void wine_vkGetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice phys_dev,
                                                        const VkPhysicalDeviceExternalFenceInfo *fence_info,
                                                        VkExternalFenceProperties *properties)
{
    properties->exportFromImportedHandleTypes = 0;
    properties->compatibleHandleTypes = 0;
    properties->externalFenceFeatures = 0;
}

static inline void wine_vk_normalize_handle_types_win(VkExternalMemoryHandleTypeFlags *types)
{
    *types &=
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT;
}

static inline void wine_vk_normalize_handle_types_host(VkExternalMemoryHandleTypeFlags *types)
{
    *types &=
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT |
/*      predicated on VK_KHR_external_memory_dma_buf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT | */
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT;
}

static const VkExternalMemoryHandleTypeFlagBits wine_vk_handle_over_fd_types =
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT |
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT |
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT |
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

static void wine_vk_get_physical_device_external_buffer_properties(struct wine_phys_dev *phys_dev,
        void (*p_vkGetPhysicalDeviceExternalBufferProperties)(VkPhysicalDevice, const VkPhysicalDeviceExternalBufferInfo *, VkExternalBufferProperties *),
        const VkPhysicalDeviceExternalBufferInfo *buffer_info, VkExternalBufferProperties *properties)
{
    VkPhysicalDeviceExternalBufferInfo buffer_info_dup = *buffer_info;

    wine_vk_normalize_handle_types_win(&buffer_info_dup.handleType);
    if (buffer_info_dup.handleType & wine_vk_handle_over_fd_types)
        buffer_info_dup.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    wine_vk_normalize_handle_types_host(&buffer_info_dup.handleType);

    if (buffer_info->handleType && !buffer_info_dup.handleType)
    {
        memset(&properties->externalMemoryProperties, 0, sizeof(properties->externalMemoryProperties));
        return;
    }

    p_vkGetPhysicalDeviceExternalBufferProperties(phys_dev->phys_dev, &buffer_info_dup, properties);

    if (properties->externalMemoryProperties.exportFromImportedHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
        properties->externalMemoryProperties.exportFromImportedHandleTypes |= wine_vk_handle_over_fd_types;
    wine_vk_normalize_handle_types_win(&properties->externalMemoryProperties.exportFromImportedHandleTypes);

    if (properties->externalMemoryProperties.compatibleHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
        properties->externalMemoryProperties.compatibleHandleTypes |= wine_vk_handle_over_fd_types;
    wine_vk_normalize_handle_types_win(&properties->externalMemoryProperties.compatibleHandleTypes);
}

void wine_vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice phys_dev_handle,
                                                      const VkPhysicalDeviceExternalBufferInfo *buffer_info,
                                                      VkExternalBufferProperties *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);
    wine_vk_get_physical_device_external_buffer_properties(phys_dev, phys_dev->instance->funcs.p_vkGetPhysicalDeviceExternalBufferProperties, buffer_info, properties);
}

void wine_vkGetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice phys_dev_handle,
                                                         const VkPhysicalDeviceExternalBufferInfo *buffer_info,
                                                         VkExternalBufferProperties *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);
    wine_vk_get_physical_device_external_buffer_properties(phys_dev, phys_dev->instance->funcs.p_vkGetPhysicalDeviceExternalBufferPropertiesKHR, buffer_info, properties);
}

static VkResult wine_vk_get_physical_device_image_format_properties_2(struct wine_phys_dev *phys_dev,
        VkResult (*p_vkGetPhysicalDeviceImageFormatProperties2)(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2 *, VkImageFormatProperties2 *),
        const VkPhysicalDeviceImageFormatInfo2 *format_info, VkImageFormatProperties2 *properties)
{
    VkPhysicalDeviceExternalImageFormatInfo *external_image_info;
    VkExternalImageFormatProperties *external_image_properties;
    VkResult res;

    if ((external_image_info = find_next_struct(format_info, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO))
            && external_image_info->handleType)
    {
        wine_vk_normalize_handle_types_win(&external_image_info->handleType);

        if (external_image_info->handleType & wine_vk_handle_over_fd_types)
            external_image_info->handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        wine_vk_normalize_handle_types_host(&external_image_info->handleType);
        if (!external_image_info->handleType)
        {
            FIXME("Unsupported handle type %#x.\n", external_image_info->handleType);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
    }

    res = p_vkGetPhysicalDeviceImageFormatProperties2(phys_dev->phys_dev,
            format_info, properties);

    if ((external_image_properties = find_next_struct(properties,
                                                      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES)))
    {
        VkExternalMemoryProperties *p = &external_image_properties->externalMemoryProperties;

        if (p->exportFromImportedHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
            p->exportFromImportedHandleTypes |= wine_vk_handle_over_fd_types;
        wine_vk_normalize_handle_types_win(&p->exportFromImportedHandleTypes);

        if (p->compatibleHandleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
            p->compatibleHandleTypes |= wine_vk_handle_over_fd_types;
        wine_vk_normalize_handle_types_win(&p->compatibleHandleTypes);
    }
    return res;
}

VkResult wine_vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice phys_dev_handle,
                                                        const VkPhysicalDeviceImageFormatInfo2 *format_info,
                                                        VkImageFormatProperties2 *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);

    return wine_vk_get_physical_device_image_format_properties_2(phys_dev,
            phys_dev->instance->funcs.p_vkGetPhysicalDeviceImageFormatProperties2,
            format_info, properties);
}

VkResult wine_vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice phys_dev_handle,
                                                           const VkPhysicalDeviceImageFormatInfo2 *format_info,
                                                           VkImageFormatProperties2 *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);

    return wine_vk_get_physical_device_image_format_properties_2(phys_dev,
            phys_dev->instance->funcs.p_vkGetPhysicalDeviceImageFormatProperties2KHR,
            format_info, properties);
}

/* From ntdll/unix/sync.c */
#define NANOSECONDS_IN_A_SECOND 1000000000
#define TICKSPERSEC             10000000

static inline VkTimeDomainEXT get_performance_counter_time_domain(void)
{
#if !defined(__APPLE__) && defined(HAVE_CLOCK_GETTIME)
# ifdef CLOCK_MONOTONIC_RAW
    return VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT;
# else
    return VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT;
# endif
#else
    FIXME("No mapping for VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT on this platform.\n");
    return VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
#endif
}

static VkTimeDomainEXT map_to_host_time_domain(VkTimeDomainEXT domain)
{
    /* Matches ntdll/unix/sync.c's performance counter implementation. */
    if (domain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT)
        return get_performance_counter_time_domain();

    return domain;
}

static inline uint64_t convert_monotonic_timestamp(uint64_t value)
{
    return value / (NANOSECONDS_IN_A_SECOND / TICKSPERSEC);
}

static inline uint64_t convert_timestamp(VkTimeDomainEXT host_domain, VkTimeDomainEXT target_domain, uint64_t value)
{
    if (host_domain == target_domain)
        return value;

    /* Convert between MONOTONIC time in ns -> QueryPerformanceCounter */
    if ((host_domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT || host_domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT)
            && target_domain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT)
        return convert_monotonic_timestamp(value);

    FIXME("Couldn't translate between host domain %d and target domain %d\n", host_domain, target_domain);
    return value;
}

VkResult wine_vkGetCalibratedTimestampsEXT(VkDevice handle, uint32_t timestamp_count,
                                           const VkCalibratedTimestampInfoEXT *timestamp_infos,
                                           uint64_t *timestamps, uint64_t *max_deviation)
{
    struct wine_device *device = wine_device_from_handle(handle);
    VkCalibratedTimestampInfoEXT* host_timestamp_infos;
    unsigned int i;
    VkResult res;
    TRACE("%p, %u, %p, %p, %p\n", device, timestamp_count, timestamp_infos, timestamps, max_deviation);

    if (!(host_timestamp_infos = malloc(sizeof(VkCalibratedTimestampInfoEXT) * timestamp_count)))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (i = 0; i < timestamp_count; i++)
    {
        host_timestamp_infos[i].sType = timestamp_infos[i].sType;
        host_timestamp_infos[i].pNext = timestamp_infos[i].pNext;
        host_timestamp_infos[i].timeDomain = map_to_host_time_domain(timestamp_infos[i].timeDomain);
    }

    res = device->funcs.p_vkGetCalibratedTimestampsEXT(device->device, timestamp_count, host_timestamp_infos, timestamps, max_deviation);
    if (res != VK_SUCCESS)
        return res;

    for (i = 0; i < timestamp_count; i++)
        timestamps[i] = convert_timestamp(host_timestamp_infos[i].timeDomain, timestamp_infos[i].timeDomain, timestamps[i]);

    free(host_timestamp_infos);

    return res;
}

VkResult wine_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice handle,
                                                             uint32_t *time_domain_count,
                                                             VkTimeDomainEXT *time_domains)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(handle);
    BOOL supports_device = FALSE, supports_monotonic = FALSE, supports_monotonic_raw = FALSE;
    const VkTimeDomainEXT performance_counter_domain = get_performance_counter_time_domain();
    VkTimeDomainEXT *host_time_domains;
    uint32_t host_time_domain_count;
    VkTimeDomainEXT out_time_domains[2];
    uint32_t out_time_domain_count;
    unsigned int i;
    VkResult res;

    /* Find out the time domains supported on the host */
    res = phys_dev->instance->funcs.p_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(phys_dev->phys_dev, &host_time_domain_count, NULL);
    if (res != VK_SUCCESS)
        return res;

    if (!(host_time_domains = malloc(sizeof(VkTimeDomainEXT) * host_time_domain_count)))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = phys_dev->instance->funcs.p_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(phys_dev->phys_dev, &host_time_domain_count, host_time_domains);
    if (res != VK_SUCCESS)
    {
        free(host_time_domains);
        return res;
    }

    for (i = 0; i < host_time_domain_count; i++)
    {
        if (host_time_domains[i] == VK_TIME_DOMAIN_DEVICE_EXT)
            supports_device = TRUE;
        else if (host_time_domains[i] == VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT)
            supports_monotonic = TRUE;
        else if (host_time_domains[i] == VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT)
            supports_monotonic_raw = TRUE;
        else
            FIXME("Unknown time domain %d\n", host_time_domains[i]);
    }

    free(host_time_domains);

    out_time_domain_count = 0;

    /* Map our monotonic times -> QPC */
    if (supports_monotonic_raw && performance_counter_domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT)
        out_time_domains[out_time_domain_count++] = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    else if (supports_monotonic && performance_counter_domain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT)
        out_time_domains[out_time_domain_count++] = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    else
        FIXME("VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT not supported on this platform.\n");

    /* Forward the device domain time */
    if (supports_device)
        out_time_domains[out_time_domain_count++] = VK_TIME_DOMAIN_DEVICE_EXT;

    /* Send the count/domains back to the app */
    if (!time_domains)
    {
        *time_domain_count = out_time_domain_count;
        return VK_SUCCESS;
    }

    for (i = 0; i < min(*time_domain_count, out_time_domain_count); i++)
        time_domains[i] = out_time_domains[i];

    res = *time_domain_count < out_time_domain_count ? VK_INCOMPLETE : VK_SUCCESS;
    *time_domain_count = out_time_domain_count;
    return res;
}

static inline void wine_vk_normalize_semaphore_handle_types_win(VkExternalSemaphoreHandleTypeFlags *types)
{
    *types &=
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT |
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT |
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
}

static inline void wine_vk_normalize_semaphore_handle_types_host(VkExternalSemaphoreHandleTypeFlags *types)
{
    *types &=
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT |
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
}

static void wine_vk_get_physical_device_external_semaphore_properties(struct wine_phys_dev *phys_dev,
    void (*p_vkGetPhysicalDeviceExternalSemaphoreProperties)(VkPhysicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo *, VkExternalSemaphoreProperties *),
    const VkPhysicalDeviceExternalSemaphoreInfo *semaphore_info, VkExternalSemaphoreProperties *properties)
{
    VkPhysicalDeviceExternalSemaphoreInfo semaphore_info_dup = *semaphore_info;
    VkSemaphoreTypeCreateInfo semaphore_type_info, *p_semaphore_type_info;

    switch(semaphore_info->handleType)
    {
        case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT:
            semaphore_info_dup.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            break;
        case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT:
        {
            unsigned int i;

            if (phys_dev->api_version < VK_API_VERSION_1_2 ||
                phys_dev->instance->api_version < VK_API_VERSION_1_2)
            {
                for (i = 0; i < phys_dev->extension_count; i++)
                {
                    if (!strcmp(phys_dev->extensions[i].extensionName, "VK_KHR_timeline_semaphore"))
                        break;
                }
                if (i == phys_dev->extension_count)
                {
                    properties->exportFromImportedHandleTypes = 0;
                    properties->compatibleHandleTypes = 0;
                    properties->externalSemaphoreFeatures = 0;
                    return;
                }
            }

            if ((p_semaphore_type_info = wine_vk_find_struct(&semaphore_info_dup, SEMAPHORE_TYPE_CREATE_INFO)))
            {
                p_semaphore_type_info->semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                p_semaphore_type_info->initialValue = 0;
            }
            else
            {
                semaphore_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
                semaphore_type_info.pNext = semaphore_info_dup.pNext;
                semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                semaphore_type_info.initialValue = 0;

                semaphore_info_dup.pNext = &semaphore_type_info;
            }

            semaphore_info_dup.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            break;
        }
        default:
            semaphore_info_dup.handleType = 0;
            break;
    }

    if (semaphore_info->handleType && !semaphore_info_dup.handleType)
    {
        properties->exportFromImportedHandleTypes = 0;
        properties->compatibleHandleTypes = 0;
        properties->externalSemaphoreFeatures = 0;
        return;
    }

    p_vkGetPhysicalDeviceExternalSemaphoreProperties(phys_dev->phys_dev, &semaphore_info_dup, properties);

    if (properties->exportFromImportedHandleTypes & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT)
        properties->exportFromImportedHandleTypes = semaphore_info->handleType;
    wine_vk_normalize_semaphore_handle_types_win(&properties->exportFromImportedHandleTypes);

    if (properties->compatibleHandleTypes & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT)
        properties->compatibleHandleTypes = semaphore_info->handleType;
    wine_vk_normalize_semaphore_handle_types_win(&properties->compatibleHandleTypes);
}

void wine_vkGetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice phys_dev_handle,
                                                         const VkPhysicalDeviceExternalSemaphoreInfo *info,
                                                         VkExternalSemaphoreProperties *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);

    TRACE("%p, %p, %p\n", phys_dev, info, properties);
    wine_vk_get_physical_device_external_semaphore_properties(phys_dev, phys_dev->instance->funcs.p_vkGetPhysicalDeviceExternalSemaphoreProperties, info, properties);
}

void wine_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice phys_dev_handle,
                                                            const VkPhysicalDeviceExternalSemaphoreInfo *info,
                                                            VkExternalSemaphoreProperties *properties)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(phys_dev_handle);

    TRACE("%p, %p, %p\n", phys_dev, info, properties);
    wine_vk_get_physical_device_external_semaphore_properties(phys_dev, phys_dev->instance->funcs.p_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR, info, properties);
}

/*
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform writeonly image2D outImage;
layout(push_constant) uniform pushConstants {
    //both in real image coords
    vec2 offset;
    vec2 extents;
} constants;

void main()
{
    vec2 texcoord = (vec2(gl_GlobalInvocationID.xy) - constants.offset) / constants.extents;
    vec4 c = texture(texSampler, texcoord);

    // Convert linear -> srgb
    bvec3 isLo = lessThanEqual(c.rgb, vec3(0.0031308f));
    vec3 loPart = c.rgb * 12.92f;
    vec3 hiPart = pow(c.rgb, vec3(5.0f / 12.0f)) * 1.055f - 0.055f;
    c.rgb = mix(hiPart, loPart, isLo);

    imageStore(outImage, ivec2(gl_GlobalInvocationID.xy), c);
}

*/
const uint32_t blit_comp_spv[] = {
    0x07230203,0x00010000,0x0008000a,0x0000005e,0x00000000,0x00020011,0x00000001,0x00020011,
    0x00000038,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,
    0x00000000,0x00000001,0x0006000f,0x00000005,0x00000004,0x6e69616d,0x00000000,0x0000000d,
    0x00060010,0x00000004,0x00000011,0x00000008,0x00000008,0x00000001,0x00030003,0x00000002,
    0x000001cc,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00050005,0x00000009,0x63786574,
    0x64726f6f,0x00000000,0x00080005,0x0000000d,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,
    0x496e6f69,0x00000044,0x00060005,0x00000012,0x68737570,0x736e6f43,0x746e6174,0x00000073,
    0x00050006,0x00000012,0x00000000,0x7366666f,0x00007465,0x00050006,0x00000012,0x00000001,
    0x65747865,0x0073746e,0x00050005,0x00000014,0x736e6f63,0x746e6174,0x00000073,0x00030005,
    0x00000021,0x00000063,0x00050005,0x00000025,0x53786574,0x6c706d61,0x00007265,0x00040005,
    0x0000002d,0x6f4c7369,0x00000000,0x00040005,0x00000035,0x61506f6c,0x00007472,0x00040005,
    0x0000003a,0x61506968,0x00007472,0x00050005,0x00000055,0x4974756f,0x6567616d,0x00000000,
    0x00040047,0x0000000d,0x0000000b,0x0000001c,0x00050048,0x00000012,0x00000000,0x00000023,
    0x00000000,0x00050048,0x00000012,0x00000001,0x00000023,0x00000008,0x00030047,0x00000012,
    0x00000002,0x00040047,0x00000025,0x00000022,0x00000000,0x00040047,0x00000025,0x00000021,
    0x00000000,0x00040047,0x00000055,0x00000022,0x00000000,0x00040047,0x00000055,0x00000021,
    0x00000001,0x00030047,0x00000055,0x00000019,0x00040047,0x0000005d,0x0000000b,0x00000019,
    0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,
    0x00040017,0x00000007,0x00000006,0x00000002,0x00040020,0x00000008,0x00000007,0x00000007,
    0x00040015,0x0000000a,0x00000020,0x00000000,0x00040017,0x0000000b,0x0000000a,0x00000003,
    0x00040020,0x0000000c,0x00000001,0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,
    0x00040017,0x0000000e,0x0000000a,0x00000002,0x0004001e,0x00000012,0x00000007,0x00000007,
    0x00040020,0x00000013,0x00000009,0x00000012,0x0004003b,0x00000013,0x00000014,0x00000009,
    0x00040015,0x00000015,0x00000020,0x00000001,0x0004002b,0x00000015,0x00000016,0x00000000,
    0x00040020,0x00000017,0x00000009,0x00000007,0x0004002b,0x00000015,0x0000001b,0x00000001,
    0x00040017,0x0000001f,0x00000006,0x00000004,0x00040020,0x00000020,0x00000007,0x0000001f,
    0x00090019,0x00000022,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,0x00000001,
    0x00000000,0x0003001b,0x00000023,0x00000022,0x00040020,0x00000024,0x00000000,0x00000023,
    0x0004003b,0x00000024,0x00000025,0x00000000,0x0004002b,0x00000006,0x00000028,0x00000000,
    0x00020014,0x0000002a,0x00040017,0x0000002b,0x0000002a,0x00000003,0x00040020,0x0000002c,
    0x00000007,0x0000002b,0x00040017,0x0000002e,0x00000006,0x00000003,0x0004002b,0x00000006,
    0x00000031,0x3b4d2e1c,0x0006002c,0x0000002e,0x00000032,0x00000031,0x00000031,0x00000031,
    0x00040020,0x00000034,0x00000007,0x0000002e,0x0004002b,0x00000006,0x00000038,0x414eb852,
    0x0004002b,0x00000006,0x0000003d,0x3ed55555,0x0006002c,0x0000002e,0x0000003e,0x0000003d,
    0x0000003d,0x0000003d,0x0004002b,0x00000006,0x00000040,0x3f870a3d,0x0004002b,0x00000006,
    0x00000042,0x3d6147ae,0x0004002b,0x0000000a,0x00000049,0x00000000,0x00040020,0x0000004a,
    0x00000007,0x00000006,0x0004002b,0x0000000a,0x0000004d,0x00000001,0x0004002b,0x0000000a,
    0x00000050,0x00000002,0x00090019,0x00000053,0x00000006,0x00000001,0x00000000,0x00000000,
    0x00000000,0x00000002,0x00000000,0x00040020,0x00000054,0x00000000,0x00000053,0x0004003b,
    0x00000054,0x00000055,0x00000000,0x00040017,0x00000059,0x00000015,0x00000002,0x0004002b,
    0x0000000a,0x0000005c,0x00000008,0x0006002c,0x0000000b,0x0000005d,0x0000005c,0x0000005c,
    0x0000004d,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
    0x0004003b,0x00000008,0x00000009,0x00000007,0x0004003b,0x00000020,0x00000021,0x00000007,
    0x0004003b,0x0000002c,0x0000002d,0x00000007,0x0004003b,0x00000034,0x00000035,0x00000007,
    0x0004003b,0x00000034,0x0000003a,0x00000007,0x0004003d,0x0000000b,0x0000000f,0x0000000d,
    0x0007004f,0x0000000e,0x00000010,0x0000000f,0x0000000f,0x00000000,0x00000001,0x00040070,
    0x00000007,0x00000011,0x00000010,0x00050041,0x00000017,0x00000018,0x00000014,0x00000016,
    0x0004003d,0x00000007,0x00000019,0x00000018,0x00050083,0x00000007,0x0000001a,0x00000011,
    0x00000019,0x00050041,0x00000017,0x0000001c,0x00000014,0x0000001b,0x0004003d,0x00000007,
    0x0000001d,0x0000001c,0x00050088,0x00000007,0x0000001e,0x0000001a,0x0000001d,0x0003003e,
    0x00000009,0x0000001e,0x0004003d,0x00000023,0x00000026,0x00000025,0x0004003d,0x00000007,
    0x00000027,0x00000009,0x00070058,0x0000001f,0x00000029,0x00000026,0x00000027,0x00000002,
    0x00000028,0x0003003e,0x00000021,0x00000029,0x0004003d,0x0000001f,0x0000002f,0x00000021,
    0x0008004f,0x0000002e,0x00000030,0x0000002f,0x0000002f,0x00000000,0x00000001,0x00000002,
    0x000500bc,0x0000002b,0x00000033,0x00000030,0x00000032,0x0003003e,0x0000002d,0x00000033,
    0x0004003d,0x0000001f,0x00000036,0x00000021,0x0008004f,0x0000002e,0x00000037,0x00000036,
    0x00000036,0x00000000,0x00000001,0x00000002,0x0005008e,0x0000002e,0x00000039,0x00000037,
    0x00000038,0x0003003e,0x00000035,0x00000039,0x0004003d,0x0000001f,0x0000003b,0x00000021,
    0x0008004f,0x0000002e,0x0000003c,0x0000003b,0x0000003b,0x00000000,0x00000001,0x00000002,
    0x0007000c,0x0000002e,0x0000003f,0x00000001,0x0000001a,0x0000003c,0x0000003e,0x0005008e,
    0x0000002e,0x00000041,0x0000003f,0x00000040,0x00060050,0x0000002e,0x00000043,0x00000042,
    0x00000042,0x00000042,0x00050083,0x0000002e,0x00000044,0x00000041,0x00000043,0x0003003e,
    0x0000003a,0x00000044,0x0004003d,0x0000002e,0x00000045,0x0000003a,0x0004003d,0x0000002e,
    0x00000046,0x00000035,0x0004003d,0x0000002b,0x00000047,0x0000002d,0x000600a9,0x0000002e,
    0x00000048,0x00000047,0x00000046,0x00000045,0x00050041,0x0000004a,0x0000004b,0x00000021,
    0x00000049,0x00050051,0x00000006,0x0000004c,0x00000048,0x00000000,0x0003003e,0x0000004b,
    0x0000004c,0x00050041,0x0000004a,0x0000004e,0x00000021,0x0000004d,0x00050051,0x00000006,
    0x0000004f,0x00000048,0x00000001,0x0003003e,0x0000004e,0x0000004f,0x00050041,0x0000004a,
    0x00000051,0x00000021,0x00000050,0x00050051,0x00000006,0x00000052,0x00000048,0x00000002,
    0x0003003e,0x00000051,0x00000052,0x0004003d,0x00000053,0x00000056,0x00000055,0x0004003d,
    0x0000000b,0x00000057,0x0000000d,0x0007004f,0x0000000e,0x00000058,0x00000057,0x00000057,
    0x00000000,0x00000001,0x0004007c,0x00000059,0x0000005a,0x00000058,0x0004003d,0x0000001f,
    0x0000005b,0x00000021,0x00040063,0x00000056,0x0000005a,0x0000005b,0x000100fd,0x00010038
};

static VkResult create_pipeline(struct wine_device *device, struct wine_swapchain *swapchain, VkShaderModule shaderModule)
{
    VkComputePipelineCreateInfo pipelineInfo = {0};
    VkResult res;

    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = swapchain->pipeline_layout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    res = device->funcs.p_vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                   NULL, &swapchain->pipeline);
    if (res != VK_SUCCESS)
    {
        ERR("vkCreateComputePipelines: %d\n", res);
        return res;
    }

    return VK_SUCCESS;
}

static VkResult create_descriptor_set(struct wine_device *device, struct wine_swapchain *swapchain,
                                      struct fs_hack_image *hack)
{
    VkDescriptorImageInfo userDescriptorImageInfo = {0}, realDescriptorImageInfo = {0};
    VkDescriptorSetAllocateInfo descriptorAllocInfo = {0};
    VkWriteDescriptorSet descriptorWrites[2] = {{0}, {0}};
    VkResult res;

    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = swapchain->descriptor_pool;
    descriptorAllocInfo.descriptorSetCount = 1;
    descriptorAllocInfo.pSetLayouts = &swapchain->descriptor_set_layout;

    res = device->funcs.p_vkAllocateDescriptorSets(device->device, &descriptorAllocInfo, &hack->descriptor_set);
    if (res != VK_SUCCESS)
    {
        ERR("vkAllocateDescriptorSets: %d\n", res);
        return res;
    }

    userDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    userDescriptorImageInfo.imageView = hack->user_view;
    userDescriptorImageInfo.sampler = swapchain->sampler;

    realDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    realDescriptorImageInfo.imageView = hack->blit_view;

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = hack->descriptor_set;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &userDescriptorImageInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = hack->descriptor_set;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &realDescriptorImageInfo;

    device->funcs.p_vkUpdateDescriptorSets(device->device, 2, descriptorWrites, 0, NULL);

    return VK_SUCCESS;
}

static VkResult init_blit_images(struct wine_device *device, struct wine_swapchain *swapchain)
{
    VkResult res;
    VkSamplerCreateInfo samplerInfo = {0};
    VkDescriptorPoolSize poolSizes[2] = {{0}, {0}};
    VkDescriptorPoolCreateInfo poolInfo = {0};
    VkDescriptorSetLayoutBinding layoutBindings[2] = {{0}, {0}};
    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = {0};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
    VkPushConstantRange pushConstants;
    VkShaderModuleCreateInfo shaderInfo = {0};
    VkShaderModule shaderModule = 0;
    VkImageViewCreateInfo viewInfo = {0};
    uint32_t i;

    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = swapchain->fs_hack_filter;
    samplerInfo.minFilter = swapchain->fs_hack_filter;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    res = device->funcs.p_vkCreateSampler(device->device, &samplerInfo, NULL, &swapchain->sampler);
    if (res != VK_SUCCESS)
    {
        WARN("vkCreateSampler failed, res=%d\n", res);
        return res;
    }

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = swapchain->n_images;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = swapchain->n_images;

    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = swapchain->n_images;

    res = device->funcs.p_vkCreateDescriptorPool(device->device, &poolInfo, NULL, &swapchain->descriptor_pool);
    if (res != VK_SUCCESS)
    {
        ERR("vkCreateDescriptorPool: %d\n", res);
        goto fail;
    }

    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[0].pImmutableSamplers = NULL;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layoutBindings[1].pImmutableSamplers = NULL;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 2;
    descriptorLayoutInfo.pBindings = layoutBindings;

    res = device->funcs.p_vkCreateDescriptorSetLayout(device->device, &descriptorLayoutInfo, NULL,
                                                      &swapchain->descriptor_set_layout);
    if (res != VK_SUCCESS)
    {
        ERR("vkCreateDescriptorSetLayout: %d\n", res);
        goto fail;
    }

    pushConstants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstants.offset = 0;
    pushConstants.size = 4 * sizeof(float); /* 2 * vec2 */

    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &swapchain->descriptor_set_layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstants;

    res = device->funcs.p_vkCreatePipelineLayout(device->device, &pipelineLayoutInfo, NULL,
                                                 &swapchain->pipeline_layout);
    if (res != VK_SUCCESS)
    {
        ERR("vkCreatePipelineLayout: %d\n", res);
        goto fail;
    }

    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = sizeof(blit_comp_spv);
    shaderInfo.pCode = blit_comp_spv;

    res = device->funcs.p_vkCreateShaderModule(device->device, &shaderInfo, NULL, &shaderModule);
    if (res != VK_SUCCESS)
    {
        ERR("vkCreateShaderModule: %d\n", res);
        goto fail;
    }

    res = create_pipeline(device, swapchain, shaderModule);
    if (res != VK_SUCCESS)
        goto fail;

    device->funcs.p_vkDestroyShaderModule(device->device, shaderModule, NULL);

    /* create imageviews */
    for (i = 0; i < swapchain->n_images; ++i)
    {
        struct fs_hack_image *hack = &swapchain->fs_hack_images[i];

        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = hack->swapchain_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        res = device->funcs.p_vkCreateImageView(device->device, &viewInfo, NULL, &hack->blit_view);
        if (res != VK_SUCCESS)
        {
            ERR("vkCreateImageView(blit): %d\n", res);
            goto fail;
        }

        res = create_descriptor_set(device, swapchain, hack);
        if (res != VK_SUCCESS)
            goto fail;
    }

    return VK_SUCCESS;

fail:
    for (i = 0; i < swapchain->n_images; ++i)
    {
        struct fs_hack_image *hack = &swapchain->fs_hack_images[i];

        device->funcs.p_vkDestroyImageView(device->device, hack->blit_view, NULL);
        hack->blit_view = VK_NULL_HANDLE;
    }

    device->funcs.p_vkDestroyShaderModule(device->device, shaderModule, NULL);

    device->funcs.p_vkDestroyPipeline(device->device, swapchain->pipeline, NULL);
    swapchain->pipeline = VK_NULL_HANDLE;

    device->funcs.p_vkDestroyPipelineLayout(device->device, swapchain->pipeline_layout, NULL);
    swapchain->pipeline_layout = VK_NULL_HANDLE;

    device->funcs.p_vkDestroyDescriptorSetLayout(device->device, swapchain->descriptor_set_layout, NULL);
    swapchain->descriptor_set_layout = VK_NULL_HANDLE;

    device->funcs.p_vkDestroyDescriptorPool(device->device, swapchain->descriptor_pool, NULL);
    swapchain->descriptor_pool = VK_NULL_HANDLE;

    device->funcs.p_vkDestroySampler(device->device, swapchain->sampler, NULL);
    swapchain->sampler = VK_NULL_HANDLE;

    return res;
}

static void destroy_fs_hack_image(struct wine_device *device, struct wine_swapchain *swapchain,
                                  struct fs_hack_image *hack)
{
    device->funcs.p_vkDestroyImageView(device->device, hack->user_view, NULL);
    device->funcs.p_vkDestroyImageView(device->device, hack->blit_view, NULL);
    device->funcs.p_vkDestroyImage(device->device, hack->user_image, NULL);
    if (hack->cmd)
        device->funcs.p_vkFreeCommandBuffers(device->device, swapchain->cmd_pools[hack->cmd_queue_idx],
                                             1, &hack->cmd);
    device->funcs.p_vkDestroySemaphore(device->device, hack->blit_finished, NULL);
}

static VkResult init_fs_hack_images(struct wine_device *device, struct wine_swapchain *swapchain,
                                    const VkSwapchainCreateInfoKHR *createinfo)
{
    VkResult res;
    VkImage *real_images = NULL;
    VkDeviceSize userMemTotal = 0, offs;
    VkImageCreateInfo imageInfo = {0};
    VkSemaphoreCreateInfo semaphoreInfo = {0};
    VkMemoryRequirements userMemReq;
    VkMemoryAllocateInfo allocInfo = {0};
    VkPhysicalDeviceMemoryProperties memProperties;
    VkImageViewCreateInfo viewInfo = {0};
    uint32_t count, i = 0, user_memory_type = -1;

    res = device->funcs.p_vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &count, NULL);
    if (res != VK_SUCCESS)
    {
        WARN("vkGetSwapchainImagesKHR failed, res=%d\n", res);
        return res;
    }

    real_images = malloc(count * sizeof(VkImage));
    swapchain->cmd_pools = calloc(device->queue_count, sizeof(VkCommandPool));
    swapchain->fs_hack_images = calloc(count, sizeof(struct fs_hack_image));
    if (!real_images || !swapchain->cmd_pools || !swapchain->fs_hack_images)
        goto fail;

    res = device->funcs.p_vkGetSwapchainImagesKHR(device->device, swapchain->swapchain, &count, real_images);
    if (res != VK_SUCCESS)
    {
        WARN("vkGetSwapchainImagesKHR failed, res=%d\n", res);
        goto fail;
    }

    /* create user images */
    for (i = 0; i < count; ++i)
    {
        struct fs_hack_image *hack = &swapchain->fs_hack_images[i];

        hack->swapchain_image = real_images[i];

        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        res = device->funcs.p_vkCreateSemaphore(device->device, &semaphoreInfo, NULL, &hack->blit_finished);
        if (res != VK_SUCCESS)
        {
            WARN("vkCreateSemaphore failed, res=%d\n", res);
            goto fail;
        }

        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = swapchain->user_extent.width;
        imageInfo.extent.height = swapchain->user_extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = createinfo->imageArrayLayers;
        imageInfo.format = createinfo->imageFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = createinfo->imageUsage | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = createinfo->imageSharingMode;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.queueFamilyIndexCount = createinfo->queueFamilyIndexCount;
        imageInfo.pQueueFamilyIndices = createinfo->pQueueFamilyIndices;

        if (createinfo->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
            imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        else if (createinfo->imageFormat != VK_FORMAT_B8G8R8A8_SRGB)
            imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

        res = device->funcs.p_vkCreateImage(device->device, &imageInfo, NULL, &hack->user_image);
        if (res != VK_SUCCESS)
        {
            ERR("vkCreateImage failed: %d\n", res);
            goto fail;
        }

        device->funcs.p_vkGetImageMemoryRequirements(device->device, hack->user_image, &userMemReq);

        offs = userMemTotal % userMemReq.alignment;
        if (offs)
            userMemTotal += userMemReq.alignment - offs;

        userMemTotal += userMemReq.size;

        swapchain->n_images++;
    }

    /* allocate backing memory */
    device->phys_dev->instance->funcs.p_vkGetPhysicalDeviceMemoryProperties(device->phys_dev->phys_dev, &memProperties);

    for (i = 0; i < memProperties.memoryTypeCount; i++)
    {
        UINT flag = memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (flag == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        {
            if (userMemReq.memoryTypeBits & (1 << i))
            {
                user_memory_type = i;
                break;
            }
        }
    }

    if (user_memory_type == -1)
    {
        ERR("unable to find suitable memory type\n");
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto fail;
    }

    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = userMemTotal;
    allocInfo.memoryTypeIndex = user_memory_type;

    res = device->funcs.p_vkAllocateMemory(device->device, &allocInfo, NULL, &swapchain->user_image_memory);
    if (res != VK_SUCCESS)
    {
        ERR("vkAllocateMemory: %d\n", res);
        goto fail;
    }

    /* bind backing memory and create imageviews */
    userMemTotal = 0;
    for (i = 0; i < count; ++i)
    {
        device->funcs.p_vkGetImageMemoryRequirements(device->device, swapchain->fs_hack_images[i].user_image, &userMemReq);

        offs = userMemTotal % userMemReq.alignment;
        if (offs)
            userMemTotal += userMemReq.alignment - offs;

        res = device->funcs.p_vkBindImageMemory(device->device, swapchain->fs_hack_images[i].user_image,
                                                swapchain->user_image_memory, userMemTotal);
        if (res != VK_SUCCESS)
        {
            ERR("vkBindImageMemory: %d\n", res);
            goto fail;
        }

        userMemTotal += userMemReq.size;

        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchain->fs_hack_images[i].user_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        res = device->funcs.p_vkCreateImageView(device->device, &viewInfo, NULL,
                                                &swapchain->fs_hack_images[i].user_view);
        if (res != VK_SUCCESS)
        {
            ERR("vkCreateImageView(user): %d\n", res);
            goto fail;
        }
    }

    free(real_images);

    return VK_SUCCESS;

fail:
    for (i = 0; i < swapchain->n_images; ++i) destroy_fs_hack_image(device, swapchain, &swapchain->fs_hack_images[i]);
    free(real_images);
    free(swapchain->cmd_pools);
    free(swapchain->fs_hack_images);
    return res;
}

VkResult wine_vkCreateSwapchainKHR(VkDevice device_handle, const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator,
                                   VkSwapchainKHR *swapchain, void *client_ptr)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    VkSwapchainCreateInfoKHR create_info_host = *info;
    struct vk_swapchain *handle = client_ptr;
    struct wine_swapchain *object;
    VkExtent2D user_sz;
    VkResult res;

    if (!(object = calloc(1, sizeof(*object))))
    {
        ERR("Failed to allocate memory for swapchain\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (create_info_host.surface)
        create_info_host.surface = wine_surface_from_handle(create_info_host.surface)->driver_surface;
    if (create_info_host.oldSwapchain)
        create_info_host.oldSwapchain = wine_swapchain_from_handle(create_info_host.oldSwapchain)->swapchain;

    if (vk_funcs->query_fs_hack &&
            vk_funcs->query_fs_hack(create_info_host.surface, &object->real_extent, &user_sz,
                                    &object->blit_dst, &object->fs_hack_filter) &&
            create_info_host.imageExtent.width == user_sz.width &&
            create_info_host.imageExtent.height == user_sz.height)
    {
        uint32_t count;
        VkSurfaceCapabilitiesKHR caps = {0};

        device->phys_dev->instance->funcs.p_vkGetPhysicalDeviceQueueFamilyProperties(device->phys_dev->phys_dev, &count, NULL);

        device->queue_props = malloc(sizeof(VkQueueFamilyProperties) * count);

        device->phys_dev->instance->funcs.p_vkGetPhysicalDeviceQueueFamilyProperties(device->phys_dev->phys_dev, &count, device->queue_props);

        res = device->phys_dev->instance->funcs.p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->phys_dev->phys_dev, create_info_host.surface, &caps);
        if (res != VK_SUCCESS)
        {
            TRACE("vkGetPhysicalDeviceSurfaceCapabilities failed, res=%d\n", res);
            free(object);
            return res;
        }

        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT))
            FIXME("Swapchain does not support required VK_IMAGE_USAGE_STORAGE_BIT\n");

        create_info_host.imageExtent = object->real_extent;
        create_info_host.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        create_info_host.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT;

        if (info->imageFormat != VK_FORMAT_B8G8R8A8_UNORM && info->imageFormat != VK_FORMAT_B8G8R8A8_SRGB)
            FIXME("swapchain image format is not BGRA8 UNORM/SRGB. Things may go badly. %d\n", create_info_host.imageFormat);

        object->fs_hack_enabled = TRUE;
    }

    res = device->funcs.p_vkCreateSwapchainKHR(device->device, &create_info_host, NULL, &object->swapchain);

    if (res != VK_SUCCESS)
    {
        free(object);
        return res;
    }

    if (object->fs_hack_enabled)
    {
        object->user_extent = info->imageExtent;

        res = init_fs_hack_images(device, object, info);
        if (res != VK_SUCCESS)
        {
            ERR("creating fs hack images failed: %d\n", res);
            device->funcs.p_vkDestroySwapchainKHR(device->device, object->swapchain, NULL);
            WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, object);
            free(object);
            return res;
        }

        res = init_blit_images(device, object);
        if (res != VK_SUCCESS)
        {
            ERR("creating blit images failed: %d\n", res);
            device->funcs.p_vkDestroySwapchainKHR(device->device, object->swapchain, NULL);
            WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, object);
            free(object);
            return res;
        }
    }

    object->handle = (uintptr_t)handle;
    handle->unix_handle = (uintptr_t)object;
    WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, object->handle, object->swapchain, object);
    *swapchain = object->handle;

    return res;
}

VkResult wine_vkCreateWin32SurfaceKHR(VkInstance handle, const VkWin32SurfaceCreateInfoKHR *createInfo,
                                      const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    struct wine_surface *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    object = calloc(1, sizeof(*object));

    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = instance->funcs.p_vkCreateWin32SurfaceKHR(instance->instance, createInfo, NULL, &object->driver_surface);

    if (res != VK_SUCCESS)
    {
        free(object);
        return res;
    }

    object->surface = vk_funcs->p_wine_get_native_surface(object->driver_surface);

    WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(instance, object, object->surface, object);

    *surface = wine_surface_to_handle(object);

    return VK_SUCCESS;
}

void wine_vkDestroySurfaceKHR(VkInstance handle, VkSurfaceKHR surface,
                              const VkAllocationCallbacks *allocator)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    struct wine_surface *object = wine_surface_from_handle(surface);

    if (!object)
        return;

    instance->funcs.p_vkDestroySurfaceKHR(instance->instance, object->driver_surface, NULL);

    WINE_VK_REMOVE_HANDLE_MAPPING(instance, object);
    free(object);
}

#define IOCTL_SHARED_GPU_RESOURCE_CREATE           CTL_CODE(FILE_DEVICE_VIDEO, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)

struct shared_resource_create
{
    obj_handle_t unix_handle;
    WCHAR name[1];
};

static HANDLE create_gpu_resource(int fd, LPCWSTR name)
{
    static const WCHAR shared_gpu_resourceW[] = {'\\','?','?','\\','S','h','a','r','e','d','G','p','u','R','e','s','o','u','r','c','e',0};
    HANDLE unix_resource = INVALID_HANDLE_VALUE;
    struct shared_resource_create *inbuff;
    UNICODE_STRING shared_gpu_resource_us;
    HANDLE shared_resource;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    DWORD in_size;

    TRACE("Creating shared vulkan resource fd %d name %s.\n", fd, debugstr_w(name));

    if (wine_server_fd_to_handle(fd, GENERIC_ALL, 0, &unix_resource) != STATUS_SUCCESS)
        return INVALID_HANDLE_VALUE;

    init_unicode_string(&shared_gpu_resource_us, shared_gpu_resourceW);

    attr.Length = sizeof(attr);
    attr.RootDirectory = 0;
    attr.Attributes = 0;
    attr.ObjectName = &shared_gpu_resource_us;
    attr.SecurityDescriptor = NULL;
    attr.SecurityQualityOfService = NULL;

    if ((status = NtCreateFile(&shared_resource, GENERIC_READ | GENERIC_WRITE, &attr, &iosb, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0)))
    {
        ERR("Failed to load open a shared resource handle, status %#lx.\n", (long int)status);
        NtClose(unix_resource);
        return INVALID_HANDLE_VALUE;
    }

    in_size = sizeof(*inbuff) + (name ? lstrlenW(name) * sizeof(WCHAR) : 0);
    inbuff = calloc(1, in_size);
    inbuff->unix_handle = wine_server_obj_handle(unix_resource);
    if (name)
        lstrcpyW(&inbuff->name[0], name);

    if ((status = NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_CREATE,
            inbuff, in_size, NULL, 0)))

    free(inbuff);
    NtClose(unix_resource);

    if (status)
    {
        ERR("Failed to create video resource, status %#lx.\n", (long int)status);
        NtClose(shared_resource);
        return INVALID_HANDLE_VALUE;
    }

    return shared_resource;
}

#define IOCTL_SHARED_GPU_RESOURCE_OPEN             CTL_CODE(FILE_DEVICE_VIDEO, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

struct shared_resource_open
{
    obj_handle_t kmt_handle;
    WCHAR name[1];
};

static HANDLE open_shared_resource(HANDLE kmt_handle, LPCWSTR name)
{
    static const WCHAR shared_gpu_resourceW[] = {'\\','?','?','\\','S','h','a','r','e','d','G','p','u','R','e','s','o','u','r','c','e',0};
    UNICODE_STRING shared_gpu_resource_us;
    struct shared_resource_open *inbuff;
    HANDLE shared_resource;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    DWORD in_size;

    init_unicode_string(&shared_gpu_resource_us, shared_gpu_resourceW);

    attr.Length = sizeof(attr);
    attr.RootDirectory = 0;
    attr.Attributes = 0;
    attr.ObjectName = &shared_gpu_resource_us;
    attr.SecurityDescriptor = NULL;
    attr.SecurityQualityOfService = NULL;

    if ((status = NtCreateFile(&shared_resource, GENERIC_READ | GENERIC_WRITE, &attr, &iosb, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0)))
    {
        ERR("Failed to load open a shared resource handle, status %#lx.\n", (long int)status);
        return INVALID_HANDLE_VALUE;
    }

    in_size = sizeof(*inbuff) + (name ? lstrlenW(name) * sizeof(WCHAR) : 0);
    inbuff = calloc(1, in_size);
    inbuff->kmt_handle = wine_server_obj_handle(kmt_handle);
    if (name)
        lstrcpyW(&inbuff->name[0], name);

    status = NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_OPEN,
            inbuff, in_size, NULL, 0);

    free(inbuff);

    if (status)
    {
        ERR("Failed to open video resource, status %#lx.\n", (long int)status);
        NtClose(shared_resource);
        return INVALID_HANDLE_VALUE;
    }

    return shared_resource;
}

#define IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE           CTL_CODE(FILE_DEVICE_VIDEO, 3, METHOD_BUFFERED, FILE_READ_ACCESS)

static int get_shared_resource_fd(HANDLE shared_resource)
{
    IO_STATUS_BLOCK iosb;
    obj_handle_t unix_resource;
    NTSTATUS status;
    int ret;

    if (NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_GET_UNIX_RESOURCE,
            NULL, 0, &unix_resource, sizeof(unix_resource)))
        return -1;

    status = wine_server_handle_to_fd(wine_server_ptr_handle(unix_resource), FILE_READ_DATA, &ret, NULL);
    NtClose(wine_server_ptr_handle(unix_resource));
    return status == STATUS_SUCCESS ? ret : -1;
}

#define IOCTL_SHARED_GPU_RESOURCE_GETKMT           CTL_CODE(FILE_DEVICE_VIDEO, 2, METHOD_BUFFERED, FILE_READ_ACCESS)

static HANDLE get_shared_resource_kmt_handle(HANDLE shared_resource)
{
    IO_STATUS_BLOCK iosb;
    obj_handle_t kmt_handle;

    if (NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_GETKMT,
            NULL, 0, &kmt_handle, sizeof(kmt_handle)))
        return INVALID_HANDLE_VALUE;

    return wine_server_ptr_handle(kmt_handle);
}

VkResult wine_vkAllocateMemory(VkDevice handle, const VkMemoryAllocateInfo *alloc_info,
                               const VkAllocationCallbacks *allocator, VkDeviceMemory *ret,
                               void *win_pAllocateInfo)
{
    struct wine_device *device = wine_device_from_handle(handle);
    const VkMemoryAllocateInfo *win_alloc_info = win_pAllocateInfo;
    struct wine_device_memory *memory;
    VkMemoryAllocateInfo info = *alloc_info;
    VkImportMemoryHostPointerInfoEXT host_pointer_info;
    uint32_t mem_flags;
    void *mapping = NULL;
    VkResult result;

    const VkImportMemoryWin32HandleInfoKHR *handle_import_info;
    const VkExportMemoryWin32HandleInfoKHR *handle_export_info;
    VkExportMemoryAllocateInfo *export_info;
    VkImportMemoryFdInfoKHR fd_import_info;
    VkMemoryGetFdInfoKHR get_fd_info;
    int fd;

    if (!(memory = calloc(sizeof(*memory), 1)))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    memory->handle = INVALID_HANDLE_VALUE;
    fd_import_info.fd = -1;
    fd_import_info.pNext = NULL;

    /* find and process handle import/export info and grab it */
    handle_import_info = wine_vk_find_struct(win_alloc_info, IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
    handle_export_info = wine_vk_find_struct(win_alloc_info, EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
    if (handle_export_info && handle_export_info->pAttributes && handle_export_info->pAttributes->lpSecurityDescriptor)
        FIXME("Support for custom security descriptor not implemented.\n");

    if ((export_info = wine_vk_find_struct(alloc_info, EXPORT_MEMORY_ALLOCATE_INFO)))
    {
        memory->handle_types = export_info->handleTypes;
        if (export_info->handleTypes & wine_vk_handle_over_fd_types)
            export_info->handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        wine_vk_normalize_handle_types_host(&export_info->handleTypes);
    }

    mem_flags = device->phys_dev->memory_properties.memoryTypes[alloc_info->memoryTypeIndex].propertyFlags;

    /* Vulkan consumes imported FDs, but not imported HANDLEs */
    if (handle_import_info)
    {
        fd_import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        fd_import_info.pNext = info.pNext;
        fd_import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        info.pNext = &fd_import_info;

        switch (handle_import_info->handleType)
        {
            case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT:
            case VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT:
                if (handle_import_info->handle)
                    NtDuplicateObject( NtCurrentProcess(), handle_import_info->handle, NtCurrentProcess(), &memory->handle, 0, 0, DUPLICATE_SAME_ACCESS );
                else if (handle_import_info->name)
                    memory->handle = open_shared_resource( 0, handle_import_info->name );
                break;
            case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT:
            case VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT:
                /* FIXME: the spec says that device memory imported from a KMT handle doesn't keep a reference to the underyling payload.
                   This means that in cases where on windows an application leaks VkDeviceMemory objects, we leak the full payload.  To
                   fix this, we would need wine_dev_mem objects to store no reference to the payload, that means no host VkDeviceMemory
                   object (as objects imported from FDs hold a reference to the payload), and no win32 handle to the object. We would then
                   extend make_vulkan to have the thunks converting wine_dev_mem to native handles open the VkDeviceMemory from the KMT
                   handle, use it in the host function, then close it again. */
                memory->handle = open_shared_resource( handle_import_info->handle, NULL );
                break;
            default:
                WARN("Invalid handle type %08x passed in.\n", handle_import_info->handleType);
                result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
                goto done;
        }

        if (memory->handle != INVALID_HANDLE_VALUE)
            fd_import_info.fd = get_shared_resource_fd(memory->handle);

        if (fd_import_info.fd == -1)
        {
            TRACE("Couldn't access resource handle or name. type=%08x handle=%p name=%s\n", handle_import_info->handleType, handle_import_info->handle,
                    handle_import_info->name ? debugstr_w(handle_import_info->name) : "");
            result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
            goto done;
        }
    }
    else if (device->phys_dev->external_memory_align && (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        !find_next_struct(alloc_info->pNext, VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT))
    {
        /* For host visible memory, we try to use VK_EXT_external_memory_host on wow64
         * to ensure that mapped pointer is 32-bit. */
        VkMemoryHostPointerPropertiesEXT props =
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
        };
        uint32_t i, align = device->phys_dev->external_memory_align - 1;
        SIZE_T alloc_size = info.allocationSize;
        static int once;

        if (!once++)
            FIXME("Using VK_EXT_external_memory_host\n");

        if (NtAllocateVirtualMemory(GetCurrentProcess(), &mapping, zero_bits(), &alloc_size,
                                    MEM_COMMIT, PAGE_READWRITE))
        {
            ERR("NtAllocateVirtualMemory failed\n");
            free(memory);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        result = device->funcs.p_vkGetMemoryHostPointerPropertiesEXT(device->device,
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, mapping, &props);
        if (result != VK_SUCCESS)
        {
            ERR("vkGetMemoryHostPointerPropertiesEXT failed: %d\n", result);
            free(memory);
            return result;
        }

        if (!(props.memoryTypeBits & (1u << info.memoryTypeIndex)))
        {
            /* If requested memory type is not allowed to use external memory,
             * try to find a supported compatible type. */
            uint32_t mask = mem_flags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            for (i = 0; i < device->phys_dev->memory_properties.memoryTypeCount; i++)
            {
                if (!(props.memoryTypeBits & (1u << i)))
                    continue;
                if ((device->phys_dev->memory_properties.memoryTypes[i].propertyFlags & mask) != mask)
                    continue;

                TRACE("Memory type not compatible with host memory, using %u instead\n", i);
                info.memoryTypeIndex = i;
                break;
            }
            if (i == device->phys_dev->memory_properties.memoryTypeCount)
            {
                FIXME("Not found compatible memory type\n");
                alloc_size = 0;
                NtFreeVirtualMemory(GetCurrentProcess(), &mapping, &alloc_size, MEM_RELEASE);
            }
        }

        if (props.memoryTypeBits & (1u << info.memoryTypeIndex))
        {
            host_pointer_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
            host_pointer_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
            host_pointer_info.pHostPointer = mapping;
            host_pointer_info.pNext = info.pNext;
            info.pNext = &host_pointer_info;

            info.allocationSize = (info.allocationSize + align) & ~align;
        }
    }

    result = device->funcs.p_vkAllocateMemory(device->device, &info, NULL, &memory->memory);
    if (result == VK_SUCCESS && memory->handle == INVALID_HANDLE_VALUE && export_info && export_info->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
    {
        get_fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        get_fd_info.pNext = NULL;
        get_fd_info.memory = memory->memory;
        get_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        if (device->funcs.p_vkGetMemoryFdKHR(device->device, &get_fd_info, &fd) == VK_SUCCESS)
        {
            memory->handle = create_gpu_resource(fd, handle_export_info ? handle_export_info->name : NULL);
            memory->access = handle_export_info ? handle_export_info->dwAccess : GENERIC_ALL;
            if (handle_export_info && handle_export_info->pAttributes)
                memory->inherit = handle_export_info->pAttributes->bInheritHandle;
            else
                memory->inherit = FALSE;
            close(fd);
        }

        if (memory->handle == INVALID_HANDLE_VALUE)
        {
            device->funcs.p_vkFreeMemory(device->device, memory->memory, NULL);
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }
    }
done:
    if (result != VK_SUCCESS)
    {
        if (fd_import_info.fd != -1)
            close(fd_import_info.fd);
        if (memory->handle != INVALID_HANDLE_VALUE)
            NtClose(memory->handle);
        free(memory);
        return result;
    }

    memory->mapping = mapping;
    *ret = wine_device_memory_to_handle(memory);
    return VK_SUCCESS;
}

void wine_vkFreeMemory(VkDevice handle, VkDeviceMemory memory_handle, const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_device_memory *memory;

    if (!memory_handle)
        return;
    memory = wine_device_memory_from_handle(memory_handle);

    device->funcs.p_vkFreeMemory(device->device, memory->memory, NULL);

    if (memory->mapping)
    {
        SIZE_T alloc_size = 0;
        NtFreeVirtualMemory(GetCurrentProcess(), &memory->mapping, &alloc_size, MEM_RELEASE);
    }

    if (memory->handle != INVALID_HANDLE_VALUE)
        NtClose(memory->handle);

    free(memory);
}

VkResult wine_vkMapMemory(VkDevice handle, VkDeviceMemory memory_handle, VkDeviceSize offset,
                          VkDeviceSize size, VkMemoryMapFlags flags, void **data)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_device_memory *memory = wine_device_memory_from_handle(memory_handle);
    VkResult result;

    if (memory->mapping)
    {
        *data = (char *)memory->mapping + offset;
        TRACE("returning %p\n", *data);
        return VK_SUCCESS;
    }

    result = device->funcs.p_vkMapMemory(device->device, memory->memory, offset, size, flags, data);

#ifdef _WIN64
    if (NtCurrentTeb()->WowTebOffset && result == VK_SUCCESS && (UINT_PTR)*data >> 32)
    {
        FIXME("returned mapping %p does not fit 32-bit pointer\n", *data);
        device->funcs.p_vkUnmapMemory(device->device, memory->memory);
        *data = NULL;
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
    }
#endif

    return result;
}

void wine_vkUnmapMemory(VkDevice handle, VkDeviceMemory memory_handle)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_device_memory *memory = wine_device_memory_from_handle(memory_handle);

    if (!memory->mapping)
        device->funcs.p_vkUnmapMemory(device->device, memory->memory);
}

VkResult wine_vkCreateBuffer(VkDevice handle, const VkBufferCreateInfo *create_info,
                             const VkAllocationCallbacks *allocator, VkBuffer *buffer)
{
    struct wine_device *device = wine_device_from_handle(handle);
    VkExternalMemoryBufferCreateInfo external_memory_info, *ext_info;
    VkBufferCreateInfo info = *create_info;

    if ((ext_info = wine_vk_find_struct(create_info, EXTERNAL_MEMORY_BUFFER_CREATE_INFO)))
    {
        if (ext_info->handleTypes & wine_vk_handle_over_fd_types)
            ext_info->handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        wine_vk_normalize_handle_types_host(&ext_info->handleTypes);
    }
    else if (device->phys_dev->external_memory_align &&
        !find_next_struct(info.pNext, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO))
    {
        external_memory_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        external_memory_info.pNext = info.pNext;
        external_memory_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        info.pNext = &external_memory_info;
    }

    return device->funcs.p_vkCreateBuffer(device->device, &info, NULL, buffer);
}

VkResult wine_vkCreateImage(VkDevice handle, const VkImageCreateInfo *create_info,
                            const VkAllocationCallbacks *allocator, VkImage *image)
{
    struct wine_device *device = wine_device_from_handle(handle);
    VkExternalMemoryImageCreateInfo external_memory_info, *update_info;
    VkImageCreateInfo info = *create_info;

    if ((update_info = find_next_struct(info.pNext, VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO)))
    {
        if (update_info->handleTypes & wine_vk_handle_over_fd_types)
            update_info->handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
        wine_vk_normalize_handle_types_host(&update_info->handleTypes);
    }
    else if (device->phys_dev->external_memory_align)
    {
        external_memory_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external_memory_info.pNext = info.pNext;
        external_memory_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        info.pNext = &external_memory_info;
    }

    return device->funcs.p_vkCreateImage(device->device, &info, NULL, image);
}

static inline void adjust_max_image_count(struct wine_phys_dev *phys_dev, VkSurfaceCapabilitiesKHR* capabilities)
{
    /* Many Windows games, for example Strange Brigade, No Man's Sky, Path of Exile
     * and World War Z, do not expect that maxImageCount can be set to 0.
     * A value of 0 means that there is no limit on the number of images.
     * Nvidia reports 8 on Windows, AMD 16.
     * https://vulkan.gpuinfo.org/displayreport.php?id=9122#surface
     * https://vulkan.gpuinfo.org/displayreport.php?id=9121#surface
     */
    if ((phys_dev->instance->quirks & WINEVULKAN_QUIRK_ADJUST_MAX_IMAGE_COUNT) && !capabilities->maxImageCount)
    {
        capabilities->maxImageCount = max(capabilities->minImageCount, 16);
    }
}

VkResult wine_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice handle, VkSurfaceKHR surface_handle,
                                                        VkSurfaceCapabilitiesKHR *capabilities)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(handle);
    struct wine_surface *surface = wine_surface_from_handle(surface_handle);
    VkResult res;
    VkExtent2D user_res;

    res = phys_dev->instance->funcs.p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev->phys_dev,
            surface->driver_surface, capabilities);

    if (res == VK_SUCCESS)
        adjust_max_image_count(phys_dev, capabilities);

    if (res == VK_SUCCESS && vk_funcs->query_fs_hack &&
        vk_funcs->query_fs_hack(surface->driver_surface, NULL, &user_res, NULL, NULL))
    {
        capabilities->currentExtent = user_res;
        capabilities->minImageExtent = user_res;
        capabilities->maxImageExtent = user_res;
    }

    return res;
}

VkResult wine_vkGetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice handle,
                                                         const VkPhysicalDeviceSurfaceInfo2KHR *surface_info,
                                                         VkSurfaceCapabilities2KHR *capabilities)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(handle);
    struct wine_surface *surface = wine_surface_from_handle(surface_info->surface);
    VkPhysicalDeviceSurfaceInfo2KHR host_info;
    VkResult res;
    VkExtent2D user_res;

    host_info.sType = surface_info->sType;
    host_info.pNext = surface_info->pNext;
    host_info.surface = surface->driver_surface;
    res = phys_dev->instance->funcs.p_vkGetPhysicalDeviceSurfaceCapabilities2KHR(phys_dev->phys_dev,
            &host_info, capabilities);

    if (res == VK_SUCCESS)
        adjust_max_image_count(phys_dev, &capabilities->surfaceCapabilities);

    if (res == VK_SUCCESS && vk_funcs->query_fs_hack &&
        vk_funcs->query_fs_hack(wine_surface_from_handle(surface_info->surface)->driver_surface, NULL, &user_res, NULL, NULL))
    {
        capabilities->surfaceCapabilities.currentExtent = user_res;
        capabilities->surfaceCapabilities.minImageExtent = user_res;
        capabilities->surfaceCapabilities.maxImageExtent = user_res;
    }

    return res;
}

VkResult wine_vkCreateDebugUtilsMessengerEXT(VkInstance handle,
                                             const VkDebugUtilsMessengerCreateInfoEXT *create_info,
                                             const VkAllocationCallbacks *allocator,
                                             VkDebugUtilsMessengerEXT *messenger)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    VkDebugUtilsMessengerCreateInfoEXT wine_create_info;
    struct wine_debug_utils_messenger *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    object->instance = instance;
    object->user_callback = create_info->pfnUserCallback;
    object->user_data = create_info->pUserData;

    wine_create_info = *create_info;

    wine_create_info.pfnUserCallback = (void *) &debug_utils_callback_conversion;
    wine_create_info.pUserData = object;

    res = instance->funcs.p_vkCreateDebugUtilsMessengerEXT(instance->instance, &wine_create_info, NULL,  &object->debug_messenger);

    if (res != VK_SUCCESS)
    {
        free(object);
        return res;
    }

    WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(instance, object, object->debug_messenger, object);
    *messenger = wine_debug_utils_messenger_to_handle(object);

    return VK_SUCCESS;
}

void wine_vkDestroyDebugUtilsMessengerEXT(VkInstance handle, VkDebugUtilsMessengerEXT messenger,
                                          const VkAllocationCallbacks *allocator)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    struct wine_debug_utils_messenger *object;

    object = wine_debug_utils_messenger_from_handle(messenger);

    if (!object)
        return;

    instance->funcs.p_vkDestroyDebugUtilsMessengerEXT(instance->instance, object->debug_messenger, NULL);
    WINE_VK_REMOVE_HANDLE_MAPPING(instance, object);

    free(object);
}

VkResult wine_vkCreateDebugReportCallbackEXT(VkInstance handle,
                                             const VkDebugReportCallbackCreateInfoEXT *create_info,
                                             const VkAllocationCallbacks *allocator,
                                             VkDebugReportCallbackEXT *callback)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    VkDebugReportCallbackCreateInfoEXT wine_create_info;
    struct wine_debug_report_callback *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    object->instance = instance;
    object->user_callback = create_info->pfnCallback;
    object->user_data = create_info->pUserData;

    wine_create_info = *create_info;

    wine_create_info.pfnCallback = (void *) debug_report_callback_conversion;
    wine_create_info.pUserData = object;

    res = instance->funcs.p_vkCreateDebugReportCallbackEXT(instance->instance, &wine_create_info, NULL, &object->debug_callback);

    if (res != VK_SUCCESS)
    {
        free(object);
        return res;
    }

    WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(instance, object, object->debug_callback, object);
    *callback = wine_debug_report_callback_to_handle(object);

    return VK_SUCCESS;
}

void wine_vkDestroyDebugReportCallbackEXT(VkInstance handle, VkDebugReportCallbackEXT callback,
                                          const VkAllocationCallbacks *allocator)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    struct wine_debug_report_callback *object;

    object = wine_debug_report_callback_from_handle(callback);

    if (!object)
        return;

    instance->funcs.p_vkDestroyDebugReportCallbackEXT(instance->instance, object->debug_callback, NULL);

    WINE_VK_REMOVE_HANDLE_MAPPING(instance, object);

    free(object);
}

VkResult wine_vkCreateDeferredOperationKHR(VkDevice                     handle,
                                           const VkAllocationCallbacks* allocator,
                                           VkDeferredOperationKHR*      deferredOperation)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_deferred_operation *object;
    VkResult res;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    res = device->funcs.p_vkCreateDeferredOperationKHR(device->device, NULL, &object->deferred_operation);

    if (res != VK_SUCCESS)
    {
        free(object);
        return res;
    }

    init_conversion_context(&object->ctx);

    WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, object, object->deferred_operation, object);
    *deferredOperation = wine_deferred_operation_to_handle(object);

    return VK_SUCCESS;
}

void wine_vkDestroyDeferredOperationKHR(VkDevice                     handle,
                                        VkDeferredOperationKHR       operation,
                                        const VkAllocationCallbacks* allocator)
{
    struct wine_device *device = wine_device_from_handle(handle);
    struct wine_deferred_operation *object;

    object = wine_deferred_operation_from_handle(operation);

    if (!object)
        return;

    device->funcs.p_vkDestroyDeferredOperationKHR(device->device, object->deferred_operation, NULL);

    WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, object);

    free_conversion_context(&object->ctx);
    free(object);
}

void wine_vkDestroySwapchainKHR(VkDevice device_handle, VkSwapchainKHR handle, const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_swapchain *swapchain = wine_swapchain_from_handle(handle);
    uint32_t i;

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (swapchain->fs_hack_enabled)
    {
        for (i = 0; i < swapchain->n_images; ++i) destroy_fs_hack_image(device, swapchain, &swapchain->fs_hack_images[i]);

        for (i = 0; i < device->queue_count; ++i)
            if (swapchain->cmd_pools[i])
                device->funcs.p_vkDestroyCommandPool(device->device, swapchain->cmd_pools[i], NULL);

        device->funcs.p_vkDestroyPipeline(device->device, swapchain->pipeline, NULL);
        device->funcs.p_vkDestroyPipelineLayout(device->device, swapchain->pipeline_layout, NULL);
        device->funcs.p_vkDestroyDescriptorSetLayout(device->device, swapchain->descriptor_set_layout, NULL);
        device->funcs.p_vkDestroyDescriptorPool(device->device, swapchain->descriptor_pool, NULL);
        device->funcs.p_vkDestroySampler(device->device, swapchain->sampler, NULL);
        device->funcs.p_vkFreeMemory(device->device, swapchain->user_image_memory, NULL);
        free(swapchain->cmd_pools);
        free(swapchain->fs_hack_images);
    }

    WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, swapchain);

    device->funcs.p_vkDestroySwapchainKHR(device->device, swapchain->swapchain, NULL);
    free(swapchain);
}

VkResult wine_vkGetSwapchainImagesKHR(VkDevice device_handle, VkSwapchainKHR handle,
                                      uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_swapchain *swapchain = wine_swapchain_from_handle(handle);
    uint32_t i;

    if (pSwapchainImages && swapchain->fs_hack_enabled)
    {
        if (*pSwapchainImageCount > swapchain->n_images)
            *pSwapchainImageCount = swapchain->n_images;
        for (i = 0; i < *pSwapchainImageCount; ++i) pSwapchainImages[i] = swapchain->fs_hack_images[i].user_image;
        return *pSwapchainImageCount == swapchain->n_images ? VK_SUCCESS : VK_INCOMPLETE;
    }

    return device->funcs.p_vkGetSwapchainImagesKHR(device->device, swapchain->swapchain,
                                                   pSwapchainImageCount, pSwapchainImages);
}

static VkCommandBuffer create_hack_cmd(struct wine_queue *queue, struct wine_swapchain *swapchain, uint32_t queue_idx)
{
    VkCommandBufferAllocateInfo allocInfo = {0};
    VkCommandBuffer cmd;
    VkResult result;

    if (!swapchain->cmd_pools[queue_idx])
    {
        VkCommandPoolCreateInfo poolInfo = {0};

        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queue_idx;

        result = queue->device->funcs.p_vkCreateCommandPool(queue->device->device, &poolInfo, NULL,
                                                            &swapchain->cmd_pools[queue_idx]);
        if (result != VK_SUCCESS)
        {
            ERR("vkCreateCommandPool failed, res=%d\n", result);
            return NULL;
        }
    }

    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = swapchain->cmd_pools[queue_idx];
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    result = queue->device->funcs.p_vkAllocateCommandBuffers(queue->device->device, &allocInfo, &cmd);
    if (result != VK_SUCCESS)
    {
        ERR("vkAllocateCommandBuffers failed, res=%d\n", result);
        return NULL;
    }

    return cmd;
}

static VkResult record_compute_cmd(struct wine_device *device, struct wine_swapchain *swapchain,
                                   struct fs_hack_image *hack)
{
    VkResult result;
    VkImageMemoryBarrier barriers[3] = {{0}};
    VkCommandBufferBeginInfo beginInfo = {0};
    float constants[4];

    TRACE("recording compute command\n");

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    device->funcs.p_vkBeginCommandBuffer(hack->cmd, &beginInfo);

    /* for the cs we run... */
    /* transition user image from PRESENT_SRC to SHADER_READ */
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = hack->user_image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    /* storage image... */
    /* transition swapchain image from whatever to GENERAL */
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = hack->swapchain_image;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    device->funcs.p_vkCmdPipelineBarrier(hack->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);

    /* perform blit shader */
    device->funcs.p_vkCmdBindPipeline(hack->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, swapchain->pipeline);

    device->funcs.p_vkCmdBindDescriptorSets(hack->cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                            swapchain->pipeline_layout, 0, 1, &hack->descriptor_set, 0, NULL);

    /* vec2: blit dst offset in real coords */
    constants[0] = swapchain->blit_dst.offset.x;
    constants[1] = swapchain->blit_dst.offset.y;

    /* offset by 0.5f because sampling is relative to pixel center */
    constants[0] -= 0.5f * swapchain->blit_dst.extent.width / swapchain->user_extent.width;
    constants[1] -= 0.5f * swapchain->blit_dst.extent.height / swapchain->user_extent.height;

    /* vec2: blit dst extents in real coords */
    constants[2] = swapchain->blit_dst.extent.width;
    constants[3] = swapchain->blit_dst.extent.height;
    device->funcs.p_vkCmdPushConstants(hack->cmd, swapchain->pipeline_layout,
                                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);

    /* local sizes in shader are 8 */
    device->funcs.p_vkCmdDispatch(hack->cmd, ceil(swapchain->real_extent.width / 8.),
            ceil(swapchain->real_extent.height / 8.), 1);

    /* transition user image from SHADER_READ back to PRESENT_SRC */
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = hack->user_image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = 0;

    /* transition swapchain image from GENERAL to PRESENT_SRC */
    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = hack->swapchain_image;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = 0;

    device->funcs.p_vkCmdPipelineBarrier(hack->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 2, barriers);

    result = device->funcs.p_vkEndCommandBuffer(hack->cmd);
    if (result != VK_SUCCESS)
    {
        ERR("vkEndCommandBuffer: %d\n", result);
        return result;
    }

    return VK_SUCCESS;
}

VkResult fshack_vk_queue_present(VkQueue queue_handle, const VkPresentInfoKHR *pPresentInfo)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);
    VkCommandBuffer *blit_cmds = NULL;
    struct wine_swapchain *swapchain;
    VkPresentInfoKHR our_presentInfo;
    VkSubmitInfo submitInfo = {0};
    uint32_t i, n_hacks = 0;
    VkSemaphore blit_sema;
    VkSwapchainKHR *arr;
    uint32_t queue_idx;
    VkResult res;

    TRACE("%p, %p\n", queue, pPresentInfo);

    our_presentInfo = *pPresentInfo;

    for (i = 0; i < our_presentInfo.swapchainCount; ++i)
    {
        swapchain = wine_swapchain_from_handle(our_presentInfo.pSwapchains[i]);

        if (swapchain->fs_hack_enabled)
        {
            struct fs_hack_image *hack = &swapchain->fs_hack_images[our_presentInfo.pImageIndices[i]];

            if (!blit_cmds)
            {
                queue_idx = queue->family_index;
                blit_cmds = malloc(our_presentInfo.swapchainCount * sizeof(VkCommandBuffer));
                blit_sema = hack->blit_finished;
            }

            if (!hack->cmd || hack->cmd_queue_idx != queue_idx)
            {
                if (hack->cmd)
                    queue->device->funcs.p_vkFreeCommandBuffers(queue->device->device,
                                                                swapchain->cmd_pools[hack->cmd_queue_idx], 1, &hack->cmd);

                hack->cmd_queue_idx = queue_idx;
                hack->cmd = create_hack_cmd(queue, swapchain, queue_idx);

                if (!hack->cmd)
                {
                    free(blit_cmds);
                    return VK_ERROR_DEVICE_LOST;
                }

                if (queue->device->queue_props[queue_idx].queueFlags & VK_QUEUE_COMPUTE_BIT) /* TODO */
                    res = record_compute_cmd(queue->device, swapchain, hack);
                else
                {
                    ERR("Present queue does not support compute!\n");
                    res = VK_ERROR_DEVICE_LOST;
                }

                if (res != VK_SUCCESS)
                {
                    queue->device->funcs.p_vkFreeCommandBuffers(queue->device->device,
                                                                swapchain->cmd_pools[hack->cmd_queue_idx], 1, &hack->cmd);
                    hack->cmd = NULL;
                    free(blit_cmds);
                    return res;
                }
            }

            blit_cmds[n_hacks] = hack->cmd;

            ++n_hacks;
        }
    }

    if (n_hacks > 0)
    {
        VkPipelineStageFlags waitStage, *waitStages, *waitStages_arr = NULL;

        if (pPresentInfo->waitSemaphoreCount > 1)
        {
            waitStages_arr = malloc(sizeof(VkPipelineStageFlags) * pPresentInfo->waitSemaphoreCount);
            for (i = 0; i < pPresentInfo->waitSemaphoreCount; ++i) waitStages_arr[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            waitStages = waitStages_arr;
        }
        else
        {
            waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            waitStages = &waitStage;
        }

        /* blit user image to real image */
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
        submitInfo.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = n_hacks;
        submitInfo.pCommandBuffers = blit_cmds;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &blit_sema;

        res = queue->device->funcs.p_vkQueueSubmit(queue->queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (res != VK_SUCCESS)
            ERR("vkQueueSubmit: %d\n", res);

        free(waitStages_arr);
        free(blit_cmds);

        our_presentInfo.waitSemaphoreCount = 1;
        our_presentInfo.pWaitSemaphores = &blit_sema;
    }

    arr = malloc(our_presentInfo.swapchainCount * sizeof(VkSwapchainKHR));
    if (!arr)
    {
        ERR("Failed to allocate memory for swapchain array\n");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    for (i = 0; i < our_presentInfo.swapchainCount; ++i)
        arr[i] = wine_swapchain_from_handle(our_presentInfo.pSwapchains[i])->swapchain;

    our_presentInfo.pSwapchains = arr;

    res = queue->device->funcs.p_vkQueuePresentKHR(queue->queue, &our_presentInfo);

    free(arr);

    return res;
}

#ifdef _WIN64

NTSTATUS vk_is_available_instance_function(void *arg)
{
    struct is_available_instance_function_params *params = arg;
    struct wine_instance *instance = wine_instance_from_handle(params->instance);
    return !!vk_funcs->p_vkGetInstanceProcAddr(instance->instance, params->name);
}

NTSTATUS vk_is_available_device_function(void *arg)
{
    struct is_available_device_function_params *params = arg;
    struct wine_device *device = wine_device_from_handle(params->device);
    if (!strcmp(params->name, "vkGetMemoryWin32HandleKHR") || !strcmp(params->name, "vkGetMemoryWin32HandlePropertiesKHR"))
        params->name = "vkGetMemoryFdKHR";
    else if (!strcmp(params->name, "vkGetSemaphoreWin32HandleKHR"))
        params->name = "vkGetSemaphoreFdKHR";
    else if (!strcmp(params->name, "vkImportSemaphoreWin32HandleKHR"))
        params->name = "vkImportSemaphoreFdKHR";
    return !!vk_funcs->p_vkGetDeviceProcAddr(device->device, params->name);
}

#endif /* _WIN64 */

NTSTATUS vk_is_available_instance_function32(void *arg)
{
    struct
    {
        UINT32 instance;
        UINT32 name;
    } *params = arg;
    struct wine_instance *instance = wine_instance_from_handle(UlongToPtr(params->instance));
    return !!vk_funcs->p_vkGetInstanceProcAddr(instance->instance, UlongToPtr(params->name));
}

NTSTATUS vk_is_available_device_function32(void *arg)
{
    struct
    {
        UINT32 device;
        UINT32 name;
    } *params = arg;
    struct wine_device *device = wine_device_from_handle(UlongToPtr(params->device));
    char *name = UlongToPtr(params->name);
    if (!strcmp(name, "vkGetMemoryWin32HandleKHR") || !strcmp(name, "vkGetMemoryWin32HandlePropertiesKHR"))
        return !!vk_funcs->p_vkGetDeviceProcAddr(device->device, "vkGetMemoryFdKHR");
    if (!strcmp(name, "vkGetSemaphoreWin32HandleKHR"))
        return !!vk_funcs->p_vkGetDeviceProcAddr(device->device, "vkGetSemaphoreFdKHR");
    if (!strcmp(name, "vkImportSemaphoreWin32HandleKHR"))
        return !!vk_funcs->p_vkGetDeviceProcAddr(device->device, "vkImportSemaphoreFdKHR");
    return !!vk_funcs->p_vkGetDeviceProcAddr(device->device, UlongToPtr(params->name));
}

VkDevice WINAPI __wine_get_native_VkDevice(VkDevice handle)
{
    struct wine_device *device = wine_device_from_handle(handle);

    return device->device;
}

VkInstance WINAPI __wine_get_native_VkInstance(VkInstance handle)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);;

    return instance->instance;
}

VkPhysicalDevice WINAPI __wine_get_native_VkPhysicalDevice(VkPhysicalDevice handle)
{
    struct wine_phys_dev *phys_dev = wine_phys_dev_from_handle(handle);

    return phys_dev->phys_dev;
}

VkQueue WINAPI __wine_get_native_VkQueue(VkQueue handle)
{
    struct wine_queue *queue = wine_queue_from_handle(handle);

    if (is_virtual_queue(queue))
    {
        FIXME("VR is using native handle of virtualized queue, this is untested.\n");
        pthread_mutex_lock(&queue->submissions_mutex);
        while (queue->processing)
            pthread_cond_wait(&queue->submissions_cond, &queue->submissions_mutex);
        pthread_mutex_unlock(&queue->submissions_mutex);
    }

    return queue->queue;
}

VkPhysicalDevice WINAPI __wine_get_wrapped_VkPhysicalDevice(VkInstance handle, VkPhysicalDevice native_phys_dev)
{
    struct wine_instance *instance = wine_instance_from_handle(handle);
    uint32_t i;
    for(i = 0; i < instance->phys_dev_count; ++i){
        if(instance->phys_devs[i]->phys_dev == native_phys_dev)
            return instance->phys_devs[i]->handle;
    }
    WARN("Unknown native physical device: %p\n", native_phys_dev);
    return NULL;
}

VkResult wine_vkGetMemoryWin32HandleKHR(VkDevice device, const VkMemoryGetWin32HandleInfoKHR *handle_info, HANDLE *handle)
{
    struct wine_device_memory *dev_mem = wine_device_memory_from_handle(handle_info->memory);
    const VkBaseInStructure *chain;
    HANDLE ret;

    TRACE("%p, %p %p\n", device, handle_info, handle);

    if (!(dev_mem->handle_types & handle_info->handleType))
        return VK_ERROR_UNKNOWN;

    if ((chain = handle_info->pNext))
        FIXME("Ignoring a linked structure of type %u.\n", chain->sType);

    switch(handle_info->handleType)
    {
        case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT:
        case VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT:
            return !NtDuplicateObject( NtCurrentProcess(), dev_mem->handle, NtCurrentProcess(), handle, dev_mem->access, dev_mem->inherit ? OBJ_INHERIT : 0, 0) ?
                VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
        case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT:
        case VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT:
        {
            if ((ret = get_shared_resource_kmt_handle(dev_mem->handle)) == INVALID_HANDLE_VALUE)
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            *handle = ret;
            return VK_SUCCESS;
        }
        default:
            FIXME("Unable to get handle of type %x, did the application ignore the capabilities?\n", handle_info->handleType);
            return VK_ERROR_UNKNOWN;
    }
}

VkResult wine_vkGetMemoryWin32HandlePropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits type, HANDLE handle, VkMemoryWin32HandlePropertiesKHR *properties)
{
    TRACE("%p %u %p %p\n", device, type, handle, properties);

    /* VUID-vkGetMemoryWin32HandlePropertiesKHR-handleType-00666
       handleType must not be one of the handle types defined as opaque */
    return VK_ERROR_INVALID_EXTERNAL_HANDLE;
}

#define IOCTL_SHARED_GPU_RESOURCE_SET_OBJECT           CTL_CODE(FILE_DEVICE_VIDEO, 6, METHOD_BUFFERED, FILE_WRITE_ACCESS)

static bool set_shared_resource_object(HANDLE shared_resource, unsigned int index, HANDLE handle)
{
    IO_STATUS_BLOCK iosb;
    struct shared_resource_set_object
    {
        unsigned int index;
        obj_handle_t handle;
    } params;

    params.index = index;
    params.handle = wine_server_obj_handle(handle);

    return NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_SET_OBJECT,
            &params, sizeof(params), NULL, 0) == STATUS_SUCCESS;
}

#define IOCTL_SHARED_GPU_RESOURCE_GET_OBJECT           CTL_CODE(FILE_DEVICE_VIDEO, 6, METHOD_BUFFERED, FILE_READ_ACCESS)

static HANDLE get_shared_resource_object(HANDLE shared_resource, unsigned int index)
{
    IO_STATUS_BLOCK iosb;
    obj_handle_t handle;

    if (NtDeviceIoControlFile(shared_resource, NULL, NULL, NULL, &iosb, IOCTL_SHARED_GPU_RESOURCE_GET_OBJECT,
            &index, sizeof(index), &handle, sizeof(handle)))
        return NULL;

    return wine_server_ptr_handle(handle);
}

static void d3d12_semaphore_lock(struct wine_semaphore *semaphore)
{
    assert( semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT );
    pthread_mutex_lock(&semaphore->d3d12_fence_shm->mutex);
}

static void d3d12_semaphore_unlock(struct wine_semaphore *semaphore)
{
    assert( semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT );
    pthread_mutex_unlock(&semaphore->d3d12_fence_shm->mutex);
}

/* returns -1 when there is no queued update that would satisfy the wait */
static uint64_t d3d12_semaphore_try_get_wait_value_locked(struct wine_semaphore *semaphore, uint64_t virtual_value,
        struct wine_queue *waiting_queue)
{
    struct pending_update *update;
    uint64_t ret = -1;
    unsigned int i;

    if (semaphore->d3d12_fence_shm->virtual_value >= virtual_value)
        return 0;

    for (i = 0; i < semaphore->d3d12_fence_shm->pending_updates_count; i++)
    {
        update = &semaphore->d3d12_fence_shm->pending_updates[i];

        if (update->virtual_value < virtual_value)
            continue;

        if (update->signalling_pid == getpid() && waiting_queue && update->signalling_queue == waiting_queue)
            return 0;

        ret = min(ret, update->physical_value);
    }

    return ret;
}

static struct pending_wait *d3d12_semaphore_push_wait_locked(struct wine_semaphore *semaphore, uint64_t virtual_value)
{
    struct pending_wait *wait;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(semaphore->d3d12_fence_shm->pending_waits); i++)
    {
        wait = &semaphore->d3d12_fence_shm->pending_waits[i];
        if (!wait->present)
            break;
    }

    if (i == ARRAY_SIZE(semaphore->d3d12_fence_shm->pending_waits))
    {
        FIXME("Failed to wait on semaphore %p, maximum waits exceeded.\n", semaphore);
        return NULL;
    }

    wait->present = true;
    wait->satisfied = false;
    wait->virtual_value = virtual_value;
    wait->physical_value = 0;

    return wait;
}

static uint64_t d3d12_semaphore_pop_wait_locked(struct wine_semaphore *semaphore, struct pending_wait *wait)
{
    wait->satisfied = false;
    wait->present = false;

    return wait->physical_value;
}

static void d3d12_semaphore_satisfy_waits_locked(struct wine_semaphore *semaphore, uint64_t virtual_value,
        uint64_t physical_value)
{
    struct pending_wait *wait;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(semaphore->d3d12_fence_shm->pending_waits); i++)
    {
        wait = &semaphore->d3d12_fence_shm->pending_waits[i];

        if (wait->present && !wait->satisfied && wait->virtual_value <= virtual_value)
        {
            wait->satisfied = true;
            wait->physical_value = physical_value;
            pthread_cond_signal(&wait->cond);
        }
    }
}

static uint64_t d3d12_semaphore_add_pending_signal_locked(struct wine_semaphore *semaphore, uint64_t virtual_value,
        struct wine_queue *signalling_queue)
{
    struct pending_update *update;

    if (semaphore->d3d12_fence_shm->pending_updates_count == ARRAY_SIZE(semaphore->d3d12_fence_shm->pending_updates))
    {
        /* Called from Unix thread, can't use Wine traces. */
        fprintf(stderr, "winevulkan/d3d12_semaphore_add_pending_signal_locked: maximum concurrent signals exceeded.\n");
        return 0;
    }

    update = &semaphore->d3d12_fence_shm->pending_updates[
        semaphore->d3d12_fence_shm->pending_updates_count++];

    update->virtual_value = virtual_value;
    update->physical_value = ++semaphore->d3d12_fence_shm->counter;
    update->signalling_pid = getpid();
    update->signalling_queue = signalling_queue;

    return update->physical_value;
}

static struct pending_update d3d12_semaphore_peek_added_signal_locked(struct wine_semaphore *semaphore)
{
    return semaphore->d3d12_fence_shm->pending_updates[semaphore->d3d12_fence_shm->pending_updates_count - 1];
}

static bool d3d12_semaphore_pop_pending_signal_locked(struct wine_semaphore *semaphore, uint64_t phys_val, struct pending_update *ret)
{
    struct pending_update *update;
    unsigned int i;

    for (i = 0; i < semaphore->d3d12_fence_shm->pending_updates_count; i++)
    {
        if (semaphore->d3d12_fence_shm->pending_updates[i].physical_value == phys_val)
        {
            update = &semaphore->d3d12_fence_shm->pending_updates[i];
            if (ret)
                *ret = *update;
            *update = semaphore->d3d12_fence_shm->pending_updates[--semaphore->d3d12_fence_shm->pending_updates_count];
            return true;
        }
    }

    return false;
}

static void d3d12_semaphore_update_phys_val_locked(struct wine_semaphore *sem, uint64_t phys_val)
{
    struct pending_update pending;

    /* Based off linked VKD3D-Proton implementation, but we don't signal CPU waits here.
        * https://github.com/HansKristian-Work/vkd3d-proton/blob/829ac72e3d381006a843c183e613e8ee77e0b292/libs/vkd3d/command.c#L758 */
    while (sem->d3d12_fence_shm->physical_value < phys_val)
    {
        sem->d3d12_fence_shm->physical_value++;

        if (d3d12_semaphore_pop_pending_signal_locked(sem, sem->d3d12_fence_shm->physical_value, &pending))
            sem->d3d12_fence_shm->virtual_value = pending.virtual_value;
    }
}

VkResult wine_vkCreateSemaphore(VkDevice device_handle, const VkSemaphoreCreateInfo *create_info,
        const VkAllocationCallbacks *allocator, VkSemaphore *semaphore, void *win_create_info)
{
    struct wine_device *device = wine_device_from_handle(device_handle);

    VkExportSemaphoreWin32HandleInfoKHR *export_handle_info = wine_vk_find_struct(win_create_info, EXPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR);
    VkExportSemaphoreCreateInfo *export_semaphore_info, timeline_export_info;
    VkSemaphoreCreateInfo create_info_dup = *create_info;
    VkSemaphoreTypeCreateInfo *found_type_info, type_info;
    VkSemaphoreGetFdInfoKHR fd_info;
    pthread_mutexattr_t mutex_attr;
    struct wine_semaphore *object;
    pthread_condattr_t cond_attr;
    OBJECT_ATTRIBUTES attr;
    HANDLE section_handle;
    LARGE_INTEGER li;
    unsigned int i;
    VkResult res;
    SIZE_T size;
    int fd;

    TRACE("(%p, %p, %p, %p)\n", device, create_info, allocator, semaphore);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    object->handle = INVALID_HANDLE_VALUE;

    if ((export_semaphore_info = wine_vk_find_struct(&create_info_dup, EXPORT_SEMAPHORE_CREATE_INFO)))
    {
        object->export_types = export_semaphore_info->handleTypes;
        if (export_semaphore_info->handleTypes & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT)
            export_semaphore_info->handleTypes |= VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        wine_vk_normalize_semaphore_handle_types_host(&export_semaphore_info->handleTypes);
    }

    if ((res = device->funcs.p_vkCreateSemaphore(device->device, &create_info_dup, NULL, &object->semaphore)) != VK_SUCCESS)
        goto done;

    if (export_semaphore_info && export_semaphore_info->handleTypes == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT)
    {
        fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        fd_info.pNext = NULL;
        fd_info.semaphore = object->semaphore;
        fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

        if ((res = device->funcs.p_vkGetSemaphoreFdKHR(device->device, &fd_info, &fd)) == VK_SUCCESS)
        {
            object->handle = create_gpu_resource(fd, export_handle_info ? export_handle_info->name : NULL);
            close(fd);
        }

        if (object->handle == INVALID_HANDLE_VALUE)
        {
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }
    }
    else if (object->export_types & VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
    {
        /* compatibleHandleTypes doesn't include any other types */
        assert(object->export_types == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT);
        object->handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

        timeline_export_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        timeline_export_info.pNext = NULL;
        timeline_export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

        type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type_info.pNext = &timeline_export_info;
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = 0;

        create_info_dup.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        create_info_dup.pNext = &type_info;
        create_info_dup.flags = 0;

        if ((res = device->funcs.p_vkCreateSemaphore(device->device, &create_info_dup, NULL, &object->fence_timeline_semaphore)) != VK_SUCCESS)
            goto done;

        fd_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        fd_info.pNext = NULL;
        fd_info.semaphore = object->fence_timeline_semaphore;
        fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

        if ((res = device->funcs.p_vkGetSemaphoreFdKHR(device->device, &fd_info, &fd)) == VK_SUCCESS)
        {
            object->handle = create_gpu_resource(fd, export_handle_info ? export_handle_info->name : NULL);
            close(fd);
        }

        if (object->handle == INVALID_HANDLE_VALUE)
        {
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }

        /* Shared Fence Memory */
        InitializeObjectAttributes(&attr, NULL, 0, NULL, NULL);
        size = li.QuadPart = sizeof(*object->d3d12_fence_shm);
        if (NtCreateSection(&section_handle, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE, &attr, &li, PAGE_READWRITE, SEC_COMMIT, NULL))
        {
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }

        if (!set_shared_resource_object(object->handle, 0, section_handle))
        {
            NtClose(section_handle);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }

        if (NtMapViewOfSection(section_handle, GetCurrentProcess(), (void**) &object->d3d12_fence_shm, 0, 0, NULL, &size, ViewShare, 0, PAGE_READWRITE))
        {
            NtClose(section_handle);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }

        NtClose(section_handle);

        if ((found_type_info = wine_vk_find_struct(create_info, SEMAPHORE_TYPE_CREATE_INFO)))
            object->d3d12_fence_shm->virtual_value = found_type_info->initialValue;

        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        if (pthread_mutex_init(&object->d3d12_fence_shm->mutex, &mutex_attr))
        {
            pthread_mutexattr_destroy(&mutex_attr);
            res = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto done;
        }
        pthread_mutexattr_destroy(&mutex_attr);

        for (i = 0; i < ARRAY_SIZE(object->d3d12_fence_shm->pending_waits); i++)
        {
            pthread_condattr_init(&cond_attr);
            pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
            pthread_cond_init(&object->d3d12_fence_shm->pending_waits[i].cond, &cond_attr);
            pthread_condattr_destroy(&cond_attr);
        }

        WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, object, object->fence_timeline_semaphore, object);
    }
    if (object->fence_timeline_semaphore == VK_NULL_HANDLE)
        WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, object, object->semaphore, object);
    *semaphore = wine_semaphore_to_handle(object);

    done:

    if (res != VK_SUCCESS)
    {
        if (object->d3d12_fence_shm)
        {
            pthread_mutex_destroy(&object->d3d12_fence_shm->mutex);
            NtUnmapViewOfSection(GetCurrentProcess(), object->d3d12_fence_shm);
        }
        if (object->handle != INVALID_HANDLE_VALUE)
            NtClose(object->handle);
        if (object->semaphore != VK_NULL_HANDLE)
            device->funcs.p_vkDestroySemaphore(device->device, object->semaphore, NULL);
        if (object->fence_timeline_semaphore != VK_NULL_HANDLE)
            device->funcs.p_vkDestroySemaphore(device->device, object->fence_timeline_semaphore, NULL);
        free(object);
    }

    return res;
}

VkResult wine_vkGetSemaphoreWin32HandleKHR(VkDevice device_handle, const VkSemaphoreGetWin32HandleInfoKHR *handle_info,
        HANDLE *handle)
{
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(handle_info->semaphore);

    if (!(semaphore->export_types & handle_info->handleType))
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;

    if (NtDuplicateObject( NtCurrentProcess(), semaphore->handle, NtCurrentProcess(), handle, 0, 0, DUPLICATE_SAME_ACCESS ))
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;

    return VK_SUCCESS;
}

void wine_vkDestroySemaphore(VkDevice device_handle, VkSemaphore semaphore_handle, const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(semaphore_handle);

    TRACE("%p, %p, %p\n", device, semaphore, allocator);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!semaphore)
        return;

    if (semaphore->handle != INVALID_HANDLE_VALUE)
        NtClose(semaphore->handle);

    if (semaphore->d3d12_fence_shm)
        NtUnmapViewOfSection(GetCurrentProcess(), semaphore->d3d12_fence_shm);

    WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, semaphore);
    device->funcs.p_vkDestroySemaphore(device->device, semaphore->semaphore, NULL);

    if (semaphore->fence_timeline_semaphore)
        device->funcs.p_vkDestroySemaphore(device->device, semaphore->fence_timeline_semaphore, NULL);

    free(semaphore);
}

VkResult wine_vkImportSemaphoreWin32HandleKHR(VkDevice device_handle,
        const VkImportSemaphoreWin32HandleInfoKHR *handle_info)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(handle_info->semaphore);
    struct wine_semaphore output_semaphore;
    VkSemaphoreTypeCreateInfo type_info;
    VkImportSemaphoreFdInfoKHR fd_info;
    VkSemaphoreCreateInfo create_info;
    HANDLE d3d12_fence_shm;
    NTSTATUS stat;
    VkResult res;
    SIZE_T size;

    TRACE("(%p, %p). semaphore = %p handle = %p\n", device, handle_info, semaphore, handle_info->handle);

    if (handle_info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT && !semaphore->fence_timeline_semaphore)
    {
        type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type_info.pNext = NULL;
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = 0;

        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        create_info.pNext = &type_info;
        create_info.flags = 0;

        if ((res = device->funcs.p_vkCreateSemaphore(device->device, &create_info, NULL, &semaphore->fence_timeline_semaphore)) != VK_SUCCESS)
        {
            ERR("Failed to create timeline semaphore backing D3D12 semaphore. vr %d.\n", res);
            return res;
        };

        WINE_VK_REMOVE_HANDLE_MAPPING(device->phys_dev->instance, semaphore);
        WINE_VK_ADD_NON_DISPATCHABLE_MAPPING(device->phys_dev->instance, semaphore, semaphore->fence_timeline_semaphore, semaphore);
    }

    output_semaphore = *semaphore;
    output_semaphore.handle = NULL;
    output_semaphore.handle_type = handle_info->handleType;
    output_semaphore.d3d12_fence_shm = NULL;

    fd_info.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
    fd_info.pNext = handle_info->pNext;
    fd_info.semaphore = wine_semaphore_host_handle(&output_semaphore);
    fd_info.flags = handle_info->flags;
    fd_info.handleType = handle_info->handleType;

    if (handle_info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT ||
        handle_info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
    {
        if (handle_info->name)
        {
            FIXME("Importing win32 semaphore by name not supported.\n");
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }

        if (NtDuplicateObject( NtCurrentProcess(), handle_info->handle, NtCurrentProcess(), &output_semaphore.handle, 0, 0, DUPLICATE_SAME_ACCESS ))
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;

        fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        if ((fd_info.fd = get_shared_resource_fd(output_semaphore.handle)) == -1)
        {
            WARN("Invalid handle %p.\n", handle_info->handle);
            NtClose(output_semaphore.handle);
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }

        if (handle_info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
        {
            if (handle_info->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT)
            {
                FIXME("Temporarily importing d3d12 fences unsupported.\n");
                close(fd_info.fd);
                NtClose(output_semaphore.handle);
                return VK_ERROR_INVALID_EXTERNAL_HANDLE;
            }

            if (!(d3d12_fence_shm = get_shared_resource_object(output_semaphore.handle, 0)))
            {
                ERR("Failed to get D3D12 semaphore memory.\n");
                close(fd_info.fd);
                NtClose(output_semaphore.handle);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            size = sizeof(*output_semaphore.d3d12_fence_shm);
            if ((stat = NtMapViewOfSection(d3d12_fence_shm, GetCurrentProcess(), (void**) &output_semaphore.d3d12_fence_shm, 0, 0, NULL, &size, ViewShare, 0, PAGE_READWRITE)))
            {
                ERR("Failed to map D3D12 semaphore memory. stat %#x.\n", (int)stat);
                close(fd_info.fd);
                NtClose(d3d12_fence_shm);
                NtClose(output_semaphore.handle);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            NtClose(d3d12_fence_shm);
        }
    }

    wine_vk_normalize_semaphore_handle_types_host(&fd_info.handleType);

    if (!fd_info.handleType)
    {
        FIXME("Importing win32 semaphore with handle type %#x not supported.\n", handle_info->handleType);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }

    if ((res = device->funcs.p_vkImportSemaphoreFdKHR(device->device, &fd_info)) == VK_SUCCESS)
    {
        if (semaphore->handle)
            NtClose(semaphore->handle);
        if (semaphore->d3d12_fence_shm)
            NtUnmapViewOfSection(GetCurrentProcess(), semaphore->d3d12_fence_shm);

        *semaphore = output_semaphore;
    }
    else
    {
        if (output_semaphore.handle)
            NtClose(output_semaphore.handle);
        if (output_semaphore.d3d12_fence_shm)
            NtUnmapViewOfSection(GetCurrentProcess(), output_semaphore.d3d12_fence_shm);

        /* importing FDs transfers ownership, importing NT handles does not  */
        close(fd_info.fd);
    }

    return res;
}

static VkResult vk_get_semaphore_counter_value(VkDevice device_handle, VkSemaphore semaphore_handle, uint64_t *value, bool khr)
{
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(semaphore_handle);
    struct wine_device *device = wine_device_from_handle(device_handle);

    if (khr)
        return device->funcs.p_vkGetSemaphoreCounterValueKHR(device->device, wine_semaphore_host_handle(semaphore), value);
    else
        return device->funcs.p_vkGetSemaphoreCounterValue(device->device, wine_semaphore_host_handle(semaphore), value);
}

static VkResult wine_vk_get_semaphore_counter_value(VkDevice device_handle, VkSemaphore semaphore_handle, uint64_t *value, bool khr)
{
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(semaphore_handle);

    if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
    {
        d3d12_semaphore_lock(semaphore);
        *value = semaphore->d3d12_fence_shm->virtual_value;
        d3d12_semaphore_unlock(semaphore);
        return VK_SUCCESS;
    }

    return vk_get_semaphore_counter_value(device_handle, semaphore_handle, value, khr);
}

VkResult wine_vkGetSemaphoreCounterValue(VkDevice device_handle, VkSemaphore semaphore_handle, uint64_t *value)
{
    return wine_vk_get_semaphore_counter_value(device_handle, semaphore_handle, value, false);
}

VkResult wine_vkGetSemaphoreCounterValueKHR(VkDevice device_handle, VkSemaphore semaphore_handle, uint64_t *value)
{
    return wine_vk_get_semaphore_counter_value(device_handle, semaphore_handle, value, true);
}

static VkResult vk_signal_semaphore(VkDevice device_handle, const VkSemaphoreSignalInfo *signal_info, bool khr)
{
    struct wine_semaphore *semaphore = wine_semaphore_from_handle(signal_info->semaphore);
    struct wine_device *device = wine_device_from_handle(device_handle);
    VkSemaphoreSignalInfo dup_signal_info = *signal_info;

    dup_signal_info.semaphore = wine_semaphore_host_handle(semaphore);
    if (khr)
        return device->funcs.p_vkSignalSemaphoreKHR(device->device, &dup_signal_info);
    else
        return device->funcs.p_vkSignalSemaphore(device->device, &dup_signal_info);
}

static NTSTATUS wine_vk_signal_semaphore(VkDevice device, const VkSemaphoreSignalInfo *signal_info, bool khr)
{
    uint64_t phys_val;
    VkResult vr;

    struct wine_semaphore *semaphore = wine_semaphore_from_handle(signal_info->semaphore);

    TRACE("(%p, %p)\n", device, signal_info);

    if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
    {
        d3d12_semaphore_lock(semaphore);

        /* vkWaitSemaphore w/ WAIT_ANY wakes on every physical value increment to check if the wait is satisfied, so
           if there are no scheduled signals, step the physical value */
        if ((vr = vk_get_semaphore_counter_value(device, signal_info->semaphore, &phys_val, khr)) != VK_SUCCESS)
        {
            d3d12_semaphore_unlock(semaphore);
            return vr;
        }

        d3d12_semaphore_update_phys_val_locked(semaphore, phys_val);

        if (!semaphore->d3d12_fence_shm->pending_updates_count)
        {
            VkSemaphoreSignalInfo step_signal_info;

            assert(semaphore->d3d12_fence_shm->counter == phys_val);
            phys_val++;

            semaphore->d3d12_fence_shm->counter = semaphore->d3d12_fence_shm->physical_value = phys_val;

            step_signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
            step_signal_info.pNext = NULL;
            step_signal_info.semaphore = signal_info->semaphore;
            step_signal_info.value = phys_val;

            vr = vk_signal_semaphore(device, &step_signal_info, khr);
            if (vr != VK_SUCCESS)
            {
                d3d12_semaphore_unlock(semaphore);
                return vr;
            }
        }

        /* If a queue is already waiting on the pending physical value of a previous submit, this won't wake it up. */
        d3d12_semaphore_satisfy_waits_locked(semaphore, signal_info->value, 0);
        semaphore->d3d12_fence_shm->virtual_value = signal_info->value;

        d3d12_semaphore_unlock(semaphore);
        return VK_SUCCESS;
    }

    return vk_signal_semaphore(device, signal_info, khr);
}

VkResult wine_vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *signal_info)
{
    return wine_vk_signal_semaphore(device, signal_info, false);
}

VkResult wine_vkSignalSemaphoreKHR(VkDevice device, const VkSemaphoreSignalInfo *signal_info)
{
    return wine_vk_signal_semaphore(device, signal_info, true);
}

static VkSemaphore *unwrap_semaphore_array(const VkSemaphore *in, uint32_t count, struct conversion_context *ctx)
{
    VkSemaphore *out;
    unsigned int i;

    if (!in || !count) return NULL;

    out = conversion_context_alloc(ctx, count * sizeof(*out));
    for (i = 0; i < count; ++i)
        out[i] = in[i] ? wine_semaphore_host_handle(wine_semaphore_from_handle(in[i])) : VK_NULL_HANDLE;

    return out;
}

static VkResult vk_wait_semaphores(struct wine_device *device, const VkSemaphoreWaitInfo *wait_info, uint64_t timeout, bool khr)
{
    VkSemaphoreWaitInfo wait_info_dup = *wait_info;
    struct conversion_context ctx;
    VkResult ret;

    init_conversion_context(&ctx);
    wait_info_dup.pSemaphores = unwrap_semaphore_array(wait_info->pSemaphores, wait_info->semaphoreCount, &ctx);
    if (khr)
        ret = device->funcs.p_vkWaitSemaphoresKHR(device->device, &wait_info_dup, timeout);
    else
        ret = device->funcs.p_vkWaitSemaphores(device->device, &wait_info_dup, timeout);
    free_conversion_context(&ctx);
    return ret;
}

static VkResult wine_vk_wait_semaphores(VkDevice device_handle, const VkSemaphoreWaitInfo *wait_info, uint64_t timeout, bool khr)
{
    VkSemaphoreWaitInfo wait_info_dup = *wait_info;
    struct timespec abs_timeout, start_time;
    struct pending_wait **pending_waits;
    struct pending_wait *pending_wait;
    unsigned int i, remaining_waits;
    VkSemaphore* semaphores_dup;
    uint64_t *values_dup;
    int64_t tv_sec_wide;
    uint64_t phys_val;
    int wait_stat;
    VkResult res;

    TRACE("(%p, %p, 0x%s)\n", device_handle, wait_info, wine_dbgstr_longlong(timeout));

    if (timeout)
    {
        clock_gettime(CLOCK_REALTIME, &start_time);

        abs_timeout.tv_sec = tv_sec_wide = start_time.tv_sec + (timeout / NANOSECONDS_IN_A_SECOND);
        abs_timeout.tv_nsec = start_time.tv_nsec + (timeout % NANOSECONDS_IN_A_SECOND);
        if (abs_timeout.tv_nsec >= NANOSECONDS_IN_A_SECOND)
        {
            abs_timeout.tv_sec++;
            tv_sec_wide++;
            abs_timeout.tv_nsec-=NANOSECONDS_IN_A_SECOND;
        }

        /* tv_sec is still! 32-bit on x86 */
        if (tv_sec_wide > abs_timeout.tv_sec)
            abs_timeout.tv_sec = INT_MAX;
    }

    wait_info_dup.pSemaphores = semaphores_dup = calloc(wait_info->semaphoreCount, sizeof(VkSemaphore));
    wait_info_dup.pValues = values_dup = calloc(wait_info->semaphoreCount, sizeof(uint64_t));
    pending_waits = calloc(wait_info->semaphoreCount, sizeof(struct pending_wait *));

    for (i = 0; i < wait_info->semaphoreCount; i++)
    {
        struct wine_semaphore *semaphore = wine_semaphore_from_handle(wait_info->pSemaphores[i]);

        semaphores_dup[i] = wait_info->pSemaphores[i];
        values_dup[i] = wait_info->pValues[i];

        if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
        {
            d3d12_semaphore_lock(semaphore);
            if ((values_dup[i] = d3d12_semaphore_try_get_wait_value_locked(semaphore, wait_info->pValues[i], NULL)) == -1)
            {
                if (!timeout)
                {
                    d3d12_semaphore_unlock(semaphore);
                    continue;
                }

                pending_wait = d3d12_semaphore_push_wait_locked(semaphore, wait_info->pValues[i]);

                if (wait_info->flags & VK_SEMAPHORE_WAIT_ANY_BIT)
                {
                    /* Keep scheduling a wait of current physical_value+1 until the desired virtual value is signaled */
                    values_dup[i] = semaphore->d3d12_fence_shm->physical_value + 1;
                    pending_waits[i] = pending_wait;
                }
                else
                {
                    while (!pending_wait->satisfied && wait_stat != ETIMEDOUT)
                        wait_stat = pthread_cond_timedwait(&pending_wait->cond, &semaphore->d3d12_fence_shm->mutex, &abs_timeout);

                    values_dup[i] = d3d12_semaphore_pop_wait_locked(semaphore, pending_wait);

                    if (wait_stat == ETIMEDOUT)
                    {
                        d3d12_semaphore_unlock(semaphore);
                        free(semaphores_dup);
                        free(values_dup);
                        free(pending_waits);
                        return VK_TIMEOUT;
                    }
                }
            }
            d3d12_semaphore_unlock(semaphore);
        }
    }
    do
    {
        if (timeout)
        {
            clock_gettime(CLOCK_REALTIME, &start_time);

            if (start_time.tv_sec > abs_timeout.tv_sec ||
                    (start_time.tv_sec == abs_timeout.tv_sec && start_time.tv_nsec >= abs_timeout.tv_nsec))
                timeout = 0;
            else
                timeout = ((abs_timeout.tv_sec - start_time.tv_sec) * NANOSECONDS_IN_A_SECOND) +
                    (abs_timeout.tv_nsec - start_time.tv_nsec);
        }

        remaining_waits = 0;
        res = vk_wait_semaphores(wine_device_from_handle(device_handle), &wait_info_dup, timeout, khr);

        for (i = 0; i < wait_info->semaphoreCount; i++)
        {
            struct wine_semaphore * semaphore = wine_semaphore_from_handle(wait_info->pSemaphores[i]);

            if (pending_waits[i])
            {
                remaining_waits++;

                d3d12_semaphore_lock(semaphore);
                if (res != VK_SUCCESS || pending_waits[i]->satisfied)
                {
                    values_dup[i] = pending_waits[i]->physical_value;
                    d3d12_semaphore_pop_wait_locked(semaphore, pending_waits[i]);
                    pending_waits[i] = NULL;
                }
                d3d12_semaphore_unlock(semaphore);
            }
        }
    }
    while (res == VK_SUCCESS && remaining_waits);

    /* Make sure the physical value we waited on is processed before returning */
    for (i = 0; i < wait_info_dup.semaphoreCount; i++)
    {
        struct wine_semaphore *semaphore = wine_semaphore_from_handle(wait_info_dup.pSemaphores[i]);

        if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
        {
            d3d12_semaphore_lock(semaphore);
            if (wait_info->flags & VK_SEMAPHORE_WAIT_ANY_BIT)
            {
                if (!vk_get_semaphore_counter_value(device_handle, semaphores_dup[i], &phys_val, khr))
                    d3d12_semaphore_update_phys_val_locked(semaphore, phys_val);
            }
            else
                d3d12_semaphore_update_phys_val_locked(semaphore, values_dup[i]);
            d3d12_semaphore_unlock(semaphore);
        }
    }

    free(semaphores_dup);
    free(values_dup);
    free(pending_waits);
    return res;
}

VkResult wine_vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *wait_info, uint64_t timeout)
{
    return wine_vk_wait_semaphores(device, wait_info, timeout, false);
}

VkResult wine_vkWaitSemaphoresKHR(VkDevice device, const VkSemaphoreWaitInfo *wait_info, uint64_t timeout)
{
    return wine_vk_wait_semaphores(device, wait_info, timeout, true);
}

VkResult vk_queue_submit_unwrap(struct wine_queue *queue, uint32_t submit_count, const VkSubmitInfo *submits_orig, VkFence fence_handle)
{
    struct wine_fence *fence = fence_handle ? wine_fence_from_handle(fence_handle) : NULL;
    struct conversion_context ctx;
    VkSubmitInfo *submits;
    unsigned int i, j;
    VkResult ret;

    init_conversion_context(&ctx);
    MEMDUP(&ctx, submits, submits_orig, submit_count);
    for (i = 0; i < submit_count; ++i)
    {
        submits[i].pWaitSemaphores = unwrap_semaphore_array(submits[i].pWaitSemaphores, submits[i].waitSemaphoreCount, &ctx);
        submits[i].pSignalSemaphores = unwrap_semaphore_array(submits[i].pSignalSemaphores, submits[i].signalSemaphoreCount, &ctx);
        if (submits[i].pCommandBuffers && submits[i].commandBufferCount)
        {
            VkCommandBuffer *out;

            out = conversion_context_alloc(&ctx, submits[i].commandBufferCount * sizeof(*out));
            for (j = 0; j < submits[i].commandBufferCount; ++j)
                out[j] = wine_cmd_buffer_from_handle(submits[i].pCommandBuffers[j])->command_buffer;
            submits[i].pCommandBuffers = out;
        }
    }


    if (fence)
        fence->queue = queue;

    ret = queue->device->funcs.p_vkQueueSubmit(queue->queue, submit_count, submits, fence ? fence->fence : VK_NULL_HANDLE);
    free_conversion_context(&ctx);
    return ret;
}

struct signal_op
{
    enum
    {
        SIGNAL_TYPE_SEMAPHORE,
        SIGNAL_TYPE_FENCE,
    } signal_type;

    union
    {
        struct
        {
            struct wine_semaphore *obj;
            uint64_t phys_val;

            bool khr;
        } semaphore;

        struct wine_fence *fence;
    };

    struct list entry;
};

static void *queue_signaller_worker(void *arg)
{
    struct wine_queue *queue = arg;
    VkSemaphoreWaitInfo wait_info;
    struct signal_op *signal_op;
    VkSemaphore sem_handle;
    bool device_lost;
    uint64_t buf;
    VkResult vr;

    for (;;)
    {
        pthread_mutex_lock(&queue->signaller_mutex);

        while (!queue->stop && list_empty(&queue->signal_ops))
            pthread_cond_wait(&queue->signaller_cond, &queue->signaller_mutex);

        if (queue->stop)
        {
            assert( list_empty(&queue->signal_ops) );
            pthread_mutex_unlock(&queue->signaller_mutex);
            return NULL;
        }

        signal_op = LIST_ENTRY(list_head(&queue->signal_ops), struct signal_op, entry);
        list_remove(&signal_op->entry);

        device_lost = queue->device_lost;

        pthread_mutex_unlock(&queue->signaller_mutex);

        if (signal_op->signal_type == SIGNAL_TYPE_SEMAPHORE)
        {
            wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.pNext = NULL;
            wait_info.flags = 0;
            wait_info.semaphoreCount = 1;
            sem_handle = wine_semaphore_to_handle(signal_op->semaphore.obj);
            wait_info.pSemaphores = &sem_handle;
            wait_info.pValues = &signal_op->semaphore.phys_val;
            if (!device_lost && (vr = vk_wait_semaphores(queue->device, &wait_info, -1, signal_op->semaphore.khr)) < 0)
            {
                /* likely GPU hang */
                fprintf(stderr, "winevulkan/queue_signaller_worker: Semaphore wait failed, vr %d.\n", vr);
                continue;
            }

            d3d12_semaphore_lock(signal_op->semaphore.obj);
            d3d12_semaphore_update_phys_val_locked(signal_op->semaphore.obj, signal_op->semaphore.phys_val);
            d3d12_semaphore_unlock(signal_op->semaphore.obj);
        }
        else
        {
            if (!device_lost && (vr = queue->device->funcs.p_vkWaitForFences
                    (queue->device->device, 1, &signal_op->fence->fence, VK_TRUE, -1)) < 0)
            {
                /* likely GPU hang */
                fprintf(stderr, "winevulkan/queue_signaller_worker: Fence wait failed, vr %d.\n", vr);
                continue;
            }

            buf = 1;
            assert( write(signal_op->fence->eventfd, &buf, sizeof(buf)) != -1 );
        }

        free(signal_op);
    }

    return NULL;
}

struct struct_chain_def
{
    VkStructureType sType;
    unsigned int size;
};

void copy_VkSubmitInfo(struct conversion_context *ctx, const VkSubmitInfo *in, VkSubmitInfo *out);
static VkSubmitInfo *copy_VkSubmitInfo_array(struct conversion_context *ctx, const VkSubmitInfo *in, uint32_t submit_count)
{
    VkSubmitInfo *out = conversion_context_alloc(ctx, sizeof(*out) * submit_count);
    unsigned int i;

    for (i = 0; i < submit_count; i++)
        copy_VkSubmitInfo(ctx, &in[i], &out[i]);
    return out;
}

void copy_VkSubmitInfo2(struct conversion_context *ctx, const VkSubmitInfo2 *in, VkSubmitInfo2 *out);
static VkSubmitInfo2 *copy_VkSubmitInfo2_array(struct conversion_context *ctx, const VkSubmitInfo2 *in, uint32_t submit_count)
{
    VkSubmitInfo2 *out = conversion_context_alloc(ctx, sizeof(*out) * submit_count);
    unsigned int i;

    for (i = 0; i < submit_count; i++)
        copy_VkSubmitInfo2(ctx, &in[i], &out[i]);
    return out;
}

struct queue_submit_unit
{
    uint32_t submit_count;
    VkSubmitInfo *submits;
    VkSubmitInfo2 *submits2;
    VkFence fence;
    bool khr;

    struct pending_wait **waits;

    struct list entry;
    struct conversion_context ctx;
};

/* Abstracts away the differences between VkSubmitInfo and VkSubmitInfo2. */
static bool get_semaphore_by_index(struct queue_submit_unit *unit, bool signal,
                                     struct wine_semaphore **semaphore_out, uint64_t **value_out, uint32_t counter)
{
    VkTimelineSemaphoreSubmitInfo *timeline_values;
    struct wine_semaphore *semaphore;
    unsigned int i, j, k;
    uint32_t sem_count;

    for (i = 0, k = 0; i < unit->submit_count; i++)
    {
        if (unit->submits)
        {
            timeline_values = wine_vk_find_struct(&unit->submits[i], TIMELINE_SEMAPHORE_SUBMIT_INFO);

            if (signal)
                sem_count = unit->submits[i].signalSemaphoreCount;
            else
                sem_count = unit->submits[i].waitSemaphoreCount;

            for (j = 0; j < sem_count; j++)
            {
                if (signal)
                    semaphore = wine_semaphore_from_handle(unit->submits[i].pSignalSemaphores[j]);
                else
                    semaphore = wine_semaphore_from_handle(unit->submits[i].pWaitSemaphores[j]);

                if (semaphore->handle_type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                    continue;

                if (k++ == counter)
                {
                    *semaphore_out = semaphore;
                    if (signal)
                        *value_out = (uint64_t *) &timeline_values->pSignalSemaphoreValues[j];
                    else
                        *value_out = (uint64_t *) &timeline_values->pWaitSemaphoreValues[j];
                    return true;
                }
            }
        }
        else
        {
            if (signal)
                sem_count = unit->submits2[i].signalSemaphoreInfoCount;
            else
                sem_count = unit->submits2[i].waitSemaphoreInfoCount;

            for (j = 0; j < sem_count; j++)
            {
                if (signal)
                    semaphore = wine_semaphore_from_handle(unit->submits2[i].pSignalSemaphoreInfos[j].semaphore);
                else
                    semaphore = wine_semaphore_from_handle(unit->submits2[i].pWaitSemaphoreInfos[j].semaphore);

                if (semaphore->handle_type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                    continue;

                if (k++ == counter)
                {
                    *semaphore_out = semaphore;
                    if (signal)
                        *value_out = (uint64_t *) &unit->submits2[i].pSignalSemaphoreInfos[j].value;
                    else
                        *value_out = (uint64_t *) &unit->submits2[i].pWaitSemaphoreInfos[j].value;
                    return true;
                }
            }
        }
    }

    return false;
}

VkResult vk_queue_submit_2_unwrap(struct wine_queue *queue, uint32_t submit_count, const VkSubmitInfo2 *submits, VkFence fence, bool khr);

static void *virtual_queue_worker(void *arg)
{
    struct wine_queue *queue = (struct wine_queue *)arg;
    struct queue_submit_unit *submit_unit;
    struct signal_op *signal_op;
    struct wine_semaphore *sem;
    struct pending_wait *wait;
    struct wine_fence *fence;
    bool device_lost = false;
    uint64_t *timeline_value;
    unsigned int i;
    VkResult vr;

    for (;;)
    {
        pthread_mutex_lock(&queue->submissions_mutex);

        while (!queue->stop && list_empty(&queue->submissions))
            pthread_cond_wait(&queue->submissions_cond, &queue->submissions_mutex);

        if (queue->stop)
        {
            assert( list_empty(&queue->submissions) );
            pthread_mutex_unlock(&queue->submissions_mutex);
            return NULL;
        }

        submit_unit = LIST_ENTRY(list_head(&queue->submissions), struct queue_submit_unit, entry);
        list_remove(&submit_unit->entry);

        pthread_mutex_unlock(&queue->submissions_mutex);

        if (device_lost)
            goto free_submit_unit;

        /* Wait for all fences to have a pending signal */
        for (i = 0; get_semaphore_by_index(submit_unit, false, &sem, &timeline_value, i); i++)
        {
            if ((wait = submit_unit->waits[i]))
            {
                d3d12_semaphore_lock(sem);

                while (!wait->satisfied)
                    pthread_cond_wait(&wait->cond, &sem->d3d12_fence_shm->mutex);

                *timeline_value = d3d12_semaphore_pop_wait_locked(sem, wait);

                d3d12_semaphore_unlock(sem);
            }
        }

        for (i = 0; get_semaphore_by_index(submit_unit, true, &sem, &timeline_value, i); i++)
        {
            d3d12_semaphore_lock(sem);

            *timeline_value = d3d12_semaphore_add_pending_signal_locked(sem, *timeline_value, queue);
        }

        if (submit_unit->submits)
            vr = vk_queue_submit_unwrap(queue, submit_unit->submit_count, submit_unit->submits, submit_unit->fence);
        else
            vr = vk_queue_submit_2_unwrap(queue, submit_unit->submit_count, submit_unit->submits2,
                    submit_unit->fence, submit_unit->khr);

        pthread_mutex_lock(&queue->signaller_mutex);

        for (i = 0; get_semaphore_by_index(submit_unit, true, &sem, &timeline_value, i); i++)
        {
            if (vr == VK_SUCCESS)
            {
                struct pending_update added_signal = d3d12_semaphore_peek_added_signal_locked(sem);
                d3d12_semaphore_satisfy_waits_locked(sem, added_signal.virtual_value, added_signal.physical_value);

                signal_op = malloc(sizeof(*signal_op));
                signal_op->signal_type = SIGNAL_TYPE_SEMAPHORE;
                signal_op->semaphore.obj = sem;
                signal_op->semaphore.phys_val = added_signal.physical_value;
                signal_op->semaphore.khr = submit_unit->khr;

                list_add_tail(&queue->signal_ops, &signal_op->entry);
            }
            else
            {
                d3d12_semaphore_pop_pending_signal_locked(sem, *timeline_value, NULL);
            }

            d3d12_semaphore_unlock(sem);
        }

        if (vr == VK_SUCCESS && (fence = wine_fence_from_handle(submit_unit->fence)))
        {
            signal_op = malloc(sizeof(*signal_op));
            signal_op->signal_type = SIGNAL_TYPE_FENCE;
            signal_op->fence = fence;

            list_add_tail(&queue->signal_ops, &signal_op->entry);
        }

        pthread_cond_signal(&queue->signaller_cond);
        pthread_mutex_unlock(&queue->signaller_mutex);

        if (vr != VK_SUCCESS)
        {
            fprintf(stderr, "winevulkan/virtual_queue_worker: queue submission failed with %d, treating as DEVICE_LOST.\n", vr);
            pthread_mutex_lock(&queue->submissions_mutex);
            queue->device_lost = device_lost = true;
            pthread_mutex_unlock(&queue->submissions_mutex);

            if ((fence = wine_fence_from_handle(submit_unit->fence)))
            {
                uint64_t buf = 1;
                assert( write(fence->eventfd, &buf, sizeof(buf)) != -1 );
            }
        }

free_submit_unit:
        free_conversion_context(&submit_unit->ctx);
        free(submit_unit->waits);
        free(submit_unit);

        pthread_mutex_lock(&queue->submissions_mutex);
        if (list_empty(&queue->submissions))
        {
            queue->processing = false;
        }
        pthread_cond_signal(&queue->submissions_cond);
        pthread_mutex_unlock(&queue->submissions_mutex);
    }

    return NULL;
}

static void init_virtual_queue(struct wine_queue *queue)
{
    if (is_virtual_queue(queue))
        return;

    pthread_mutex_lock(&queue->submissions_mutex);

    if (queue->virtual_queue)
    {
        pthread_mutex_unlock(&queue->submissions_mutex);
        return;
    }

    __atomic_store_n(&queue->virtual_queue, 1, __ATOMIC_RELEASE);

    pthread_create(&queue->virtual_queue_thread, NULL, virtual_queue_worker, queue);
    pthread_create(&queue->signal_thread, NULL, queue_signaller_worker, queue);

    queue->virtual_queue = true;

    pthread_mutex_unlock(&queue->submissions_mutex);
}

static NTSTATUS virtual_queue_submit(struct wine_queue *queue, uint32_t submit_count, const VkSubmitInfo *submits,
        VkFence fence, void *submits_win_ptr)
{
    VkTimelineSemaphoreSubmitInfo *timeline_submit_info, *host_timeline_values;
    const VkSubmitInfo *submits_win = submits_win_ptr;
    VkD3D12FenceSubmitInfoKHR *d3d12_submit_info;
    struct queue_submit_unit *submit_unit;
    struct wine_semaphore *sem;
    unsigned int i, j, k;
    uint64_t wait_value;
    bool device_lost;

    init_virtual_queue(queue);

    pthread_mutex_lock(&queue->submissions_mutex);
    device_lost = queue->device_lost;
    pthread_mutex_unlock(&queue->submissions_mutex);
    if (device_lost)
        return VK_ERROR_DEVICE_LOST;

    submit_unit = malloc(sizeof(*submit_unit));
    init_conversion_context(&submit_unit->ctx);
    submit_unit->submit_count = submit_count;
    submit_unit->submits = copy_VkSubmitInfo_array(&submit_unit->ctx, submits, submit_count);
    submit_unit->submits2 = NULL;
    submit_unit->fence = fence;
    submit_unit->waits = NULL;
    submit_unit->khr = queue->device->phys_dev->api_version < VK_API_VERSION_1_2 ||
                       queue->device->phys_dev->instance->api_version < VK_API_VERSION_1_2;

    /* As D3D12 fences are rewindable, we add the wait synchronously as not to miss a temporarily signalled value
     between vkQueueSubmit and processing the submit unit*/
    for (i = 0, k = 0; i < submit_count; i++)
    {
        timeline_submit_info = wine_vk_find_struct(&submits[i], TIMELINE_SEMAPHORE_SUBMIT_INFO);
        d3d12_submit_info = wine_vk_find_struct(&submits_win[i], D3D12_FENCE_SUBMIT_INFO_KHR);

        host_timeline_values = wine_vk_find_struct(&submit_unit->submits[i], TIMELINE_SEMAPHORE_SUBMIT_INFO);

        if (d3d12_submit_info && !host_timeline_values)
        {
            host_timeline_values = conversion_context_alloc(&submit_unit->ctx, sizeof(*host_timeline_values));
            host_timeline_values->sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            host_timeline_values->pNext = submit_unit->submits[i].pNext;
            host_timeline_values->waitSemaphoreValueCount = d3d12_submit_info->waitSemaphoreValuesCount;
            MEMDUP(&submit_unit->ctx, host_timeline_values->pWaitSemaphoreValues, d3d12_submit_info->pWaitSemaphoreValues,
                    d3d12_submit_info->waitSemaphoreValuesCount);
            host_timeline_values->signalSemaphoreValueCount = d3d12_submit_info->signalSemaphoreValuesCount;
            MEMDUP(&submit_unit->ctx, host_timeline_values->pSignalSemaphoreValues, d3d12_submit_info->pSignalSemaphoreValues,
                    d3d12_submit_info->signalSemaphoreValuesCount);
            submit_unit->submits[i].pNext = host_timeline_values;
        }

        for (j = 0; j < submits[i].waitSemaphoreCount; j++)
        {
            sem = wine_semaphore_from_handle(submits[i].pWaitSemaphores[j]);

            if (sem->handle_type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                continue;

            if (timeline_submit_info)
                wait_value = timeline_submit_info->pWaitSemaphoreValues[j];
            else
                wait_value = d3d12_submit_info->pWaitSemaphoreValues[j];

            submit_unit->waits = realloc(submit_unit->waits, (k + 1) * sizeof(*submit_unit->waits));
            submit_unit->waits[k] = NULL;

            d3d12_semaphore_lock(sem);

            if ((((uint64_t*)host_timeline_values->pWaitSemaphoreValues)[j] =
                                d3d12_semaphore_try_get_wait_value_locked(sem, wait_value, queue)) == -1)
                submit_unit->waits[k] = d3d12_semaphore_push_wait_locked(sem, wait_value);

            d3d12_semaphore_unlock(sem);
            k++;
        }
    }

    pthread_mutex_lock(&queue->submissions_mutex);
    queue->processing = true;
    if (fence)
    {
        wine_fence_from_handle(fence)->queue = queue;
        wine_fence_from_handle(fence)->wait_assist = true;
    }
    list_add_tail(&queue->submissions, &submit_unit->entry);
    pthread_cond_signal(&queue->submissions_cond);
    pthread_mutex_unlock(&queue->submissions_mutex);

    return VK_SUCCESS;
}

VkResult wine_vkQueueSubmit(VkQueue queue_handle, uint32_t submit_count, const VkSubmitInfo *submits, VkFence fence,
        void *submits_win)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);
    unsigned int i, k;

    TRACE("(%p %u %p 0x%s)\n", queue_handle, submit_count, submits, wine_dbgstr_longlong(fence));

    if (is_virtual_queue(queue))
        return virtual_queue_submit(queue, submit_count, submits, fence, submits_win);

    for (i = 0; i < submit_count; i++)
    {
        for (k = 0; k < submits[i].waitSemaphoreCount; k++)
        {
            if (wine_semaphore_from_handle(submits[i].pWaitSemaphores[k])->handle_type ==
                    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                return virtual_queue_submit(queue, submit_count, submits, fence, submits_win);
        }

        for (k = 0; k < submits[i].signalSemaphoreCount; k++)
        {
            if (wine_semaphore_from_handle(submits[i].pSignalSemaphores[k])->handle_type ==
                    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                return virtual_queue_submit(queue, submit_count, submits, fence, submits_win);
        }
    }
    return vk_queue_submit_unwrap(queue, submit_count, submits, fence);
}

static void duplicate_array_for_unwrapping(struct conversion_context *ctx, void **ptr, unsigned int size)
{
    void *out;

    if (!*ptr || !size)
        return;

    out = conversion_context_alloc(ctx, size);
    memcpy(out, *ptr, size);
    *ptr = out;
}

static NTSTATUS virtual_queue_submit2(struct wine_queue *queue, uint32_t submit_count, const VkSubmitInfo2 *submits, VkFence fence, bool khr)
{
    VkSemaphoreSubmitInfo *sem_submit_info;
    struct queue_submit_unit *submit_unit;
    VkSubmitInfo2 *queue_submit;
    struct wine_semaphore *sem;
    unsigned int i, j, k;
    uint64_t wait_value;
    bool device_lost;

    init_virtual_queue(queue);

    pthread_mutex_lock(&queue->submissions_mutex);
    device_lost = queue->device_lost;
    pthread_mutex_unlock(&queue->submissions_mutex);
    if (device_lost)
        return VK_ERROR_DEVICE_LOST;

    submit_unit = malloc(sizeof(*submit_unit));
    init_conversion_context(&submit_unit->ctx);
    submit_unit->submit_count = submit_count;
    submit_unit->submits = NULL;
    submit_unit->submits2 = copy_VkSubmitInfo2_array(&submit_unit->ctx, submits, submit_count);
    submit_unit->fence = fence;
    submit_unit->waits = NULL;
    submit_unit->khr = khr;

    /* As D3D12 fences are rewindable, we add the wait synchronously as not to miss a temporarily signalled value
     between vkQueueSubmit and processing the submit unit */
    for (i = 0, k = 0; i < submit_count; i++)
    {
        queue_submit = &submit_unit->submits2[i];

        for (j = 0; j < queue_submit->waitSemaphoreInfoCount; j++)
        {
            sem_submit_info = (VkSemaphoreSubmitInfo *) &queue_submit->pWaitSemaphoreInfos[j];
            sem = wine_semaphore_from_handle(sem_submit_info->semaphore);

            if (sem->handle_type != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                continue;

            wait_value = sem_submit_info->value;

            submit_unit->waits = realloc(submit_unit->waits, (k + 1) * sizeof(*submit_unit->waits));
            submit_unit->waits[k] = NULL;

            d3d12_semaphore_lock(sem);

            if ((sem_submit_info->value =
                                d3d12_semaphore_try_get_wait_value_locked(sem, wait_value, queue)) == -1)
                submit_unit->waits[k] = d3d12_semaphore_push_wait_locked(sem, wait_value);

            d3d12_semaphore_unlock(sem);
            k++;
        }
    }

    pthread_mutex_lock(&queue->submissions_mutex);
    queue->processing = true;
    if (fence)
    {
        wine_fence_from_handle(fence)->queue = queue;
        wine_fence_from_handle(fence)->wait_assist = true;
    }
    list_add_tail(&queue->submissions, &submit_unit->entry);
    pthread_cond_signal(&queue->submissions_cond);
    pthread_mutex_unlock(&queue->submissions_mutex);

    return VK_SUCCESS;
}

VkResult vk_queue_submit_2_unwrap(struct wine_queue *queue, uint32_t submit_count, const VkSubmitInfo2 *submits_orig,
        VkFence fence_handle, bool khr)
{
    struct wine_fence *fence = fence_handle ? wine_fence_from_handle(fence_handle) : NULL;
    struct conversion_context ctx;
    VkSubmitInfo2 *submits;
    unsigned int i, j;
    VkResult ret;

    init_conversion_context(&ctx);
    MEMDUP(&ctx, submits, submits_orig, submit_count);
    for (i = 0; i < submit_count; ++i)
    {
        duplicate_array_for_unwrapping(&ctx, (void **)&submits[i].pWaitSemaphoreInfos,
                submits[i].waitSemaphoreInfoCount * sizeof(*submits[i].pWaitSemaphoreInfos));
        for (j = 0; j < submits[i].waitSemaphoreInfoCount; ++j)
            if (submits[i].pWaitSemaphoreInfos[j].semaphore)
                ((VkSemaphoreSubmitInfo *)submits[i].pWaitSemaphoreInfos)[j].semaphore
                        = wine_semaphore_host_handle(wine_semaphore_from_handle(submits[i].pWaitSemaphoreInfos[j].semaphore));

        duplicate_array_for_unwrapping(&ctx, (void **)&submits[i].pSignalSemaphoreInfos,
                submits[i].signalSemaphoreInfoCount * sizeof(*submits[i].pSignalSemaphoreInfos));
        for (j = 0; j < submits[i].signalSemaphoreInfoCount; ++j)
            if (submits[i].pSignalSemaphoreInfos[j].semaphore)
                ((VkSemaphoreSubmitInfo *)submits[i].pSignalSemaphoreInfos)[j].semaphore
                        = wine_semaphore_host_handle(wine_semaphore_from_handle(submits[i].pSignalSemaphoreInfos[j].semaphore));

        if (submits[i].pCommandBufferInfos && submits[i].commandBufferInfoCount)
        {
            duplicate_array_for_unwrapping(&ctx, (void **)&submits[i].pCommandBufferInfos,
                    submits[i].commandBufferInfoCount * sizeof(*submits[i].pCommandBufferInfos));
            for (j = 0; j < submits[i].commandBufferInfoCount; ++j)
                ((VkCommandBufferSubmitInfo *)submits[i].pCommandBufferInfos)[j].commandBuffer
                        = wine_cmd_buffer_from_handle(submits[i].pCommandBufferInfos[j].commandBuffer)->command_buffer;
        }
    }
    if (fence)
        fence->queue = queue;

    if (khr)
        ret = queue->device->funcs.p_vkQueueSubmit2KHR(queue->queue, submit_count, submits, fence ? fence->fence : VK_NULL_HANDLE);
    else
        ret = queue->device->funcs.p_vkQueueSubmit2(queue->queue, submit_count, submits, fence ? fence->fence : VK_NULL_HANDLE);

    free_conversion_context(&ctx);
    return ret;
}

static VkResult vk_queue_submit_2(VkQueue queue_handle, uint32_t submit_count, const VkSubmitInfo2 *submits, VkFence fence_handle, bool khr)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);
    unsigned int i, k;

    TRACE("(%p, %u, %p, %s)\n", queue_handle, submit_count, submits, wine_dbgstr_longlong(fence_handle));

    if (is_virtual_queue(queue))
        return virtual_queue_submit2(queue, submit_count, submits, fence_handle, khr);

    for (i = 0; i < submit_count; i++)
    {
        for (k = 0; k < submits[i].waitSemaphoreInfoCount; k++)
        {
            if (wine_semaphore_from_handle(submits[i].pWaitSemaphoreInfos[k].semaphore)->handle_type ==
                    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                return virtual_queue_submit2(queue, submit_count, submits, fence_handle, khr);
        }

        for (k = 0; k < submits[i].signalSemaphoreInfoCount; k++)
        {
            if (wine_semaphore_from_handle(submits[i].pSignalSemaphoreInfos[k].semaphore)->handle_type ==
                    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
                return virtual_queue_submit2(queue, submit_count, submits, fence_handle, khr);
        }
    }

    return vk_queue_submit_2_unwrap(queue, submit_count, submits, fence_handle, khr);
}

VkResult wine_vkQueueSubmit2(VkQueue queue, uint32_t submit_count, const VkSubmitInfo2 *submits, VkFence fence)
{
    return vk_queue_submit_2(queue, submit_count, submits, fence, false);
}

VkResult wine_vkQueueSubmit2KHR(VkQueue queue, uint32_t submit_count, const VkSubmitInfo2 *submits, VkFence fence)
{
    return vk_queue_submit_2(queue, submit_count, submits, fence, true);
}

VkResult wine_vkQueuePresentKHR(VkQueue queue_handle, const VkPresentInfoKHR *present_info)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);
    VkPresentInfoKHR host_present_info = *present_info;
    struct wine_semaphore *semaphore;
    struct conversion_context ctx;
    unsigned int i;
    VkResult ret;

    TRACE("%p %p\n", queue_handle, present_info);

    for (i = 0; i < present_info->waitSemaphoreCount; ++i)
    {
        semaphore = wine_semaphore_from_handle(present_info->pWaitSemaphores[i]);

        if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
        {
            FIXME("Waiting on D3D12-Fence compatible timeline semaphore not supported.\n");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    if (is_virtual_queue(queue))
    {
        pthread_mutex_lock(&queue->submissions_mutex);
        while (queue->processing)
            pthread_cond_wait(&queue->submissions_cond, &queue->submissions_mutex);
        pthread_mutex_unlock(&queue->submissions_mutex);
    }

    init_conversion_context(&ctx);
    host_present_info.pWaitSemaphores = unwrap_semaphore_array(present_info->pWaitSemaphores, present_info->waitSemaphoreCount, &ctx);
    ret = fshack_vk_queue_present(queue_handle, &host_present_info);
    free_conversion_context(&ctx);
    return ret;
}

VkResult wine_vkQueueBindSparse(VkQueue queue_handle, uint32_t bind_info_count, const VkBindSparseInfo *bind_info, VkFence fence)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);
    struct wine_semaphore *semaphore;
    struct conversion_context ctx;
    VkBindSparseInfo *batch;
    unsigned int i, j, k;
    VkResult ret;

    TRACE("(%p, %u, %p, 0x%s)\n", queue, bind_info_count, bind_info, wine_dbgstr_longlong(fence));

    if (is_virtual_queue(queue))
    {
        FIXME("Can't process sparse bind calls on virtual queue, flushing.\n");
        pthread_mutex_lock(&queue->submissions_mutex);
        while (queue->processing)
            pthread_cond_wait(&queue->submissions_cond, &queue->submissions_mutex);
        pthread_mutex_unlock(&queue->submissions_mutex);
    }

    for (i = 0; i < bind_info_count; i++)
    {
        batch = (VkBindSparseInfo *)&bind_info[i];

        for (k = 0; k < batch->waitSemaphoreCount; k++)
        {
            semaphore = wine_semaphore_from_handle(batch->pWaitSemaphores[k]);

            if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
            {
                FIXME("Waiting on D3D12-Fence compatible timeline semaphore not supported.\n");
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }

        for(k = 0; k < batch->signalSemaphoreCount; k++)
        {
            semaphore = wine_semaphore_from_handle(batch->pSignalSemaphores[k]);

            if (semaphore->handle_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT)
            {
                FIXME("Signalling D3D12-Fence compatible timeline semaphore not supported.\n");
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }
    }

    init_conversion_context(&ctx);
    for (i = 0; i < bind_info_count; ++i)
    {
        batch = (VkBindSparseInfo *)&bind_info[i];
        batch->pWaitSemaphores = unwrap_semaphore_array(batch->pWaitSemaphores, batch->waitSemaphoreCount, &ctx);
        batch->pSignalSemaphores = unwrap_semaphore_array(batch->pSignalSemaphores, batch->signalSemaphoreCount, &ctx);

        duplicate_array_for_unwrapping(&ctx, (void **)&batch->pBufferBinds, batch->bufferBindCount * sizeof(*batch->pBufferBinds));
        for (j = 0; j < batch->bufferBindCount; ++j)
        {
            VkSparseBufferMemoryBindInfo *bind = (VkSparseBufferMemoryBindInfo *)&batch->pBufferBinds[j];
            duplicate_array_for_unwrapping(&ctx, (void **)&bind->pBinds, bind->bindCount * sizeof(*bind->pBinds));
            for (k = 0; k < bind->bindCount; ++k)
                if (bind->pBinds[k].memory)
                    ((VkSparseMemoryBind *)bind->pBinds)[k].memory = wine_device_memory_from_handle(bind->pBinds[k].memory)->memory;
        }

        duplicate_array_for_unwrapping(&ctx, (void **)&batch->pImageOpaqueBinds, batch->imageOpaqueBindCount * sizeof(*batch->pImageOpaqueBinds));
        for (j = 0; j < batch->imageOpaqueBindCount; ++j)
        {
            VkSparseImageOpaqueMemoryBindInfo *bind = (VkSparseImageOpaqueMemoryBindInfo *)&batch->pImageOpaqueBinds[j];
            duplicate_array_for_unwrapping(&ctx, (void **)&bind->pBinds, bind->bindCount * sizeof(*bind->pBinds));
            for (k = 0; k < bind->bindCount; ++k)
                if (bind->pBinds[k].memory)
                    ((VkSparseMemoryBind *)bind->pBinds)[k].memory = wine_device_memory_from_handle(bind->pBinds[k].memory)->memory;
        }

        duplicate_array_for_unwrapping(&ctx, (void **)&batch->pImageBinds, batch->imageBindCount * sizeof(*batch->pImageBinds));
        for (j = 0; j < batch->imageBindCount; ++j)
        {
            VkSparseImageMemoryBindInfo *bind = (VkSparseImageMemoryBindInfo *)&batch->pImageBinds[j];
            duplicate_array_for_unwrapping(&ctx, (void **)&bind->pBinds, bind->bindCount * sizeof(*bind->pBinds));
            for (k = 0; k < bind->bindCount; ++k)
                if (bind->pBinds[k].memory)
                    ((VkSparseImageMemoryBind *)bind->pBinds)[k].memory = wine_device_memory_from_handle(bind->pBinds[k].memory)->memory;
        }
    }
    ret = queue->device->funcs.p_vkQueueBindSparse(queue->queue, bind_info_count, bind_info, fence);
    free_conversion_context(&ctx);
    return ret;
}

VkResult wine_vkCreateFence(VkDevice device_handle, const VkFenceCreateInfo *create_info, const VkAllocationCallbacks *allocator, VkFence *fence)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_fence *object;
    VkResult vr;

    TRACE("(%p, %p, %p, %p)\n", device, create_info, allocator, fence);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!(object = calloc(1, sizeof(*object))))
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    if ((object->eventfd = eventfd(0, EFD_CLOEXEC)) == -1)
        ERR("Failed to create eventfd for fence.\n");

    if ((vr = device->funcs.p_vkCreateFence(device->device, create_info, NULL, &object->fence)) == VK_SUCCESS)
        *fence = wine_fence_to_handle(object);
    else
        free(object);

    return vr;
}

void wine_vkDestroyFence(VkDevice device_handle, VkFence fence_handle, const VkAllocationCallbacks *allocator)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct wine_fence *fence = wine_fence_from_handle(fence_handle);

    TRACE("(%p, %p, %p)\n", device, fence, allocator);

    if (allocator)
        FIXME("Support for allocation callbacks not implemented yet\n");

    if (!fence_handle)
        return;

    if (fence->eventfd != -1)
        close(fence->eventfd);

    device->funcs.p_vkDestroyFence(device->device, fence->fence, NULL);
    free(fence);
}

static VkFence *unwrap_fence_array(const VkFence *in, uint32_t count, struct conversion_context *ctx)
{
    VkFence *out;
    unsigned int i;

    if (!in || !count) return NULL;

    out = conversion_context_alloc(ctx, count * sizeof(*out));
    for (i = 0; i < count; ++i)
        out[i] = in[i] ? wine_fence_from_handle(in[i])->fence : VK_NULL_HANDLE;

    return out;
}

VkResult wine_vkResetFences(VkDevice device_handle, uint32_t fence_count, const VkFence *fences)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct conversion_context ctx;
    struct wine_fence *fence;
    VkFence *fences_unwrap;
    unsigned int i;
    uint64_t buf;
    VkResult vr;

    TRACE("(%p, %u, %p)\n", device, fence_count, fences);

    init_conversion_context(&ctx);
    fences_unwrap = unwrap_fence_array(fences, fence_count, &ctx);
    vr = device->funcs.p_vkResetFences(device->device, fence_count, fences_unwrap);
    free_conversion_context(&ctx);
    if (vr)
        return vr;

    for (i = 0; i < fence_count; i++)
    {
        fence = wine_fence_from_handle(fences[i]);

        fence->queue = NULL;
        fence->swapchain = NULL;
        if (fence->wait_assist)
        {
            fence->wait_assist = false;
            if (read(fence->eventfd, &buf, sizeof(buf)) == -1)
                ERR("Failed to reset event fd.\n");
        }
    }

    return VK_SUCCESS;
}

VkResult wine_vkWaitForFences(VkDevice device_handle, uint32_t fence_count, const VkFence *fences,
        VkBool32 wait_all, uint64_t timeout)
{
    struct wine_device *device = wine_device_from_handle(device_handle);
    struct signal_op *signal_op;
    bool assisted_wait = false;
    struct wine_fence *fence;
    struct pollfd *wait_fds;
    struct pollfd wait_fd;
    unsigned int i;
    VkResult vr;
    int ret;

    TRACE("(%p, %u, %p, %u, 0x%s)\n", device, fence_count, fences, wait_all, wine_dbgstr_longlong(timeout));

    for (i = 0; i < fence_count; i++)
    {
        fence = wine_fence_from_handle(fences[i]);
        if (!fence->wait_assist)
            continue;

        if (!wait_all && fence_count > 1)
        {
            assisted_wait = true;
            break;
        }

        wait_fd.fd = fence->eventfd;
        wait_fd.events = POLLIN;
        ret = poll(&wait_fd, 1, timeout / 1000000);
        if (ret == -1)
        {
            ERR("Failed to poll wait assisted fence.\n");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        if (!ret)
            return VK_TIMEOUT;

        if (wait_fd.revents & (POLLERR | POLLHUP | POLLNVAL))
            ERR("Polling on fd %d returned %#x.", fence->eventfd, wait_fd.revents);
        return VK_SUCCESS;
    }

    if (assisted_wait)
    {
        /* Turn all non assisted waits into assisted waits, then poll on all */
        wait_fds = malloc( sizeof(wait_fds[0]) * fence_count );

        for (i = 0; i < fence_count; i++)
        {
            if (!fence->wait_assist)
            {
                assert(fence->queue || fence->swapchain);

                if (fence->queue)
                {
                    fence->wait_assist = true;

                    /* If virtual-queue requiring work was submitted after the work signalling this mutex,
                     * we will end up unnecessarily waiting on that work first,
                     * but this will only happen once per queue */
                    init_virtual_queue(fence->queue);

                    signal_op = malloc(sizeof(*signal_op));
                    signal_op->signal_type = SIGNAL_TYPE_FENCE;
                    signal_op->fence = fence;

                    pthread_mutex_lock(&fence->queue->signaller_mutex);
                    list_add_tail(&fence->queue->signal_ops, &signal_op->entry);
                    pthread_cond_signal(&fence->queue->signaller_cond);
                    pthread_mutex_unlock(&fence->queue->signaller_mutex);
                }
                else
                {
                    FIXME("Wait assist for swapchain signaled fences not supported.\n");
                    free(wait_fds);
                    return VK_ERROR_OUT_OF_HOST_MEMORY;
                }
            }

            wait_fds[i].fd = fence->eventfd;
            wait_fds[i].events = POLLIN;
        }

        if (poll(wait_fds, fence_count, timeout / 1000000) == -1)
        {
            ERR("Failed to poll wait assisted fences.\n");
            vr = VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        free(wait_fds);
    }
    else
    {
        struct conversion_context ctx;
        VkFence *fences_unwrap;

        init_conversion_context(&ctx);
        fences_unwrap = unwrap_fence_array(fences, fence_count, &ctx);
        vr = device->funcs.p_vkWaitForFences(device->device, fence_count, fences_unwrap, wait_all, timeout);
        free_conversion_context(&ctx);
    }

    return vr;
}

VkResult wine_vkQueueWaitIdle(VkQueue queue_handle)
{
    struct wine_queue *queue = wine_queue_from_handle(queue_handle);

    TRACE("(%p)\n", queue);

    if (is_virtual_queue(queue))
    {
        pthread_mutex_lock(&queue->submissions_mutex);
        while (queue->processing)
            pthread_cond_wait(&queue->submissions_cond, &queue->submissions_mutex);
        pthread_mutex_unlock(&queue->submissions_mutex);
    }

    return queue->device->funcs.p_vkQueueWaitIdle(queue->queue);
}
