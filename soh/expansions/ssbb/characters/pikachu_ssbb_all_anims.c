// The 322 SSBB animation float tables (*_ssbb.c, ~82 MB) are no longer compiled
// into the .exe. They are shipped as a flat little-endian binary,
// NEI/pikachu_anims.bin, and loaded once at runtime by PikaAnims_EnsureLoaded()
// in soh/mods/transformation_masks/pikachu_form.cpp, which fills the
// pikachu_ssbb_all_anims[] table declared in pikachu_ssbb_all_anims.h.
//
// Regenerate the binary with:
//   python apps/verify_ssbb_anims.py build \
//       --chars soh/expansions/ssbb/characters --name pikachu_ssbb \
//       --out NEI/pikachu_anims.bin
//
// Or via the extractor: brawl_to_oot.py ... --emit-bin NEI/pikachu_anims.bin
//
// This translation unit is intentionally empty and is no longer #included.
