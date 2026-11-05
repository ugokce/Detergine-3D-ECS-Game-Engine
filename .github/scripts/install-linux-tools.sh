#!/usr/bin/env bash
# Installs the Linux CI toolchain: Ninja, pkg-config (needed by vcpkg ports) and Clang 19, exposed as clang/clang++
# so the linux-x64 preset picks it up. Older Clang cannot use libstdc++'s std::expected.
set -euo pipefail
sudo apt-get update
sudo apt-get install -y ninja-build pkg-config
if ! sudo apt-get install -y clang-19; then
    # Fall back to LLVM's official apt repository when the distribution does not ship Clang 19.
    wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
    sudo bash /tmp/llvm.sh 19
fi
sudo ln -sf "$(command -v clang-19)" /usr/local/bin/clang
sudo ln -sf "$(command -v clang++-19)" /usr/local/bin/clang++
clang++ --version
