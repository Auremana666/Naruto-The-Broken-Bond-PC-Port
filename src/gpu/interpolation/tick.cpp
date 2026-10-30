// The logic tick of the engine at a free frame rate (tick.h).
#include "gpu/interpolation/tick.h"
#include "gpu/interpolation/transforms.h"
#include "gpu/common/guest_memory.h"
#include "gpu/common/title_loading.h"
#include "gpu/d3d/d3d.h"
#include "gpu/d3d/frame.h"
#include "gpu/host/services.h"
#include "gpu/settings.h"
#include <windows.h>
#include <fmt/format.h>
void WriteIconValues();  // keyboard_prompts.cpp
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string_view>

extern "C" void __imp__sub_827F7668(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_827F73F0(PPCContext& __restrict, uint8_t*);
extern "C" void sub_827F6F38(PPCContext& __restrict, uint8_t*);
extern "C" void sub_82BB9B28(PPCContext& __restrict, uint8_t*);
extern "C" void sub_82BD9520(PPCContext& __restrict, uint8_t*);
extern "C" void sub_82BECF98(PPCContext& __restrict, uint8_t*);
extern "C" void sub_82BBADE0(PPCContext& __restrict, uint8_t*);

namespace gpu::tick {
namespace {
constexpr uint32_t kTimePointer = 0x833A30CC;
// The game device context: +448 the presentation interval of the frame mode.
constexpr uint32_t kContext = 0x833A376C;
// The view block of 827F73F0: +8 the world, +12 the view.
constexpr uint32_t kViews = 0x8336B2E4;
constexpr uint32_t kViewsOff = 0x833A34F0;  // Set while a world is replaced: no view.
// The world of the display: +720 the view entries (172 bytes each), +728 their count.
constexpr uint32_t kWorld = 0x833A3464;
constexpr uint32_t kWorldEffects = 0x833A3468;  // The argument of 82BECF98.

int enabled = -1;
bool by_variable = false;  // The mode set by REX_NATIVE_STATE_INTERPOLATION: F8 and the title's label.
uint32_t rate = 0;
bool switched_off = false, f8_down = false;
// The limiter's rate (PaceRate), decided on the main thread and read by the
// render thread; 0 before the first decision.
std::atomic<uint32_t> pace_rate{0};
std::atomic<float> speed{1.0f};
std::atomic<float> pace_speed{1.0f};  // PaceSpeed.
// The longest fixed step the speed up gives a tick (SpeedStep).
constexpr float kSpeedStepMax = 1.0f / 15.0f;
std::atomic<bool> frozen{false};
// Main thread.
Frame frame;
bool decided = false, started = false, in_step = false;
uint32_t frames_since_tick = 0;
int64_t frequency = 0, last = 0;
double debt = 0;  // Real time owed to the game, seconds.
uint64_t capped = 0;  // Iterations whose debt reached two periods (the game behind the real time).
int64_t window_start = 0;
uint64_t window_ticks = 0, window_iterations = 0;
// The time of the view block (tick.h): 0 off, 1 the image's advance, 2 the
// probe (the tick's dt in the ticks, 0 between them).
int view_time = -1;
struct EngineDt {
  uint64_t us = 0;  // +56, int64.
  float dt = 0.0f, raw = 0.0f;  // +64, +68.
};
EngineDt view_saved, view_set;
uint32_t view_address = 0;  // The time structure written, 0 when nothing is.
bool view_started = false;
double view_shown = 0;  // The image's time of the last view block, in periods.
double window_view_dt = 0, window_tick_dt = 0;  // The dt the view blocks saw and the ticks' dt, seconds.
// The step as last recorded (native-d3d-time-step).
struct Step {
  uint32_t fixed = 0;
  float period = 0.0f;
  uint32_t interval = 0;
  bool operator==(const Step&) const = default;
};
Step step_recorded{0xFFFFFFFFu, 0.0f, 0xFFFFFFFFu};
uint64_t step_iterations = 0, step_changes = 0;

uint64_t Get64(uint8_t* base, uint32_t a) { return (uint64_t(Get32(base, a)) << 32) | Get32(base, a + 4); }
void Put64(uint8_t* base, uint32_t a, uint64_t v) {
  Put32(base, a, uint32_t(v >> 32));
  Put32(base, a + 4, uint32_t(v));
}
EngineDt ReadDt(uint8_t* base, uint32_t time) {
  return {Get64(base, time + 56), GetFloat(base, time + 64), GetFloat(base, time + 68)};
}
void WriteDt(uint8_t* base, uint32_t time, const EngineDt& d) {
  Put64(base, time + 56, d.us);
  PutFloat(base, time + 64, d.dt);
  PutFloat(base, time + 68, d.raw);
}

int ViewTime() {
  if (view_time < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_VIEW_TIME");
    const std::string_view value = text ? text : "";
    view_time = !Enabled() || value == "0" ? 0 : value == "tick" ? 2 : 1;
  }
  return view_time;
}

void Record(const char* reason, int64_t now) {
  const double seconds = window_start && frequency ? double(now - window_start) / double(frequency) : 0.0;
  host::Record("native-d3d-tick", fmt::format(
      "\"reason\":\"{}\",\"iteration\":{},\"swap\":{},\"owning\":{},\"gate\":{},\"tick\":{},\"period\":{:.6f},"
      "\"alpha\":{:.4f},\"frame_rate\":{},\"pace_rate\":{},\"ticks\":{},\"skipped\":{},\"capped\":{},"
      "\"debt_ms\":{:.3f},\"window_s\":{:.3f},\"window_ticks_per_s\":{:.3f},\"window_iterations_per_s\":{:.3f},"
      "\"view_time\":{},\"window_view_dt_per_s\":{:.4f},\"window_tick_dt_per_s\":{:.4f}",
      reason, frame.index, swaps.load(std::memory_order_relaxed), Owning(), frame.gate, frame.tick, frame.period,
      frame.alpha, FrameRate(), PaceRate(), frame.ticks, frame.skipped, capped, debt * 1e3, seconds,
      seconds > 0 ? double(window_ticks) / seconds : 0.0, seconds > 0 ? double(window_iterations) / seconds : 0.0,
      ViewTime(), seconds > 0 ? window_view_dt / seconds : 0.0, seconds > 0 ? window_tick_dt / seconds : 0.0));
  window_start = now;
  window_ticks = window_iterations = 0;
  window_view_dt = window_tick_dt = 0;
}

// Once per loop iteration of the native mode, mode on or off: the step and the
// title's interval as the logic of the previous tick left them.
void NoteStep(uint8_t* base) {
  ++step_iterations;
  const uint32_t time = Get32(base, kTimePointer), context = Get32(base, kContext);
  const Step step{time ? Get32(base, time + 72) : 0u, time ? GetFloat(base, time + 76) : 0.0f,
                  context ? Get32(base, context + 448) : 0u};
  if (step == step_recorded) return;
  step_recorded = step;
  ++step_changes;
  const bool finite = std::isfinite(step.period);
  host::Record("native-d3d-time-step", fmt::format(
      "\"iteration\":{},\"swap\":{},\"changes\":{},\"fixed\":{},\"step\":{:.7f},\"step_rate\":{:.3f},"
      "\"interval\":{},\"mode\":{},\"title_loading\":{},\"world\":\"{:08x}\"",
      step_iterations, swaps.load(std::memory_order_relaxed), step_changes, step.fixed, finite ? step.period : 0.0f,
      finite && step.period > 0.0f ? 1.0 / double(step.period) : 0.0, step.interval, Owning(), TitleLoading(),
      WorldKey()));
}

// Once per loop iteration, by the first of its two calls.
void Decide(uint8_t* base) {
  if (decided) return;
  decided = true;
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (!frequency) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart;
    window_start = now.QuadPart;
  }
  const bool f8 = by_variable && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  const bool toggled = f8 && !f8_down;
  f8_down = f8;
  if (toggled) switched_off = !switched_off;
  ++frame.index;
  const uint32_t time = Get32(base, kTimePointer);
  const bool fixed = time && Get32(base, time + 72);
  const float period = time ? GetFloat(base, time + 76) : 0.0f;
  // Slow motion (Speed below 1): the ticks come later in real time, the frames between them
  // still interpolated; frame_period is a frame's share of the game's time.
  const double slow = std::min(double(Speed()), 1.0);
  const double frame_period = slow / double(FrameRate());
  // A step of 0 is the pause: the logic runs every frame without advancing.
  const bool valid_period = fixed && std::isfinite(period) && period > 1e-4f && period <= 0.25f;
  const bool gate = Owning() && valid_period && period > frame_period * 1.25 && Speed() <= 1.0f;
  // Without the gate under the fixed step, every iteration is a tick at the
  // title's step: the limiter paces at the tick rate (PaceRate).
  pace_rate.store(valid_period && !gate ? uint32_t(std::lround(1.0 / double(period))) : FrameRate());
  const bool changed = gate != frame.gate || (gate && std::fabs(period - frame.period) > 1e-6f);
  frame.gate = gate;
  frame.period = gate ? period : 0.0f;
  if (!gate) {
    frame.tick = true;
    frame.alpha = 1.0f;
    started = false;
    view_started = false;
    debt = 0;
    frames_since_tick = 0;
  } else {
    // A new run of the gate starts with a tick; then the real time since the
    // previous iteration is owed. A tick is due within half a frame of its
    // period, so the frames at a steady rate alternate without jitter.
    debt = started ? debt + slow * double(now.QuadPart - last) / double(frequency) : double(period);
    started = true;
    if (debt > 2.0 * period) {
      debt = 2.0 * period;
      ++capped;
    }
    frame.tick = debt >= double(period) - frame_period * 0.5;
    if (frame.tick) {
      debt -= period;
      ++frame.ticks;
      frames_since_tick = 0;
    } else {
      ++frame.skipped;
      ++frames_since_tick;
    }
    // The frames are presented at the limiter's steady rate, not when their
    // iteration starts (the delivery waits for the render thread): the alpha
    // follows the frames since the tick, and the real time only when a frame
    // is more than half a frame off its slot (a slow frame).
    const double real = debt / double(period);
    const double paced = double(frames_since_tick) * frame_period / double(period);
    const double alpha = std::fabs(paced - real) <= 0.5 * frame_period / double(period) ? paced : real;
    frame.alpha = float(std::clamp(alpha, 0.0, 1.0));
  }
  last = now.QuadPart;
  ++window_iterations;
  if (frame.tick) ++window_ticks;
  if (toggled || changed || frame.index % 300 == 0)
    Record(toggled ? "f8" : changed ? "mode" : "periodic", now.QuadPart);
}

// The view block of 827F73F0 (827F75AC to 827F7654) without the sound manager
// (827BCE88 advances by the dt) and the end of the iteration (827EE0D0): the
// arguments, stores and return addresses of the original, in its order. The
// original stores the view in the first view entry, or at 144 when the world
// has none; the store is left out then.
void ViewBlock(PPCContext& ctx, uint8_t* base) {
  const uint32_t view = Get32(base, kViews + 12);
  if (!view || Get32(base, kViewsOff)) return;
  const uint64_t lr = ctx.lr;
  auto call = [&](void (*fn)(PPCContext& __restrict, uint8_t*), uint32_t r3, uint32_t r4, uint32_t return_address) {
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    ctx.lr = return_address;
    fn(ctx, base);
  };
  call(sub_827F6F38, view, 0, 0x827F75F4);
  call(sub_82BB9B28, view, 0, 0x827F75F8);
  const uint32_t world = Get32(base, kViews + 8);
  Put32(base, view + 2268, world);
  if (world) {
    const uint32_t display_world = Get32(base, kWorld);
    const uint32_t entries = display_world && Get32(base, display_world + 728) ? Get32(base, display_world + 720) : 0;
    if (entries) Put32(base, entries + 144, view);
    call(sub_82BD9520, Get32(base, view + 2268), view, 0x827F763C);
  }
  call(sub_82BECF98, Get32(base, kWorldEffects), view, 0x827F7648);
  call(sub_82BBADE0, view, 1, 0x827F7654);
  ctx.lr = lr;
}

bool Gating() { return gpu::Active() && Enabled(); }

// The speed up of a tick (SetSpeed): the fixed step (+76) lengthened up to
// kSpeedStepMax for the main step, so one tick advances the game more; the
// limiter paces the rest of the factor (PaceSpeed). Put back after the step
// unless the logic set a new one.
struct SpeedStep {
  uint32_t time = 0;
  float own = 0.0f, set = 0.0f;
};
SpeedStep BeginSpeedStep(uint8_t* base, bool gated) {
  const float factor = Speed();
  if (gated && factor < 1.0f) {  // The gate slows the ticks instead (Decide).
    pace_speed.store(1.0f);
    return {};
  }
  const uint32_t time = factor != 1.0f ? Get32(base, kTimePointer) : 0u;
  const float own = time && Get32(base, time + 72) ? GetFloat(base, time + 76) : 0.0f;
  if (!std::isfinite(own) || own <= 1e-4f || own > 0.25f) {
    pace_speed.store(factor);
    return {};
  }
  // Slow motion (below 1): a shorter step at the normal pace.
  const float step_factor = factor < 1.0f ? factor : std::clamp(kSpeedStepMax / own, 1.0f, factor);
  pace_speed.store(factor < 1.0f ? 1.0f : factor / step_factor);
  const SpeedStep step{time, own, own * step_factor};
  PutFloat(base, time + 76, step.set);
  return step;
}
void EndSpeedStep(uint8_t* base, const SpeedStep& step) {
  if (step.time && GetFloat(base, step.time + 76) == step.set) PutFloat(base, step.time + 76, step.own);
}

// A frozen iteration (SetFrozen): the view block with the engine's dt at 0.
void FrozenStep(PPCContext& ctx, uint8_t* base) {
  const uint32_t time = Get32(base, kTimePointer);
  const EngineDt saved = time ? ReadDt(base, time) : EngineDt{};
  if (time) WriteDt(base, time, {});
  ViewBlock(ctx, base);
  if (time) WriteDt(base, time, saved);
}
}  // namespace

bool Enabled() {
  if (enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_STATE_INTERPOLATION");
    by_variable = text && *text;
    enabled = by_variable ? (std::string_view(text) == "1" ? 1 : 0) : CurrentSettings().frame_rate > 30 ? 1 : 0;
  }
  return enabled == 1;
}

bool Owning() { return Enabled() && !switched_off; }

bool ByVariable() { return Enabled() && by_variable; }

uint32_t FrameRate() {
  if (!rate) {
    const char* text = std::getenv("REX_NATIVE_FRAME_RATE");
    const int n = text ? std::atoi(text) : 0;
    const uint32_t option = CurrentSettings().frame_rate;
    rate = n >= 30 && n <= 240 ? uint32_t(n) : option > 30 ? option : 60u;
  }
  return rate;
}

uint32_t PaceRate() {
  const uint32_t rate = pace_rate.load();
  return rate ? rate : FrameRate();
}

const Frame& Current() { return frame; }

bool InGatedStep() { return in_step; }

void BeginView(uint8_t* base) {
  view_address = 0;
  if (!Gating() || !in_step || !frame.gate || !ViewTime()) return;
  const uint32_t time = Get32(base, kTimePointer);
  if (!time) return;
  // The image's time, in periods: the last tick plus alpha. A new run of the
  // gate starts with a frame's share of the period.
  const double shown = double(frame.ticks) + frame.alpha;
  const double advance = view_started ? shown - view_shown : 1.0 / (double(frame.period) * FrameRate());
  view_started = true;
  view_shown = shown;
  const double factor = ViewTime() == 2 ? (frame.tick ? 1.0 : 0.0) : std::clamp(advance, 0.0, 2.0);
  view_saved = ReadDt(base, time);
  view_set = {uint64_t(int64_t(double(int64_t(view_saved.us)) * factor)), float(view_saved.dt * factor),
              float(view_saved.raw * factor)};
  WriteDt(base, time, view_set);
  view_address = time;
  window_view_dt += view_set.dt;
  window_tick_dt += view_saved.dt;
}

void EndView(uint8_t* base) {
  if (!view_address) return;
  const EngineDt now = ReadDt(base, view_address);
  if (now.us == view_set.us && now.dt == view_set.dt && now.raw == view_set.raw) WriteDt(base, view_address, view_saved);
  view_address = 0;
}

void SetSpeed(float value) {
  speed.store(value >= 0.1f && value <= 16.0f ? value : 1.0f);
  if (speed.load() == 1.0f) pace_speed.store(1.0f);
}
float PaceSpeed() { return pace_speed.load(std::memory_order_relaxed); }
float Speed() { return speed.load(std::memory_order_relaxed); }
void SetFrozen(bool value) { frozen.store(value); }
bool Frozen() { return frozen.load(std::memory_order_relaxed); }

const char* Label() {
  if (!Enabled() || !by_variable) return "";
  if (switched_off) return "F8: off";
  if (!frame.gate) return "F8: ticks (no gate)";
  return interpolation::Enabled() ? "F8: interpolated" : "F8: ticks";
}

// The two calls of the main loop.
void WorldChangeStep(PPCContext& ctx, uint8_t* base) {
  if (gpu::Active() && Frozen()) return;
  if (Gating()) {
    Decide(base);
    if (!frame.tick) return;
  }
  __imp__sub_827F7668(ctx, base);
}

void MainStep(PPCContext& ctx, uint8_t* base) {
  WriteIconValues();  // keyboard_prompts.cpp: the button icons, on the game thread
  if (gpu::Active()) NoteStep(base);
  if (gpu::Active() && Frozen()) {
    FrozenStep(ctx, base);
    decided = false;
    return;
  }
  if (!Gating()) {
    const SpeedStep speed_step = BeginSpeedStep(base, false);
    __imp__sub_827F73F0(ctx, base);
    EndSpeedStep(base, speed_step);
    return;
  }
  Decide(base);
  in_step = frame.gate;
  if (frame.tick) {
    const SpeedStep speed_step = BeginSpeedStep(base, frame.gate);
    __imp__sub_827F73F0(ctx, base);
    EndSpeedStep(base, speed_step);
  } else {
    ViewBlock(ctx, base);
  }
  in_step = false;
  decided = false;
}
}  // namespace gpu::tick

REX_HOOK_RAW(sub_827F7668) { gpu::tick::WorldChangeStep(ctx, base); }
REX_HOOK_RAW(sub_827F73F0) { gpu::tick::MainStep(ctx, base); }
