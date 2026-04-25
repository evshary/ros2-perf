# ros2-perf

The performance benchmark tool for ROS 2

## Build

```shell
mkdir -p ~/ros2_perf_ws/src && cd ~/ros2_perf_ws/src
git clone https://github.com/evshary/ros2-perf.git
cd ..
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Run

* Terminal 1: Run pong

```shell
ros2 run perf pong
# Run without info
ros2 run perf pong --ros-args --log-level warn
# Run with QoS
ros2 run perf pong --ros-args -p reliability:=BEST_EFFORT -p durability:=TRANSIENT_LOCAL -p history:=KEEP_ALL
```

* Terminal 2: Run ping

```shell
ros2 run perf ping
# Run without info
ros2 run perf ping --ros-args --log-level warn
# Run with QoS
ros2 run perf ping --ros-args -p reliability:=BEST_EFFORT -p durability:=TRANSIENT_LOCAL -p history:=KEEP_ALL
# Other configuration
ros2 run perf ping --ros-args -p warmup:=5.0 -p size:=32 -p samples:=100 -p rate:=10
```
