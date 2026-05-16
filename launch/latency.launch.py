from launch import LaunchDescription
from launch.actions import ExecuteProcess, LogInfo


def generate_launch_description():
    return LaunchDescription([
        LogInfo(msg="Starting latency payload sweep and HTML plot generation"),
        ExecuteProcess(
            cmd=["ros2", "run", "perf", "latency_runner.py"],
            output="screen",
        ),
    ])
