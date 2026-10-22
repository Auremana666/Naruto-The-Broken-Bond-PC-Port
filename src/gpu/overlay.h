// The PC overlay (options menu, title screen, FPS counter, toasts) in the
// native renderer. The SDK's Dear ImGui drawer runs in its detached mode on
// the UI thread and records each frame here (pc/overlay_drawer.h); the native
// renderer's present draws the latest recorded frame over the game image
// (backend/d3d12/overlay_renderer.h).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace gpu::overlay {

// An RGBA8 texture, immutable once created; the renderer uploads it on first use
// and releases its copy when no frame refers to it anymore.
struct Texture {
  uint32_t width = 0, height = 0;
  bool linear = true;
  std::vector<uint8_t> rgba;
};

struct Vertex {
  float x, y, u, v;
  uint32_t color;  // R8G8B8A8, R in the low byte (Dear ImGui's order).
};

struct Command {
  std::shared_ptr<const Texture> texture;  // Null: color only.
  uint32_t first_index = 0, index_count = 0;
  bool scissor = false;
  float left = 0, top = 0, right = 0, bottom = 0;  // In the frame's coordinate space.
};

struct Frame {
  float width = 0, height = 0;  // Coordinate space of the vertices.
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;  // Absolute, into vertices.
  std::vector<Command> commands;
};

// UI thread: the frame just recorded (an empty frame clears the overlay).
void Publish(std::shared_ptr<const Frame> frame);
// Render thread: the most recent frame, or null.
std::shared_ptr<const Frame> Latest();

// The app's request to record a new frame (posted to the UI thread). The
// renderer calls RequestFrame at every present; a request still pending is not
// posted again.
void SetFrameRequest(std::function<void()> request);
void RequestFrame();
void FrameRecorded();
// A game frame was presented; and the time since the last one.
void NotePresent();
std::chrono::steady_clock::duration SinceLastPresent();

// The rate of the native renderer's presents (frames shown per second), for
// the FPS counter; 0 until a second of presents is known.
double PresentRate();

// Ultrawide (PC Options > Display): the aspect ratio the present stretches the
// game's 16:9 image to, after the game widened its camera; 0 keeps the image's own.
void SetTargetAspect(double aspect);
double TargetAspect();

}  // namespace gpu::overlay
