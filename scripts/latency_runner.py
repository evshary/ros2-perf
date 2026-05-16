#!/usr/bin/env python3

import json
import os
import signal
import subprocess
import sys
import time
from contextlib import suppress
from datetime import datetime, timezone
from pathlib import Path


PAYLOAD_SIZES = [32, 128, 512, 1024, 4096, 16384, 65536, 262144, 1048576]
PID_FILE = Path("/tmp/ros2-perf-latency-pids.json")


def read_env_float(name: str, default: float) -> float:
    return float(os.environ.get(name, default))


def read_env_int(name: str, default: int) -> int:
    return int(os.environ.get(name, default))


def read_payload_sizes() -> list[int]:
    payloads = os.environ.get('ROS2_PERF_PAYLOAD_SIZES')
    if not payloads:
        return PAYLOAD_SIZES
    return [
        int(value.strip()) for value in payloads.split(',') if value.strip()
    ]


def create_output_dir() -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir = Path.cwd() / "benchmark_results" / f"latency-{timestamp}"
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def terminate_process(process: subprocess.Popen, name: str) -> None:
    with suppress(ProcessLookupError):
        process_group_id = os.getpgid(process.pid)
        os.killpg(process_group_id, signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            with suppress(ProcessLookupError):
                os.killpg(process_group_id, signal.SIGKILL)
            process.wait(timeout=5)
        print(f"Stopped {name}")


def terminate_pid(pid: int, name: str) -> None:
    with suppress(ProcessLookupError):
        process_group_id = os.getpgid(pid)
        os.killpg(process_group_id, signal.SIGINT)
        for _ in range(50):
            time.sleep(0.1)
            with suppress(ProcessLookupError):
                os.killpg(process_group_id, 0)
                continue
            print(f"Stopped {name} pid={pid}")
            return
        with suppress(ProcessLookupError):
            os.killpg(process_group_id, signal.SIGKILL)
        print(f"Killed {name} pid={pid}")


def load_registered_pids() -> list[dict[str, int | str]]:
    if not PID_FILE.exists():
        return []
    return json.loads(PID_FILE.read_text(encoding='utf-8'))


def save_registered_pids(entries: list[dict[str, int | str]]) -> None:
    PID_FILE.write_text(json.dumps(entries), encoding='utf-8')


def register_process(process: subprocess.Popen, name: str) -> None:
    entries = [
        entry for entry in load_registered_pids()
        if entry['pid'] != process.pid
    ]
    entries.append({'pid': process.pid, 'name': name})
    save_registered_pids(entries)


def unregister_process(process: subprocess.Popen) -> None:
    entries = [
        entry for entry in load_registered_pids()
        if entry['pid'] != process.pid
    ]
    if entries:
        save_registered_pids(entries)
    else:
        PID_FILE.unlink(missing_ok=True)


def cleanup_registered_processes() -> None:
    for entry in load_registered_pids():
        terminate_pid(int(entry['pid']), str(entry['name']))
    PID_FILE.unlink(missing_ok=True)


def run_payload(payload_size: int, output_dir: Path) -> None:
    output_json = output_dir / f"latency-{payload_size}.json"
    warmup = read_env_float('ROS2_PERF_LATENCY_WARMUP', 5.0)
    samples = read_env_int('ROS2_PERF_LATENCY_SAMPLES', 100)
    rate = read_env_int('ROS2_PERF_LATENCY_RATE', 10)
    pong_command = [
        "ros2",
        "run",
        "perf",
        "pong",
        "--ros-args",
        "--log-level",
        "warn",
    ]
    ping_command = [
        "ros2",
        "run",
        "perf",
        "ping",
        "--ros-args",
        "--log-level",
        "warn",
        "-p",
        f"warmup:={warmup}",
        "-p",
        f"samples:={samples}",
        "-p",
        f"rate:={rate}",
        "-p",
        f"size:={payload_size}",
        "-p",
        f"output_json:={output_json}",
    ]

    print(f"Running latency payload sweep for {payload_size} bytes")
    pong_process = subprocess.Popen(pong_command, start_new_session=True)
    register_process(pong_process, "pong")
    time.sleep(1.0)
    try:
        subprocess.run(ping_command, check=True)
    finally:
        terminate_process(pong_process, "pong")
        unregister_process(pong_process)


def generate_plot(output_dir: Path) -> None:
    plot_script = Path(__file__).with_name("generate_payload_plot.py")
    output_html = output_dir / "latency.html"
    command = [
        sys.executable,
        str(plot_script),
        "--benchmark",
        "latency",
        "--input-dir",
        str(output_dir),
        "--output",
        str(output_html),
    ]
    subprocess.run(command, check=True)


def main() -> None:
    output_dir = create_output_dir()
    print(f"Latency results will be written to {output_dir}")
    cleanup_registered_processes()

    for payload_size in read_payload_sizes():
        run_payload(payload_size, output_dir)

    generate_plot(output_dir)
    print(f"Latency sweep completed: {output_dir / 'latency.html'}")


if __name__ == "__main__":
    main()
