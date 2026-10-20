"""Run a host test with a fresh evidence path on every CTest invocation."""
import pathlib
import subprocess
import sys
import tempfile

executable, output_root = map(pathlib.Path, sys.argv[1:3])
output_root.mkdir(parents=True, exist_ok=True)
case = pathlib.Path(tempfile.mkdtemp(prefix=executable.stem + "-", dir=output_root))
print(f"Evidence: {case}", flush=True)
result = subprocess.run([str(executable), str(case / "evidence.jsonl")], timeout=80)
sys.exit(result.returncode)
