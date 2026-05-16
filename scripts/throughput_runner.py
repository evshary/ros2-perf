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
PID_FILE = Path("/tmp/ros2-perf-throughput-pids.json")


def create_output_dir() -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir = Path.cwd() / "benchmark_results" / f"throughput-{timestamp}"
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
    return json.loads(PID_FILE.read_text(encoding="utf-8"))


def save_registered_pids(entries: list[dict[str, int | str]]) -> None:
    PID_FILE.write_text(json.dumps(entries), encoding="utf-8")


def register_process(process: subprocess.Popen, name: str) -> None:
    entries = [entry for entry in load_registered_pids() if entry["pid"] != process.pid]
    entries.append({"pid": process.pid, "name": name})
    save_registered_pids(entries)


def unregister_process(process: subprocess.Popen) -> None:
    entries = [entry for entry in load_registered_pids() if entry["pid"] != process.pid]
    if entries:
        save_registered_pids(entries)
    else:
        PID_FILE.unlink(missing_ok=True)


def cleanup_registered_processes() -> None:
    for entry in load_registered_pids():
        terminate_pid(int(entry["pid"]), str(entry["name"]))
    PID_FILE.unlink(missing_ok=True)


def run_payload(payload_size: int, output_dir: Path) -> None:
    output_json = output_dir / f"throughput-{payload_size}.json"
    recv_command = [
        "ros2",
        "run",
        "perf",
        "throughput_recv",
        "--ros-args",
        "--log-level",
        "warn",
        "-p",
        f"output_json:={output_json}",
    ]
    send_command = [
        "ros2",
        "run",
        "perf",
        "throughput_send",
        "--ros-args",
        "--log-level",
        "warn",
        "-p",
        f"size:={payload_size}",
    ]

    print(f"Running throughput payload sweep for {payload_size} bytes")
    recv_process = subprocess.Popen(recv_command, start_new_session=True)
    register_process(recv_process, "throughput_recv")
    time.sleep(1.0)
    send_process = subprocess.Popen(send_command, start_new_session=True)
    register_process(send_process, "throughput_send")
    try:
        recv_return_code = recv_process.wait()
        if recv_return_code != 0:
            raise subprocess.CalledProcessError(recv_return_code, recv_command)
    finally:
        terminate_process(send_process, "throughput_send")
        unregister_process(send_process)
        terminate_process(recv_process, "throughput_recv")
        unregister_process(recv_process)


def generate_plot(output_dir: Path) -> None:
    plot_script = Path(__file__).with_name("generate_payload_plot.py")
    output_html = output_dir / "throughput.html"
    command = [
        sys.executable,
        str(plot_script),
        "--benchmark",
        "throughput",
        "--input-dir",
        str(output_dir),
        "--output",
        str(output_html),
    ]
    subprocess.run(command, check=True)


def main() -> None:
    output_dir = create_output_dir()
    print(f"Throughput results will be written to {output_dir}")
    cleanup_registered_processes()

    for payload_size in PAYLOAD_SIZES:
        run_payload(payload_size, output_dir)

    generate_plot(output_dir)
    print(f"Throughput sweep completed: {output_dir / 'throughput.html'}")


if __name__ == "__main__":
    main()
