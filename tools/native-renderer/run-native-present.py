"""Capture one owned native window, compare its pixels and verify ordered shutdown."""
import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def load_clear_checker():
    spec = importlib.util.spec_from_file_location('native_clear', Path(__file__).with_name('run-native-clear.py'))
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    return checker


clear = load_clear_checker()
require, sha = clear.require, clear.sha
WIDTH, HEIGHT = clear.WIDTH * 8, clear.HEIGHT * 8
BGRA = bytes([204, 85, 17, 255])


def inspect_output(path, window=False):
    actual = path.read_bytes()
    require(len(actual) == WIDTH * HEIGHT * 4, 'Wrong output byte count')
    channels = (0, 1, 2) if window else (0, 1, 2, 3)
    mismatches = sum(actual[i] != BGRA[i % 4] for i in range(len(actual)) if i % 4 in channels)
    require(mismatches == 0, f'{path.name}: {mismatches} mismatched output channels')
    return dict(path=str(path), sha256=sha(path), bytes=len(actual), width=WIDTH, height=HEIGHT,
                pixels_checked=WIDTH * HEIGHT, channels_checked=list(channels), mismatched_channels=mismatches,
                expected_bgra=list(BGRA), alpha_checked=not window)


def validate(journal, exit_code, image, back, window, capture, pid):
    records = [json.loads(line) for line in journal.read_text(encoding='utf-8').splitlines()]
    required = ['configuration', 'window-create', 'target', 'initialize', 'surface-create', 'clear-recorded',
                'clear-submit-wait', 'surface-readback', 'pixel-compare', 'image-written', 'window-attach',
                'front-stage', 'window-present', 'present-pixel-compare', 'present-image-written',
                'window-ready-for-capture', 'window-close-request', 'idle-before-release', 'surface-release',
                'shutdown', 'release-contract', 'stage-closed', 'run-after-close-refused',
                'post-after-close-refused', 'shutdown-idempotent', 'window-worker-joined', 'window-destroy',
                'window-class-release', 'objects-destroyed', 'verdict']
    require([record['kind'] for record in records] == required, 'Incomplete or unexpected presentation journal')
    require(all(record.get('ok') is True for record in records[:-1]), 'A harness check failed')
    evidence = {record['kind']: record.get('evidence') for record in records}
    verdict = records[-1]
    require(verdict == dict(kind='verdict', passed=True, expected_exit_code=0, exit_code=0,
                           guest_executed=False, clear=True, readback=True, presentation=True)
            and exit_code == 0, 'Wrong scope or terminal verdict')
    require(evidence['configuration'] == dict(sampler_enabled=False, debug_layer_requested=True,
                                              guest_executed=False), 'Wrong environment')
    require(evidence['target'] == dict(key=clear.KEY, width=clear.WIDTH, height=clear.HEIGHT,
            host_format='R8G8B8A8_UNORM', samples=1, resolution_scale=[1, 1],
            clear_rect=[0, 0, clear.WIDTH, clear.HEIGHT], expected_rgba=list(clear.PIXEL)), 'Wrong source image')
    require(image['sha256'] == image['expected_sha256'] and image['mismatched_channels'] == 0,
            'Source image differs from task 03')
    created, ready = evidence['window-create'], evidence['window-ready-for-capture']
    require(created['process'] == ready['process'] == capture['process'] == pid and
            created['window'] == ready['window'] == capture['window'] and created['window'] != 0 and
            created['resizable'] is False and created['client_size'] == ready['client_size'] ==
            capture['client_size'] == [WIDTH, HEIGHT], 'Wrong window identity or size')
    require(capture['method'] == 'PrintWindow' and capture['flags'] == 3 and capture['dwm_flush'] is True
            and capture['visible'] is True and capture['minimized'] is False and
            capture['raw_layout'] == 'BGRX8' and capture['row_order'] == 'top-to-bottom', 'Wrong capture method')
    initialization, shutdown = evidence['initialize'], evidence['shutdown']
    device, engine = initialization['engine'], shutdown['engine']
    require(initialization['dedicated_owner'] and initialization['deferred_translation'] and
            initialization['owner_thread'] == shutdown['owner_thread'] != created['ui_thread'], 'Wrong engine owner')
    require(device['debug_layer'] and device['debug_messages'] == 0 and device['fence_signal'] == 1 and
            device['fence_completed'] >= 1 and device['direct_queue'], 'Readiness not proved')
    require(evidence['surface-create']['width'] == clear.WIDTH and evidence['surface-create']['height'] == clear.HEIGHT
            and evidence['surface-create']['host_format'] == 'R8G8B8A8_UNORM' and
            evidence['surface-create']['samples'] == 1, 'Wrong source surface')
    require(evidence['clear-recorded']['cleared'] and evidence['clear-recorded']['fence'] == 2 and
            evidence['clear-submit-wait']['observed_completed'] >= 2 and
            evidence['surface-readback']['after_fence'] == 2 and evidence['surface-readback']['read_fence'] == 3,
            'Source clear/readback order not proved')
    attach = evidence['window-attach']
    require(attach['attached'] and attach['window'] == created['window'] and attach['size'] == [WIDTH, HEIGHT]
            and attach['format'] == 87 and attach['buffers'] == 2 and attach['max_frame_latency'] == 1 and
            attach['vsync'] and attach['waitable'] and not attach['tearing_option'] and
            attach['brightness'] == 0.5 and attach['effect'] == attach['post_effect'] == 0, 'Wrong presentation mode')
    staged, present = evidence['front-stage'], evidence['window-present']
    require(staged['staged'] and staged['key'] == clear.KEY and staged['slot'] == 0 and staged['fence'] == 4
            and staged['size'] == [clear.WIDTH, clear.HEIGHT] and staged['staged_frames'] == 1, 'Wrong staged source')
    require(present['presented'] and present['captured'] and present['key'] == clear.KEY and present['slot'] == 0
            and present['size'] == [WIDTH, HEIGHT] and present['image'] == [0, 0, WIDTH, HEIGHT] and
            present['fence'] == 4 and present['presents'] == 1 and present['passes'] == 'present' and
            present['vsync'] and not present['tearing'] and not present['resized'] and
            not present['gamma_from_title'] and present['gamma_updates'] == 0, 'Presentation not proved')
    require(evidence['present-pixel-compare'] == dict(width=WIDTH, height=HEIGHT, bytes=WIDTH * HEIGHT * 4,
            expected_bgra=list(BGRA), mismatched_channels=0), 'Harness output comparison failed')
    require(back['alpha_checked'] and not window['alpha_checked'] and back['pixels_checked'] ==
            window['pixels_checked'] == WIDTH * HEIGHT, 'Missing independent output comparisons')
    require(evidence['window-close-request'] == dict(wait_result=0, window_alive=True) and
            evidence['window-worker-joined'] == dict(close_messages=1, window_alive=True), 'Wrong close sequence')
    idle, release = evidence['idle-before-release'], evidence['surface-release']['surface']
    require(idle['idle'] and idle['observed_completed'] >= 5 and idle['device_removed_reason'] == 0 and
            release['destroyed'] and not release['deferred'] and release['last_use_fence'] == 4 and
            release['observed_completed'] >= 5 and release['live_surfaces'] == release['retired_surfaces'] == 0,
            'Release preceded presentation completion')
    counts = evidence['surface-release']['counts']
    require(counts['swapchain'] and counts['stage_slots'] == 1 and counts['surfaces'] == 0,
            'Unexpected presentation resources before shutdown')
    require(engine['swapchain_released'] and engine['presents'] == 1 and engine['fence_completed'] >= 5 and
            engine['batches_closed'] == 2 and not engine['open_batch_discarded'] and
            engine['debug_layer'] and engine['debug_archive_complete'] and engine['accepted_diagnostics_cleared'] and
            engine['unexpected_debug_messages'] == engine['device_removed_reason'] == engine['posted_failure_sequence'] == 0,
            'GPU shutdown/diagnostic check failed')
    require(engine['released'] == ['info-queue', 'fence-event-and-fence', 'queue', 'device', 'interface'] and
            shutdown['dispatcher_shutdown'] == dict(translator_joined=True, pool_threads_joined=0, pool_active=0,
            owner_joined=True, owner_work_pending=0, posted_pending=0, engine_destroyed=True), 'Incomplete release/joins')
    require(evidence['stage-closed']['closed'] and evidence['stage-closed']['pending'] == 0, 'Stage remained open')
    return device, dict(clear_fence=2, source_readback_fence=3, stage_present_fence=4,
                        completed_after_present=engine['fence_completed'],
                        archived_clear_warnings=engine['archived_clear_warnings'])


def wait_ready(journal, process, deadline):
    while time.monotonic() < deadline:
        if journal.exists():
            lines = journal.read_text(encoding='utf-8').splitlines()
            for line in lines:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue  # The child may still be flushing the last record.
                require(record.get('ok', True) is not False, f"Harness failed: {record['kind']}")
                if record['kind'] == 'window-ready-for-capture':
                    return record['evidence']
        require(process.poll() is None, 'Child exited before capture readiness')
        time.sleep(0.02)
    raise subprocess.TimeoutExpired(process.args, 35)


def request_close(window, pid):
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL
    owner = wintypes.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
    require(owner.value == pid, 'Refusing to close a window owned by another process')
    require(user32.PostMessageW(window, 0x10, 0, 0), 'WM_CLOSE failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new evidence directory')
    parser.add_argument('--executable', type=Path, default=Path('build/native-backend-present/broken_bond_native_backend.exe'))
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=35)
    args = parser.parse_args()
    if os.name != 'nt' or not 1 <= args.repeat <= 20 or not 10 <= args.timeout <= 60:
        parser.error('Windows, 1..20 repetitions and a 10..60 second watchdog are required')
    repo = Path(__file__).resolve().parents[2]
    executable = args.executable.resolve(strict=True)
    powershell = shutil.which('pwsh') or shutil.which('powershell')
    require(powershell, 'PowerShell is required for owned window capture')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = ['tools/native-renderer/isolated/link_main.cpp', 'tools/native-renderer/isolated/CMakeLists.txt',
               'tools/native-renderer/run-native-present.py', 'tools/native-renderer/capture-native-window.ps1',
               'tools/native-renderer/run-native-clear.py', 'scripts/build-native-backend.ps1',
               'tools/native-renderer/plume-engine.cmake', 'tools/native-renderer/pinned-source.cmake',
               'tools/native-renderer/dependencies.json', 'src/gpu/backend/engine.cpp', 'src/gpu/backend/engine.h',
               'src/gpu/backend/dispatcher.cpp', 'src/gpu/backend/dispatcher.h', 'src/gpu/diagnostics/sampler.cpp']
    report = dict(schema=1, started_utc=datetime.now(timezone.utc).isoformat(),
                  executable=str(executable), executable_sha256=sha(executable), timeout_seconds=args.timeout,
                  sources={path: sha(repo / path) for path in sources}, runs=[], passed=False)

    def save():
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    save()
    for iteration in range(1, args.repeat + 1):
        name = f'{iteration:02}-window-present'
        journal, rgba, bgra, png, window_raw, console, capture_log = (
            output / f'{name}{suffix}' for suffix in
            ('.jsonl', '.rgba', '.bgra', '.window.png', '.window.bgra', '.console.log', '.capture.log'))
        command = [str(executable), 'window-present', str(journal)]
        run = dict(iteration=iteration, command=command, exit_code=None, forced_exit=False, passed=False)
        started = time.monotonic()
        deadline = started + args.timeout
        process = None
        try:
            with console.open('xb') as stream:
                process = subprocess.Popen(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                run['pid'] = process.pid
                ready = wait_ready(journal, process, deadline)
                capture_command = [powershell, '-NoProfile', '-File', str(repo / 'tools/native-renderer/capture-native-window.ps1'),
                                   '-ProcessId', str(process.pid), '-WindowHandle', str(ready['window']),
                                   '-OutputPath', str(png), '-RawOutputPath', str(window_raw)]
                run['capture_command'] = capture_command
                try:
                    capture = subprocess.run(capture_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             timeout=max(0.01, deadline - time.monotonic()),
                                             creationflags=subprocess.CREATE_NO_WINDOW)
                    capture_log.write_bytes(capture.stdout)
                    require(capture.returncode == 0, 'Owned window capture failed; see capture log')
                    run['capture'] = json.loads(capture.stdout.decode('utf-8-sig'))
                finally:
                    request_close(ready['window'], process.pid)
                run['exit_code'] = process.wait(timeout=max(0.01, deadline - time.monotonic()))
            run['source_image'] = clear.inspect_image(rgba)
            run['back_buffer'] = inspect_output(bgra)
            run['window_image'] = inspect_output(window_raw, window=True)
            run['device'], run['gpu'] = validate(journal, run['exit_code'], run['source_image'],
                run['back_buffer'], run['window_image'], run['capture'], process.pid)
            run['passed'] = True
        except subprocess.TimeoutExpired:
            run['error'] = 'Watchdog expired; presentation validation FAILED'
        except (OSError, ValueError, KeyError, TypeError) as error:
            run['error'] = str(error)
        finally:
            if process is not None and process.poll() is None:
                # A rejected capture still permits the harness's own close
                # timeout/ordered cleanup before resorting to a forced kill.
                try:
                    process.wait(timeout=max(0.01, deadline - time.monotonic()))
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    run['forced_exit'] = True
            if process is not None:
                run['exit_code'] = process.returncode
        run['elapsed_seconds'] = round(time.monotonic() - started, 3)
        run['logs'] = {path.name: sha(path) for path in (journal, rgba, bgra, png, window_raw, console, capture_log)
                       if path.is_file()}
        report['runs'].append(run)
        save()
        print(f"{name}: exit={run['exit_code']} forced_exit={run['forced_exit']} passed={run['passed']}", flush=True)
    report['passed'] = all(run['passed'] for run in report['runs'])
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
