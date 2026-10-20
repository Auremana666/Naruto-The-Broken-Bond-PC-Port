// The failure of an engine session, shared by everything that finds or obeys
// one. The owner retains the failure of a posted operation; a backend retains
// the ones its own services find (the completion of a submission, the Vulkan
// backend's); the admission of posted operations, Drain, the frame
// publication and the stop read it, from any thread.
//
// The first terminal failure is kept with its evidence; later ones are only
// counted. Retain stores it under the lock and only then runs the wakers, so
// whoever wakes finds it, without another operation having to fail.
//
// Authorize is the linearization point of what a failure must stop: a
// submission of the backend, the publication of a guest frame fence. The
// action runs under the same lock as Retain, so it either ran before the
// failure was retained or does not run. An action must not wait for anything
// that can need this lock (another thread's Retain or Authorize): the frame
// publication takes the device's fence lock first and authorizes only the
// guest store inside it.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gpu {

class FailureChannel {
 public:
  // The source of a posted operation's failure (the dispatcher's, or the
  // engine's LatchFailure); a backend's service names its own.
  static constexpr const char* kPostedOperation = "posted-operation";

  struct Failure {
    uint64_t sequence = 0;  // The posted operation, or the submission, it concerns.
    std::string source;     // Who found it: kPostedOperation, or the backend's service.
    std::string evidence;   // A JSON value, with the native result when there is one.
  };

  FailureChannel();
  FailureChannel(const FailureChannel&) = delete;
  FailureChannel& operator=(const FailureChannel&) = delete;

  // The session of the channel: unique in the process, never zero.
  uint64_t Session() const { return session_; }

  // Keeps failure when none was retained yet and returns true; then runs the
  // wakers, outside the lock. A later failure is counted and returns false.
  bool Retain(Failure failure);
  // Lock-free: true once a failure was retained.
  bool Failed() const { return failed_.load(std::memory_order_acquire); }
  std::optional<Failure> First() const;
  uint64_t Later() const;

  // Runs action under the lock unless a failure was retained; true when it ran.
  template <typename Action>
  bool Authorize(Action&& action) {
    std::lock_guard lock(mutex_);
    if (failed_.load(std::memory_order_relaxed)) return false;
    action();
    return true;
  }

  // A waker runs after every retained failure (blocked producers waiting for
  // room, for instance). Register them before the first Retain can happen.
  void OnRetained(std::function<void()> wake);

  // The evidence of an operation refused after the failure:
  // {"refused":true,"reason":"<source>-failed","failed_sequence":N,"failure":...}.
  std::string Refusal() const;

 private:
  const uint64_t session_;
  mutable std::mutex mutex_;
  std::atomic<bool> failed_{false};
  Failure first_;
  uint64_t later_ = 0;
  std::vector<std::function<void()>> wakers_;
};

}  // namespace gpu
