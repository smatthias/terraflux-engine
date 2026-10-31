"""Compare complete C++ grid-spread frames and metrics to Python reference fixtures."""
import argparse
import json
import os
import subprocess
from pathlib import Path

from app.contracts import SpreadRequest, SpreadResponse
from app.spread_kernel import simulate

DEFAULT_FIXTURES = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "grid_spread_cases.json"


def comparable(result: SpreadResponse) -> dict:
    value = result.model_dump()
    value.pop("engine", None)
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", default=os.environ.get("TERRAFLUX_NATIVE_RUNNER", "/usr/local/bin/terraflux-compute-runner"))
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES)
    arguments = parser.parse_args()

    fixture_document = json.loads(arguments.fixtures.read_text())
    failures: list[dict] = []
    passed = 0
    for case in fixture_document["cases"]:
        request = SpreadRequest.model_validate(case["request"])
        reference = simulate(request)
        completed = subprocess.run(
            [arguments.runner], input=request.model_dump_json() + "\n", text=True,
            capture_output=True, check=False, timeout=15,
        )
        if completed.returncode != 0:
            failures.append({"case": case["name"], "error": completed.stderr.strip() or "native runner failed"})
            continue
        try:
            native = SpreadResponse.model_validate_json(completed.stdout)
        except ValueError as error:
            failures.append({"case": case["name"], "error": f"invalid native response: {error}"})
            continue
        if comparable(reference) != comparable(native):
            failures.append({
                "case": case["name"], "error": "result mismatch",
                "python": comparable(reference), "native": comparable(native),
            })
            continue
        passed += 1

    report = {
        "contractVersion": fixture_document["contractVersion"],
        "runner": arguments.runner,
        "passed": passed,
        "failed": len(failures),
        "failures": failures,
    }
    print(json.dumps(report, indent=2, default=str))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
