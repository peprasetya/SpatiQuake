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

#ifndef _XR_H_
#define _XR_H_

// OpenXR mode: the world drawn once per eye into the runtime's swapchains, the 2D on a quad
// layer in front of the head. Without a runtime (or with -noxr) none of this is active and the
// engine is the ordinary flat vkQuake.

typedef enum
{
	XR_TARGET_LEFT,
	XR_TARGET_RIGHT,
	XR_TARGET_UI,
	XR_TARGET_COUNT
} xr_target_t;

// Before the Vulkan instance: load the loader, make the XrInstance, find a head-mounted system.
// Returns false (and stays inactive) if there is none.
qboolean XR_Init (void);
qboolean XR_Enabled (void);
void	 XR_Shutdown (void);

// Vulkan through the runtime (XR_KHR_vulkan_enable2).
VkResult		 XR_CreateVulkanInstance (PFN_vkGetInstanceProcAddr get_proc_addr, const VkInstanceCreateInfo *info, VkInstance *instance);
VkPhysicalDevice XR_GetVulkanPhysicalDevice (VkInstance instance);
VkResult XR_CreateVulkanDevice (PFN_vkGetInstanceProcAddr get_proc_addr, VkPhysicalDevice physical_device, const VkDeviceCreateInfo *info, VkDevice *device);
void	 XR_CreateSession (VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, SDL_Mutex *queue_mutex);

// The size each eye (and the 2D layer) is drawn at.
void XR_GetEyeSize (int *width, int *height);

// Swapchains: one per target, all width x height in format. Their images, in target order, are
// handed out as one list (the engine's "swapchain" images while in the OpenXR mode).
qboolean XR_CreateSwapchains (int width, int height, VkFormat format);
void	 XR_DestroySwapchains (void);
uint32_t XR_GetSwapchainImages (VkImage *images, uint32_t capacity);

// The frame: XR_BeginFrame waits for the runtime and locates the eyes; if it returns false nothing
// is to be drawn this frame (the frame has been ended already). Each target is acquired, drawn and
// released in turn, then XR_EndFrame submits the layers.
qboolean XR_SessionRunning (void);
qboolean XR_BeginFrame (void);
uint32_t XR_AcquireTarget (xr_target_t target); // index into the XR_GetSwapchainImages list
void	 XR_ReleaseTarget (xr_target_t target);
void	 XR_EndFrame (void);

// Per-eye view, while an eye is being drawn (XR_SetDrawingEye). The origin offset is in Quake
// units, turned by body_yaw; angles are the eye's orientation with body_yaw added.
void	 XR_SetDrawingEye (int eye); // -1: not drawing an eye
int		 XR_DrawingEye (void);
void	 XR_GetEyeView (int eye, float body_yaw, vec3_t origin_offset, vec3_t angles, float *fov_x, float *fov_y);
qboolean XR_EyeProjection (float matrix[16], float near_clip);
qboolean XR_GetHeadView (float body_yaw, vec3_t origin_offset, vec3_t angles);

// Input: OpenXR's actions as the gamepad's keys and sticks; the aim apart from the view.
qboolean XR_InputActive (void); // the runtime's controllers are in use (SDL's gamepad is then ignored)
float	 XR_BodyYaw (void);
void	 XR_UpdateInput (void);
void	 XR_AdjustMove (usercmd_t *cmd);
void	 XR_ApplyBindings (void);

extern cvar_t xr_worldscale;

#endif
