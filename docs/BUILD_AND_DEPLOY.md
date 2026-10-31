# 构建与部署说明

## 已用环境

- 电脑：Ubuntu 22.04 WSL，ARM hard-float GCC/G++ 9 交叉工具链、CMake。
- OpenCV：本地 4.10.0 源码构建的 ARM 静态库；原始构建脚本使用 `$HOME/bx71_opencv/install-arm/lib/cmake/opencv4`。完整 OpenCV 构建配置未包含，换环境时需检查依赖。
- 板端：BX71 Zynq-7010，Linux 4.14，USB Host 摄像头支持、V4L2、FPGA Manager、`devmem` 工具与 root 权限。
- 硬件工程记录：Vivado 2026.1；器件 `xc7z010clg400-2`。

## 编译采集程序（电脑 Ubuntu）

在仓库根目录执行：

```sh
mkdir -p build
arm-linux-gnueabihf-gcc-9 -O2 -Wall -Wextra -static software/capture/bx71_capture.c -o build/bx71_capture
```

## 编译识别程序（电脑 Ubuntu）

准备上述 ARM 版 OpenCV 和交叉编译器后执行：

```sh
bash software/bx71_gesture_photo/build.sh
```

原始脚本会把结果放入该目录的 `build-arm/`，并复制到 WSL 的 `/mnt/d/bx71_gesture_photo`。无 D 盘挂载的环境应调整该复制位置。不要用电脑 x86 版 OpenCV 替代 ARM 库。

## 硬件工程

打开 `hardware/bx71_uart.xpr`，保持同级 `.srcs`、`.gen` 目录结构。wrapper 按原工程引用路径保留。IP 输出产物、缓存和实现结果未打包，应在匹配的 Vivado 中重新生成并验证 Block Design，再综合、实现及生成 bitstream。该归档未在整理环境重建验证。

PS 经 AXI SmartConnect 访问双通道 AXI GPIO。通道 1 宽度 1，通道 2 宽度 4。通道 2 DATA 地址 `0x41200008`，TRI 地址 `0x4120000C`，全输出配置为 0。

| 信号 | XDC 管脚 | 通道 2 掩码 |
|---|---|---|
| 板载 LED | T10 | 属于通道 1 |
| 红灯 | L14 | 1 |
| 黄灯 | L15 | 2 |
| 绿灯 | K14 | 4 |
| 蜂鸣器控制 | J14 | 8 |

所有列出信号在 XDC 中为 LVCMOS33。接口供电按实物规格连接，GPIO 是控制信号。

## 已验证板卡的运行布局

TF 卡启动分区挂载 `/run/media/mmcblk0p1`：

| 相对路径 | 内容 |
|---|---|
| `bx71_capture` | ARM 采集可执行文件 |
| `bx71_led_buzzer.bin` | 与当前硬件匹配的 FPGA Manager 加载文件（本包未附） |
| `bx71_model_check/bx71_gesture_photo` | ARM 识别可执行文件 |
| `bx71_model_check/palm_detection_mediapipe_2023feb.onnx` | 手掌检测模型（本包未附） |
| `bx71_model_check/handpose_estimation_mediapipe_2023feb.onnx` | 手部关键点模型（本包未附） |

控制脚本位于板端 `/etc/bx71_gesture_start.sh`。原始脚本把程序复制到 `/tmp` 执行，并把 FPGA 加载文件复制到 `/lib/firmware` 后使用 FPGA Manager 加载。不要把裸机应用 BOOT.BIN 当成这个 FPGA 加载文件；本包未提供该文件的生成流程及 Linux 镜像重建流程。

板端 `/etc/inittab` 已确认存在以下行（用户输出的 `43:` 是行号，不属于配置）：

```text
bg:2345:once:/bin/sh -c "exec /bin/sh /etc/bx71_gesture_start.sh >> /tmp/bx71_autostart.log 2>&1"
```

这是原 SysV init 环境的配置片段；不要覆盖完整 inittab，也不要重复添加相同启动项。`once` 不会在程序退出后自动重启。

## 查看运行状态（板端串口）

```sh
cat /tmp/bx71_autostart.log
tail -n 20 /tmp/capture.log
tail -n 20 /tmp/gesture.log
```

预期日志包含 `READY: gesture control started`、`GESTURE=... COMMAND=...` 和 `ACTION: ...`。Windows 调试时已用命令：

```bat
python -m serial.tools.miniterm COM5 115200
```

COM 号以实际设备为准。自动运行中的脚本已占用摄像头，不要同时开启第二个采集循环。
