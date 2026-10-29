/**
 * @file    gpu/backend/frame_double.h
 * @brief   nights-nx (2026-10-29): 60 frames a second on the screen while the game runs at its own 30.
 *
 * NiGHTS ties its game speed to its frame rate and presents at interval 2 (30 frames a second). With
 * nights_present_60 on, the render thread keeps what it drew for a game frame (every draw with its device snapshot,
 * every clear, resolve and tiling change, as they ran) and draws the frame again with each draw's vertex shader
 * constants moved along their path from the previous frame's matching draw: predicting (mode 0) it shows the game's
 * frame and, half a frame later, the frame half a frame on (c + (c - p) / 2); interpolating (mode 1) it shows the
 * frame halfway from the one before ((c + p) / 2) and, half a frame later, the game's own frame drawn again. When the
 * game's frames come late with it (the render thread cannot do both), it pauses (2 s; again soon after: up to 32 s). The game,
 * its timing and its input are untouched; only the screen gets the extra frame. Changes the picture: off by default
 * (the user's decision).
 *
 * What moves: whatever the vertex shader constants carry (the camera, the objects' and the bones' matrices). What does
 * not: geometry the game writes itself every frame (particles, Draw*UP), pixel shader constants, textures. A register
 * that jumped (a camera cut, a flag, a scrolling offset wrapping round) keeps its value. Predicting, a register's step
 * is scaled by how steady its motion is over the last three frames (all of it when steady, part in a turn or a change
 * of speed, none for an animation looping back to its first pose: the snaps of the first console test); interpolating
 * needs no such test (nothing overshoots). A draw moves whole only when it
 * is the one draw of its kind in both frames; a repeated draw (the balls of a ring, the chips: the same mesh and
 * shaders many times, in an order and number that change) moves only the registers its vertex shader has the same in
 * all its draws (the camera and the like), since it cannot be paired with its own draw of the frame before. A frame
 * that took more than 150 ms (loading, a stall) gets no extra frame.
 */
#pragma once

#include <chrono>
#include <functional>
#include <string>

#include <rex/types.h>

#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/render/draw.h"

namespace renut::gpu::frame_double {

// nights_present_60, read once.
bool On();

// The render thread's work as it runs (backend/queued.cpp, d3d/hooks/draw.cpp), State().mutex held.
void LogDraw(const u8 *device_state, const DrawCall &call, const u8 *vertices, u32 vertex_bytes, const u8 *indices,
             u32 index_bytes);
void LogClear(const u8 *device_state, u32 flags, const ClearRect *rect, const float color[4], float depth,
              u32 stencil);
void LogResolve(const u8 *device_state, u32 flags, const ClearRect *source_rect, u32 dest_texture_va,
                const d3d::TextureFetchConstant *dest_format, i32 dest_x, i32 dest_y, u32 dest_level,
                u32 dest_slice, const float clear_color[4], float clear_depth, u32 clear_stencil);
void LogTiling(u32 width, u32 height, const ClearRect *tiles, u32 count);

// What a game frame gets (nights_present_60_mode). kPredict: the game's frame, then the frame predicted half a
// frame on (c + (c - p) / 2), two renders a game frame, no added latency. kInterpolate: the frame halfway between the
// one before and this one ((c + p) / 2), then this one drawn again: three renders, the game's frame shown half a frame
// later. kNone: the game's frame alone (no frame before to match, or a frame that took over 150 ms).
enum class Plan { kNone, kPredict, kInterpolate };
enum class Shift { kPredict, kMiddle, kExact };

// The swap's item, before its present (State().mutex not held): measures the game frame and says what it gets.
Plan PlanFrame();
// Runs the frame's log again with its vertex shader constants shifted (State().mutex not held; takes it).
void Draw(Shift shift);
// The game frame's second present, due half a game frame after PlanFrame. The render thread goes on with the next
// frame's work meanwhile and presents it between two items when it is due (render_thread.cpp), or at the next
// frame's swap at the latest (it never waits for it).
void SchedulePresent(std::function<void()> present);
bool PresentPending();
std::chrono::steady_clock::time_point PresentDue();
// Presents the scheduled frame when it is due (or now, with 'force'). State().mutex must not be held.
void PresentIfDue(bool force = false);
// The frame's log becomes the previous one; a new one starts.
void EndFrame();

// nights-nx (2026-10-29, nights_diag_camera_frames): the camera's registers frame by frame to camera_diag.bin; every
// draw (backend/queued.cpp) and the frame's end (the swap's item), on the render thread.
bool CameraDiagOn();
void CameraDiagDraw(const u8 *device_state);
void CameraDiagEndFrame();

}  // namespace renut::gpu::frame_double
