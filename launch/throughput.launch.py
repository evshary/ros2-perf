# flake8: noqa: D100,D103,Q000
# This launch entrypoint favors ROS-style executable strings over repo-wide
# docstring/quote conventions, so we scope the ignore to this file only.

from launch import LaunchDescription
from launch.actions import ExecuteProcess, LogInfo


def generate_launch_description():
    return LaunchDescription([
        LogInfo(
            msg='Starting throughput payload sweep and HTML plot generation',
        ),
        ExecuteProcess(
            cmd=['ros2', 'run', 'perf', 'throughput_runner.py'],
            output='screen',
        ),
    ])
