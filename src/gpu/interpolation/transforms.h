#pragma once

// The transforms between two logic ticks (state interpolation, tick.h), ported
// from Rise of a Ninja (gpu/interpolation/transforms.*) to Broken Bond.
//
// In the iterations the gate decides, the view block of the main step draws
// the objects and the camera between the last two ticks instead of the last
// one. At the view start of the main step (827F6F38 at its return address
// 827F75F4), in an iteration with a tick, the matrix of each object the last
// display mounted (82764A08: the 12-byte entries of the holder, flag 0x40000
// without 0x200004 at object+4), [object + 20] (16 floats, position at
// +48..+56), becomes the current transform, and the previous current one the
// previous; so does the camera, the matrix at the start of the view's entry
// (the 172-byte entries of world + 720, the same layout), at the display's
// camera (82BD9108, which builds the view's camera from it). Then, in every
// iteration of the gate, the transform at the tick's alpha (tick.h) is written
// over the live one (the objects before the view block, the camera before
// 82BD9108) and the live one comes back after the delivery of the main step
// (82BBADE0 at its return address 827F7654), unless something wrote the place
// meanwhile (but for the turned matrices below). The main step can run a
// nested view block (the online match's leave, 82BCD110 -> 82BC43B8), as can
// the loading thread (827F8AA0): the return addresses keep them out. The
// display copies the matrices into the frame's packet, as the bone copy
// (821549A8) does, so the render thread does not read them.
//
// An engine matrix has a type word at +64; with its scale flag the fourth
// column scales the rows, and without it the column blends as a scale of 1
// (when the blend scales a matrix whose tick does not, the flag is written
// with it and comes back too). The display may turn some matrices in place
// (billboards: in Rise the indicators over the characters), so the write does
// not come back; the logic of the next tick reads them. Where the view block
// only turned a written matrix, the tick's scale and position go back under
// the turn, and a frame without a tick writes the interpolated value under it
// again. REX_NATIVE_INTERPOLATION_TURNED=0 leaves them as the view block left
// them; with the mode set by the variable (tick.h), F7 switches it at run time.
//
// Rows of the rotation are interpolated linearly and keep the interpolated
// length; the position linearly. Cuts, without interpolation until the next
// tick: an object whose matrix moved more than kCutDistance or turned more
// than ~60 degrees in one tick, or whose matrix pointer changed; the camera
// when it moved 10 units or turned 30 degrees in one tick (the thresholds of
// 827E3E90) or while the game's cut counter 833A34C4 is set.
//
// The pose of the skinned meshes: the render of a visual object (82BAF318)
// copies, for each bone of a mesh, its matrix relative to the object into the
// frame's packet (82B97DE8 -> [display state + 64] = 821549A8); the render
// thread turns that array into the bone palette. The bone matrices are the
// global matrices of the bones' objects [bone + 20] (relative to [object +
// 20]) or matrices of the object's bone array, written by the logic in the
// tick. Around each copy (82B97DE8), the matrices it reads go at the tick's
// alpha, tracked by address with the same cuts as the objects, and come back
// right after it.
//
// On with the gate of tick.h (frame_rate above 30, or
// REX_NATIVE_STATE_INTERPOLATION=1);
// REX_NATIVE_INTERPOLATION_TRANSFORMS=0 leaves the frames between the ticks
// as the last tick (the gate alone); REX_NATIVE_INTERPOLATION_BONES=0 leaves
// the poses at the last tick. Every 300 view blocks of the gate the counts
// below are recorded as native-d3d-interpolation. Main thread.
#include <cstdint>

namespace gpu::interpolation {
bool Enabled();
// The last view block: the matrices written (objects), a camera entry written
// and a camera cut at the tick, and the object cuts of the last tick; the
// poses copied on the main thread, the bone matrices written, cut at the tick
// and lost (written outside the logic), and the copies of another thread (not
// interpolated); the wait for the effects' update of the tick before the
// block's writes (it runs on its work thread), and whether it hit its bound.
struct Applied {
  uint32_t objects = 0, object_cuts = 0;
  bool camera = false, camera_cut = false;
  uint32_t poses = 0, bones = 0, bone_cuts = 0, bones_lost = 0, pose_other_thread = 0;
  uint32_t effects_wait_us = 0;
  bool effects_timeout = false;
};
const Applied& LastApplied();
// Mounts (82764A08) seen from another thread than the view block's while
// collecting, since the start: not tracked.
uint32_t MountsOtherThread();
// A pointer the engine frees (82120A70), any thread: a freed object leaves the
// tracks before the next write. Broken Bond also frees through copies of that
// dispatch inlined in other functions; those are not seen. The tracks start
// again after a world load or a loading screen (which free in bulk), an object
// is captured only while it still carries the mount's flags, and the reads
// are guarded.
void NoteFree(uint32_t pointer);
}  // namespace gpu::interpolation
