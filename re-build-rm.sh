#!/bin/bash
# 请将这个文件放在 ~/目录下(例如/home/color/目录下)
set -e  # 任何命令返回非零状态时立即退出脚本
rm -rf build # 删除build目录
meson setup build # 创建新的build目录
meson compile -C build # 重新编译
