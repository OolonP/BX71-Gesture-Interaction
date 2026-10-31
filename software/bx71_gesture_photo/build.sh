#!/bin/bash
set -eu
project_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
cmake -S "$project_dir" -B "$project_dir/build-arm" \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++-9 \
  -DCMAKE_CXX_FLAGS="-march=armv7-a -mfpu=neon -mfloat-abi=hard" \
  -DCMAKE_EXE_LINKER_FLAGS="-static" \
  -DOpenCV_DIR="$HOME/bx71_opencv/install-arm/lib/cmake/opencv4" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build-arm" -j4
file "$project_dir/build-arm/bx71_gesture_photo"
cp "$project_dir/build-arm/bx71_gesture_photo" /mnt/d/bx71_gesture_photo
echo GESTURE_BUILD_OK
echo 'Windows output: D:\bx71_gesture_photo'
