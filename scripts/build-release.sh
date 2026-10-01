#!/usr/bin/env bash
# build-release.sh - Build de release (optimizado, LTO, sin debug info)
set -euo pipefail

# --- Config ---------------------------------------------------------------
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-release}"         # release | minsize
STRIP_BIN="${STRIP_BIN:-1}"
LTO="${LTO:-1}"
JOBS="${JOBS:-$(nproc)}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# --- Detección de toolchain ----------------------------------------------
detect_native_file() {
  if [[ -n "${NATIVE_FILE:-}" ]]; then
    echo "$NATIVE_FILE"
    return
  fi

  if command -v g++ >/dev/null 2>&1; then
    local gcc_major
    gcc_major=$(g++ -dumpversion | cut -d. -f1)
    if (( gcc_major >= 15 )); then
      echo ""
      return
    fi
  fi

  if [[ -f "./scripts/llvm_native.txt" ]]; then
    echo "./scripts/llvm_native.txt"
    return
  fi

  echo "!! No se encontró GCC 15+ ni ./scripts/llvm_native.txt" >&2
  exit 1
}

NATIVE_FILE="$(detect_native_file)"

# --- Setup ----------------------------------------------------------------
SETUP_ARGS=(
  "--buildtype=$BUILD_TYPE"
)

if [[ -n "$NATIVE_FILE" ]]; then
  SETUP_ARGS=( "--native-file" "$NATIVE_FILE" "${SETUP_ARGS[@]}" )
  echo ">> Usando native file: $NATIVE_FILE"
else
  echo ">> Usando toolchain del sistema (GCC 15+)"
fi

if [[ "$LTO" == "1" ]]; then
  SETUP_ARGS+=( -Db_lto=true )
  echo ">> LTO habilitado"
fi

# Si el build dir ya existe, reconfigurar en lugar de fallar
if [[ -d "$BUILD_DIR" ]]; then
  echo ">> Reconfigurando build existente en '$BUILD_DIR'"
  meson setup "${SETUP_ARGS[@]}" "$BUILD_DIR" --reconfigure
else
  echo ">> Configurando Meson en '$BUILD_DIR' (buildtype=$BUILD_TYPE)"
  meson setup "${SETUP_ARGS[@]}" "$BUILD_DIR"
fi

# --- Compilar -------------------------------------------------------------
echo ">> Compilando con $JOBS jobs"
meson compile -C "$BUILD_DIR" -j "$JOBS"

# --- Post-proceso ---------------------------------------------------------
BIN="$BUILD_DIR/targets/app/Minecraft.Client"
if [[ -x "$BIN" ]]; then
  if [[ "$STRIP_BIN" == "1" ]] && command -v strip >/dev/null 2>&1; then
    echo ">> Strippeando binario"
    strip --strip-unneeded "$BIN" || true
  fi
  SIZE=$(du -h "$BIN" | cut -f1)
  echo ""
  echo "✅ Build release listo: $ROOT_DIR/$BIN  ($SIZE)"
else
  echo "⚠️  No se encontró el binario en $BIN" >&2
  exit 1
fi
