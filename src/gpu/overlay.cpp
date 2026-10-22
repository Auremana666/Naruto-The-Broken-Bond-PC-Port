#include "gpu/overlay.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

namespace gpu::overlay {
namespace {
std::mutex mutex;
std::shared_ptr<const Frame> latest;
std::function<void()> request;
std::atomic<bool> pending{false};
std::mutex rate_mutex;
std::deque<std::chrono::steady_clock::time_point> presents;
std::atomic<double> target_aspect{0.0};
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
  std::lock_guard lock(mutex);
  request = std::move(function);
  pending = false;
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
  std::function<void()> function;
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
