#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <windows.h>

namespace gpu {

struct TitleWaitOrigin {
  uint32_t guest = 0, tid = 0, guest_handle = 0;
  uint64_t wrapper = 0, owner_value = 0, caller_lr = 0;
  uintptr_t object = 0, backend = 0, native_handle = 0;
  bool wrapper_seen = false, helper_seen = false, original_wait_active = false;
};

struct TitleWitnessEpoch {
  uint64_t sequence = 0;
  uint32_t active_mutations = 0;
  bool armed = false, invalid = false;
};

struct TitleHostWorker {
  uint32_t kind = 0, id = 0, tid = 0;
  uintptr_t object = 0, owner_value = 0;
  bool active = false;
};

class TitleWaitWitness {
 public:
  bool Arm(uint32_t version, uint32_t hooks) {
    std::lock_guard lock(mutex_);
    if (armed_ || version != 1 || hooks < 10 || hooks > 128) return false;
    armed_ = true;
    hook_count_ = hooks;
    return true;
  }
  void BindWrapper(uint32_t guest, uint32_t tid, uint64_t wrapper, uint64_t owner, uint64_t lr) {
    std::lock_guard lock(mutex_);
    auto* row = Guest(guest);
    if (!row || row->wrapper_seen || !tid || !wrapper) { invalid_ = true; return; }
    row->guest = guest;
    row->tid = tid;
    row->wrapper = wrapper;
    row->owner_value = owner;
    row->caller_lr = lr;
    row->wrapper_seen = true;
    ++sequence_;
  }
  void BindHelper(uint32_t guest, uint32_t tid, uint64_t handle, uint64_t timeout, uint64_t alertable) {
    std::lock_guard lock(mutex_);
    auto* row = Guest(guest);
    if (!row || !row->wrapper_seen || row->helper_seen || row->tid != tid ||
        !handle || (timeout & UINT32_MAX) != UINT32_MAX || (alertable & 255) != 0) {
      invalid_ = true; return;
    }
    row->guest_handle = uint32_t(handle);
    row->helper_seen = true;
    ++sequence_;
  }
  bool OriginalWait(uint32_t tid, uintptr_t object, uintptr_t backend, uintptr_t native_handle, bool entering) {
    std::lock_guard lock(mutex_);
    auto* row = Thread(tid);
    if (!row || !row->helper_seen) { invalid_ = true; return !terminal_leaves_; }
    if (!entering) { row->original_wait_active = false; if (protected_) invalid_ = true; }
    else {
      if (row->original_wait_active || !object || !backend || !native_handle) invalid_ = true;
      row->object = object;
      row->backend = backend;
      row->native_handle = native_handle;
      row->original_wait_active = true;
    }
    ++sequence_;
    return !(terminal_leaves_ && invalid_);
  }
  void Protect(uint32_t cell, uintptr_t host_base, bool terminal_leaves = false,
               int64_t started_qpc = 0, int64_t frequency = 0) {
    std::lock_guard lock(mutex_);
    if (!cell || !host_base || protected_) { invalid_ = true; return; }
    cell_ = cell;
    host_base_ = host_base;
    protected_ = true;
    terminal_leaves_ = terminal_leaves;
    started_qpc_ = started_qpc;
    frequency_ = frequency;
    ++sequence_;
  }
  // Observation of original mutations only. Even irrelevant operations are
  // counted while in flight, so a baseline cannot hide an earlier entry.
  bool Mutation(uint32_t operation, uintptr_t address, uint64_t bytes, bool entering) {
    std::lock_guard lock(mutex_);
    ++sequence_;
    if (entering) {
      ++active_mutations_;
      // A rejected cold variant may have delegated UI/deferred work before
      // the baseline. Keep that history invalidating instead of clearing it.
      if (operation == 0) { invalid_ = true; ++delegated_producers_; }
      bool watched_handle = false;
      if (operation == 4 || operation == 8)
        for (const auto& row : rows_) watched_handle = watched_handle || address == row.native_handle;
      if (operation == 4 || operation == 8)
        for (const auto handle : native_threads_) watched_handle = watched_handle || address == handle;
      const bool global = operation <= 3 || ((operation == 4 || operation == 8) && watched_handle) ||
          (operation == 8 && address == 0);
      const bool guest = operation == 5 || operation == 6;
      const bool relevant = global || (operation != 4 && operation != 8 &&
          (guest ? GuestOverlap(address, bytes) : HostOverlap(address, bytes)));
      const bool own_retirement = operation == 3 && bytes == 0 &&
          (address == retired_objects_[0] || address == retired_objects_[1]) && address != 0;
      const bool own_close = operation == 4 && bytes == 0 &&
          (address == retired_handles_[0] || address == retired_handles_[1]) && address != 0;
      if (protected_ && relevant && !own_retirement && !own_close) invalid_ = true;
    } else {
      if (!active_mutations_) invalid_ = true;
      else --active_mutations_;
    }
    return !(terminal_leaves_ && invalid_);
  }
  bool Copy(std::array<TitleWaitOrigin, 4>& rows, TitleWitnessEpoch& epoch) const {
    std::lock_guard lock(mutex_);
    rows = rows_;
    epoch = {sequence_, active_mutations_, armed_, invalid_};
    if (!armed_ || invalid_ || active_mutations_) return false;
    for (size_t i = 0; i != rows.size(); ++i)
      if (rows[i].guest != 5 + i || !rows[i].wrapper_seen || !rows[i].helper_seen ||
          !rows[i].original_wait_active) return false;
    return true;
  }
  bool Unchanged(const TitleWitnessEpoch& before) const {
    std::lock_guard lock(mutex_);
    return armed_ && !invalid_ && !active_mutations_ && sequence_ == before.sequence;
  }
  void Invalidate() { std::lock_guard lock(mutex_); invalid_ = true; ++sequence_; }
  bool ContinueOriginal() const {
    std::lock_guard lock(mutex_);
    return !terminal_leaves_ || !invalid_;
  }
  bool CompletionReady() const {
    uint64_t sequence;
    int64_t started, frequency;
    {
      std::lock_guard lock(mutex_);
      if (!armed_ || !protected_ || !terminal_leaves_ || invalid_ || active_mutations_ ||
          !private_closing_ || !private_exit_seen_[0] || !private_exit_seen_[1] ||
          !owner_generation_ || !owner_exit_permitted_ || !owner_exit_seen_ || !owner_joined_ ||
          started_qpc_ <= 0 || frequency_ <= 0 || frequency_ > INT64_MAX / 10) return false;
      sequence = sequence_;
      started = started_qpc_;
      frequency = frequency_;
    }
    LARGE_INTEGER qpc{};
    if (!QueryPerformanceCounter(&qpc) || qpc.QuadPart < started ||
        qpc.QuadPart - started > frequency * 10) return false;
    std::lock_guard lock(mutex_);
    return armed_ && protected_ && terminal_leaves_ && !invalid_ &&
        !active_mutations_ && sequence_ == sequence;
  }
  uint64_t DelegatedProducers() const {
    std::lock_guard lock(mutex_);
    return delegated_producers_;
  }
  bool PermitRetirement(uintptr_t first, uintptr_t second, uintptr_t first_handle, uintptr_t second_handle) {
    std::lock_guard lock(mutex_);
    if (!armed_ || invalid_ || active_mutations_ || !protected_ ||
        !private_closing_ || !private_exit_seen_[0] || !private_exit_seen_[1] ||
        !first || !second || first == second || !first_handle || !second_handle ||
        first_handle == second_handle || retired_objects_[0] || retired_objects_[1]) return false;
    retired_objects_ = {first, second};
    retired_handles_ = {first_handle, second_handle};
    ++sequence_;
    return true;
  }
  bool BindNativeThread(size_t index, uintptr_t handle) {
    std::lock_guard lock(mutex_);
    if (!armed_ || invalid_ || index >= native_threads_.size() || !handle || native_threads_[index]) return false;
    native_threads_[index] = handle;
    ++sequence_;
    return true;
  }
  bool BindPrivateThread(size_t index, uint32_t id, uint32_t tid) {
    std::lock_guard lock(mutex_);
    if (!armed_ || invalid_ || !protected_ || index >= 2 || !id || !tid || private_ids_[index]) return false;
    const size_t other = 1 - index;
    if (private_ids_[other] == id || private_tids_[other] == tid) return false;
    private_ids_[index] = id;
    private_tids_[index] = tid;
    ++sequence_;
    return true;
  }
  bool PermitPrivateSelfExit(const std::array<uint32_t, 2>& ids, const std::array<uint32_t, 2>& tids) {
    std::lock_guard lock(mutex_);
    if (!armed_ || invalid_ || active_mutations_ || !protected_ || !terminal_leaves_ ||
        private_closing_ || !private_ids_[0] || !private_ids_[1] || ids != private_ids_ || tids != private_tids_) return false;
    private_closing_ = true;
    ++sequence_;
    return true;
  }
  // Identity of the dedicated dispatcher owner (a plain std::thread outside the
  // ten SDK actors). Bound once, by the creator, before any title work. Without
  // an armed journal there is nothing to bind, so other processes are unaffected.
  bool BindDispatcherOwner(uint64_t generation, uintptr_t handle, uint32_t tid, uint64_t create_time) {
    std::lock_guard lock(mutex_);
    if (!armed_) return true;
    if (invalid_ || !generation || !handle || !tid || !create_time || owner_generation_) return false;
    owner_generation_ = generation;
    owner_handle_ = handle;
    owner_tid_ = tid;
    owner_create_time_ = create_time;
    ++sequence_;
    return true;
  }
  // Called by the owner itself, only after its engine was shut down and
  // destroyed on that thread, immediately before publishing stopped. Needs both
  // private workers already seen leaving, so it never precedes their joins.
  bool PermitOwnerSelfExit(uint64_t generation, uint32_t tid) {
    std::lock_guard lock(mutex_);
    if (!armed_) return true;
    if (!protected_) return !invalid_;
    if (invalid_ || active_mutations_ || !terminal_leaves_ || !private_closing_ ||
        !private_exit_seen_[0] || !private_exit_seen_[1] || !owner_generation_ ||
        generation != owner_generation_ || tid != owner_tid_ || owner_exit_permitted_) return false;
    owner_exit_permitted_ = true;
    ++sequence_;
    return true;
  }
  // The creator's real std::thread::join has returned for exactly this owner.
  bool OwnerJoined(uint64_t generation, uint32_t tid) {
    std::lock_guard lock(mutex_);
    if (!armed_) return true;
    if (!protected_) return !invalid_;
    if (invalid_ || generation != owner_generation_ || tid != owner_tid_ || !owner_exit_permitted_ ||
        !owner_exit_seen_ || owner_joined_) { invalid_ = true; return false; }
    owner_joined_ = true;
    ++sequence_;
    return true;
  }
  bool OriginalPrivateSelfExit(uint32_t tid, uint32_t status, bool entering, uint64_t create_time = 0) {
    std::lock_guard lock(mutex_);
    ++sequence_;
    // Before protection this remains an observation of an original OS call.
    if (!protected_) return !invalid_;
    // The dispatcher owner is separate from the private worker pair: it must match
    // its bound TID and creation time and leave once, after its own permit.
    if (owner_tid_ && tid == owner_tid_) {
      if (!owner_exit_permitted_ || !entering || status != 0 || create_time != owner_create_time_ ||
          owner_exit_seen_ || owner_joined_) invalid_ = true;
      else owner_exit_seen_ = true;
      return !invalid_;
    }
    size_t index = 2;
    for (size_t i = 0; i != 2; ++i) if (private_tids_[i] == tid) index = i;
    if (!private_closing_ || !entering || status != 0 || index == 2 ||
        (index < 2 && private_exit_seen_[index])) invalid_ = true;
    else private_exit_seen_[index] = true;
    // Success of the original syscall never returns. A leave callback is a
    // failed variant and must stop before any Windows/SDK continuation.
    return !invalid_;
  }
  void HostWorker(uint32_t kind, uint32_t id, uint32_t tid,
                  uintptr_t object, uintptr_t owner, bool entering) {
    std::lock_guard lock(mutex_);
    ++sequence_;
    if (kind < 1 || kind > 3 || id < 1 || id > 10 || !tid || !object || !owner) {
      invalid_ = true; return;
    }
    auto& row = host_workers_[kind - 1];
    if (entering) {
      if (row.active || row.object) { invalid_ = true; return; }
      for (const auto& other : host_workers_)
        if (other.object && (other.id == id || other.tid == tid || other.object == object)) {
          invalid_ = true; return;
        }
      row = {kind, id, tid, object, owner, true};
    } else {
      if (!row.active || row.id != id || row.tid != tid || row.object != object || row.owner_value != owner)
        invalid_ = true;
      row.active = false;
      if (protected_) invalid_ = true;
    }
  }
  bool CopyHostWorkers(std::array<TitleHostWorker, 3>& rows) const {
    std::lock_guard lock(mutex_);
    rows = host_workers_;
    return armed_ && !invalid_ && rows[0].active && rows[1].active && rows[2].active;
  }

 private:
  TitleWaitOrigin* Guest(uint32_t guest) { return guest >= 5 && guest <= 8 ? &rows_[guest - 5] : nullptr; }
  TitleWaitOrigin* Thread(uint32_t tid) {
    for (auto& row : rows_) if (row.tid == tid && tid) return &row;
    return nullptr;
  }
  static bool Overlap(uintptr_t at, uint64_t bytes, uintptr_t root, uint64_t size) {
    if (!at || !root) return false;
    if (!bytes) return at == root;
    if (bytes > UINTPTR_MAX - at) return true;
    return at < root + size && root < at + bytes;
  }
  bool GuestOverlap(uintptr_t at, uint64_t bytes) const {
    const bool allocation_release = bytes == 0 && (at == cell_ || at == 0x82000000);
    return allocation_release || Overlap(at, bytes, cell_, 4096) ||
        Overlap(at, bytes, 0x82000000, 0x1000);
  }
  bool HostOverlap(uintptr_t at, uint64_t bytes) const {
    if (!host_base_) return false;
    return (bytes == 0 && at == host_base_) ||
        Overlap(at, bytes, host_base_ + cell_, 4096) ||
        Overlap(at, bytes, host_base_ + 0x82000000, 0x1000);
  }
  mutable std::mutex mutex_;
  std::array<TitleWaitOrigin, 4> rows_{};
  std::array<uintptr_t, 2> retired_objects_{};
  std::array<uintptr_t, 2> retired_handles_{};
  std::array<uintptr_t, 10> native_threads_{};
  std::array<uint32_t, 2> private_ids_{}, private_tids_{};
  std::array<bool, 2> private_exit_seen_{};
  bool private_closing_ = false;
  uint64_t owner_generation_ = 0, owner_create_time_ = 0;
  uintptr_t owner_handle_ = 0;
  uint32_t owner_tid_ = 0;
  bool owner_exit_permitted_ = false, owner_exit_seen_ = false, owner_joined_ = false;
  std::array<TitleHostWorker, 3> host_workers_{};
  uint64_t sequence_ = 0;
  int64_t started_qpc_ = 0, frequency_ = 0;
  uint64_t delegated_producers_ = 0;
  uint32_t hook_count_ = 0, active_mutations_ = 0, cell_ = 0;
  uintptr_t host_base_ = 0;
  bool armed_ = false, invalid_ = false, protected_ = false, terminal_leaves_ = false;
};

inline TitleWaitWitness title_wait_witness;
inline std::atomic<uint32_t> title_diagnostic_end_count{0};

}  // namespace gpu

extern "C" __declspec(dllexport) uint32_t NativeTitleWitnessArm(uint32_t version, uint32_t hooks);
extern "C" __declspec(dllexport) void NativeTitleWitnessWait(uint32_t tid, uintptr_t object,
                                                          uintptr_t backend, uintptr_t handle, uint32_t entering);
extern "C" __declspec(dllexport) void NativeTitleWitnessMutation(uint32_t operation, uintptr_t address,
                                                               uint64_t bytes, uint32_t entering);
extern "C" __declspec(dllexport) void NativeTitleWitnessDiagnosticEnd();
extern "C" __declspec(dllexport) __declspec(noreturn) void NativeTitleWitnessFault();
extern "C" __declspec(dllexport) __declspec(noreturn) void NativeTitleWitnessComplete(uint32_t exit_code);
extern "C" __declspec(dllexport) void NativeTitleWitnessHostWorker(uint32_t kind, uintptr_t owner, uint32_t entering);
extern "C" __declspec(dllexport) void NativeTitleWitnessSelfExit(uint32_t tid, uintptr_t caller,
                                                               uint32_t status, uint32_t entering);
