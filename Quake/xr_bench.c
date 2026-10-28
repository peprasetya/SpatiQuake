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
// xr_bench.c -- what the OpenXR mode's frames cost

// xr_bench N: over the next N display frames, the GPU time of each picture (left eye, right eye,
// 2D, from timestamp queries), the CPU time of a frame's work, and the frames the display missed
// (two frames' display times further apart than the display's period). A summary is printed, and
// a line with the settings that matter is appended to xr_bench.txt in the game folder, so runs on
// one machine can be compared.

#include "quakedef.h"

#define XR_BENCH_MAX_FRAMES 36000 // ten minutes at 60 Hz

static int	  bench_frames_wanted; // 0: not benchmarking
static int	  bench_frames;
static int	  bench_late_frames;   // frames shown later than the one before them plus a period
static int	  bench_missed_frames; // display frames that went by with nothing new
static double bench_last_display_time;
static double bench_period_sum;
static float *bench_cpu_ms;
static float *bench_gpu_ms[XR_TARGET_COUNT];
static int	  bench_gpu_count[XR_TARGET_COUNT];

static void XR_Bench_Free (void)
{
	Mem_Free (bench_cpu_ms);
	bench_cpu_ms = NULL;
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
	{
		Mem_Free (bench_gpu_ms[t]);
		bench_gpu_ms[t] = NULL;
	}
	bench_frames_wanted = 0;
	gl_timing_wanted = false;
}

static int XR_Bench_CompareFloats (const void *a, const void *b)
{
	const float x = *(const float *)a, y = *(const float *)b;
	return (x > y) - (x < y);
}

typedef struct
{
	float avg, p95, max;
} bench_stats_t;

static bench_stats_t XR_Bench_Stats (float *samples, int count)
{
	bench_stats_t stats = {0, 0, 0};
	if (count <= 0)
		return stats;
	double sum = 0.0;
	for (int i = 0; i < count; ++i)
		sum += samples[i];
	qsort (samples, count, sizeof (float), XR_Bench_CompareFloats);
	stats.avg = (float)(sum / count);
	stats.p95 = samples[q_min (count - 1, (int)(count * 0.95f))];
	stats.max = samples[count - 1];
	return stats;
}

static void XR_Bench_Report (void)
{
	static const char *names[XR_TARGET_COUNT] = {"left eye", "right eye", "2D"};
	const double	   period_ms = bench_frames ? 1000.0 * bench_period_sum / bench_frames : 0.0;
	bench_stats_t	   gpu[XR_TARGET_COUNT];
	float			   gpu_total = 0.0f;

	for (int t = 0; t < XR_TARGET_COUNT; ++t)
	{
		gpu[t] = XR_Bench_Stats (bench_gpu_ms[t], bench_gpu_count[t]);
		gpu_total += gpu[t].avg;
	}
	const bench_stats_t cpu = XR_Bench_Stats (bench_cpu_ms, bench_frames);

	const char *map = cl.mapname[0] ? cl.mapname : "no map";
	Con_Printf (
		"xr_bench: %d frames in %s, display %.2f ms (%.0f Hz); %d late (%.1f%%), %d display frames missed\n", bench_frames, map, period_ms,
		period_ms > 0.0 ? 1000.0 / period_ms : 0.0, bench_late_frames, bench_frames ? 100.0 * bench_late_frames / bench_frames : 0.0, bench_missed_frames);
	Con_Printf ("  GPU ms         avg    p95    max\n");
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
		Con_Printf ("  %-10s  %6.2f %6.2f %6.2f\n", names[t], gpu[t].avg, gpu[t].p95, gpu[t].max);
	Con_Printf ("  all three   %6.2f\n", gpu_total);
	Con_Printf ("  CPU ms      %6.2f %6.2f %6.2f\n", cpu.avg, cpu.p95, cpu.max);

	// One line a run, with what decides the cost.
	const int	r_width = (int)Cvar_VariableValue ("r_width"), r_height = (int)Cvar_VariableValue ("r_height");
	const char *render = (r_width > 0 && r_height > 0) ? va ("%dx%d", r_width, r_height) : "full";
	FILE	   *f = fopen (va ("%s/xr_bench.txt", com_gamedir), "a");
	if (!f)
	{
		Con_Printf ("xr_bench: couldn't write %s/xr_bench.txt\n", com_gamedir);
		return;
	}
	fprintf (
		f,
		"map %s frames %d period_ms %.2f late %d missed %d gpu_left %.2f/%.2f gpu_right %.2f/%.2f gpu_2d %.2f/%.2f gpu_all %.2f cpu %.2f/%.2f"
		" | eye %dx%d render %s vid_fsaa %s r_ssao %s r_oit %s r_rtshadows %s r_dynamic %s r_particles %s xr_sharedlightmaps %s\n",
		map, bench_frames, period_ms, bench_late_frames, bench_missed_frames, gpu[0].avg, gpu[0].p95, gpu[1].avg, gpu[1].p95, gpu[2].avg, gpu[2].p95, gpu_total,
		cpu.avg, cpu.p95, vid.width, vid.height, render, Cvar_VariableString ("vid_fsaa"), Cvar_VariableString ("r_ssao"), Cvar_VariableString ("r_oit"),
		Cvar_VariableString ("r_rtshadows"), Cvar_VariableString ("r_dynamic"), Cvar_VariableString ("r_particles"),
		Cvar_VariableString ("xr_sharedlightmaps"));
	fclose (f);
	Con_Printf ("xr_bench: added to %s/xr_bench.txt\n", com_gamedir);
}

static void XR_Bench_f (void)
{
	if (!XR_Enabled ())
	{
		Con_Printf ("xr_bench: not in the OpenXR mode\n");
		return;
	}
	if (Cmd_Argc () < 2)
	{
		Con_Printf ("xr_bench <frames>: what the next frames cost, on the GPU (each eye, the 2D) and the CPU\n");
		return;
	}
	XR_Bench_Free ();
	const int frames = CLAMP (1, atoi (Cmd_Argv (1)), XR_BENCH_MAX_FRAMES);
	bench_cpu_ms = (float *)Mem_Alloc (sizeof (float) * frames);
	for (int t = 0; t < XR_TARGET_COUNT; ++t)
	{
		bench_gpu_ms[t] = (float *)Mem_Alloc (sizeof (float) * frames);
		bench_gpu_count[t] = 0;
	}
	bench_frames_wanted = frames;
	bench_frames = bench_late_frames = bench_missed_frames = 0;
	bench_last_display_time = bench_period_sum = 0.0;
	gl_timing_wanted = true;
	Con_Printf ("xr_bench: measuring %d frames\n", frames);
}

/*
==================
XR_Bench_GpuTime

A picture's GPU time came back (rs_gputime_us, rs_gputime_tag: the picture it is of).
==================
*/
void XR_Bench_GpuTime (void)
{
	if (!bench_frames_wanted || rs_gputime_tag < 0 || rs_gputime_tag >= XR_TARGET_COUNT || !rs_gputime_us)
		return;
	if (bench_gpu_count[rs_gputime_tag] < bench_frames_wanted)
		bench_gpu_ms[rs_gputime_tag][bench_gpu_count[rs_gputime_tag]++] = (float)rs_gputime_us / 1000.0f;
}

/*
==================
XR_Bench_Frame

A display frame was submitted; its work took cpu_seconds.
==================
*/
void XR_Bench_Frame (double cpu_seconds)
{
	if (!bench_frames_wanted)
		return;
	double display_time, period;
	if (!XR_FrameTiming (&display_time, &period))
		return;
	if (bench_last_display_time > 0.0 && period > 0.0 && (display_time - bench_last_display_time) > 1.5 * period)
	{
		++bench_late_frames;
		bench_missed_frames += (int)((display_time - bench_last_display_time) / period + 0.5) - 1;
	}
	bench_last_display_time = display_time;
	bench_period_sum += period;
	bench_cpu_ms[bench_frames++] = (float)(cpu_seconds * 1000.0);
	if (bench_frames >= bench_frames_wanted)
	{
		XR_Bench_Report ();
		XR_Bench_Free ();
	}
}

void XR_Bench_Init (void)
{
	Cmd_AddCommand ("xr_bench", XR_Bench_f);
}
