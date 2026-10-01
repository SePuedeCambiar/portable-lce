#!/usr/bin/env bash
# build-dev.sh - Build de desarrollo (debug, símbolos, sin optimizar)
set -euo pipefail

# --- Config ---------------------------------------------------------------
BUILD_DIR="${BUILD_DIR:-build-dev}"
BUILD_TYPE="${BUILD_TYPE:-debug}"          # debug | debugoptimized
ENABLE_SANITIZERS="${ENABLE_SANITIZERS:-0}"
JOBS="${JOBS:-$(nproc)}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# --- Detección de toolchain ----------------------------------------------
detect_native_file() {
  # Si el usuario ya lo forzó, respétalo
  if [[ -n "${NATIVE_FILE:-}" ]]; then
    echo "$NATIVE_FILE"
    return
  fi

  # ¿GCC 15+ en el sistema?
  if command -v g++ >/dev/null 2>&1; then
    local gcc_major
    gcc_major=$(g++ -dumpversion | cut -d. -f1)
    if (( gcc_major >= 15 )); then
      echo ""   # usar toolchain del sistema
      return
    fi
  fi

  # Fallback a LLVM/libc++
  if [[ -f "./scripts/llvm_native.txt" ]]; then
    echo "./scripts/llvm_native.txt"
    return
  fi

  echo "!! No se encontró GCC 15+ ni ./scripts/llvm_native.txt" >&2
  echo "   Instala GCC 15+ o LLVM con libc++ (ver README)." >&2
  exit 1
}

NATIVE_FILE="$(detect_native_file)"

# --- Setup ----------------------------------------------------------------
SETUP_ARGS=(
  "--buildtype=$BUILD_TYPE"
  "--wipe"                    # reconfigura limpio en dev
)

if [[ -n "$NATIVE_FILE" ]]; then
  SETUP_ARGS=( "--native-file" "$NATIVE_FILE" "${SETUP_ARGS[@]}" )
  echo ">> Usando native file: $NATIVE_FILE"
else
  echo ">> Usando toolchain del sistema (GCC 15+)"
fi

if [[ "$ENABLE_SANITIZERS" == "1" ]]; then
  SETUP_ARGS+=( -Db_sanitize=address,undefined )
  echo ">> Sanitizers habilitados (ASan + UBSan)"
fi

echo ">> Configurando Meson en '$BUILD_DIR' (buildtype=$BUILD_TYPE)"
meson setup "${SETUP_ARGS[@]}" "$BUILD_DIR"

# --- Compilar -------------------------------------------------------------
echo ">> Compilando con $JOBS jobs"
meson compile -C "$BUILD_DIR" -j "$JOBS"

# --- Assets ---------------------------------------------------------------
# El README dice que los assets se copian automáticamente,
# pero por si acaso en dev forzamos copia:
meson compile -C "$BUILD_DIR" --target=assets 2>/dev/null || true

# --- Salida ---------------------------------------------------------------
BIN="$BUILD_DIR/targets/app/Minecraft.Client"
if [[ -x "$BIN" ]]; then
  echo ""
  echo "✅ Build dev listo: $ROOT_DIR/$BIN"
  echo "   Ejecuta:  ./$BIN"
else
  echo "⚠️  No se encontró el binario en $BIN" >&2
  exit 1
fi
