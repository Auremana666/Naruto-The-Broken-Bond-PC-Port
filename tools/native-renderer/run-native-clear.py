"""Validate one offscreen clear, every readback pixel and ordered host shutdown."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

WIDTH, HEIGHT, KEY = 67, 37, 0x3000
PIXEL = bytes([17, 85, 204, 255])
EXPECTED = PIXEL * (WIDTH * HEIGHT)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect_image(path):
    actual = path.read_bytes()
    bad_pixels = 0
    first = None
    for pixel in range(max(WIDTH * HEIGHT, (len(actual) + 3) // 4)):
        got, wanted = actual[pixel * 4:pixel * 4 + 4], EXPECTED[pixel * 4:pixel * 4 + 4]
        if got != wanted:
            bad_pixels += 1
            if first is None:
                first = dict(pixel=pixel, x=pixel % WIDTH, y=pixel // WIDTH,
                             actual_rgba=list(got), expected_rgba=list(wanted))
    bad_channels = sum(got != wanted for got, wanted in zip(actual, EXPECTED)) + abs(len(actual) - len(EXPECTED))
    return dict(path=str(path), bytes=len(actual), sha256=hashlib.sha256(actual).hexdigest(),
                expected_sha256=hashlib.sha256(EXPECTED).hexdigest(), width=WIDTH, height=HEIGHT,
                format='R8G8B8A8_UNORM', samples=1, packed_row_bytes=WIDTH * 4,
                expected_rgba=list(PIXEL), pixels_checked=WIDTH * HEIGHT,
                mismatched_pixels=bad_pixels, mismatched_channels=bad_channels, first_mismatch=first)


def validate(path, exit_code, image):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]
    required = ['configuration', 'target', 'initialize', 'surface-create', 'clear-recorded',
                'clear-submit-wait', 'surface-readback', 'pixel-compare', 'image-written',
                'idle-before-release', 'surface-release', 'shutdown', 'release-contract', 'stage-closed',
                'run-after-close-refused', 'post-after-close-refused', 'shutdown-idempotent',
                'objects-destroyed', 'verdict']
    require([record['kind'] for record in records] == required, 'Incomplete or unexpected journal')
    require(all(record.get('ok') is True for record in records[:-1]), 'A harness check failed')
    evidence = {record['kind']: record.get('evidence') for record in records}
    verdict = records[-1]
    require(verdict['passed'] is True and exit_code == verdict['exit_code'] == verdict['expected_exit_code'] == 0,
            'Exit code/verdict mismatch')
    require(verdict['clear'] is True and verdict['readback'] is True and verdict['presentation'] is False
            and verdict['guest_executed'] is False, 'Wrong scope')
    require(evidence['configuration'] == dict(sampler_enabled=False, debug_layer_requested=True,
            guest_executed=False), 'Wrong debug/sampler configuration')
    require(evidence['target'] == dict(key=KEY, width=WIDTH, height=HEIGHT, host_format='R8G8B8A8_UNORM',
            samples=1, resolution_scale=[1, 1], clear_rect=[0, 0, WIDTH, HEIGHT], expected_rgba=list(PIXEL)),
            'Unexpected target or clear contract')
    initialization, shutdown = evidence['initialize'], evidence['shutdown']
    require(initialization['dedicated_owner'] and initialization['deferred_translation'] and
            initialization['owner_thread'] == shutdown['owner_thread'], 'Engine owner changed')
    device = initialization['engine']
    require(device['debug_layer'] is True and device['debug_messages'] == 0 and
            device['fence_signal'] == 1 and device['fence_completed'] >= 1 and device['direct_queue'] is True,
            'D3D12 readiness/debug validation failed')
    surface = evidence['surface-create']
    require(surface['created'] and surface['key'] == KEY and surface['width'] == WIDTH and surface['height'] == HEIGHT
            and surface['samples'] == 1 and surface['host_format'] == 'R8G8B8A8_UNORM' and surface['live_surfaces'] == 1,
            'Wrong created surface')
    clear = evidence['clear-recorded']
    require(clear['cleared'] and clear['fence'] == 2 and clear['color_surfaces'] == 1 and clear['depth'] is False
            and clear['rect'] == [0, 0, WIDTH, HEIGHT] and clear['operations'] == 1,
            'Clear did not record the selected operation')
    require(len(clear['color']) == 4 and all(abs(got - wanted / 255) < 1e-6 for
            got, wanted in zip(clear['color'], PIXEL)), 'Wrong requested clear color')
    submitted = evidence['clear-submit-wait']
    require(submitted['idle'] and submitted['target'] == clear['fence'] and
            submitted['observed_completed'] >= submitted['target'] and submitted['wait_result'] == 0 and
            submitted['device_removed_reason'] == 0 and submitted['batches_closed'] == 1,
            'Clear batch submission/completion not proved')
    read = evidence['surface-readback']
    require(read['read'] and read['key'] == KEY and read['size'] == [WIDTH, HEIGHT] and read['format'] == 28
            and read['samples'] == 1 and read['after_fence'] == 2 and read['completed_before_copy'] >= 2
            and read['read_fence'] == 3 and read['operations'] == 2 and read['row_pitch'] == 512,
            'Wrong readback description or fence ordering')
    require(evidence['pixel-compare'] == dict(width=WIDTH, height=HEIGHT, bytes=len(EXPECTED),
            expected_rgba=list(PIXEL), pixels_compared=WIDTH * HEIGHT, mismatched_pixels=0,
            mismatched_channels=0, first_mismatch=None), 'Harness pixel comparison failed')
    require(image['bytes'] == len(EXPECTED) and image['sha256'] == image['expected_sha256'] and
            image['mismatched_pixels'] == image['mismatched_channels'] == 0,
            f"Independent image comparison failed: {image['first_mismatch']}")
    require(evidence['image-written'] == dict(bytes=len(EXPECTED), pixel_layout='RGBA8', row_order='top-to-bottom'),
            'Image persistence not confirmed')
    idle = evidence['idle-before-release']
    require(idle['idle'] and idle['observed_completed'] >= read['read_fence'] and idle['target'] == 0
            and idle['device_removed_reason'] == 0, 'Readback completion not proved before release')
    release = evidence['surface-release']['surface']
    require(release['destroyed'] and release['key'] == KEY and release['deferred'] is False and
            release['last_use_fence'] == 3 and release['observed_completed'] >= 3 and
            release['live_surfaces'] == release['retired_surfaces'] == 0, 'Surface released before completion')
    counts = evidence['surface-release']['counts']
    require(all(counts[field] == 0 for field in ('textures', 'sampled', 'surfaces', 'retired_surfaces',
            'retired_textures', 'retired_sampled', 'retired_buffers', 'stage_slots', 'depth_resolves',
            'buffers', 'buffer_heaps', 'buffer_blocks_used')) and counts['swapchain'] is False,
            'Live renderer resources remain')
    engine = shutdown['engine']
    require(engine['released'] == ['info-queue', 'fence-event-and-fence', 'queue', 'device', 'interface'] and
            engine['fence_completed'] >= 3 and engine['debug_layer'] and engine['debug_archive_complete'] and
            engine['accepted_diagnostics_cleared'] and engine['unexpected_debug_messages'] ==
            engine['device_removed_reason'] == engine['posted_failure_sequence'] == engine['presents'] == 0,
            'Engine release/debug evidence failed')
    require(engine['batches_closed'] == engine['allocators'] == 1 and engine['upload_pages'] == 0 and
            not engine['open_batch_discarded'] and not engine['swapchain_released'], 'Unexpected GPU operations')
    require(shutdown['dispatcher_shutdown'] == dict(translator_joined=True, pool_threads_joined=0, pool_active=0,
            owner_joined=True, owner_work_pending=0, posted_pending=0, engine_destroyed=True), 'Incomplete joins')
    require(evidence['stage-closed']['closed'] is True and evidence['stage-closed']['pending'] ==
            evidence['stage-closed']['cancelled'] == 0, 'Translation stage did not close empty')
    return device, dict(clear_fence=clear['fence'], read_fence=read['read_fence'],
                        gpu_row_pitch=read['row_pitch'], archived_clear_warnings=engine['archived_clear_warnings'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new evidence directory')
    parser.add_argument('--executable', type=Path, default=Path('build/native-backend-clear/broken_bond_native_backend.exe'))
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=35, help='watchdog seconds; killing always fails')
    args = parser.parse_args()
    if os.name != 'nt' or not 1 <= args.repeat <= 20 or not 10 <= args.timeout <= 60:
        parser.error('Windows, 1..20 repetitions and a 10..60 second watchdog are required')
    repo = Path(__file__).resolve().parents[2]
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = ['CMakeLists.txt', 'scripts/build-native-backend.ps1', 'tools/native-renderer/isolated/link_main.cpp',
               'tools/native-renderer/isolated/CMakeLists.txt', 'tools/native-renderer/run-native-clear.py',
               'src/gpu/backend/engine.cpp', 'src/gpu/backend/engine.h', 'src/gpu/backend/dispatcher.cpp',
               'src/gpu/backend/dispatcher.h', 'src/gpu/diagnostics/sampler.cpp', 'tools/native-renderer/dependencies.json']
    report = dict(schema=1, started_utc=datetime.now(timezone.utc).isoformat(),
                  executable=str(executable), executable_sha256=sha(executable), timeout_seconds=args.timeout,
                  sources={path: sha(repo / path) for path in sources}, runs=[], passed=False)
    def save():
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    save()
    for iteration in range(1, args.repeat + 1):
        name = f'{iteration:02}-color-clear'
        journal, image_path, console = (output / f'{name}{suffix}' for suffix in ('.jsonl', '.rgba', '.console.log'))
        command = [str(executable), 'color-clear', str(journal)]
        run = dict(iteration=iteration, command=command, expected_exit_code=0, exit_code=None,
                   forced_exit=False, passed=False)
        started = time.monotonic()
        try:
            with console.open('xb') as stream:
                process = subprocess.run(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT,
                                         timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            run['exit_code'] = process.returncode
            run['image'] = inspect_image(image_path)
            run['device'], run['gpu'] = validate(journal, process.returncode, run['image'])
            run['passed'] = True
        except subprocess.TimeoutExpired:
            run['forced_exit'] = True
            run['error'] = 'Watchdog killed the owned child; clear validation FAILED'
        except (OSError, ValueError, KeyError, TypeError) as error:
            run['error'] = str(error)
        run['elapsed_seconds'] = round(time.monotonic() - started, 3)
        run['logs'] = {path.name: sha(path) for path in (journal, console, image_path) if path.is_file()}
        report['runs'].append(run)
        save()
        print(f"{name}: exit={run['exit_code']} forced_exit={run['forced_exit']} passed={run['passed']}", flush=True)
    report['passed'] = all(run['passed'] for run in report['runs'])
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
