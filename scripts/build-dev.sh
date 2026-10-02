#!/usr/bin/env bash
# build-dev.sh - Build de desarrollo con compilación incremental rápida
set -euo pipefail

# --- Config ---------------------------------------------------------------
BUILD_DIR="${BUILD_DIR:-build-dev}"
BUILD_TYPE="${BUILD_TYPE:-debugoptimized}"  # 'debugoptimized' da -O2 + símbolos (mucho más rápido)
ENABLE_SANITIZERS="${ENABLE_SANITIZERS:-0}"
JOBS="${JOBS:-$(nproc)}"
FORCE_RECONFIGURE="${FORCE_RECONFIGURE:-0}" # Pon 1 si cambiaste un meson.build

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

# --- Setup Solo si no existe la carpeta o se fuerza -----------------------
if [[ ! -d "$BUILD_DIR/meson-private" ]] || [[ "$FORCE_RECONFIGURE" == "1" ]]; then
  NATIVE_FILE="$(detect_native_file)"
  SETUP_ARGS=(
    "--buildtype=$BUILD_TYPE"
  )

  if [[ -n "$NATIVE_FILE" ]]; then
    SETUP_ARGS=( "--native-file" "$NATIVE_FILE" "${SETUP_ARGS[@]}" )
    echo ">> Usando native file: $NATIVE_FILE"
  fi

  if [[ "$ENABLE_SANITIZERS" == "1" ]]; then
    SETUP_ARGS+=( -Db_sanitize=address,undefined )
    echo ">> Sanitizers habilitados"
  fi

  echo ">> Configurando Meson por primera vez en '$BUILD_DIR'..."
  meson setup "${SETUP_ARGS[@]}" "$BUILD_DIR"
else
  echo ">> Usando configuración existente en '$BUILD_DIR' (Compilación incremental activada)"
fi

# --- Compilar incrementalmente --------------------------------------------
echo ">> Compilando cambios con $JOBS jobs..."
meson compile -C "$BUILD_DIR" -j "$JOBS"

# --- Assets ---------------------------------------------------------------
meson compile -C "$BUILD_DIR" --target=assets 2>/dev/null || true

# --- Salida ---------------------------------------------------------------
BIN="$BUILD_DIR/targets/app/Minecraft.Client"
if [[ -x "$BIN" ]]; then
  echo ""
  echo "✅ Build listo en segundos: $ROOT_DIR/$BIN"
  echo "   Ejecuta:  ./$BIN"
else
  echo "⚠️  No se encontró el binario en $BIN" >&2
  exit 1
fi
