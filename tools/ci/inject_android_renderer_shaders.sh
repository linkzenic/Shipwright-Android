#!/usr/bin/env bash

set -euo pipefail

archive="${1:-Android/app/src/main/assets/soh.o2r}"
shader_root="libultraship/src/fast/shaders"
shader_sources=(
    "opengl/default.shader.vs"
    "opengl/default.shader.fs"
)
archive_entries=(
    "shaders/opengl/default.shader.vs"
    "shaders/opengl/default.shader.fs"
)

if [[ ! -f "${archive}" ]]; then
    echo "ERROR: Resource archive not found: ${archive}" >&2
    exit 1
fi

for shader in "${shader_sources[@]}"; do
    if [[ ! -f "${shader_root}/${shader}" ]]; then
        echo "ERROR: Android renderer shader not found: ${shader_root}/${shader}" >&2
        exit 1
    fi
done

archive_dir="$(cd "$(dirname "${archive}")" && pwd)"
archive_path="${archive_dir}/$(basename "${archive}")"

# GenerateSohOtr packages the modern combined shader. Android instead loads the
# OpenGL ES-compatible vertex/fragment entries from soh.o2r. In the unified build
# these entries intentionally include the cel-shading inputs and uniforms. The
# archive paths include a shaders/ prefix that is not present beneath shader_root,
# so stage that exact layout before updating the archive.
zip -q -d "${archive_path}" "${archive_entries[@]}" >/dev/null 2>&1 || true

staging_dir="$(mktemp -d)"
trap 'rm -rf "${staging_dir}"' EXIT

for i in "${!shader_sources[@]}"; do
    staged_shader="${staging_dir}/${archive_entries[$i]}"
    mkdir -p "$(dirname "${staged_shader}")"
    cp "${shader_root}/${shader_sources[$i]}" "${staged_shader}"
done

(
    cd "${staging_dir}"
    zip -q "${archive_path}" "${archive_entries[@]}"
)

for shader in "${archive_entries[@]}"; do
    if ! unzip -Z1 "${archive_path}" | grep -Fxq "${shader}"; then
        echo "ERROR: Failed to add ${shader} to ${archive_path}" >&2
        exit 1
    fi
done

echo "Added Android compatibility renderer shaders to ${archive_path}"
