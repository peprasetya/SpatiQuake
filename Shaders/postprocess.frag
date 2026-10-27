#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	float gamma;
	float contrast;
	float keep_alpha; // 1 for a picture that is a layer over others (OpenXR's 2D), 0 for the screen
}
push_constants;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec4 input_color = subpassLoad (color_input);
	vec3 frag = input_color.rgb * push_constants.contrast;
	out_frag_color = vec4 (pow (frag, vec3 (push_constants.gamma)), mix (1.0, input_color.a, push_constants.keep_alpha));
}
