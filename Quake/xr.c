/*
Copyright (C) 2026 SpatiQuake contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// xr.c -- the OpenXR mode

#include "quakedef.h"

#ifdef USE_OPENXR

#define XR_NO_PROTOTYPES
#define RAD2DEG(a) ((a) / M_PI_DIV_180)
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined(_WIN32)
#define XR_LOADER_LIBRARY "openxr_loader.dll"
#elif defined(__APPLE__)
#define XR_LOADER_LIBRARY "libopenxr_loader.dylib"
#else
#define XR_LOADER_LIBRARY "libopenxr_loader.so.1"
#endif

cvar_t		  xr_worldscale = {"xr_worldscale", "32", CVAR_ARCHIVE};	  // Quake units in a metre
static cvar_t xr_hud_distance = {"xr_hud_distance", "1.5", CVAR_ARCHIVE}; // metres in front of the head
static cvar_t xr_hud_size = {"xr_hud_size", "0.85", CVAR_ARCHIVE};		  // of the eyes' field across

// The entry points, all found through xrGetInstanceProcAddr.
#define XR_FUNCTIONS                               \
	XR_FN (xrEnumerateInstanceExtensionProperties) \
	XR_FN (xrCreateInstance)                       \
	XR_FN (xrDestroyInstance)                      \
	XR_FN (xrGetInstanceProperties)                \
	XR_FN (xrPollEvent)                            \
	XR_FN (xrResultToString)                       \
	XR_FN (xrGetSystem)                            \
	XR_FN (xrGetSystemProperties)                  \
	XR_FN (xrEnumerateViewConfigurationViews)      \
	XR_FN (xrGetVulkanGraphicsRequirements2KHR)    \
	XR_FN (xrCreateVulkanInstanceKHR)              \
	XR_FN (xrGetVulkanGraphicsDevice2KHR)          \
	XR_FN (xrCreateVulkanDeviceKHR)                \
	XR_FN (xrCreateSession)                        \
	XR_FN (xrDestroySession)                       \
	XR_FN (xrBeginSession)                         \
	XR_FN (xrEndSession)                           \
	XR_FN (xrCreateReferenceSpace)                 \
	XR_FN (xrDestroySpace)                         \
	XR_FN (xrLocateViews)                          \
	XR_FN (xrEnumerateSwapchainFormats)            \
	XR_FN (xrCreateSwapchain)                      \
	XR_FN (xrDestroySwapchain)                     \
	XR_FN (xrEnumerateSwapchainImages)             \
	XR_FN (xrAcquireSwapchainImage)                \
	XR_FN (xrWaitSwapchainImage)                   \
	XR_FN (xrReleaseSwapchainImage)                \
	XR_FN (xrWaitFrame)                            \
	XR_FN (xrBeginFrame)                           \
	XR_FN (xrEndFrame)

#define XR_FN(name) static PFN_##name q##name;
XR_FUNCTIONS
#undef XR_FN

static void						*xr_library;
static PFN_xrGetInstanceProcAddr qxrGetInstanceProcAddr;

static qboolean	  xr_enabled;
static XrInstance xr_instance = XR_NULL_HANDLE;
static XrSystemId xr_system = XR_NULL_SYSTEM_ID;
static XrSession  xr_session = XR_NULL_HANDLE;
static XrSpace	  xr_local_space = XR_NULL_HANDLE;
static XrSpace	  xr_view_space = XR_NULL_HANDLE;
static qboolean	  xr_session_running;
static SDL_Mutex *xr_queue_mutex;

static uint32_t xr_eye_width, xr_eye_height;

typedef struct
{
	XrSwapchain handle;
	uint32_t	image_count;
	uint32_t	first_image; // in the list handed to the engine
	VkImage		images[8];
	qboolean	acquired;
	uint32_t	acquired_index;
	qboolean	drawn; // released with a picture this frame
} xr_swapchain_t;

static xr_swapchain_t xr_swapchains[XR_TARGET_COUNT];
static int			  xr_swapchain_width, xr_swapchain_height;

static XrFrameState xr_frame_state;
static qboolean		xr_frame_open;
static XrView		xr_views[2];
static qboolean		xr_views_valid;
static int			xr_drawing_eye = -1;

static qboolean XR_Check (XrResult result, const char *what)
{
	if (XR_SUCCEEDED (result))
		return true;
	char text[XR_MAX_RESULT_STRING_SIZE] = "";
	if (qxrResultToString && xr_instance != XR_NULL_HANDLE)
		qxrResultToString (xr_instance, result, text);
	Con_Printf ("OpenXR: %s failed: %s (%d)\n", what, text[0] ? text : "?", (int)result);
	return false;
}

static void XR_UnloadLibrary (void)
{
	if (xr_library)
		SDL_UnloadObject (xr_library);
	xr_library = NULL;
	qxrGetInstanceProcAddr = NULL;
}

static qboolean XR_LoadGlobal (void)
{
	xr_library = SDL_LoadObject (XR_LOADER_LIBRARY);
	if (!xr_library)
	{
		Con_Printf ("OpenXR: no loader (%s)\n", XR_LOADER_LIBRARY);
		return false;
	}
	qxrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)SDL_LoadFunction (xr_library, "xrGetInstanceProcAddr");
	if (!qxrGetInstanceProcAddr)
	{
		XR_UnloadLibrary ();
		return false;
	}
	qxrGetInstanceProcAddr (XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction *)&qxrEnumerateInstanceExtensionProperties);
	qxrGetInstanceProcAddr (XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&qxrCreateInstance);
	return qxrEnumerateInstanceExtensionProperties && qxrCreateInstance;
}

static qboolean XR_LoadInstanceFunctions (void)
{
	qboolean ok = true;
#define XR_FN(name)                                                                                  \
	if (!XR_SUCCEEDED (qxrGetInstanceProcAddr (xr_instance, #name, (PFN_xrVoidFunction *)&q##name))) \
	{                                                                                                \
		Con_Printf ("OpenXR: no %s\n", #name);                                                       \
		ok = false;                                                                                  \
	}
	XR_FUNCTIONS
#undef XR_FN
	return ok;
}

/*
===============
XR_Init
===============
*/
qboolean XR_Init (void)
{
	Cvar_RegisterVariable (&xr_worldscale);
	Cvar_RegisterVariable (&xr_hud_distance);
	Cvar_RegisterVariable (&xr_hud_size);

	if (COM_CheckParm ("-noxr"))
		return false;

	if (!XR_LoadGlobal ())
	{
		return false;
	}

	uint32_t count = 0;
	qboolean has_vulkan2 = false;
	if (XR_SUCCEEDED (qxrEnumerateInstanceExtensionProperties (NULL, 0, &count, NULL)) && count > 0)
	{
		XrExtensionProperties *props = Mem_Alloc (sizeof (XrExtensionProperties) * count);
		for (uint32_t i = 0; i < count; ++i)
			props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		if (XR_SUCCEEDED (qxrEnumerateInstanceExtensionProperties (NULL, count, &count, props)))
			for (uint32_t i = 0; i < count; ++i)
				if (!strcmp (props[i].extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
					has_vulkan2 = true;
		Mem_Free (props);
	}
	if (!has_vulkan2)
	{
		Con_Printf ("OpenXR: no runtime with %s\n", XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
		XR_UnloadLibrary ();
		return false;
	}

	const char			*extensions[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
	XrInstanceCreateInfo info = {XR_TYPE_INSTANCE_CREATE_INFO};
	q_strlcpy (info.applicationInfo.applicationName, "SpatiQuake", sizeof (info.applicationInfo.applicationName));
	info.applicationInfo.applicationVersion = 1;
	q_strlcpy (info.applicationInfo.engineName, "vkQuake", sizeof (info.applicationInfo.engineName));
	info.applicationInfo.engineVersion = 1;
	info.applicationInfo.apiVersion = XR_MAKE_VERSION (1, 0, 0);
	info.enabledExtensionCount = countof (extensions);
	info.enabledExtensionNames = extensions;
	XrResult result = qxrCreateInstance (&info, &xr_instance);
	if (!XR_SUCCEEDED (result) || !XR_LoadInstanceFunctions ())
	{
		Con_Printf ("OpenXR: xrCreateInstance failed (%d)\n", (int)result);
		XR_Shutdown ();
		return false;
	}

	XrInstanceProperties instance_props = {XR_TYPE_INSTANCE_PROPERTIES};
	if (XR_SUCCEEDED (qxrGetInstanceProperties (xr_instance, &instance_props)))
		Con_Printf (
			"OpenXR runtime: %s %u.%u.%u\n", instance_props.runtimeName, XR_VERSION_MAJOR (instance_props.runtimeVersion),
			XR_VERSION_MINOR (instance_props.runtimeVersion), XR_VERSION_PATCH (instance_props.runtimeVersion));

	XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!XR_Check (qxrGetSystem (xr_instance, &system_info, &xr_system), "xrGetSystem"))
	{
		XR_Shutdown ();
		return false;
	}

	XrViewConfigurationView views[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
	count = 0;
	if (!XR_Check (
			qxrEnumerateViewConfigurationViews (xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views),
			"xrEnumerateViewConfigurationViews") ||
		count != 2)
	{
		XR_Shutdown ();
		return false;
	}
	xr_eye_width = views[0].recommendedImageRectWidth;
	xr_eye_height = views[0].recommendedImageRectHeight;
	Con_Printf ("OpenXR: %u x %u per eye\n", xr_eye_width, xr_eye_height);

	// Asked before the session, as XR_KHR_vulkan_enable2 requires.
	XrGraphicsRequirementsVulkan2KHR requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
	if (!XR_Check (qxrGetVulkanGraphicsRequirements2KHR (xr_instance, xr_system, &requirements), "xrGetVulkanGraphicsRequirements2KHR"))
	{
		XR_Shutdown ();
		return false;
	}

	xr_enabled = true;
	return true;
}

qboolean XR_Enabled (void)
{
	return xr_enabled;
}

void XR_Shutdown (void)
{
	if (xr_session != XR_NULL_HANDLE)
	{
		if (xr_session_running)
			qxrEndSession (xr_session);
		XR_DestroySwapchains ();
		if (xr_local_space != XR_NULL_HANDLE)
			qxrDestroySpace (xr_local_space);
		if (xr_view_space != XR_NULL_HANDLE)
			qxrDestroySpace (xr_view_space);
		qxrDestroySession (xr_session);
	}
	xr_local_space = xr_view_space = XR_NULL_HANDLE;
	xr_session = XR_NULL_HANDLE;
	xr_session_running = false;
	if (xr_instance != XR_NULL_HANDLE && qxrDestroyInstance)
		qxrDestroyInstance (xr_instance);
	xr_instance = XR_NULL_HANDLE;
	xr_enabled = false;
	XR_UnloadLibrary ();
}

/*
===============
Vulkan through the runtime
===============
*/
VkResult XR_CreateVulkanInstance (PFN_vkGetInstanceProcAddr get_proc_addr, const VkInstanceCreateInfo *info, VkInstance *instance)
{
	XrVulkanInstanceCreateInfoKHR create_info = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
	create_info.systemId = xr_system;
	create_info.pfnGetInstanceProcAddr = get_proc_addr;
	create_info.vulkanCreateInfo = info;
	VkResult vk_result = VK_ERROR_INITIALIZATION_FAILED;
	if (!XR_Check (qxrCreateVulkanInstanceKHR (xr_instance, &create_info, instance, &vk_result), "xrCreateVulkanInstanceKHR"))
		return vk_result != VK_SUCCESS ? vk_result : VK_ERROR_INITIALIZATION_FAILED;
	return vk_result;
}

VkPhysicalDevice XR_GetVulkanPhysicalDevice (VkInstance instance)
{
	XrVulkanGraphicsDeviceGetInfoKHR info = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
	info.systemId = xr_system;
	info.vulkanInstance = instance;
	VkPhysicalDevice device = VK_NULL_HANDLE;
	if (!XR_Check (qxrGetVulkanGraphicsDevice2KHR (xr_instance, &info, &device), "xrGetVulkanGraphicsDevice2KHR"))
		Sys_Error ("OpenXR: the runtime named no Vulkan device");
	return device;
}

VkResult XR_CreateVulkanDevice (PFN_vkGetInstanceProcAddr get_proc_addr, VkPhysicalDevice physical_device, const VkDeviceCreateInfo *info, VkDevice *device)
{
	XrVulkanDeviceCreateInfoKHR create_info = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
	create_info.systemId = xr_system;
	create_info.pfnGetInstanceProcAddr = get_proc_addr;
	create_info.vulkanPhysicalDevice = physical_device;
	create_info.vulkanCreateInfo = info;
	VkResult vk_result = VK_ERROR_INITIALIZATION_FAILED;
	if (!XR_Check (qxrCreateVulkanDeviceKHR (xr_instance, &create_info, device, &vk_result), "xrCreateVulkanDeviceKHR"))
		return vk_result != VK_SUCCESS ? vk_result : VK_ERROR_INITIALIZATION_FAILED;
	return vk_result;
}

void XR_CreateSession (VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, SDL_Mutex *queue_mutex)
{
	XrGraphicsBindingVulkan2KHR binding = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
	binding.instance = instance;
	binding.physicalDevice = physical_device;
	binding.device = device;
	binding.queueFamilyIndex = queue_family;
	binding.queueIndex = 0;

	XrSessionCreateInfo info = {XR_TYPE_SESSION_CREATE_INFO};
	info.next = &binding;
	info.systemId = xr_system;
	if (!XR_Check (qxrCreateSession (xr_instance, &info, &xr_session), "xrCreateSession"))
		Sys_Error ("OpenXR: could not make a session");
	xr_queue_mutex = queue_mutex;

	XrReferenceSpaceCreateInfo space_info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	space_info.poseInReferenceSpace.orientation.w = 1.0f;
	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (!XR_Check (qxrCreateReferenceSpace (xr_session, &space_info, &xr_local_space), "xrCreateReferenceSpace (local)"))
		Sys_Error ("OpenXR: no local space");
	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!XR_Check (qxrCreateReferenceSpace (xr_session, &space_info, &xr_view_space), "xrCreateReferenceSpace (view)"))
		Sys_Error ("OpenXR: no view space");
	Con_Printf ("OpenXR: session made\n");
}

void XR_GetEyeSize (int *width, int *height)
{
	*width = (int)xr_eye_width;
	*height = (int)xr_eye_height;
}

/*
===============
Swapchains
===============
*/
qboolean XR_CreateSwapchains (int width, int height, VkFormat format)
{
	uint32_t count = 0;
	if (!XR_Check (qxrEnumerateSwapchainFormats (xr_session, 0, &count, NULL), "xrEnumerateSwapchainFormats"))
		return false;
	int64_t *formats = Mem_Alloc (sizeof (int64_t) * q_max (count, 1u));
	qxrEnumerateSwapchainFormats (xr_session, count, &count, formats);
	qboolean found = false;
	for (uint32_t i = 0; i < count; ++i)
		if (formats[i] == (int64_t)format)
			found = true;
	Mem_Free (formats);
	if (!found)
	{
		Con_Printf ("OpenXR: the runtime has no swapchain in format %d\n", (int)format);
		return false;
	}

	uint32_t first = 0;
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
	{
		xr_swapchain_t		 *sc = &xr_swapchains[t];
		XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
		info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
		info.format = format;
		info.sampleCount = 1;
		info.width = width;
		info.height = height;
		info.faceCount = 1;
		info.arraySize = 1;
		info.mipCount = 1;
		SDL_LockMutex (xr_queue_mutex);
		XrResult result = qxrCreateSwapchain (xr_session, &info, &sc->handle);
		SDL_UnlockMutex (xr_queue_mutex);
		if (!XR_Check (result, "xrCreateSwapchain"))
			return false;

		XrSwapchainImageVulkan2KHR images[countof (sc->images)];
		for (uint32_t i = 0; i < countof (images); ++i)
		{
			images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
			images[i].next = NULL;
		}
		count = 0;
		qxrEnumerateSwapchainImages (sc->handle, 0, &count, NULL);
		if (count == 0 || count > countof (images) ||
			!XR_Check (qxrEnumerateSwapchainImages (sc->handle, count, &count, (XrSwapchainImageBaseHeader *)images), "xrEnumerateSwapchainImages"))
			return false;
		sc->image_count = count;
		sc->first_image = first;
		for (uint32_t i = 0; i < count; ++i)
			sc->images[i] = images[i].image;
		first += count;
		sc->acquired = false;
	}
	xr_swapchain_width = width;
	xr_swapchain_height = height;
	Con_Printf ("OpenXR: swapchains %d x %d, %u images\n", width, height, first);
	return true;
}

void XR_DestroySwapchains (void)
{
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
	{
		xr_swapchain_t *sc = &xr_swapchains[t];
		if (sc->handle != XR_NULL_HANDLE)
		{
			SDL_LockMutex (xr_queue_mutex);
			qxrDestroySwapchain (sc->handle);
			SDL_UnlockMutex (xr_queue_mutex);
		}
		memset (sc, 0, sizeof (*sc));
	}
}

uint32_t XR_GetSwapchainImages (VkImage *images, uint32_t capacity)
{
	uint32_t n = 0;
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
		for (uint32_t i = 0; i < xr_swapchains[t].image_count && n < capacity; ++i)
			images[n++] = xr_swapchains[t].images[i];
	return n;
}

/*
===============
The session's state
===============
*/
static void XR_PollEvents (void)
{
	XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
	while (qxrPollEvent (xr_instance, &event) == XR_SUCCESS)
	{
		switch (event.type)
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;
			Con_DPrintf ("OpenXR: session state %d\n", (int)changed->state);
			switch (changed->state)
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo info = {XR_TYPE_SESSION_BEGIN_INFO};
				info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (XR_Check (qxrBeginSession (xr_session, &info), "xrBeginSession"))
				{
					xr_session_running = true;
					Con_Printf ("OpenXR: session running\n");
				}
				break;
			}
			case XR_SESSION_STATE_STOPPING:
				qxrEndSession (xr_session);
				xr_session_running = false;
				break;
			case XR_SESSION_STATE_EXITING:
			case XR_SESSION_STATE_LOSS_PENDING:
				xr_session_running = false;
				Cbuf_AddText ("quit\n");
				break;
			default:
				break;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			xr_session_running = false;
			Cbuf_AddText ("quit\n");
			break;
		default:
			break;
		}
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
		event.next = NULL;
	}
}

qboolean XR_SessionRunning (void)
{
	if (!xr_enabled || xr_session == XR_NULL_HANDLE)
		return false;
	XR_PollEvents ();
	return xr_session_running;
}

/*
===============
The frame
===============
*/
qboolean XR_BeginFrame (void)
{
	XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
	xr_frame_state.type = XR_TYPE_FRAME_STATE;
	xr_frame_state.next = NULL;
	if (!XR_Check (qxrWaitFrame (xr_session, &wait_info, &xr_frame_state), "xrWaitFrame"))
		return false;
	XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
	if (!XR_Check (qxrBeginFrame (xr_session, &begin_info), "xrBeginFrame"))
		return false;
	xr_frame_open = true;
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
		xr_swapchains[t].drawn = false;

	XrViewLocateInfo locate_info = {XR_TYPE_VIEW_LOCATE_INFO};
	locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate_info.displayTime = xr_frame_state.predictedDisplayTime;
	locate_info.space = xr_local_space;
	XrViewState view_state = {XR_TYPE_VIEW_STATE};
	uint32_t	count = 0;
	xr_views[0].type = xr_views[1].type = XR_TYPE_VIEW;
	xr_views[0].next = xr_views[1].next = NULL;
	xr_views_valid = XR_SUCCEEDED (qxrLocateViews (xr_session, &locate_info, &view_state, 2, &count, xr_views)) && count == 2 &&
					 (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

	if (!xr_frame_state.shouldRender || !xr_views_valid)
	{
		XR_EndFrame ();
		return false;
	}
	return true;
}

uint32_t XR_AcquireTarget (xr_target_t target)
{
	xr_swapchain_t			   *sc = &xr_swapchains[target];
	XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	XrSwapchainImageWaitInfo	wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wait_info.timeout = XR_INFINITE_DURATION;
	SDL_LockMutex (xr_queue_mutex);
	XrResult result = qxrAcquireSwapchainImage (sc->handle, &acquire_info, &sc->acquired_index);
	if (XR_SUCCEEDED (result))
		result = qxrWaitSwapchainImage (sc->handle, &wait_info);
	SDL_UnlockMutex (xr_queue_mutex);
	if (!XR_Check (result, "xrAcquireSwapchainImage"))
		Sys_Error ("OpenXR: could not acquire a swapchain image");
	sc->acquired = true;
	return sc->first_image + sc->acquired_index;
}

void XR_ReleaseTarget (xr_target_t target)
{
	xr_swapchain_t			   *sc = &xr_swapchains[target];
	XrSwapchainImageReleaseInfo release_info = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	if (!sc->acquired)
		return;
	SDL_LockMutex (xr_queue_mutex);
	XR_Check (qxrReleaseSwapchainImage (sc->handle, &release_info), "xrReleaseSwapchainImage");
	SDL_UnlockMutex (xr_queue_mutex);
	sc->acquired = false;
	sc->drawn = true;
}

void XR_EndFrame (void)
{
	if (!xr_frame_open)
		return;

	XrCompositionLayerProjectionView	projection_views[2];
	XrCompositionLayerProjection		projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerQuad				quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
	const XrCompositionLayerBaseHeader *layers[2];
	uint32_t							layer_count = 0;

	if (xr_frame_state.shouldRender && xr_views_valid && xr_swapchains[XR_TARGET_LEFT].drawn && xr_swapchains[XR_TARGET_RIGHT].drawn)
	{
		for (int eye = 0; eye < 2; ++eye)
		{
			XrCompositionLayerProjectionView *view = &projection_views[eye];
			memset (view, 0, sizeof (*view));
			view->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			view->pose = xr_views[eye].pose;
			view->fov = xr_views[eye].fov;
			view->subImage.swapchain = xr_swapchains[eye].handle;
			view->subImage.imageRect.extent.width = xr_swapchain_width;
			view->subImage.imageRect.extent.height = xr_swapchain_height;
		}
		projection.space = xr_local_space;
		projection.viewCount = 2;
		projection.views = projection_views;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&projection;
	}

	if (xr_frame_state.shouldRender && xr_views_valid && xr_swapchains[XR_TARGET_UI].drawn)
	{
		// In front of the head and sized to a share of the eyes' field across, so all of it is in
		// view on glasses with a narrow field.
		const float distance = q_max (0.1f, xr_hud_distance.value);
		const float half_fov = q_max (-xr_views[0].fov.angleLeft, xr_views[1].fov.angleRight);
		const float width = 2.0f * distance * tanf (half_fov) * CLAMP (0.1f, xr_hud_size.value, 1.0f);
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.space = xr_view_space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = xr_swapchains[XR_TARGET_UI].handle;
		quad.subImage.imageRect.extent.width = xr_swapchain_width;
		quad.subImage.imageRect.extent.height = xr_swapchain_height;
		quad.pose.orientation.w = 1.0f;
		quad.pose.position.z = -distance;
		quad.size.width = width;
		quad.size.height = width * (float)xr_swapchain_height / (float)xr_swapchain_width;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}

	XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};
	end_info.displayTime = xr_frame_state.predictedDisplayTime;
	end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end_info.layerCount = layer_count;
	end_info.layers = layers;
	SDL_LockMutex (xr_queue_mutex);
	XrResult result = qxrEndFrame (xr_session, &end_info);
	SDL_UnlockMutex (xr_queue_mutex);
	XR_Check (result, "xrEndFrame");
	xr_frame_open = false;
}

/*
===============
Views

OpenXR's axes are x right, y up, -z forward; Quake's are x forward, y left, z up.
===============
*/
static void XR_ToQuake (const XrVector3f *v, vec3_t out)
{
	out[0] = -v->z;
	out[1] = -v->x;
	out[2] = v->y;
}

static void XR_Rotate (const XrQuaternionf *q, const XrVector3f *v, XrVector3f *out)
{
	// v + 2w(q x v) + 2 q x (q x v)
	const float tx = 2.0f * (q->y * v->z - q->z * v->y);
	const float ty = 2.0f * (q->z * v->x - q->x * v->z);
	const float tz = 2.0f * (q->x * v->y - q->y * v->x);
	out->x = v->x + q->w * tx + (q->y * tz - q->z * ty);
	out->y = v->y + q->w * ty + (q->z * tx - q->x * tz);
	out->z = v->z + q->w * tz + (q->x * ty - q->y * tx);
}

static void XR_TurnYaw (vec3_t v, float yaw)
{
	const float s = sinf (DEG2RAD (yaw)), c = cosf (DEG2RAD (yaw));
	const float x = v[0] * c - v[1] * s;
	const float y = v[0] * s + v[1] * c;
	v[0] = x;
	v[1] = y;
}

// Quake angles (pitch down, yaw left, roll) of an orientation, turned by body_yaw.
static void XR_PoseAngles (const XrQuaternionf *q, float body_yaw, vec3_t angles)
{
	const XrVector3f forward_xr = {0.0f, 0.0f, -1.0f}, right_xr = {1.0f, 0.0f, 0.0f}, up_xr = {0.0f, 1.0f, 0.0f};
	XrVector3f		 f, r, u;
	vec3_t			 forward, right, up;
	XR_Rotate (q, &forward_xr, &f);
	XR_Rotate (q, &right_xr, &r);
	XR_Rotate (q, &up_xr, &u);
	XR_ToQuake (&f, forward);
	XR_ToQuake (&r, right);
	XR_ToQuake (&u, up);
	// Quake's AngleVectors: forward = (cp cy, cp sy, -sp), right.z = -sr cp, up.z = cr cp
	angles[PITCH] = RAD2DEG (-asinf (CLAMP (-1.0f, forward[2], 1.0f)));
	angles[YAW] = RAD2DEG (atan2f (forward[1], forward[0])) + body_yaw;
	angles[ROLL] = RAD2DEG (atan2f (-right[2], up[2]));
}

static void XR_PoseOffset (const XrVector3f *position, float body_yaw, vec3_t offset)
{
	XR_ToQuake (position, offset);
	VectorScale (offset, xr_worldscale.value, offset);
	XR_TurnYaw (offset, body_yaw);
}

void XR_SetDrawingEye (int eye)
{
	xr_drawing_eye = eye;
}

int XR_DrawingEye (void)
{
	return xr_drawing_eye;
}

void XR_GetEyeView (int eye, float body_yaw, vec3_t origin_offset, vec3_t angles, float *fov_x, float *fov_y)
{
	const XrView *view = &xr_views[eye];
	XR_PoseOffset (&view->pose.position, body_yaw, origin_offset);
	XR_PoseAngles (&view->pose.orientation, body_yaw, angles);
	// A symmetric field that holds the eye's own, for culling.
	*fov_x = RAD2DEG (2.0f * q_max (fabsf (view->fov.angleLeft), fabsf (view->fov.angleRight)));
	*fov_y = RAD2DEG (2.0f * q_max (fabsf (view->fov.angleUp), fabsf (view->fov.angleDown)));
}

qboolean XR_GetHeadView (float body_yaw, vec3_t origin_offset, vec3_t angles)
{
	if (!xr_views_valid)
		return false;
	XrVector3f centre = {
		0.5f * (xr_views[0].pose.position.x + xr_views[1].pose.position.x),
		0.5f * (xr_views[0].pose.position.y + xr_views[1].pose.position.y),
		0.5f * (xr_views[0].pose.position.z + xr_views[1].pose.position.z),
	};
	XR_PoseOffset (&centre, body_yaw, origin_offset);
	XR_PoseAngles (&xr_views[0].pose.orientation, body_yaw, angles);
	return true;
}

// The eye's own (asymmetric) field, in the form of GL_FrustumMatrix: reversed, infinite depth.
qboolean XR_EyeProjection (float matrix[16], float near_clip)
{
	if (xr_drawing_eye < 0)
		return false;
	const XrFovf *fov = &xr_views[xr_drawing_eye].fov;
	const float	  l = tanf (fov->angleLeft), r = tanf (fov->angleRight);
	const float	  u = tanf (fov->angleUp), d = tanf (fov->angleDown);
	memset (matrix, 0, 16 * sizeof (float));
	matrix[0 * 4 + 0] = 2.0f / (r - l);
	matrix[2 * 4 + 0] = (r + l) / (r - l);
	matrix[1 * 4 + 1] = -2.0f / (u - d);
	matrix[2 * 4 + 1] = -(u + d) / (u - d);
	matrix[2 * 4 + 3] = -1.0f;
	matrix[3 * 4 + 2] = near_clip;
	return true;
}

#else // !USE_OPENXR: built without OpenXR's headers, the engine is flat

cvar_t xr_worldscale = {"xr_worldscale", "32", CVAR_ARCHIVE};

qboolean XR_Init (void)
{
	Cvar_RegisterVariable (&xr_worldscale);
	return false;
}
qboolean XR_Enabled (void)
{
	return false;
}
void	 XR_Shutdown (void) {}
VkResult XR_CreateVulkanInstance (PFN_vkGetInstanceProcAddr get_proc_addr, const VkInstanceCreateInfo *info, VkInstance *instance)
{
	return VK_ERROR_INITIALIZATION_FAILED;
}
VkPhysicalDevice XR_GetVulkanPhysicalDevice (VkInstance instance)
{
	return VK_NULL_HANDLE;
}
VkResult XR_CreateVulkanDevice (PFN_vkGetInstanceProcAddr get_proc_addr, VkPhysicalDevice physical_device, const VkDeviceCreateInfo *info, VkDevice *device)
{
	return VK_ERROR_INITIALIZATION_FAILED;
}
void	 XR_CreateSession (VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, SDL_Mutex *queue_mutex) {}
void	 XR_GetEyeSize (int *width, int *height) {}
qboolean XR_CreateSwapchains (int width, int height, VkFormat format)
{
	return false;
}
void	 XR_DestroySwapchains (void) {}
uint32_t XR_GetSwapchainImages (VkImage *images, uint32_t capacity)
{
	return 0;
}
qboolean XR_SessionRunning (void)
{
	return false;
}
qboolean XR_BeginFrame (void)
{
	return false;
}
uint32_t XR_AcquireTarget (xr_target_t target)
{
	return 0;
}
void XR_ReleaseTarget (xr_target_t target) {}
void XR_EndFrame (void) {}
void XR_SetDrawingEye (int eye) {}
int	 XR_DrawingEye (void)
{
	return -1;
}
void	 XR_GetEyeView (int eye, float body_yaw, vec3_t origin_offset, vec3_t angles, float *fov_x, float *fov_y) {}
qboolean XR_EyeProjection (float matrix[16], float near_clip)
{
	return false;
}
qboolean XR_GetHeadView (float body_yaw, vec3_t origin_offset, vec3_t angles)
{
	return false;
}

#endif
