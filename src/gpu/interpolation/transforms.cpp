// The transforms between two logic ticks (transforms.h).
#include "gpu/interpolation/transforms.h"
#include "gpu/interpolation/tick.h"
#include "gpu/common/guest_memory.h"
#include "gpu/common/title_loading.h"
#include "gpu/d3d/d3d.h"
#include "gpu/host/services.h"
#include <windows.h>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" void __imp__sub_827F6F38(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82BBADE0(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82764A08(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82BD9108(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82B97DE8(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82120A70(PPCContext& __restrict, uint8_t*);

namespace gpu::interpolation {
namespace {
// The return addresses of the main step's view start and delivery (827F73F0,
// and the view block of tick.cpp, which calls them with the same).
constexpr uint32_t kViewStartReturn = 0x827F75F4, kDeliveryReturn = 0x827F7654;
constexpr uint32_t kCutCounter = 0x833A34C4;
constexpr uint32_t kDisplayData = 0x833A31B8;  // The display's state (the current view): +204 its flags.
// The world's effects: +4 their list, +156 the manager of their work thread.
// The logic posts the effects' update in the tick (8277CA38: the list at
// manager + 236, then the start event); the thread (8277CB40) runs it
// (82BE45F0) and clears +236 when done; the effects' draw waits for it inside
// the view block (82BECF98).
constexpr uint32_t kWorldEffects = 0x833A3468;
constexpr auto kEffectsWaitMax = std::chrono::milliseconds(100);
constexpr float kCutDistance = 4.0f;    // Units in one tick (Rise: ~120 units/s; walking is ~7).
constexpr float kCutCosine = 0.5f;      // A row turning more than 60 degrees in one tick.
constexpr float kCameraCutDistance = 10.0f, kCameraCutCosine = 0.866f;  // 827E3E90.
// The camera: the matrix at the start of the view's entry (the 172-byte
// entries of world + 720), written by the logic in the tick. The display
// builds the view's camera from it (82BD9108 -> 82BADE50: the world matrix at
// view + 424, the view matrix at +492, the projection and the planes at +1104).
constexpr size_t kCameraFloats = 16;

using Matrix = std::array<float, 16>;
using Camera = std::array<float, kCameraFloats>;
// An engine matrix is 68 bytes: the 16 floats and a type word at +64. With
// kScaleFlag in it, the fourth column (+12, +28, +44) scales the rows. Without
// it the engine's own transforms leave the column alone, but Broken Bond still
// reads it elsewhere: the light direction of a character's materials (VS c27)
// comes out tilted when the column holds anything but the object's own values
// (the title keeps 0 there), so the blend keeps the column as the two ticks
// have it and never forces a scale of 1 into it (Rise did; its title does not
// read the column).
constexpr uint32_t kTypeOffset = 64, kScaleFlag = 8;
struct Track {
  uint32_t matrix = 0;
  uint64_t tick = 0;
  bool valid = false;
  Matrix previous{}, current{};
  uint32_t previous_type = 0, current_type = 0;
  // The display turned the written matrix in place (a billboard): what the
  // view block left on it (Restore), until the next write or tick. A matrix
  // shared by objects is written once per block, and this is on the track
  // that wrote it.
  bool turned = false;
  Matrix left{};
};
struct CameraTrack {
  uint32_t entry = 0;
  uint64_t tick = 0;
  bool valid = false, cut = false;
  Camera previous{}, current{};
};
// A bone matrix the pose copy reads (82B97DE8), by its address.
struct BoneTrack {
  uint32_t owner = 0, bone = 0;  // The skinned object and the bone's object (0 for a local matrix).
  uint64_t tick = 0;
  bool valid = false;
  Matrix previous{}, current{};
};
// A place written over the live transform, put back after the delivery.
struct Written {
  uint32_t address = 0, count = 0;
  Matrix value{}, live{};
  uint32_t object = 0;  // An object matrix's object, 0 otherwise.
};

int enabled = -1, bones_enabled = -1, turned_enabled = -1;
std::unordered_map<uint32_t, Track> tracks;  // By object.
std::unordered_map<uint32_t, BoneTrack> bone_tracks;  // By matrix.
std::unordered_set<uint32_t> object_written;  // The object matrices written in this view block.
uint32_t main_thread = 0;  // The guest thread of the view block.
std::vector<uint32_t> mounted, candidates;   // Objects of the current and the last display.
bool collecting = false;
std::unordered_map<uint32_t, CameraTrack> cameras;  // By the view's entry.
std::unordered_set<uint32_t> camera_entries_done;   // The entries handled in this display.
std::vector<Written> written;         // The object matrices written in this view block.
std::vector<Written> camera_written;  // The camera entries written in this view block.
// The global matrices of the bone objects (transforms.h): the objects of the
// list of a mounted object's root (the list the texture projector's lookup
// 82BEB878 walks), by their matrix [X + 20], the owner the object X; written
// for the whole view block.
std::unordered_map<uint32_t, BoneTrack> global_tracks;
std::vector<Written> global_written;
int globals_enabled = -1;
Applied applied;
std::atomic<uint32_t> mounts_other_thread{0};
// The pointers the engine freed (82120A70, any thread) since the last view
// block: an object freed by the tick leaves the candidates and the tracks
// before anything is written.
std::atomic<bool> watching{false};
std::mutex freed_mutex;
std::vector<uint32_t> freed;
bool freed_overflow = false;
constexpr size_t kFreedMax = 1 << 20;
// A world loaded or a loading screen shown since the last view block (their
// counts in common/title_loading.h): the loads free objects, bones and view
// entries in bulk through paths the free hook does not see, and the memory is
// reused by the new world, so everything starts again.
uint32_t loads_seen = 0;
uint64_t resets = 0;  // Since the start.

// Guarded reads of guest memory: a pointer taken from an object the display
// mounted a tick ago, or from a bone array, may no longer point at mapped
// memory; CopyFromGuest fails instead of faulting.
bool Read32(uint8_t* base, uint32_t address, uint32_t& out) {
  if (!CopyFromGuest(&out, base + address, 4)) return false;
  out = _byteswap_ulong(out);
  return true;
}
bool ReadFloats(uint8_t* base, uint32_t address, float* out, size_t count) {
  if (!CopyFromGuest(out, base + address, 4 * count)) return false;
  for (size_t i = 0; i < count; ++i) {
    uint32_t v;
    std::memcpy(&v, out + i, 4);
    v = _byteswap_ulong(v);
    std::memcpy(out + i, &v, 4);
  }
  return true;
}
bool Finite(const float* v, size_t count) {
  return std::all_of(v, v + count, [](float f) { return std::isfinite(f); });
}
float Length3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float Cosine3(const float* a, const float* b) {
  const float la = Length3(a), lb = Length3(b);
  if (la <= 1e-6f || lb <= 1e-6f) return 1.0f;
  return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
}
float Distance3(const float* a, const float* b) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return Length3(d);
}
// A direction row between two: linear, with the length interpolated.
void BlendRow(const float* a, const float* b, float t, float* out) {
  for (int i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
  const float length = Length3(out), target = Length3(a) + (Length3(b) - Length3(a)) * t;
  if (length > 1e-6f)
    for (int i = 0; i < 3; ++i) out[i] *= target / length;
}
void BlendMatrix(const float* a, const float* b, float t, float* out) {
  for (int row = 0; row < 3; ++row) {
    BlendRow(a + 4 * row, b + 4 * row, t, out + 4 * row);
    out[4 * row + 3] = a[4 * row + 3] + (b[4 * row + 3] - a[4 * row + 3]) * t;
  }
  for (int i = 12; i < 16; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}
// A matrix as the engine keeps one: rotation rows of a sane length (a scale
// from 1/1000 to 1000, or 0) and a finite position within the world. An object
// freed and reused by other data often passes the guarded reads, not this.
bool Plausible(const Matrix& m) {
  for (int row = 0; row < 3; ++row) {
    const float length = Length3(m.data() + 4 * row);
    if (!std::isfinite(length) || (length != 0.0f && (length < 1e-3f || length > 1e3f))) return false;
  }
  for (int i = 12; i < 15; ++i)
    if (!(std::fabs(m[i]) < 1e6f)) return false;
  return true;
}
bool MatrixCut(const Matrix& a, const Matrix& b) {
  if (Distance3(a.data() + 12, b.data() + 12) > kCutDistance) return true;
  for (int row = 0; row < 3; ++row)
    if (Cosine3(a.data() + 4 * row, b.data() + 4 * row) < kCutCosine) return true;
  return false;
}
bool ReadCamera(uint8_t* base, uint32_t entry, Camera& out) { return ReadFloats(base, entry, out.data(), kCameraFloats); }
void WriteMatrix(uint8_t* base, uint32_t address, const Matrix& m) {
  for (uint32_t i = 0; i < 16; ++i) PutFloat(base, address + 4 * i, m[i]);
}
void WriteCamera(uint8_t* base, uint32_t entry, const Camera& c) {
  for (uint32_t i = 0; i < kCameraFloats; ++i) PutFloat(base, entry + 4 * i, c[i]);
}
// The position (row 3) and the rotation rows.
bool CameraCut(const Camera& a, const Camera& b) {
  if (Distance3(a.data() + 12, b.data() + 12) >= kCameraCutDistance) return true;
  for (int row = 0; row < 3; ++row)
    if (Cosine3(a.data() + 4 * row, b.data() + 4 * row) < kCameraCutCosine) return true;
  return false;
}
// The camera at the tick: the current one becomes the previous.
void TickCamera(CameraTrack& track, uint32_t entry, const Camera& live, uint64_t tick) {
  const bool continuing = track.entry == entry && track.tick + 1 == tick;
  track.previous = continuing ? track.current : live;
  track.current = live;
  track.cut = continuing && CameraCut(track.previous, track.current);
  // Both ends finite: a NaN previous passes the cuts (their comparisons are false).
  track.valid = continuing && !track.cut && Finite(track.previous.data(), kCameraFloats) &&
                Finite(live.data(), kCameraFloats);
  track.entry = entry;
  track.tick = tick;
}
// Writes the camera of the track at alpha; false when it does not apply.
bool ApplyCamera(uint8_t* base, CameraTrack& track, uint32_t entry, uint64_t tick, float alpha) {
  if (!track.valid || track.entry != entry || track.tick != tick || Get32(base, kCutCounter)) return false;
  if (track.previous == track.current) return false;
  Camera live;
  if (!ReadCamera(base, entry, live) || live != track.current) {  // Something else wrote it since the tick.
    track.valid = false;
    return false;
  }
  Written w;
  w.address = entry;
  w.count = kCameraFloats;
  BlendMatrix(track.previous.data(), track.current.data(), alpha, w.value.data());
  w.live = track.current;
  WriteCamera(base, entry, w.value);
  camera_written.push_back(w);
  return true;
}

bool Active() { return gpu::Active() && Enabled() && tick::InGatedStep(); }

bool BonesEnabled() {
  if (bones_enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_BONES");
    bones_enabled = Enabled() && !(text && std::string_view(text) == "0") ? 1 : 0;
  }
  return bones_enabled == 1;
}
// The bone objects' global matrices (transforms.h);
// REX_NATIVE_INTERPOLATION_BONE_GLOBALS=0 leaves them at the tick.
bool GlobalsEnabled() {
  if (globals_enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_BONE_GLOBALS");
    globals_enabled = BonesEnabled() && !(text && std::string_view(text) == "0") ? 1 : 0;
  }
  return globals_enabled == 1;
}

// The root of an object (82BDF2A0): up the parents ([[object + 28] + 8] + 4)
// while the object has flag 1; the object itself when the chain breaks.
uint32_t Root(uint8_t* base, uint32_t object) {
  uint32_t flags = 0;
  if (!Read32(base, object, flags) || !(flags & 1)) return object;
  uint32_t node = object;
  for (int depth = 0; depth < 64; ++depth) {
    if (!(flags & 1)) return node;
    uint32_t extension = 0, link = 0, parent = 0;
    if (!Read32(base, node + 28, extension) || !extension || !Read32(base, extension + 8, link) || !link ||
        !Read32(base, link + 4, parent) || !parent || !Read32(base, parent, flags))
      return object;
    node = parent;
  }
  return object;
}
// The bone objects of a root (the list 82BEB878 walks: the root has flag 2;
// [[root + 28] + 0] the skeleton, [[skeleton + 12] + 0] the list of 8-byte
// entries from +16 to +4, each an object pointer with two tag bits, those with
// the tag 1 skipped; [skeleton + 28] the id table it needs). False when the
// structure is not there or cannot be read.
bool RootBones(uint8_t* base, uint32_t root, std::vector<uint32_t>& out) {
  uint32_t flags = 0, extension = 0, skeleton = 0, holder = 0, list = 0, ids = 0, begin = 0, end = 0;
  if (!Read32(base, root, flags) || !(flags & 2) || !Read32(base, root + 28, extension) || !extension ||
      !Read32(base, extension, skeleton) || !skeleton || !Read32(base, skeleton + 12, holder) || !holder ||
      !Read32(base, holder, list) || !list || !Read32(base, skeleton + 28, ids) || !ids ||
      !Read32(base, list + 16, begin) || !Read32(base, list + 4, end) || end < begin || end - begin > 8 * 4096)
    return false;
  for (uint32_t p = begin; p + 8 <= end; p += 8) {
    uint32_t x = 0;
    if (!Read32(base, p, x)) return false;
    if (x && (x & 3) == 0) out.push_back(x);
  }
  return true;
}

// The matrices the display turns in place (transforms.h):
// REX_NATIVE_INTERPOLATION_TURNED=0 leaves them as the view block left them;
// with the mode set by the variable, F7 switches it at run time.
bool TurnedEnabled() {
  if (turned_enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_TURNED");
    turned_enabled = !(text && std::string_view(text) == "0") ? 1 : 0;
  }
  return turned_enabled == 1;
}
bool f7_down = false;
bool turned_switched = false;  // F7 pressed since the last record.
void SwitchTurned() {
  const bool down = tick::ByVariable() && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
  if (down && !f7_down) {
    turned_enabled = TurnedEnabled() ? 0 : 1;
    turned_switched = true;
  }
  f7_down = down;
}

// The counts of the view blocks of the gate since the last record, written as
// native-d3d-interpolation every kRecordEvery blocks and at an F7 switch: the
// object matrices written, those the view block changed, those only turned
// (given the tick's scale and position back), turned ones written again in a
// frame without a tick, and those written outside the logic (lost); the
// camera, the poses, the bone objects' globals written for the whole block
// (those the block changed, those cut at the tick, and the roots walked) and
// the effects' waits.
constexpr uint64_t kRecordEvery = 300;
struct Counts {
  uint64_t blocks = 0, ticks = 0, objects = 0, object_cuts = 0, cameras = 0, camera_cuts = 0;
  uint64_t changed = 0, turned = 0, rewritten = 0, lost = 0;
  uint64_t poses = 0, bones = 0, bone_cuts = 0, bones_lost = 0, pose_other_thread = 0;
  uint64_t globals = 0, globals_changed = 0, global_cuts = 0, globals_lost = 0, roots = 0;
  uint64_t effects_waits = 0, effects_wait_us = 0, effects_wait_max_us = 0, effects_timeouts = 0;
} counts;
uint64_t blocks_total = 0;
void Count(bool tick) {
  ++counts.blocks;
  if (tick) ++counts.ticks;
  counts.objects += applied.objects;
  counts.object_cuts += applied.object_cuts;
  counts.cameras += applied.camera;
  counts.camera_cuts += applied.camera_cut;
  counts.poses += applied.poses;
  counts.bones += applied.bones;
  counts.bone_cuts += applied.bone_cuts;
  counts.bones_lost += applied.bones_lost;
  counts.pose_other_thread += applied.pose_other_thread;
  if (applied.effects_wait_us) ++counts.effects_waits;
  counts.effects_wait_us += applied.effects_wait_us;
  counts.effects_wait_max_us = std::max<uint64_t>(counts.effects_wait_max_us, applied.effects_wait_us);
  counts.effects_timeouts += applied.effects_timeout;
  ++blocks_total;
  if (counts.blocks < kRecordEvery && !turned_switched) return;
  const auto& c = counts;
  host::Record("native-d3d-interpolation", fmt::format(
      "\"blocks_total\":{},\"blocks\":{},\"ticks\":{},\"objects\":{},\"object_cuts\":{},\"cameras\":{},"
      "\"camera_cuts\":{},\"changed\":{},\"turned\":{},\"rewritten\":{},\"lost\":{},\"turned_on\":{},"
      "\"turned_switched\":{},\"poses\":{},\"bones\":{},\"bone_cuts\":{},\"bones_lost\":{},\"pose_other_thread\":{},"
      "\"globals\":{},\"globals_changed\":{},\"global_cuts\":{},\"globals_lost\":{},\"roots\":{},\"globals_on\":{},"
      "\"mounts_other_thread\":{},\"effects_waits\":{},\"effects_wait_us\":{},\"effects_wait_max_us\":{},"
      "\"effects_timeouts\":{},\"tracks\":{},\"bone_tracks\":{},\"global_tracks\":{},\"camera_tracks\":{},\"resets\":{}",
      blocks_total, c.blocks, c.ticks, c.objects, c.object_cuts, c.cameras, c.camera_cuts, c.changed, c.turned,
      c.rewritten, c.lost, TurnedEnabled(), turned_switched, c.poses, c.bones, c.bone_cuts, c.bones_lost,
      c.pose_other_thread, c.globals, c.globals_changed, c.global_cuts, c.globals_lost, c.roots, GlobalsEnabled(),
      mounts_other_thread.load(), c.effects_waits, c.effects_wait_us, c.effects_wait_max_us,
      c.effects_timeouts, tracks.size(), bone_tracks.size(), global_tracks.size(), cameras.size(), resets));
  counts = {};
  turned_switched = false;
}

// Puts back the live value where nothing else wrote since; false where
// something did.
bool PutBack(uint8_t* base, const Written& w) {
  for (uint32_t i = 0; i < w.count; ++i)
    if (GetFloat(base, w.address + 4 * i) != w.value[i]) return false;
  for (uint32_t i = 0; i < w.count; ++i) PutFloat(base, w.address + 4 * i, w.live[i]);
  return true;
}
void PutBack(uint8_t* base, const std::vector<Written>& list) {
  for (const auto& w : list) PutBack(base, w);
}

// The bone matrices the pose copy of a skinned mesh reads (82B97DE8, the
// object and its geometry), with the bone's object (0 for a local matrix):
// the skin [geometry + 60] (+2 its bones, +4 their entries: +0 the bone, +72
// its vertices), copied only with the flags of the display (+204 0x20000), of
// the object (0x200000) and of the geometry (+92 bit 3). The bones are
// [[object + 28] + 12] (+0 their count, +4 the array). With the object's flag
// 0x01000000 the array has 12-byte pointers (+0 a matrix, +4 the bone's
// object, +8 -1 or an id): the bone's global matrix [bone + 20], relative to
// the object's (when +8 is -1 or the bone's object has the same flag), or the
// matrix at +0; without it, 68-byte matrices relative to the object.
void BoneMatrices(uint8_t* base, uint32_t object, uint32_t geometry, std::vector<std::pair<uint32_t, uint32_t>>& out) {
  const uint32_t skin = Get32(base, geometry + 60), display = Get32(base, kDisplayData);
  const uint32_t flags = Get32(base, object);
  if (!skin || !display || !(Get32(base, display + 204) & 0x20000) || !(flags & 0x200000) ||
      !(Get32(base, geometry + 92) & 8))
    return;
  const uint32_t extension = Get32(base, object + 28);
  const uint32_t bones = extension ? Get32(base, extension + 12) : 0;
  if (!bones) return;
  const uint32_t count = Get32(base, bones), array = Get32(base, bones + 4);
  const uint32_t entries = Get32(base, skin + 4), n = Get16(base, skin + 2);
  if (!array || !entries) return;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t entry = Get32(base, entries + 4 * i);
    if (!entry || !Get16(base, entry + 72)) continue;
    const uint32_t index = Get16(base, entry);
    if (index >= count) continue;
    if (!(flags & 0x01000000)) {
      out.emplace_back(array + 68 * index, 0);
      continue;
    }
    const uint32_t pointer = array + 12 * index, bone = Get32(base, pointer + 4);
    if (Get32(base, pointer + 8) == 0xFFFFFFFFu || (bone && (Get32(base, bone) & 0x01000000))) {
      if (const uint32_t m = bone ? Get32(base, bone + 20) : 0) out.emplace_back(m, bone);
    } else if (const uint32_t m = Get32(base, pointer)) {
      out.emplace_back(m, bone);
    }
  }
}

// The pose of a skinned mesh (82B97DE8), before the display copies it into
// the frame's packet: the bone matrices at the tick's alpha (out, put back
// right after the copy).
void Pose(uint8_t* base, uint32_t object, uint32_t geometry, std::vector<Written>& out) {
  if (!collecting || !BonesEnabled()) return;
  if (GuestThread() != main_thread) {
    ++applied.pose_other_thread;
    return;
  }
  ++applied.poses;
  static std::vector<std::pair<uint32_t, uint32_t>> bones;  // Main thread (above): a scratch kept across the calls.
  bones.clear();
  BoneMatrices(base, object, geometry, bones);
  const auto& f = tick::Current();
  for (const auto& [m, bone] : bones) {
    if (object_written.count(m) ||
        std::any_of(out.begin(), out.end(), [&](const Written& w) { return w.address == m; }))
      continue;
    Matrix live;
    if (!ReadFloats(base, m, live.data(), 16)) continue;
    BoneTrack& t = bone_tracks[m];
    if (f.tick && t.tick != f.ticks) {
      const bool continuing = t.tick + 1 == f.ticks && t.owner == object && t.bone == bone;
      t.previous = continuing ? t.current : live;
      t.current = live;
      const bool cut = continuing && MatrixCut(t.previous, t.current);
      if (cut) ++applied.bone_cuts;
      t.valid = continuing && !cut && Finite(t.previous.data(), 16) && Finite(live.data(), 16);
      t.owner = object;
      t.bone = bone;
      t.tick = f.ticks;
    }
    if (!t.valid || t.tick != f.ticks || t.previous == t.current) continue;
    if (live != t.current) {  // Written outside the logic since the tick.
      t.valid = false;
      ++applied.bones_lost;
      continue;
    }
    Written w;
    w.address = m;
    w.count = 16;
    BlendMatrix(t.previous.data(), t.current.data(), f.alpha, w.value.data());
    std::copy(live.begin(), live.end(), w.live.begin());
    WriteMatrix(base, m, w.value);
    out.push_back(w);
    ++applied.bones;
  }
}

// In an iteration with a tick, the effects' update may still run on its work
// thread when the view block starts: the block's writes (the interpolated
// matrices, the view block's dt of tick.h) wait for it, so the effects keep
// reading the tick's state. +236 is read as written by the other thread; the
// wait is bounded (kEffectsWaitMax).
uint32_t effects_wait_us = 0;
bool effects_timeout = false;
void SettleEffects(uint8_t* base) {
  effects_wait_us = 0;
  effects_timeout = false;
  if (!gpu::Active() || !tick::InGatedStep() || !tick::Current().tick) return;
  uint32_t effects = 0, manager = 0;
  if (!Read32(base, kWorldEffects, effects) || !effects || !Read32(base, effects + 156, manager) || !manager) return;
  const volatile uint32_t* list = reinterpret_cast<const volatile uint32_t*>(base + manager + 236);
  if (!*list) return;
  const auto start = std::chrono::steady_clock::now();
  while (*list) {
    if (std::chrono::steady_clock::now() - start > kEffectsWaitMax) {
      effects_timeout = true;
      break;
    }
    std::this_thread::yield();
  }
  effects_wait_us = uint32_t(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}

// The view start of the main step (827F6F38, r3 the view).
void ViewStart(uint8_t* base) {
  candidates.swap(mounted);
  mounted.clear();
  collecting = Active();
  applied = {};
  applied.effects_wait_us = effects_wait_us;
  applied.effects_timeout = effects_timeout;
  object_written.clear();
  camera_entries_done.clear();
  const uint32_t loads = title_loadings.load(std::memory_order_relaxed) + world_loads.load(std::memory_order_relaxed);
  const bool reloaded = loads != loads_seen || TitleLoading();
  loads_seen = loads;
  if (!collecting || reloaded) {
    if (collecting && !(tracks.empty() && bone_tracks.empty() && cameras.empty() && candidates.empty())) ++resets;
    tracks.clear();
    bone_tracks.clear();
    global_tracks.clear();
    cameras.clear();
    candidates.clear();
    std::lock_guard lock(freed_mutex);
    freed.clear();
    freed_overflow = false;
  }
  if (!collecting) {
    watching = false;
    return;
  }
  main_thread = GuestThread();
  watching = true;
  SwitchTurned();
  {
    std::vector<uint32_t> gone;
    bool overflow = false;
    {
      std::lock_guard lock(freed_mutex);
      gone.swap(freed);
      overflow = freed_overflow;
      freed_overflow = false;
    }
    if (overflow) {
      tracks.clear();
      bone_tracks.clear();
      global_tracks.clear();
      candidates.clear();
    } else if (!gone.empty() && !(tracks.empty() && bone_tracks.empty() && global_tracks.empty() && candidates.empty())) {
      const std::unordered_set<uint32_t> set(gone.begin(), gone.end());
      for (auto it = tracks.begin(); it != tracks.end();)
        it = set.count(it->first) || set.count(it->second.matrix) ? tracks.erase(it) : std::next(it);
      for (auto it = bone_tracks.begin(); it != bone_tracks.end();)
        it = set.count(it->first) || set.count(it->second.owner) || set.count(it->second.bone) ? bone_tracks.erase(it)
                                                                                                : std::next(it);
      for (auto it = global_tracks.begin(); it != global_tracks.end();)
        it = set.count(it->first) || set.count(it->second.owner) ? global_tracks.erase(it) : std::next(it);
      std::erase_if(candidates, [&](uint32_t object) { return set.count(object) != 0; });
    }
  }
  const auto& f = tick::Current();
  const uint64_t tick = f.ticks;
  if (f.tick) {
    std::unordered_set<uint32_t> seen;
    for (const uint32_t object : candidates) {
      if (!seen.insert(object).second) continue;
      // An object freed since the display by another path than 82120A70 may
      // hold anything at +20: the reads are guarded, the object must still
      // carry the flags of the mount and its matrix look like one, and
      // otherwise the track stays at its tick (no write, gone after two ticks).
      uint32_t m = 0, type = 0, flags = 0;
      Matrix live;
      if (!Read32(base, object + 4, flags) || !(flags & 0x00040000) || (flags & 0x00200004) ||
          !Read32(base, object + 20, m) || !m || !ReadFloats(base, m, live.data(), 16) || !Plausible(live) ||
          !Read32(base, m + kTypeOffset, type))
        continue;
      Track& t = tracks[object];
      const bool continuing = t.matrix == m && t.tick + 1 == tick;
      t.previous = continuing ? t.current : live;
      t.current = live;
      t.previous_type = continuing ? t.current_type : type;
      t.current_type = type;
      t.turned = false;
      const bool cut = continuing && MatrixCut(t.previous, t.current);
      if (cut) ++applied.object_cuts;
      t.valid = continuing && !cut && Finite(t.previous.data(), 16) && Finite(live.data(), 16);
      t.matrix = m;
      t.tick = tick;
    }
    for (auto it = tracks.begin(); it != tracks.end();) it = it->second.tick + 2 < tick ? tracks.erase(it) : std::next(it);
    for (auto it = bone_tracks.begin(); it != bone_tracks.end();)
      it = it->second.tick + 2 < tick ? bone_tracks.erase(it) : std::next(it);
    for (auto it = global_tracks.begin(); it != global_tracks.end();)
      it = it->second.tick + 2 < tick ? global_tracks.erase(it) : std::next(it);
    for (auto it = cameras.begin(); it != cameras.end();) it = it->second.tick + 2 < tick ? cameras.erase(it) : std::next(it);
  }
  const float alpha = f.alpha;
  std::unordered_set<uint32_t> matrices;
  for (auto& [object, t] : tracks) {
    if (!t.valid || t.tick != tick || t.previous == t.current || !matrices.insert(t.matrix).second) continue;
    // In a frame without a tick, a matrix the display turned holds the turn
    // the last view block left (Restore): the interpolated value goes under
    // it again, as the display ran once per state of the logic on the console.
    Matrix live;
    if (!ReadFloats(base, t.matrix, live.data(), 16)) {
      t.valid = false;
      continue;
    }
    const bool turned = TurnedEnabled() && t.turned && live == t.left;
    if (live != t.current && !turned) {  // Written outside the logic since the tick.
      t.valid = false;
      ++counts.lost;
      continue;
    }
    if (turned) ++counts.rewritten;
    t.turned = false;
    Written w;
    w.address = t.matrix;
    w.count = 16;
    w.object = object;
    // A matrix whose scale flag changed between the ticks has a fourth column
    // that means different things at the two ends: it stays at the tick.
    if ((t.previous_type ^ t.current_type) & kScaleFlag) continue;
    BlendMatrix(t.previous.data(), t.current.data(), alpha, w.value.data());
    std::copy(t.current.begin(), t.current.end(), w.live.begin());
    WriteMatrix(base, w.address, w.value);
    written.push_back(w);
    object_written.insert(w.address);
    ++applied.objects;
  }
  // The global matrices [X + 20] of the bone objects X of each mounted
  // object's root (transforms.h), for the whole view block: the texture
  // projector of a material (82155868 -> 82155608 -> 821551B0, types 4 and 5)
  // inverts one of them against the object's matrix, and the logic writes
  // them in the tick with the relative matrices the poses read (global =
  // relative x object, the engine's own 82B97F3C). Tracked by matrix like the
  // bones, with the same cuts; one no root listed for two ticks leaves.
  if (!GlobalsEnabled()) return;
  std::unordered_set<uint32_t> roots_done;
  static std::vector<uint32_t> bones;  // Main thread: a scratch kept across the blocks.
  for (const uint32_t object : candidates) {
    const uint32_t root = Root(base, object);
    if (!roots_done.insert(root).second) continue;
    bones.clear();
    if (!RootBones(base, root, bones)) continue;
    ++counts.roots;
    for (const uint32_t x : bones) {
      uint32_t m = 0;
      if (!Read32(base, x + 20, m) || !m || object_written.count(m)) continue;
      Matrix live;
      if (!ReadFloats(base, m, live.data(), 16)) continue;
      BoneTrack& t = global_tracks[m];
      if (f.tick && t.tick != tick) {
        const bool continuing = t.tick + 1 == tick && t.owner == x;
        t.previous = continuing ? t.current : live;
        t.current = live;
        const bool cut = continuing && MatrixCut(t.previous, t.current);
        if (cut) ++counts.global_cuts;
        t.valid = continuing && !cut && Plausible(live) && Finite(t.previous.data(), 16) && Finite(live.data(), 16);
        t.owner = x;
        t.tick = tick;
      }
      if (!t.valid || t.tick != tick || t.previous == t.current) continue;
      if (live != t.current) {  // Written outside the logic since the tick.
        t.valid = false;
        ++counts.globals_lost;
        continue;
      }
      Written w;
      w.address = m;
      w.count = 16;
      BlendMatrix(t.previous.data(), t.current.data(), alpha, w.value.data());
      std::copy(live.begin(), live.end(), w.live.begin());
      WriteMatrix(base, m, w.value);
      global_written.push_back(w);
      object_written.insert(m);
      ++counts.globals;
    }
  }
}

// The display's camera (82BD9108, r3 the view's entry), before it builds the
// view's camera from the entry: once per display and entry (the display calls
// it again at its end, over the same entry), with a track per entry.
void CameraEntry(uint8_t* base, uint32_t entry) {
  if (!collecting || !entry || !camera_entries_done.insert(entry).second) return;
  const auto& f = tick::Current();
  CameraTrack& camera = cameras[entry];
  if (f.tick) {
    Camera live;
    if (!ReadCamera(base, entry, live)) {
      cameras.erase(entry);
      return;
    }
    TickCamera(camera, entry, live, f.ticks);
    applied.camera_cut = applied.camera_cut || camera.cut;
  }
  applied.camera = ApplyCamera(base, camera, entry, f.ticks, f.alpha) || applied.camera;
}

// After the delivery of the main step: the live transforms come back where
// nothing else wrote. Where the display only turned a written object matrix
// (a billboard: the scale column and the position as written), the tick's
// scale and position go back under the turn: the logic of the next tick reads
// the matrix, and on the console it read its own values turned by the display.
void Restore(uint8_t* base) {
  const bool counting = collecting;
  for (const auto& w : written) {
    if (PutBack(base, w)) continue;
    ++counts.changed;
    Matrix now;
    bool turned = TurnedEnabled() && ReadFloats(base, w.address, now.data(), 16);
    for (int row = 0; row < 3; ++row) turned = turned && now[4 * row + 3] == w.value[4 * row + 3];
    for (int i = 12; i < 16; ++i) turned = turned && now[i] == w.value[i];
    const auto it = tracks.find(w.object);
    if (!turned || it == tracks.end() || it->second.matrix != w.address) continue;
    for (int row = 0; row < 3; ++row) now[4 * row + 3] = w.live[4 * row + 3];
    for (int i = 12; i < 16; ++i) now[i] = w.live[i];
    WriteMatrix(base, w.address, now);
    it->second.turned = true;
    it->second.left = now;
    ++counts.turned;
  }
  written.clear();
  for (const auto& w : global_written)
    if (!PutBack(base, w)) ++counts.globals_changed;
  global_written.clear();
  PutBack(base, camera_written);
  camera_written.clear();
  collecting = false;
  if (counting) Count(tick::Current().tick);
}
}  // namespace

bool Enabled() {
  if (enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_TRANSFORMS");
    enabled = tick::Enabled() && !(text && std::string_view(text) == "0") ? 1 : 0;
  }
  return enabled == 1;
}

const Applied& LastApplied() { return applied; }

uint32_t MountsOtherThread() { return mounts_other_thread.load(); }

void NoteFree(uint32_t pointer) {
  if (!watching.load(std::memory_order_relaxed)) return;
  std::lock_guard lock(freed_mutex);
  if (freed.size() < kFreedMax) freed.push_back(pointer);
  else freed_overflow = true;
}
}  // namespace gpu::interpolation

// The view start and the delivery, only at the main step's return addresses:
// 827F6F38 is also called by 827F7000, and 82BBADE0 by 827F7000, the loading
// thread 827F8AA0 and the online match's leave 82BC43B8 (inside the main
// step); a call from there would clear the tracks.
REX_HOOK_RAW(sub_827F6F38) {
  if (ctx.lr != gpu::interpolation::kViewStartReturn) {
    __imp__sub_827F6F38(ctx, base);
    return;
  }
  gpu::interpolation::SettleEffects(base);  // Before any write of the view block.
  gpu::tick::BeginView(base);
  gpu::interpolation::ViewStart(base);
  __imp__sub_827F6F38(ctx, base);
}
REX_HOOK_RAW(sub_82BBADE0) {
  const bool main_step = ctx.lr == gpu::interpolation::kDeliveryReturn;
  __imp__sub_82BBADE0(ctx, base);
  if (!main_step) return;
  gpu::tick::EndView(base);
  gpu::interpolation::Restore(base);
}
// The mount of the display's lists: the objects go to the tracks only from
// the view block's thread (82749990 also calls it); a mount from another
// thread is counted, not tracked.
REX_HOOK_RAW(sub_82764A08) {
  using namespace gpu::interpolation;
  const bool tracking = collecting && gpu::GuestThread() == main_thread;
  if (collecting && !tracking) ++mounts_other_thread;
  if (tracking) {
    const uint32_t holder = ctx.r3.u32, count = gpu::Get32(base, holder + 8), array = gpu::Get32(base, holder + 4);
    for (uint32_t i = 0; array && i < count && i < 65536; ++i) {
      const uint32_t object = gpu::Get32(base, array + 12 * i);
      if (!object) continue;
      const uint32_t flags = gpu::Get32(base, object + 4);
      if (!(flags & 0x00040000) || (flags & 0x00200004)) continue;
      mounted.push_back(object);
    }
  }
  __imp__sub_82764A08(ctx, base);
}
REX_HOOK_RAW(sub_82BD9108) {
  gpu::interpolation::CameraEntry(base, ctx.r3.u32);
  __imp__sub_82BD9108(ctx, base);
}
REX_HOOK_RAW(sub_82B97DE8) {
  std::vector<gpu::interpolation::Written> pose;
  gpu::interpolation::Pose(base, ctx.r3.u32, ctx.r4.u32, pose);
  __imp__sub_82B97DE8(ctx, base);
  gpu::interpolation::PutBack(base, pose);
}
// The engine's free (r3 the pointer): the tracks of the freed objects.
REX_HOOK_RAW(sub_82120A70) {
  gpu::interpolation::NoteFree(ctx.r3.u32);
  __imp__sub_82120A70(ctx, base);
}
