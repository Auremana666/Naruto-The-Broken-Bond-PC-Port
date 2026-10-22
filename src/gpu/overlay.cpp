#include "gpu/overlay.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace gpu::overlay {
namespace {
std::mutex mutex;
std::shared_ptr<const Frame> latest;
std::function<void()> request;
std::atomic<bool> pending{false};
std::mutex rate_mutex;
std::deque<std::chrono::steady_clock::time_point> presents;
std::atomic<double> target_aspect{0.0};

// The requests reach the app from this thread, never from the renderer's: the
// app posts to the UI thread (SDL_PushEvent), which can wait on the UI thread
// while it closes the window, and the close waits on the renderer.
std::mutex pump_mutex;
std::condition_variable pump_wake;
bool pump_wanted = false, pump_running = false;

void Pump() {
  for (;;) {
    std::function<void()> function;
    {
      std::unique_lock lock(pump_mutex);
      pump_wake.wait(lock, [] { return pump_wanted || !pump_running; });
      if (!pump_running) return;
      pump_wanted = false;
    }
    {
      std::lock_guard lock(mutex);
      function = request;
    }
    if (function) {
      function();
    } else {
      pending = false;
    }
  }
}
}  // namespace

void Publish(std::shared_ptr<const Frame> frame) {
  std::lock_guard lock(mutex);
  latest = std::move(frame);
}

std::shared_ptr<const Frame> Latest() {
  std::lock_guard lock(mutex);
  return latest;
}

void SetFrameRequest(std::function<void()> function) {
  const bool run = function != nullptr;
  {
    std::lock_guard lock(mutex);
    request = std::move(function);
    pending = false;
  }
  std::lock_guard lock(pump_mutex);
  if (run && !pump_running) {
    pump_running = true;
    std::thread(Pump).detach();  // Never joined: it may wait on the UI thread at exit.
  } else if (!run) {
    pump_running = false;
  }
  pump_wake.notify_one();
}

void NotePresent() {
  // The present rate over the last second.
  std::lock_guard lock(rate_mutex);
  const auto now = std::chrono::steady_clock::now();
  presents.push_back(now);
  while (!presents.empty() && now - presents.front() > std::chrono::seconds(1)) presents.pop_front();
}

std::chrono::steady_clock::duration SinceLastPresent() {
  std::lock_guard lock(rate_mutex);
  if (presents.empty()) return std::chrono::hours(1);
  return std::chrono::steady_clock::now() - presents.back();
}

void RequestFrame() {
  if (pending.exchange(true)) return;
  {
    std::lock_guard lock(pump_mutex);
    pump_wanted = true;
  }
  pump_wake.notify_one();
}

void FrameRecorded() { pending = false; }

double PresentRate() {
  std::lock_guard lock(rate_mutex);
  if (presents.size() < 2) return 0.0;
  const auto now = std::chrono::steady_clock::now();
  if (now - presents.back() > std::chrono::milliseconds(500)) return 0.0;
  const double seconds = std::chrono::duration<double>(presents.back() - presents.front()).count();
  return seconds > 0.0 ? double(presents.size() - 1) / seconds : 0.0;
}

void SetTargetAspect(double aspect) { target_aspect = aspect; }
double TargetAspect() { return target_aspect; }

}  // namespace gpu::overlay
