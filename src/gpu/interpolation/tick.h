#pragma once

// The logic tick of the engine at a free frame rate (state interpolation),
// ported from Rise of a Ninja (gpu/interpolation/tick.*) to Broken Bond's main
// loop.
//
// The main loop (827F7708) calls, once per presented frame, the pending world
// change (827F7668) and the main step (827F73F0): the time update 82BC8FA8,
// the services, the online match, the scripts and the world, the effects' post
// (8277CA38), then the sound manager (827BCE88, which advances by the dt) and
// the view block (the view start 827F6F38, the view 82BB9B28, the display
// 82BD9520, the effects' draw 82BECF98 and the delivery 82BBADE0), and the end
// of the iteration (827EE0D0). Broken Bond has no HUD map call in the loop.
// The time structure ([833A30CC]) keeps Rise's fields: the fixed-step flag at
// +72 and the step at +76. The title's frame mode (82821510, chosen by the AI)
// writes them: 1/30 s with the presentation interval 2, 1/60 s with the
// interval 1, or the measured dt (+72 clear); the pause writes a step of 0.
// With the fixed step on, the logic runs at the period of +76, as on the
// console: the clock owes the real time to the game and a loop iteration is a
// tick when a period is due. An iteration without a tick runs only the view
// block of the original (no time update, no sound, no logic), so the frames
// between two ticks show the last tick again; the transforms are interpolated
// over them (later). Without the fixed step, or when the period is not longer
// than the frame (1/60 s at 60), every iteration is a tick.
//
// The option frame_rate above 30 (settings.h) turns the mode on at that rate;
// for the boots, REX_NATIVE_STATE_INTERPOLATION (1 on, 0 off) wins over it,
// with REX_NATIVE_FRAME_RATE for the rate (60 when neither gives one). The own
// limiter then paces at the rate (PaceRate: the tick rate when the gate is off
// under the fixed step) and the time step stays the title's. With the mode set
// by the variable, F8 switches it off and on at run time (off, the game runs
// as without the mode: the title's interval and step) and the window title
// shows it. Mode changes and every 300th iteration are
// recorded as native-d3d-tick. In the native mode, each change of the step,
// its flag or the title's interval is recorded as native-d3d-time-step, with
// the mode on or off. Main thread.
//
// The time of the view block: the display and the effects' draw advance some
// state by the engine's dt (+56 in microseconds, +64 scaled, +68 raw). The
// engine's time is updated only in the ticks, so between them the view block
// would see the tick's dt in every frame, twice per tick at 60 Hz. In the
// iterations the gate decides, from the view start (827F6F38) to the end of
// the delivery (82BBADE0), the three fields are scaled to the advance of the
// image's time (ticks + alpha) since the last view block, in periods: half the
// tick's dt per frame at 60 Hz, a quarter at 120 Hz; then the tick's values
// come back. The effects' work thread gets its dt as an argument (82BE45F0).
// REX_NATIVE_INTERPOLATION_VIEW_TIME=0 leaves the tick's dt; =tick gives the
// tick's dt in the ticks and 0 between them (a probe).
#include <cstdint>

namespace gpu::tick {
// The mode is on (frame_rate above 30, or REX_NATIVE_STATE_INTERPOLATION=1),
// whatever F8 says.
bool Enabled();
// On and not switched off by F8: the limiter paces at the frame rate.
bool Owning();
// REX_NATIVE_FRAME_RATE (30 to 240), else frame_rate above 30, else 60.
uint32_t FrameRate();
// The rate the limiter paces at while Owning(), any thread: FrameRate() when
// the gate decides or the engine measures its dt; the tick rate (1 / +76)
// when the fixed step is on and the gate is off, since every iteration is
// then a tick at the title's step, and a frame rate below the tick's or within
// a quarter of it would slow the game down or speed it up (1/60 s at 72).
uint32_t PaceRate();
// The current loop iteration.
struct Frame {
  uint64_t index = 0;     // Loop iterations of the mode.
  bool gate = false;      // The gate decides (fixed step longer than the frame).
  bool tick = true;       // The logic runs in this iteration.
  float period = 0.0f;    // The tick period (+76), seconds.
  float alpha = 0.0f;     // This frame between the last two ticks, 0 to 1.
  uint64_t ticks = 0;     // Ticks of the gated iterations.
  uint64_t skipped = 0;   // Gated iterations without a tick.
};
const Frame& Current();
// Inside the main step of an iteration the gate decides.
bool InGatedStep();
// The mode comes from REX_NATIVE_STATE_INTERPOLATION: the keys of the
// comparisons (F8 here, F7 in transforms.h) act.
bool ByVariable();
// The label of the window title; empty when the mode is off or set by the option.
const char* Label();
// The view start (827F6F38) and the delivery's end (82BBADE0) of the main
// step, main thread: the engine's dt of the view block (above).
void BeginView(uint8_t* base);
void EndView(uint8_t* base);
// PC speed up (pc/game_speed.h), any thread: the game runs this many times
// faster, 1 normally. While above 1 every iteration is a tick (no gate) whose
// fixed step is lengthened up to 1/15 s, and the own limiter paces the rest
// of the factor faster (PaceSpeed); vertical sync or a slow frame caps that
// part.
void SetSpeed(float speed);
float Speed();
float PaceSpeed();
// PC free camera (pc/free_camera.h), any thread: while frozen, no iteration
// is a tick; the view block runs alone with the engine's dt at 0, so the
// world stands still and only the camera moves.
void SetFrozen(bool frozen);
bool Frozen();
}  // namespace gpu::tick
