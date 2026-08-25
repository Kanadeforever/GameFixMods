#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build dist
/usr/local/swift/usr/bin/clang++ --target=x86_64-pc-windows-msvc -fuse-ld=lld-link -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin -nostdlib -Wl,/dll,/entry:DllMainCRTStartup,/subsystem:windows,/nodefaultlib,/out:build/SAOHF_Enhance.asi src/SAOHF_Enhance.cpp
cp SAOHF_Enhance.ini build/SAOHF_Enhance.ini
cp build/SAOHF_Enhance.asi dist/SAOHF_Enhance.asi
cp build/SAOHF_Enhance.ini dist/SAOHF_Enhance.ini
