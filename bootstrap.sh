#!/usr/bin/env bash
set -euo pipefail

# 1) Fetch lv_port_linux into external/lv_port_linux (as a git submodule-style folder)
if [ ! -d external/lv_port_linux/.git ]; then
  echo "Cloning lv_port_linux into external/lv_port_linux ..."
  git clone https://github.com/lvgl/lv_port_linux.git external/lv_port_linux
fi

echo "Initializing submodules (LVGL) ..."
git -C external/lv_port_linux submodule update --init --recursive

echo "Applying patch to use custom app UI ..."
git -C external/lv_port_linux apply --whitespace=nowarn ../patches/0001-use-custom-ui.patch

echo "Done."
echo "Next:"
echo "  cmake -S external/lv_port_linux -B build -DCONFIG=fbdev"
echo "  cmake --build build -j"
echo "  ./build/bin/lvglsim"
