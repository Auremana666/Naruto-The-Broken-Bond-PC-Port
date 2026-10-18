#include "gpu/backend/dispatcher.h"
#include "gpu/diagnostics/sampler.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <fmt/format.h>

namespace {
using namespace std::chrono_literals;

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Handshakes bound the test work itself. They never terminate the process or
// substitute for Shutdown/join. A missed handshake makes the verdict fail.
class Event {
 public:
  void Set() {
    std::lock_guard lock(mutex_);
    set_ = true;
    wake_.notify_all();
  }
  bool Wait() {
    std::unique_lock lock(mutex_);
    return wake_.wait_for(lock, 5s, [&] { return set_; });
  }
 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  bool set_ = false;
};

template <class Predicate> bool Until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(1ms);
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

struct Journal {
  std::ofstream output;
  bool passed = true;
  explicit Journal(const char* path) : output(path) {}
  void Check(const char* kind, bool ok, const std::string& evidence = "null") {
    passed = passed && ok;
    output << "{\"kind\":\"" << kind << "\",\"ok\":" << (ok ? "true" : "false")
           << ",\"evidence\":" << (evidence.empty() ? "null" : evidence) << "}" << std::endl;
  }
};

class PendingTask final : public gpu::StageTask {
 public:
  PendingTask(std::atomic<unsigned>& ran, std::atomic<unsigned>& destroyed) : ran_(ran), destroyed_(destroyed) {}
  ~PendingTask() override { ++destroyed_; }
  void Run() override { ++ran_; }
 private:
  std::atomic<unsigned>& ran_;
  std::atomic<unsigned>& destroyed_;
};

int RunCase(Journal& log, bool fail) {
  // Captured host state outlives the dispatcher and every callback it owns.
  Event owner_started, owner_release, tail_forwarded, stage_started, stage_release, pool_release;
  std::atomic<unsigned> pool_started{0}, pool_finished{0}, pool_done{0}, tail_ran{0}, pending_ran{0},
                        pending_destroyed{0}, queued_run_ran{0};
  std::atomic<bool> owner_wait_ok{false}, stage_wait_ok{false}, stage_finished{false}, pool_wait_ok{true},
                    pool_engine_ok{true}, tail_owner_ok{true}, pool_stopped{false};
  gpu::Engine* engine_address = nullptr;
  DWORD owner_id = 0;
  gpu::Dispatcher dispatcher(true, true);
  std::string evidence;
  const bool ready = dispatcher.Initialize(evidence);
  log.Check("initialize", ready, evidence);
  if (!ready) {
    const bool down = dispatcher.Shutdown(evidence);
    log.Check("shutdown-after-unexpected-initialization-failure", down, evidence);
    if (!down) { std::fflush(nullptr); std::_Exit(88); }
    return 4;
  }
  const bool identified = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
    engine_address = &engine;
    owner_id = GetCurrentThreadId();
    out = "{\"owner_identified\":true}";
    return true;
  }, evidence);
  log.Check("identify-owner", identified && owner_id != GetCurrentThreadId(), evidence);

  const bool pool = dispatcher.StartPool(2, 2, [&](size_t, gpu::Engine& engine) {
    if (&engine != engine_address) pool_engine_ok = false;
    ++pool_started;
    if (!pool_release.Wait()) pool_wait_ok = false;
    ++pool_finished;
  }, [&](bool stopped) { pool_stopped = stopped; ++pool_done; });
  log.Check("pool-in-flight", pool && Until([&] { return pool_started == 2; }));

  const bool posted = dispatcher.Post([&](gpu::Engine& engine, std::string& out) {
    const bool owner_ok = &engine == engine_address && GetCurrentThreadId() == owner_id;
    owner_started.Set();
    owner_wait_ok = owner_release.Wait() && owner_ok;
    out = fail ? "{\"reason\":\"harness-controlled-posted-failure\",\"injection\":\"first-post-after-readiness\"}"
               : "{\"host_work_finished\":true}";
    return !fail && owner_wait_ok;
  });
  log.Check("owner-in-flight", posted && owner_started.Wait());
  const bool tail = dispatcher.Post([&](gpu::Engine& engine, std::string& out) {
    tail_owner_ok = &engine == engine_address && GetCurrentThreadId() == owner_id;
    ++tail_ran;
    out = "{\"tail_finished\":true}";
    return true;
  });
  dispatcher.Defer([&] { tail_forwarded.Set(); });
  log.Check("owner-tail-pending", tail && tail_forwarded.Wait() && tail_ran == 0);

  dispatcher.Defer([&] {
    stage_started.Set();
    stage_wait_ok = stage_release.Wait();
    stage_finished = true;
  });
  log.Check("translator-in-flight", stage_started.Wait());
  const bool pending = dispatcher.Enqueue(std::make_unique<PendingTask>(pending_ran, pending_destroyed));
  bool waiter_ok = true;
  std::string waiter_evidence;
  std::jthread waiter([&] {
    waiter_ok = dispatcher.Run([&](gpu::Engine&, std::string&) { ++queued_run_ran; return true; }, waiter_evidence);
  });
  const bool stage_pending = pending && Until([&] {
    return Has(dispatcher.StageSummary(), "\"pending\":2");
  });
  log.Check("stage-tasks-pending", stage_pending, "{" + dispatcher.StageSummary() + "}");

  bool down = false;
  std::string shutdown_evidence;
  log.Check("shutdown-request", true, fail ?
      "{\"controlled_failure\":\"first-post-after-readiness\",\"expected_sequence\":1}" :
      "{\"controlled_failure\":null}");
  std::jthread closer([&] { down = dispatcher.Shutdown(shutdown_evidence); });
  // Observe actual admission closure before releasing the active translator;
  // the two pending tasks must be cancelled, independent of scheduling speed.
  log.Check("stage-admission-closed", Until([&] { return Has(dispatcher.StageSummary(), "\"closed\":true"); }));
  stage_release.Set();
  pool_release.Set();
  owner_release.Set();
  waiter.join();
  closer.join();
  log.Check("shutdown", down, shutdown_evidence);
  // A failed drain is diagnostic failure, never a passing timeout/fatal exit.
  // Flush the explicit evidence before the backend's fatal fallback.
  if (!down) { log.output.flush(); std::fflush(nullptr); std::_Exit(88); }

  log.Check("pending-run-cancelled", !waiter_ok && queued_run_ran == 0 &&
      Has(waiter_evidence, "dispatcher-closed"), waiter_evidence);
  log.Check("stage-work-released", stage_wait_ok && stage_finished && pending_ran == 0 && pending_destroyed == 1,
      "{" + dispatcher.StageSummary() + ",\"active_task_finished\":" + (stage_finished ? "true" : "false") +
      ",\"active_wait_ok\":" + (stage_wait_ok ? "true" : "false") +
      ",\"pending_task_runs\":" + std::to_string(pending_ran.load()) +
      ",\"pending_task_destructions\":" + std::to_string(pending_destroyed.load()) + "}");
  log.Check("pool-work-finished", pool_wait_ok && pool_engine_ok && pool_finished == 2 && pool_done == 1,
      std::string("{\"started\":") + std::to_string(pool_started.load()) + ",\"finished\":" +
      std::to_string(pool_finished.load()) + ",\"done_calls\":" + std::to_string(pool_done.load()) +
      ",\"done_observed_stop\":" + (pool_stopped ? "true}" : "false}"));
  log.Check("owner-work-finished", owner_wait_ok && tail_owner_ok && tail_ran == (fail ? 0u : 1u),
      std::string("{\"owner_wait_ok\":") + (owner_wait_ok ? "true" : "false") +
      ",\"tail_owner_ok\":" + (tail_owner_ok ? "true" : "false") +
      ",\"tail_runs\":" + std::to_string(tail_ran.load()) + "}");
  evidence.clear();
  const bool latched = dispatcher.PostedFailure(evidence);
  log.Check("failure-latch", fail ? latched && Has(evidence, "harness-controlled-posted-failure") &&
      Has(evidence, "\"failed_sequence\":1") && Has(evidence, "\"dropped\":1") : !latched, evidence);
  log.Check("release-contract", Has(shutdown_evidence, "\"engine_destroyed\":true") &&
      Has(shutdown_evidence, "\"translator_joined\":true") && Has(shutdown_evidence, "\"owner_joined\":true") &&
      Has(shutdown_evidence, "\"pool_threads_joined\":2") && Has(shutdown_evidence, "\"pool_active\":0") &&
      Has(shutdown_evidence, "\"owner_work_pending\":0") && Has(shutdown_evidence, "\"posted_pending\":0") &&
      Has(shutdown_evidence, "\"batches_closed\":0") && Has(shutdown_evidence, "\"presents\":0") &&
      Has(shutdown_evidence, fail ? "\"posted_failure_sequence\":1" : "\"posted_failure_sequence\":0"));
  const bool late = dispatcher.Run([](gpu::Engine&, std::string&) { return true; }, evidence);
  log.Check("run-after-close-refused", !late && Has(evidence, "dispatcher-closed"), evidence);
  log.Check("post-after-close-refused", !dispatcher.Post([](gpu::Engine&, std::string&) { return true; }));
  const bool again = dispatcher.Shutdown(evidence);
  log.Check("shutdown-idempotent", again && evidence.empty(), evidence);
  return log.passed ? (fail ? 20 : 0) : 1;
}

struct WindowCase {
  HWND window = nullptr;
  HANDLE close_requested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  unsigned close_messages = 0;
  ~WindowCase() {
    if (finished) CloseHandle(finished);
    if (close_requested) CloseHandle(close_requested);
  }
};

int RunColorClear(Journal& log, const std::filesystem::path& image_path, WindowCase* window_case = nullptr) {
  constexpr uint32_t key = 0x3000, width = 67, height = 37;
  constexpr std::array<uint8_t, 4> expected{17, 85, 204, 255};
  // Captures remain alive until all work completes and the dispatcher is gone.
  uint64_t clear_fence = 0;
  uint32_t read_width = 0, read_height = 0;
  std::vector<uint8_t> rgba;
  gpu::Dispatcher dispatcher(true, true);
  std::string evidence;
  log.Check("target", true,
      "{\"key\":12288,\"width\":67,\"height\":37,\"host_format\":\"R8G8B8A8_UNORM\","
      "\"samples\":1,\"resolution_scale\":[1,1],\"clear_rect\":[0,0,67,37],\"expected_rgba\":[17,85,204,255]}");
  const bool ready = dispatcher.Initialize(evidence);
  log.Check("initialize", ready, evidence);
  if (!ready) {
    const bool down = dispatcher.Shutdown(evidence);
    log.Check("shutdown-after-unexpected-initialization-failure", down, evidence);
    if (!down) { log.output.flush(); std::fflush(nullptr); std::_Exit(88); }
    return 4;
  }
  const bool created = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
    gpu::SurfaceDesc desc;
    desc.key = key;
    desc.width = width;
    desc.height = height;
    desc.samples = 1;
    desc.format = gpu::SurfaceFormat::Color8888;
    return engine.CreateSurface(desc, out);
  }, evidence);
  log.Check("surface-create", created, evidence);

  bool cleared = false, submitted = false, read = false;
  if (created) {
    cleared = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
      gpu::ClearRequest request;
      request.color_keys[0] = key;
      request.clear_color = true;
      request.color = {expected[0] / 255.0f, expected[1] / 255.0f, expected[2] / 255.0f, expected[3] / 255.0f};
      request.right = width;
      request.bottom = height;
      return engine.ClearSurfaces(request, clear_fence, out);
    }, evidence);
    log.Check("clear-recorded", cleared && clear_fence != 0, evidence);
    if (cleared) {
      // ClearSurfaces only records. Drain closes/submits that batch and proves
      // its completion before ReadSurface submits a separate readback copy.
      submitted = dispatcher.Drain(evidence);
      log.Check("clear-submit-wait", submitted, evidence);
    }
    if (submitted) {
      read = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
        return engine.ReadSurface(key, clear_fence, rgba, read_width, read_height, out);
      }, evidence);
      log.Check("surface-readback", read, evidence);
    }
  }

  size_t bad_pixels = 0, bad_channels = 0;
  std::string first = "null";
  for (size_t pixel = 0; pixel < rgba.size() / 4; ++pixel) {
    bool bad = false;
    for (size_t channel = 0; channel < 4; ++channel) {
      if (rgba[pixel * 4 + channel] != expected[channel]) { ++bad_channels; bad = true; }
    }
    if (bad) {
      ++bad_pixels;
      if (first == "null") first = fmt::format(
          "{{\"pixel\":{},\"x\":{},\"y\":{},\"actual_rgba\":[{},{},{},{}]}}", pixel, pixel % width, pixel / width,
          rgba[pixel * 4], rgba[pixel * 4 + 1], rgba[pixel * 4 + 2], rgba[pixel * 4 + 3]);
    }
  }
  const bool pixels_ok = read && read_width == width && read_height == height &&
      rgba.size() == size_t(width) * height * 4 && bad_pixels == 0;
  log.Check("pixel-compare", pixels_ok, fmt::format(
      "{{\"width\":{},\"height\":{},\"bytes\":{},\"expected_rgba\":[17,85,204,255],\"pixels_compared\":{},"
      "\"mismatched_pixels\":{},\"mismatched_channels\":{},\"first_mismatch\":{}}}", read_width, read_height,
      rgba.size(), rgba.size() / 4, bad_pixels, bad_channels, first));
  if (read) {
    // Persist the bytes actually returned by the GPU, including on mismatch.
    std::ofstream image(image_path, std::ios::binary);
    image.write(reinterpret_cast<const char*>(rgba.data()), std::streamsize(rgba.size()));
    image.close();
    log.Check("image-written", !image.fail(), fmt::format(
        "{{\"bytes\":{},\"pixel_layout\":\"RGBA8\",\"row_order\":\"top-to-bottom\"}}", rgba.size()));
  }

  if (window_case && pixels_ok) {
    const bool attached = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
      gpu::PresentMode mode;
      mode.allow_tearing = false;
      return engine.AttachWindow(window_case->window, out, mode);
    }, evidence);
    log.Check("window-attach", attached, evidence);
    uint32_t slot = UINT32_MAX;
    const bool staged = attached && dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
      return engine.StageFront(key, slot, out);
    }, evidence);
    log.Check("front-stage", staged, evidence);
    std::vector<uint8_t> bgra;
    uint32_t output_width = 0, output_height = 0;
    const bool presented = staged && dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
      return engine.Present(slot, true, bgra, output_width, output_height, out);
    }, evidence);
    log.Check("window-present", presented && Has(evidence, "\"presented\":true"), evidence);
    size_t mismatches = 0;
    for (size_t i = 0; i < bgra.size(); ++i) {
      if (bgra[i] != expected[std::array<size_t, 4>{2, 1, 0, 3}[i % 4]]) ++mismatches;
    }
    const bool output_ok = presented && output_width == width * 8 && output_height == height * 8 &&
        bgra.size() == size_t(width * 8) * (height * 8) * 4 && mismatches == 0;
    log.Check("present-pixel-compare", output_ok, fmt::format(
        "{{\"width\":{},\"height\":{},\"bytes\":{},\"expected_bgra\":[204,85,17,255],"
        "\"mismatched_channels\":{}}}", output_width, output_height, bgra.size(), mismatches));
    auto back_path = image_path;
    back_path.replace_extension(".bgra");
    std::ofstream back(back_path, std::ios::binary);
    back.write(reinterpret_cast<const char*>(bgra.data()), std::streamsize(bgra.size()));
    back.close();
    log.Check("present-image-written", !back.fail(), fmt::format(
        "{{\"bytes\":{},\"pixel_layout\":\"BGRA8\",\"row_order\":\"top-to-bottom\"}}", bgra.size()));
    // The UI thread keeps pumping while the runner captures this owned HWND.
    // WM_CLOSE requests cleanup; it cannot destroy the swapchain's window.
    log.Check("window-ready-for-capture", output_ok && !back.fail(), fmt::format(
        "{{\"window\":{},\"process\":{},\"client_size\":[536,296]}}",
        reinterpret_cast<uintptr_t>(window_case->window), GetCurrentProcessId()));
    const DWORD wait = WaitForSingleObject(window_case->close_requested, 15000);
    log.Check("window-close-request", wait == WAIT_OBJECT_0 && IsWindow(window_case->window),
        fmt::format("{{\"wait_result\":{},\"window_alive\":{}}}", wait, IsWindow(window_case->window) != FALSE));
  }

  // The readback normally waited already. Keep an explicit proof before
  // releasing the target, and attempt the same drain on an unexpected error.
  const bool idle = dispatcher.Drain(evidence);
  log.Check("idle-before-release", idle, evidence);
  if (created && idle) {
    const bool released = dispatcher.Run([&](gpu::Engine& engine, std::string& out) {
      std::string surface;
      const bool ok = engine.DestroySurface(key, surface);
      out = "{\"surface\":" + surface + ",\"counts\":" + engine.ResourceCounts() + "}";
      return ok;
    }, evidence);
    log.Check("surface-release", released, evidence);
  }
  const bool down = dispatcher.Shutdown(evidence);
  log.Check("shutdown", down, evidence);
  if (!down) { log.output.flush(); std::fflush(nullptr); std::_Exit(88); }
  log.Check("release-contract", Has(evidence, "\"engine_destroyed\":true") &&
      Has(evidence, "\"translator_joined\":true") && Has(evidence, "\"owner_joined\":true") &&
      Has(evidence, "\"pool_threads_joined\":0") && Has(evidence, "\"pool_active\":0") &&
      Has(evidence, "\"owner_work_pending\":0") && Has(evidence, "\"posted_pending\":0") &&
      Has(evidence, window_case ? "\"batches_closed\":2" : "\"batches_closed\":1") &&
      Has(evidence, window_case ? "\"presents\":1" : "\"presents\":0") &&
      (!window_case || Has(evidence, "\"swapchain_released\":true")) &&
      Has(evidence, "\"posted_failure_sequence\":0"));
  log.Check("stage-closed", Has(dispatcher.StageSummary(), "\"pending\":0") &&
      Has(dispatcher.StageSummary(), "\"closed\":true"), "{" + dispatcher.StageSummary() + "}");
  const bool late = dispatcher.Run([](gpu::Engine&, std::string&) { return true; }, evidence);
  log.Check("run-after-close-refused", !late && Has(evidence, "dispatcher-closed"), evidence);
  log.Check("post-after-close-refused", !dispatcher.Post([](gpu::Engine&, std::string&) { return true; }));
  const bool again = dispatcher.Shutdown(evidence);
  log.Check("shutdown-idempotent", again && evidence.empty(), evidence);
  return log.passed ? 0 : 1;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* state = static_cast<WindowCase*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  auto* state = reinterpret_cast<WindowCase*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_CLOSE && state) {
    ++state->close_messages;
    SetEvent(state->close_requested);
    return 0;
  }
  if (message == WM_ERASEBKGND) return 1;
  return DefWindowProcW(window, message, wparam, lparam);
}

int RunWindowPresent(Journal& log, const std::filesystem::path& image_path) {
  SetProcessDPIAware();
  WindowCase state;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const wchar_t* class_name = L"BrokenBondIsolatedNativePresent";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProcedure;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  const bool registered = RegisterClassW(&window_class) != 0;
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  RECT bounds{0, 0, 536, 296};
  const bool adjusted = AdjustWindowRectEx(&bounds, style, FALSE, 0) != FALSE;
  if (registered && adjusted && state.close_requested && state.finished) {
    state.window = CreateWindowExW(0, class_name, L"Broken Bond - isolated native presentation",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, &state);
  }
  RECT client{};
  DWORD process = 0;
  const DWORD ui_thread = GetWindowThreadProcessId(state.window, &process);
  const bool created = state.window && GetClientRect(state.window, &client) &&
      client.right == 536 && client.bottom == 296 && process == GetCurrentProcessId() &&
      ui_thread == GetCurrentThreadId();
  log.Check("window-create", created, fmt::format(
      "{{\"window\":{},\"process\":{},\"ui_thread\":{},\"client_size\":[{},{}],\"resizable\":false}}",
      reinterpret_cast<uintptr_t>(state.window), process, ui_thread, client.right, client.bottom));
  int code = 1;
  if (created) {
    ShowWindow(state.window, SW_SHOWNORMAL);
    UpdateWindow(state.window);
    // DXGI creation/presentation may synchronously message the window. Keep
    // the UI thread available while Dispatcher::Run waits on its owner.
    std::jthread worker([&] {
      code = RunColorClear(log, image_path, &state);
      SetEvent(state.finished);
    });
    while (WaitForSingleObject(state.finished, 0) != WAIT_OBJECT_0) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      MsgWaitForMultipleObjects(1, &state.finished, FALSE, 50, QS_ALLINPUT);
    }
    worker.join();
    log.Check("window-worker-joined", true, fmt::format(
        "{{\"close_messages\":{},\"window_alive\":{}}}", state.close_messages, IsWindow(state.window) != FALSE));
  }
  const bool destroyed = state.window && DestroyWindow(state.window);
  log.Check("window-destroy", destroyed && !IsWindow(state.window));
  const bool unregistered = registered && UnregisterClassW(class_name, instance);
  log.Check("window-class-release", unregistered);
  return log.passed ? code : 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || (std::string(argv[1]) != "normal" && std::string(argv[1]) != "controlled-failure" &&
                   std::string(argv[1]) != "color-clear" && std::string(argv[1]) != "window-present") ||
      std::filesystem::exists(argv[2])) {
    std::fputs("Usage: broken_bond_native_backend <normal|controlled-failure|color-clear|window-present> <new.jsonl>\n", stderr);
    return 2;
  }
  const bool present = std::string(argv[1]) == "window-present";
  const bool clear = std::string(argv[1]) == "color-clear" || present;
  const auto image_path = std::filesystem::path(argv[2]).replace_extension(".rgba");
  if (clear && (image_path == std::filesystem::path(argv[2]) || std::filesystem::exists(image_path))) return 2;
  const auto back_path = std::filesystem::path(argv[2]).replace_extension(".bgra");
  if (present && (back_path == std::filesystem::path(argv[2]) || std::filesystem::exists(back_path))) return 2;
  Journal log(argv[2]);
  if (!log.output) return 3;
  // The imported sampler detaches a perpetual thread when enabled. This
  // harness excludes that profiler instead of leaving a thread to process exit.
  const bool environment = _putenv_s("REX_NATIVE_SAMPLE_PERIOD_US", "0") == 0 &&
                           _putenv_s("REX_NATIVE_D3D12_DEBUG", "1") == 0;
  log.Check("configuration", environment && !gpu::sampler::Enabled(),
      "{\"sampler_enabled\":false,\"debug_layer_requested\":true,\"guest_executed\":false}");
  if (!environment) return 1;
  const bool fail = std::string(argv[1]) == "controlled-failure";
  const int code = present ? RunWindowPresent(log, image_path) : clear ? RunColorClear(log, image_path) : RunCase(log, fail);
  // RunCase returned through scope destruction, including both dispatcher and
  // engine destructors. Neither fatal _Exit(88) nor watchdog kills reach here.
  log.Check("objects-destroyed", true);
  log.output << "{\"kind\":\"verdict\",\"passed\":" << (log.passed && (code == 0 || code == 20) ? "true" : "false")
             << ",\"expected_exit_code\":" << (fail ? 20 : 0) << ",\"exit_code\":" << code
             << ",\"guest_executed\":false,\"clear\":" << (clear ? "true" : "false")
             << ",\"readback\":" << (clear ? "true" : "false")
             << ",\"presentation\":" << (present ? "true" : "false") << "}" << std::endl;
  std::printf("Native backend case %s completed with exit code %d. Log: %s\n", argv[1], code, argv[2]);
  return log.output.good() ? code : 3;
}
