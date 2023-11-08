# rmw_alternative

![](https://github.com/yadunund/rmw_alternative/workflows/build/badge.svg)
![](https://github.com/yadunund/rmw_alternative/workflows/style/badge.svg)

A ROS 2 RMW implementation based on Zenoh that is written using the zenoh-c bindings.

## Design

For information about the Design please visit [design](docs/design.md) page.

## Requirements
- [ROS 2](https://docs.ros.org): Rolling/Iron


## Setup

Install latest rustc.
> Note: The version of rustc that can be installed via apt is outdated.
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Build `rmw_zenoh_cpp`

```bash
mkdir ~/ws_rmw_alternative/src -p && cd ~/ws_rmw_alternative/src
git clone git@github.com:yadunund/rmw_alternative
cd ~/ws_rmw_alternative
source /opt/ros/<DISTRO>/setup.bash # replace <DISTRO> with ROS 2 distro of choice
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release

```

## Test
```bash
cd ~/ws_rmw_alternative
source install/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
ros2 topic pub "/chatter" std_msgs/msg/String '{data: hello}'
```

## Config
The zenoh session may be configured by setting the `ZENOH_CONFIG_PATH` environment variable to point to the custom zenoh configuration file. The [rmw_zenoh_config.json5](./rmw_zenoh_config.json5) may be used to configure the zenoh session for this middleware. For a complete list of configurable settings, see [documentation](https://github.com/eclipse-zenoh/zenoh/blob/master/DEFAULT_CONFIG.json5).

## TODO Features
- [x] Publisher
- [ ] Subscription
- [ ] Client
- [ ] Service
- [ ] Graph introspection
