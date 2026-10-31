#!/bin/sh
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
sd=/run/media/mmcblk0p1
model=$sd/bx71_model_check
fpga=/sys/class/fpga_manager/fpga0
n=0
while [ ! -f "$model/bx71_gesture_photo" ] || [ ! -c /dev/video0 ]; do
    n=$((n + 1))
    [ "$n" -lt 90 ] || { echo "ERROR: SD files or camera not ready"; exit 1; }
    sleep 1
done
mkdir /tmp/bx71_gesture.lock 2>/dev/null || { echo "Already running (or stale lock)"; exit 1; }
ready=0
cleanup() {
    [ "$ready" = 0 ] || devmem 0x41200008 32 0
    rmdir /tmp/bx71_gesture.lock
}
trap cleanup EXIT
trap 'exit 0' INT TERM
cp "$sd/bx71_capture" /tmp/bx71_capture || exit 1
cp "$model/bx71_gesture_photo" /tmp/bx71_gesture_photo || exit 1
chmod +x /tmp/bx71_capture /tmp/bx71_gesture_photo || exit 1
mkdir -p /lib/firmware || exit 1
cp "$sd/bx71_led_buzzer.bin" /lib/firmware/ || exit 1
echo 0 > "$fpga/flags" || exit 1
echo bx71_led_buzzer.bin > "$fpga/firmware" || exit 1
[ "$(cat "$fpga/state")" = operating ] || { echo "ERROR: FPGA not operating"; exit 1; }
devmem 0x41200008 32 0 || exit 1
devmem 0x4120000C 32 0 || exit 1
ready=1
cd "$model" || exit 1
last=""
count=0
applied=""
echo "READY: gesture control started"
while true; do
    cmd=""
    if /tmp/bx71_capture /dev/video0 /tmp/auto_hand > /tmp/capture.log 2>&1; then
        /tmp/bx71_gesture_photo palm_detection_mediapipe_2023feb.onnx handpose_estimation_mediapipe_2023feb.onnx /tmp/auto_hand.jpg /tmp/auto_result.jpg > /tmp/gesture.log 2>&1
        grep '^GESTURE=' /tmp/gesture.log
        cmd=$(sed -n 's/^GESTURE=.* COMMAND=\([0-3]\) .*/\1/p' /tmp/gesture.log)
    else
        echo "Capture failed"
        cat /tmp/capture.log
    fi
    if [ -z "$cmd" ]; then
        last=""; count=0
    sleep 0.1
        continue
    fi
    if [ "$cmd" = "$last" ]; then count=$((count + 1)); else last="$cmd"; count=1; fi
    if [ "$count" -ge 2 ] && [ "$cmd" != "$applied" ]; then
        case "$cmd" in
            1) devmem 0x41200008 32 9; echo "ACTION: RED + BUZZER"; sleep 3; devmem 0x41200008 32 1 ;;
            2) devmem 0x41200008 32 2; echo "ACTION: YELLOW" ;;
            3) devmem 0x41200008 32 4; echo "ACTION: GREEN" ;;
            0) devmem 0x41200008 32 0; echo "ACTION: ALL OFF" ;;
        esac
        applied="$cmd"
    fi
    sleep 0.1
done
