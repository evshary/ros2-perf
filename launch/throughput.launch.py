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
