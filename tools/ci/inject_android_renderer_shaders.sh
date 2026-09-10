#!/usr/bin/env bash

set -euo pipefail

archive="${1:-Android/app/src/main/assets/soh.o2r}"
# NEI 2 uses the modern renderer. Preserve this pipeline entry point, but
# validate the generated combined shader instead of injecting legacy shaders.
if [[ -f libultraship/src/fast/shaders/opengl/default.shader.glsl ]]; then
    entries="$(unzip -Z1 "${archive}")"
    if ! grep -Fxq 'shaders/opengl/default.shader.glsl' <<< "${entries}"; then
        echo "ERROR: NEI 2 combined shader is missing; regenerate soh.o2r." >&2
        exit 1
    fi
    echo "Verified NEI 2 combined renderer shader in ${archive}"
    exit 0
fi
shader_root="libultraship/src/fast/shaders"
vertex_shader="opengl/default.shader.vs"
fragment_shader="opengl/default.shader.fs"

if [[ ! -f "${archive}" ]]; then
    echo "ERROR: Resource archive not found: ${archive}" >&2
    exit 1
fi

for shader in "${vertex_shader}" "${fragment_shader}"; do
    if [[ ! -f "${shader_root}/${shader}" ]]; then
        echo "ERROR: Android renderer shader not found: ${shader_root}/${shader}" >&2
        exit 1
    fi
done

if grep -q "o_toon" "${shader_root}/${vertex_shader}" "${shader_root}/${fragment_shader}"; then
    echo "ERROR: Cel-shading shaders cannot be used by the compatibility renderer." >&2
    exit 1
fi

archive_dir="$(cd "$(dirname "${archive}")" && pwd)"
archive_path="${archive_dir}/$(basename "${archive}")"

# GenerateSohOtr packages the modern combined shader. The Android compatibility
# renderer instead loads these legacy vertex/fragment entries from soh.o2r.
zip -q -d "${archive_path}" "${vertex_shader}" "${fragment_shader}" >/dev/null 2>&1 || true
(
    cd "${shader_root}"
    zip -q "${archive_path}" "${vertex_shader}" "${fragment_shader}"
)

for shader in "${vertex_shader}" "${fragment_shader}"; do
    if ! unzip -Z1 "${archive_path}" | grep -Fxq "${shader}"; then
        echo "ERROR: Failed to add ${shader} to ${archive_path}" >&2
        exit 1
    fi
done

echo "Added Android compatibility renderer shaders to ${archive_path}"
