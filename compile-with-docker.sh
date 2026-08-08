#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------
# Usage:
#   ./compile-with-docker.sh [Preset] [CMake options...]
# Examples:
#   ./compile-with-docker.sh Custom
#   ./compile-with-docker.sh Bandscope -DENABLE_SPECTRUM=ON
#   ./compile-with-docker.sh Broadcast -DENABLE_FEAT_F4HWN_GAME=ON -DENABLE_NOAA=ON
#   ./compile-with-docker.sh Fusion -DDEV=ON
#   ./compile-with-docker.sh All
# Default preset: "Custom"
# ---------------------------------------------

IMAGE=uvk1-uvk5v3
PRESET=${1:-Custom}
shift || true  # remove preset from arguments if present

# Any remaining args will be treated as CMake cache variables
EXTRA_ARGS=("$@")

# ---------------------------------------------
# Validate preset name
# ---------------------------------------------
if [[ ! "$PRESET" =~ ^(Custom|Bandscope|Broadcast|Basic|RescueOps|Game|Fusion|All)$ ]]; then
  echo "❌ Unknown preset: '$PRESET'"
  echo "Valid presets are: Custom, Bandscope, Broadcast, Basic, RescueOps, Game, Fusion, All"
  exit 1
fi

# ---------------------------------------------
# Build the Docker image (only needed once)
# ---------------------------------------------
if [[ "$(docker images -q $IMAGE)" == "" ]]; then
  echo "Building Docker image..."
  docker build -t "$IMAGE" .
fi

# ---------------------------------------------
# Clean existing CMake cache to ensure toolchain reload
# ---------------------------------------------
rm -rf build
export MSYS_NO_PATHCONV=1
# ---------------------------------------------
# Function to build one preset
# ---------------------------------------------
build_preset() {
  local preset="$1"
  echo ""
  echo "=== 🚀 Building preset: ${preset} ==="
  echo "---------------------------------------------"
  docker run --rm \
    -u $(id -u):$(id -g) \
    -it -v "$PWD":/src -w /src "$IMAGE" \
    bash -c "which arm-none-eabi-gcc && arm-none-eabi-gcc --version && \
             cmake --preset ${preset} ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} && \
             cmake --build --preset ${preset} -j"
  echo "✅ Done: ${preset}"
}

# ---------------------------------------------
# After Fusion build, copy bin to local web projects if dirs exist
# ---------------------------------------------
deploy_fusion_bin() {
  local src="build/Fusion/f4hwn-3ch.fusion.bin"
  if [[ ! -f "$src" ]]; then
    echo "⚠️  Skip deploy: $src not found"
    return
  fi

  local dests=(
    "D:/File/code/uvk1/uv-k1-k6v3-multi-system/web/dist/firmware/f4hwn-3ch.fusion.bin"
    "D:/File/code/uvk1/uv-k1-k6v3-multi-system/web/src/firmware/f4hwn-3ch.fusion.bin"
    "D:/File/code/uvk1/uv-k1-k6v3-multi-system-web/firmware/f4hwn-3ch.fusion.bin"
  )

  for dest in "${dests[@]}"; do
    local dir
    dir="$(dirname "$dest")"
    if [[ -d "$dir" ]]; then
      cp -f "$src" "$dest"
      echo "📦 Copied to $dest"
    else
      echo "⚠️  Skip copy (dir missing): $dir"
    fi
  done
}

# ---------------------------------------------
# Handle 'All' preset
# ---------------------------------------------
if [[ "$PRESET" == "All" ]]; then
  PRESETS=(Bandscope Broadcast Basic RescueOps Game Fusion)
  for p in "${PRESETS[@]}"; do
    build_preset "$p"
  done
  echo ""
  echo "🎉 All presets built successfully!"
  deploy_fusion_bin
else
  build_preset "$PRESET"
  if [[ "$PRESET" == "Fusion" ]]; then
    deploy_fusion_bin
  fi
fi
