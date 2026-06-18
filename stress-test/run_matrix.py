#!/usr/bin/env python3
"""Run the ChatService benchmark at several concurrency levels."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_LEVELS = (10, 50, 100, 200, 500, 1000, 2000)


def parse_levels(value: str) -> list[int]:
    try:
        levels = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "levels must be comma-separated integers"
        ) from exc
    if not levels or any(level <= 0 for level in levels):
        raise argparse.ArgumentTypeError("all levels must be greater than zero")
    return levels


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run ChatService stress tests at multiple user counts."
    )
    parser.add_argument(
        "--levels",
        type=parse_levels,
        default=list(DEFAULT_LEVELS),
        help="comma-separated user counts",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--rooms", type=int, default=10)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--message-rate", type=float, default=1.0)
    parser.add_argument(
        "--mode",
        choices=("connect_only", "private", "room", "mixed"),
        default="room",
    )
    parser.add_argument("--grace-period", type=float, default=2.0)
    parser.add_argument("--server-pid", type=int)
    parser.add_argument("--connect-concurrency", type=int, default=0)
    parser.add_argument(
        "--results-dir",
        default=str(Path(__file__).resolve().parent / "results"),
    )
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument(
        "--log-level",
        choices=("DEBUG", "INFO", "WARNING", "ERROR"),
        default="INFO",
    )
    return parser


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="")
    os.replace(temporary, path)


def write_matrix_outputs(
    results_dir: Path, matrix_name: str, rows: list[dict[str, Any]]
) -> tuple[Path, Path]:
    json_path = results_dir / f"{matrix_name}_summary.json"
    csv_path = results_dir / f"{matrix_name}_summary.csv"
    payload = {
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "results": rows,
    }
    atomic_write(
        json_path, json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    )

    from io import StringIO

    output = StringIO(newline="")
    if rows:
        fieldnames = list(rows[0].keys())
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    atomic_write(csv_path, output.getvalue())
    return json_path, csv_path


def matrix_row(summary: dict[str, Any]) -> dict[str, Any]:
    keys = (
        "run_id",
        "mode",
        "total_users_requested",
        "successful_connections",
        "failed_connections",
        "connection_failure_rate_percent",
        "active_users",
        "total_messages_sent",
        "expected_message_deliveries",
        "unique_message_deliveries",
        "message_loss_rate_percent",
        "average_latency_ms",
        "p50_latency_ms",
        "p95_latency_ms",
        "p99_latency_ms",
        "max_latency_ms",
        "sent_throughput_messages_per_second",
        "received_throughput_deliveries_per_second",
        "unexpected_disconnect_count",
        "server_cpu_average_percent",
        "server_cpu_max_percent",
        "server_rss_max_bytes",
        "laggy",
    )
    row = {key: summary.get(key) for key in keys}
    row["laggy_reasons"] = "|".join(summary.get("laggy_reasons", []))
    return row


def main() -> int:
    args = build_parser().parse_args()
    script = Path(__file__).resolve().parent / "stress_test.py"
    results_dir = Path(args.results_dir).resolve()
    results_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    matrix_name = f"matrix_{timestamp}_{args.mode}"
    rows: list[dict[str, Any]] = []

    for users in args.levels:
        result_name = f"{matrix_name}_{users}u"
        command = [
            sys.executable,
            str(script),
            "--host",
            args.host,
            "--port",
            str(args.port),
            "--users",
            str(users),
            "--rooms",
            str(args.rooms),
            "--duration",
            str(args.duration),
            "--message-rate",
            str(args.message_rate),
            "--mode",
            args.mode,
            "--grace-period",
            str(args.grace_period),
            "--connect-concurrency",
            str(args.connect_concurrency),
            "--results-dir",
            str(results_dir),
            "--result-name",
            result_name,
            "--log-level",
            args.log_level,
        ]
        if args.server_pid:
            command.extend(("--server-pid", str(args.server_pid)))

        print(f"\n=== Running {users} users ===", flush=True)
        completed = subprocess.run(command, check=False)
        summary_path = results_dir / f"{result_name}_summary.json"
        if completed.returncode != 0 or not summary_path.exists():
            print(
                f"benchmark failed for {users} users "
                f"(exit {completed.returncode})",
                file=sys.stderr,
            )
            if not args.continue_on_error:
                return completed.returncode or 1
            continue
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        rows.append(matrix_row(summary))
        write_matrix_outputs(results_dir, matrix_name, rows)

    json_path, csv_path = write_matrix_outputs(results_dir, matrix_name, rows)
    atomic_write(
        results_dir / "latest_matrix_summary.json",
        json_path.read_text(encoding="utf-8"),
    )
    atomic_write(
        results_dir / "latest_matrix_summary.csv",
        csv_path.read_text(encoding="utf-8"),
    )

    first_laggy = next((row for row in rows if row["laggy"]), None)
    print("\nMatrix complete")
    if first_laggy:
        first_laggy_index = rows.index(first_laggy)
        healthy_before_limit = [
            row for row in rows[:first_laggy_index] if not row["laggy"]
        ]
        print(
            "  first laggy level: "
            f"{first_laggy['total_users_requested']} users "
            f"({first_laggy['laggy_reasons']})"
        )
    else:
        healthy_before_limit = [row for row in rows if not row["laggy"]]
        print("  no tested level crossed a laggy threshold")
    if healthy_before_limit:
        print(
            "  highest tested non-laggy level: "
            f"{healthy_before_limit[-1]['total_users_requested']} users"
        )
    print(f"  JSON: {json_path}")
    print(f"  CSV:  {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
