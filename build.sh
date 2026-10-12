#!/usr/bin/env bash
# Compila o ScreenCapturePro.exe (Windows 64 bits) com o MinGW-w64.
#
#  No Linux (Ubuntu/Debian), compilação cruzada:
#    sudo apt install g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64
#    ./build.sh
#
#  No Windows, pelo MSYS2 (terminal "MSYS2 MINGW64"):
#    pacman -S --needed mingw-w64-x86_64-gcc
#    CXX=g++ WINDRES=windres ./build.sh
#
# Uso:  ./build.sh            (gera build/ScreenCapturePro.exe)
#       ./build.sh debug      (com console de depuração, em build_debug/)
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
OUT=build
STRIP="-s"

DEFS="-DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS"
CXXFLAGS="-std=c++17 -O2 -pipe -Wall -Wno-unknown-pragmas -Wno-missing-field-initializers -Wno-cast-function-type -Ithird_party/imgui $DEFS"
SUBSYS="-mwindows"
if [[ "${1:-}" == "debug" ]]; then
  CXXFLAGS="$CXXFLAGS -DSCP_DEBUG_CONSOLE -g"
  SUBSYS="-mconsole"
  OUT=build_debug
  STRIP=""
fi
mkdir -p "$OUT/obj"

SRC=(src/*.cpp
     third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
     third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
     third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp)

# o cabeçalho mais novo de src/: se qualquer .h mudar, recompila tudo de src/
# (evita objetos "velhos" com layout de classe diferente)
NEWEST_H=$(ls -t src/*.h | head -1)
compile() {
  local src="$1"
  local obj="$OUT/obj/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  local hdr_newer=0
  if [[ "$src" == src/* && -f "$obj" && "$NEWEST_H" -nt "$obj" ]]; then hdr_newer=1; fi
  if [[ ! -f "$obj" || "$src" -nt "$obj" || $hdr_newer == 1 ]]; then
    echo "  CXX $src"
    $CXX $CXXFLAGS -c "$src" -o "$obj"
  fi
}
export -f compile
export CXX CXXFLAGS OUT NEWEST_H
printf '%s\n' "${SRC[@]}" | xargs -P "$(nproc)" -I{} bash -c 'compile "$@"' _ {}

echo "  RC  src/resources.rc"
$WINDRES -I src src/resources.rc -O coff -o "$OUT/obj/resources.res.o"

echo "  LD  $OUT/ScreenCapturePro.exe"
$CXX -o "$OUT/ScreenCapturePro.exe" "$OUT"/obj/*.o -municode $SUBSYS -static -static-libgcc -static-libstdc++ $STRIP \
  -ld3d11 -ldxgi -ld3dcompiler_47 -ld2d1 -ldwrite -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lshlwapi \
  -lcomdlg32 -lgdi32 -luser32 -lwinmm -lversion -limm32 -lkernel32 -ladvapi32
ls -la "$OUT/ScreenCapturePro.exe"
