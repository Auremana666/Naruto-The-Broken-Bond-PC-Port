"""Find porting candidates in ReXGlue's generated PPC instruction comments.

This is static, generated-code evidence, not an ABI verifier or runtime trace.
No guest code or instruction listings are written to the report. Branch targets
are normalized; operands and constants are otherwise preserved. Short functions
and duplicate signatures must never be treated as unique semantic identities.
"""
import argparse
from collections import Counter, defaultdict
from difflib import SequenceMatcher
import hashlib
import json
from pathlib import Path
import re

DEFINITION = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
INSTRUCTION = re.compile(r"^\s*// ([a-z][a-z0-9.+_-]*)(?:\s+(.*))?$")
ADDRESS = re.compile(r"0x[0-9a-fA-F]+")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalize(instruction):
    mnemonic, _, operands = instruction.partition(" ")
    if mnemonic.startswith("b"):
        operands = ADDRESS.sub("<target>", operands)
    return (mnemonic + " " + operands).strip()


def read_functions(root):
    functions, files = {}, []
    for path in sorted(root.glob("*_recomp.*.cpp")):
        data = path.read_bytes()
        files.append({"path": path.name, "sha256": digest(data)})
        current = None
        for number, line in enumerate(data.decode("utf-8-sig").splitlines(), 1):
            match = DEFINITION.match(line)
            if match:
                current = {"path": path.name, "line": number, "instructions": [], "calls": []}
                functions[match[1]] = current
            elif current is not None and (match := INSTRUCTION.match(line)):
                mnemonic, operands = match.groups()
                operands = operands or ""
                current["instructions"].append(normalize((mnemonic + " " + operands).strip()))
                if mnemonic in ("bl", "b") and ADDRESS.fullmatch(operands):
                    current["calls"].append("sub_" + operands[2:].upper())
    if not files:
        raise ValueError(f"No generated ReXGlue files in {root}")
    for item in functions.values():
        item["signature"] = digest("\n".join(item["instructions"]).encode())
        item["opcodes"] = Counter(x.split()[0] for x in item["instructions"])
    return functions, files


def compare(source, target, seeds):
    exact = defaultdict(list)
    for name, item in target.items():
        exact[item["signature"]].append(name)
    results = []
    for name in sorted(seeds):
        old = source.get(name)
        result = {"source": name, "source_references": seeds[name], "candidates": []}
        results.append(result)
        if not old or not old["instructions"]:
            result["status"] = "source-body-unavailable"
            continue
        size = len(old["instructions"])
        result.update(source_location=f'{old["path"]}:{old["line"]}', instructions=size,
                      signature=old["signature"])
        matches = exact.get(old["signature"], [])
        ranked = []
        if matches:
            result["status"] = "normalized-exact-candidates"
            ranked = [(1.0, other) for other in matches]
        else:
            result["status"] = "similarity-candidates"
            pool = []
            for other, new in target.items():
                count = len(new["instructions"])
                if not count or not 0.65 * size <= count <= 1.5 * size:
                    continue
                overlap = sum((old["opcodes"] & new["opcodes"]).values())
                pool.append((2 * overlap / (size + count), other))
            for _, other in sorted(pool, reverse=True)[:24]:
                score = SequenceMatcher(None, old["instructions"], target[other]["instructions"],
                                        autojunk=False).ratio()
                ranked.append((score, other))
        for score, other in sorted(ranked, reverse=True)[:5]:
            new = target[other]
            result["candidates"].append({"target": other, "score": round(score, 6),
                "instructions": len(new["instructions"]),
                "location": f'{new["path"]}:{new["line"]}', "signature": new["signature"],
                "direct_branch_targets": sorted(set(new["calls"]))})
        result["exact_candidate_count"] = len(matches)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-generated", required=True, type=Path)
    parser.add_argument("--target-generated", required=True, type=Path)
    parser.add_argument("--renderer", required=True, type=Path)
    parser.add_argument("--source-xex", required=True, type=Path)
    parser.add_argument("--target-xex", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists; select a new report path")
    source, source_files = read_functions(args.source_generated)
    target, target_files = read_functions(args.target_generated)
    seeds = defaultdict(list)
    for path in sorted(args.renderer.rglob("*")):
        if path.suffix not in (".h", ".cpp") or "historical" in path.parts:
            continue
        for name in sorted(set(re.findall(r"\bsub_[0-9A-Fa-f]{8}\b", path.read_text(encoding="utf-8-sig")))):
            seeds[name].append(str(path.relative_to(args.renderer)).replace("\\", "/"))
    report = {"schema": 1, "evidence_kind": "local-generated-ppc-comparison",
        "limitations": ["Not REA provider Evidence; no target execution.",
            "XEX hashes identify inputs; generated code freshness is not independently verified here.",
            "Candidates are not validated hooks, ABIs, offsets, or runtime coverage.",
            "Branch destinations are ignored for matching; inspect actual callees before promotion.",
            "Similarity searches only the top 24 opcode-histogram candidates and returns up to five.",
            "Direct branch targets include tail branches and internal labels; indirect calls are absent."],
        "source_xex_sha256": digest(args.source_xex.read_bytes()),
        "target_xex_sha256": digest(args.target_xex.read_bytes()),
        "tool_sha256": digest(Path(__file__).read_bytes()),
        "source_generated": source_files, "target_generated": target_files,
        "source_function_count": len(source), "target_function_count": len(target),
        "results": compare(source, target, seeds)}
    report["summary"] = dict(Counter(x["status"] for x in report["results"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"]))


if __name__ == "__main__":
    main()
