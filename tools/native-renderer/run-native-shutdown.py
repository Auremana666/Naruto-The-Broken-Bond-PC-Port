"""Run only isolated backend readiness/shutdown, with a failing timeout watchdog."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(path, case, exit_code):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]
    kinds = [record['kind'] for record in records]
    required = ['configuration', 'initialize', 'identify-owner', 'pool-in-flight', 'owner-in-flight',
                'owner-tail-pending', 'translator-in-flight', 'stage-tasks-pending', 'shutdown-request',
                'stage-admission-closed', 'shutdown', 'pending-run-cancelled', 'stage-work-released',
                'pool-work-finished', 'owner-work-finished', 'failure-latch', 'release-contract',
                'run-after-close-refused', 'post-after-close-refused', 'shutdown-idempotent',
                'objects-destroyed', 'verdict']
    require(kinds == required, f'Incomplete or unexpected journal: {kinds}')
    require(all(record.get('ok') is True for record in records[:-1]), 'A harness check failed')
    evidence = {record['kind']: record.get('evidence') for record in records}
    verdict = records[-1]
    expected = 20 if case == 'controlled-failure' else 0
    require(verdict['passed'] is True and exit_code == expected == verdict['exit_code'] ==
            verdict['expected_exit_code'], 'Exit code/verdict mismatch')
    require(all(verdict[field] is False for field in ('guest_executed', 'clear', 'readback', 'presentation')),
            'Case crossed its scope boundary')
    require(evidence['configuration'] == dict(sampler_enabled=False, debug_layer_requested=True,
                                            guest_executed=False), 'Unsupported environment')
    initialized = evidence['initialize']
    shutdown = evidence['shutdown']
    require(initialized['dedicated_owner'] and initialized['deferred_translation'], 'Wrong dispatcher mode')
    require(initialized['owner_thread'] == shutdown['owner_thread'], 'Engine released on a different owner')
    require(initialized['engine']['debug_layer'] is True and initialized['engine']['debug_messages'] == 0,
            'Debug readiness was not established')
    engine = shutdown['engine']
    require(engine['released'] == ['info-queue', 'fence-event-and-fence', 'queue', 'device', 'interface'],
            'Unexpected engine release order')
    require(engine['fence_completed'] >= initialized['engine']['fence_signal'], 'Readiness completion lost')
    require(engine['debug_archive_complete'] and engine['accepted_diagnostics_cleared'] and
            engine['unexpected_debug_messages'] == engine['device_removed_reason'] == 0,
            'Incomplete or failing D3D12 diagnostics')
    require(engine['batches_closed'] == engine['allocators'] == engine['upload_pages'] == engine['presents'] == 0
            and not engine['open_batch_discarded'] and not engine['swapchain_released'],
            'Unexpected rendering work/resources')
    require(engine['posted_failure_sequence'] == (1 if expected else 0), 'Failure sequence mismatch')
    require(shutdown['dispatcher_shutdown'] == dict(translator_joined=True, pool_threads_joined=2,
            pool_active=0, owner_joined=True, owner_work_pending=0, posted_pending=0, engine_destroyed=True),
            'Incomplete dispatcher shutdown')
    require(evidence['stage-tasks-pending']['pending'] == 2 and
            evidence['stage-tasks-pending']['closed'] is False, 'The two tasks were not pending before shutdown')
    stage = evidence['stage-work-released']
    require(stage['pending'] == 0 and stage['closed'] is True and stage['cancelled'] == 2,
            'Pending translation tasks did not cancel')
    require(stage['active_task_finished'] and stage['active_wait_ok'] and stage['pending_task_runs'] == 0
            and stage['pending_task_destructions'] == 1, 'Stage work outlived its owner')
    require(evidence['pool-work-finished']['finished'] == 2 and
            evidence['pool-work-finished']['done_calls'] == 1, 'Pool callbacks survived shutdown')
    require(evidence['owner-work-finished'] == dict(owner_wait_ok=True, tail_owner_ok=True,
            tail_runs=0 if expected else 1), 'Owner work did not finish or drop as expected')
    if expected:
        failure = evidence['failure-latch']
        require(failure['failed_sequence'] == 1 and failure['posted'] == 2 and failure['dropped'] == 1 and
                failure['engine'] == dict(reason='harness-controlled-posted-failure',
                                          injection='first-post-after-readiness'), 'Original failure was lost')
    else:
        require(evidence['failure-latch'] is None, 'Unexpected posted failure')
    return initialized['engine']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new evidence directory')
    parser.add_argument('--executable', type=Path, default=Path('build/native-backend-shutdown/broken_bond_native_backend.exe'))
    parser.add_argument('--repeat', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=35, help='watchdog seconds; killing always fails')
    args = parser.parse_args()
    if os.name != 'nt' or not 1 <= args.repeat <= 20 or not 10 <= args.timeout <= 60:
        parser.error('Windows, 1..20 repetitions and a 10..60 second watchdog are required')
    repo = Path(__file__).resolve().parents[2]
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = ['tools/native-renderer/isolated/link_main.cpp', 'tools/native-renderer/isolated/CMakeLists.txt',
               'tools/native-renderer/run-native-shutdown.py', 'src/gpu/backend/engine.cpp',
               'src/gpu/backend/dispatcher.cpp', 'src/gpu/backend/dispatcher.h',
               'src/gpu/diagnostics/sampler.cpp', 'tools/native-renderer/dependencies.json']
    report = dict(schema=1, started_utc=datetime.now(timezone.utc).isoformat(),
                  executable=str(executable), executable_sha256=sha(executable), timeout_seconds=args.timeout,
                  sources={path: sha(repo / path) for path in sources}, runs=[], passed=False)
    def save():
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    save()
    for iteration in range(1, args.repeat + 1):
        for case in ('normal', 'controlled-failure'):
            name = f'{iteration:02}-{case}'
            journal = output / f'{name}.jsonl'
            console = output / f'{name}.console.log'
            command = [str(executable), case, str(journal)]
            run = dict(case=case, iteration=iteration, command=command, expected_exit_code=20 if
                       case == 'controlled-failure' else 0, exit_code=None, forced_exit=False, passed=False)
            started = time.monotonic()
            try:
                with console.open('xb') as stream:
                    process = subprocess.run(command, cwd=output, stdout=stream, stderr=subprocess.STDOUT,
                                             timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW)
                run['exit_code'] = process.returncode
                run['device'] = validate(journal, case, process.returncode)
                run['passed'] = True
            except subprocess.TimeoutExpired:
                # subprocess.run kills and reaps only its owned child on timeout.
                run['forced_exit'] = True
                run['error'] = 'Watchdog killed the child; shutdown validation FAILED'
            except (OSError, ValueError, KeyError, TypeError) as error:
                run['error'] = str(error)
            run['elapsed_seconds'] = round(time.monotonic() - started, 3)
            run['logs'] = {path.name: sha(path) for path in (journal, console) if path.is_file()}
            report['runs'].append(run)
            save()
            print(f"{name}: exit={run['exit_code']} forced_exit={run['forced_exit']} passed={run['passed']}", flush=True)
    report['passed'] = all(run['passed'] for run in report['runs'])
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
