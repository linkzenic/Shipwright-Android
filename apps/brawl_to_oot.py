#!/usr/bin/env python3
"""
brawl_to_oot.py - Convert Smash Bros Brawl assets to OOT SkelAnime format

Converts BrawlCrate-exported COLLADA (.dae) models and Maya (.anim) animations
into C source files compatible with Ship of Harkinian's SkelAnime system.

Rotation convention: ZYX Euler (absolute values, degrees → s16)
Confirmed via smash_viewer.html testing against Blender reference.

Usage (from repo root):
    python apps/brawl_to_oot.py --dae model.dae --anim idle.anim --name character --out soh/expansions/ssbb/characters/
    python apps/brawl_to_oot.py --dae model.dae --name character --out soh/expansions/ssbb/characters/
    python apps/brawl_to_oot.py --dae model.dae --info  (just show bone hierarchy)
"""

import argparse
import os
import sys
import math
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from collections import Counter

# ── Constants ────────────────────────────────────────────────────────────────

DEG_TO_S16 = 65536.0 / 360.0  # degrees → OOT s16 rotation
LIMB_NONE = 0xFF
MAX_VTX_LOAD = 32  # N64 vertex buffer limit

NS_2005 = 'http://www.collada.org/2005/11/COLLADASchema'
NS_2008 = 'http://www.collada.org/2008/03/COLLADASchema'
NS = {'c': NS_2008}  # default, auto-detected in parser

# ── Data Classes ─────────────────────────────────────────────────────────────

@dataclass
class Bone:
    name: str
    index: int
    parent_index: int  # -1 for root
    local_pos: Tuple[float, float, float]  # position relative to parent
    local_rot_deg: Tuple[float, float, float]  # ZYX Euler degrees
    children: List[int] = field(default_factory=list)
    # OOT limb tree
    child_limb: int = LIMB_NONE
    sibling_limb: int = LIMB_NONE

@dataclass
class MeshPart:
    """Vertices and triangles assigned to one limb"""
    limb_index: int
    positions: List[Tuple[float, float, float]]
    normals: List[Tuple[float, float, float]]
    uvs: List[Tuple[float, float]]
    triangles: List[Tuple[int, int, int]]

# ── Matrix Helpers ────────────────────────────────────────────────────────────

def _mat4_mul_vec3(mat, v, w=1.0):
    """Multiply 4x4 matrix (list of 4 lists of 4) by (x,y,z,w), return (x,y,z)"""
    x = mat[0][0]*v[0] + mat[0][1]*v[1] + mat[0][2]*v[2] + mat[0][3]*w
    y = mat[1][0]*v[0] + mat[1][1]*v[1] + mat[1][2]*v[2] + mat[1][3]*w
    z = mat[2][0]*v[0] + mat[2][1]*v[1] + mat[2][2]*v[2] + mat[2][3]*w
    return (x, y, z)

def _mat4_mul_dir3(mat, v):
    """Multiply upper 3x3 of 4x4 matrix by direction vector (no translation), normalize"""
    x = mat[0][0]*v[0] + mat[0][1]*v[1] + mat[0][2]*v[2]
    y = mat[1][0]*v[0] + mat[1][1]*v[1] + mat[1][2]*v[2]
    z = mat[2][0]*v[0] + mat[2][1]*v[1] + mat[2][2]*v[2]
    length = math.sqrt(x*x + y*y + z*z)
    if length > 0.0001:
        return (x/length, y/length, z/length)
    return (0.0, 0.0, 1.0)

def _mat4_mul(a, b):
    """Multiply two 4x4 matrices"""
    result = [[0.0]*4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            for k in range(4):
                result[i][j] += a[i][k] * b[k][j]
    return result

def _mat4_identity():
    return [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]

def _mat4_translate(tx, ty, tz):
    return [[1,0,0,tx],[0,1,0,ty],[0,0,1,tz],[0,0,0,1]]

def _mat4_rotate_x(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return [[1,0,0,0],[0,c,-s,0],[0,s,c,0],[0,0,0,1]]

def _mat4_rotate_y(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return [[c,0,s,0],[0,1,0,0],[-s,0,c,0],[0,0,0,1]]

def _mat4_rotate_z(deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return [[c,-s,0,0],[s,c,0,0],[0,0,1,0],[0,0,0,1]]

def _mat4_from_translate_rotate_zyx(pos, rot_deg):
    """Create transform matching OOT's Matrix_TranslateRotateZYX: T * Rz * Ry * Rx"""
    t = _mat4_translate(pos[0], pos[1], pos[2])
    rz = _mat4_rotate_z(rot_deg[2])
    ry = _mat4_rotate_y(rot_deg[1])
    rx = _mat4_rotate_x(rot_deg[0])
    return _mat4_mul(_mat4_mul(_mat4_mul(t, rz), ry), rx)

def _mat4_affine_inverse(m):
    """Inverse of affine transform (rotation + translation, no scale).
    M = [R | t], M^-1 = [R^T | -R^T * t]"""
    rt = [[m[0][0], m[1][0], m[2][0], 0.0],
          [m[0][1], m[1][1], m[2][1], 0.0],
          [m[0][2], m[1][2], m[2][2], 0.0],
          [0.0, 0.0, 0.0, 1.0]]
    tx, ty, tz = m[0][3], m[1][3], m[2][3]
    rt[0][3] = -(rt[0][0]*tx + rt[0][1]*ty + rt[0][2]*tz)
    rt[1][3] = -(rt[1][0]*tx + rt[1][1]*ty + rt[1][2]*tz)
    rt[2][3] = -(rt[2][0]*tx + rt[2][1]*ty + rt[2][2]*tz)
    return rt

def _mat4_general_inverse(m):
    """General 4x4 matrix inverse using Gauss-Jordan elimination."""
    # Augmented matrix [M | I]
    n = 4
    aug = [m[i][:] + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for col in range(n):
        # Pivot
        max_row = max(range(col, n), key=lambda r: abs(aug[r][col]))
        aug[col], aug[max_row] = aug[max_row], aug[col]
        pivot = aug[col][col]
        if abs(pivot) < 1e-12:
            return _mat4_identity()  # Singular
        for j in range(2 * n):
            aug[col][j] /= pivot
        for row in range(n):
            if row == col:
                continue
            factor = aug[row][col]
            for j in range(2 * n):
                aug[row][j] -= factor * aug[col][j]
    return [aug[i][n:] for i in range(n)]

@dataclass
class AnimChannel:
    bone_name: str
    attr: str  # rotateX, rotateY, rotateZ, translateX, etc.
    keyframes: List[Tuple[float, float]]  # (time, value) pairs


# ── Fast64 C File Parser ─────────────────────────────────────────────────────

import re

def parse_fast64_c(filepath, scale=1.0):
    """Parse a Fast64-exported C file and extract vertex data and triangles.

    Returns:
        positions: List[(x,y,z)] in Brawl units (divided by scale to undo OOT scaling)
        normals:   List[(nx,ny,nz)] normalized floats
        uvs:       List[(s,t)] raw s10.5 texture coords
        alphas:    List[int] vertex alpha values (0-255)
        triangles: List[(a,b,c)] global vertex indices
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        text = f.read()

    # Parse all Vtx arrays (skip cull verts)
    positions = []
    normals = []
    uvs = []
    alphas = []
    vtx_array_offsets = {}  # array_name -> start index in global positions list

    # Match Vtx array declarations: Vtx name[count] = { ... };
    vtx_pattern = re.compile(
        r'Vtx\s+(\w+)\s*\[\s*\d+\s*\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )

    for m in vtx_pattern.finditer(text):
        array_name = m.group(1)
        # Skip cull vertex arrays
        if 'cull' in array_name.lower():
            continue

        array_body = m.group(2)
        vtx_array_offsets[array_name] = len(positions)

        # Parse each vertex: {{ {x, y, z}, flag, {u, v}, {nx, ny, nz, a} }}
        vtx_re = re.compile(
            r'\{\s*\{\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}'
            r'\s*,\s*\d+\s*,'
            r'\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*\}\s*,'
            r'\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}'
            r'\s*\}\s*\}'
        )
        for vm in vtx_re.finditer(array_body):
            px, py, pz = int(vm.group(1)), int(vm.group(2)), int(vm.group(3))
            u, v = int(vm.group(4)), int(vm.group(5))
            nx, ny, nz = int(vm.group(6)), int(vm.group(7)), int(vm.group(8))
            a = int(vm.group(9))
            # Convert integer positions back to Brawl units (undo the OOT scale)
            positions.append((px / scale, py / scale, pz / scale))
            # Convert s8 normals to float
            def s8(v):
                return (v - 256) / 127.0 if v > 127 else v / 127.0
            normals.append((s8(nx), s8(ny), s8(nz)))
            uvs.append((u, v))
            alphas.append(a)

    # Parse triangle display lists to get triangle indices
    triangles = []

    # Find all tri DLs (skip material DLs)
    dl_pattern = re.compile(
        r'Gfx\s+(\w+_tri_\d+)\s*\[\s*\]\s*=\s*\{(.*?)\};',
        re.DOTALL
    )

    for dm in dl_pattern.finditer(text):
        dl_name = dm.group(1)
        dl_body = dm.group(2)

        # Find which vertex array this DL loads from (gsSPVertex)
        # gsSPVertex(array_name + offset, count, bufstart)
        current_vtx_base = 0
        current_buf_remap = {}  # buffer_pos -> global_index

        for line in dl_body.split('\n'):
            line = line.strip()

            # Parse gsSPVertex
            sp_vtx = re.match(
                r'gsSPVertex\s*\(\s*(\w+)\s*\+?\s*(\d+)?\s*,\s*(\d+)\s*,\s*(\d+)\s*\)',
                line
            )
            if sp_vtx:
                arr_name = sp_vtx.group(1)
                arr_offset = int(sp_vtx.group(2)) if sp_vtx.group(2) else 0
                count = int(sp_vtx.group(3))
                buf_start = int(sp_vtx.group(4))
                global_base = vtx_array_offsets.get(arr_name, 0) + arr_offset
                for i in range(count):
                    current_buf_remap[buf_start + i] = global_base + i
                continue

            # Parse gsSP2Triangles
            sp2t = re.match(
                r'gsSP2Triangles\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*,'
                r'\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\)',
                line
            )
            if sp2t:
                a0, b0, c0 = int(sp2t.group(1)), int(sp2t.group(2)), int(sp2t.group(3))
                a1, b1, c1 = int(sp2t.group(4)), int(sp2t.group(5)), int(sp2t.group(6))
                triangles.append((current_buf_remap.get(a0, a0),
                                  current_buf_remap.get(b0, b0),
                                  current_buf_remap.get(c0, c0)))
                triangles.append((current_buf_remap.get(a1, a1),
                                  current_buf_remap.get(b1, b1),
                                  current_buf_remap.get(c1, c1)))
                continue

            # Parse gsSP1Triangle
            sp1t = re.match(
                r'gsSP1Triangle\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\d+\s*\)',
                line
            )
            if sp1t:
                a, b, c = int(sp1t.group(1)), int(sp1t.group(2)), int(sp1t.group(3))
                triangles.append((current_buf_remap.get(a, a),
                                  current_buf_remap.get(b, b),
                                  current_buf_remap.get(c, c)))

    print(f'  Fast64 C: {len(positions)} vertices, {len(triangles)} triangles')
    return positions, normals, uvs, alphas, triangles


def _detect_axis_transform(f64_positions, dae_positions):
    """Auto-detect axis mapping + scale between Fast64 (Blender Z-up) and DAE (Y-up) coordinates.

    Tries all axis permutations and sign combos to find the mapping where
    F64 = transform(DAE) with the most uniform scale factor.

    Returns: (axis_map, signs, scale) where:
        axis_map: (i,j,k) meaning F64.x=DAE[i], F64.y=DAE[j], F64.z=DAE[k]
        signs: (sx,sy,sz) ±1 per axis
        scale: uniform scale factor
    """
    from itertools import permutations

    dae_min = [min(p[i] for p in dae_positions) for i in range(3)]
    dae_max = [max(p[i] for p in dae_positions) for i in range(3)]
    f64_min = [min(p[i] for p in f64_positions) for i in range(3)]
    f64_max = [max(p[i] for p in f64_positions) for i in range(3)]

    dae_ranges = [dae_max[i] - dae_min[i] for i in range(3)]
    f64_ranges = [f64_max[i] - f64_min[i] for i in range(3)]
    dae_centers = [(dae_max[i] + dae_min[i]) / 2.0 for i in range(3)]
    f64_centers = [(f64_max[i] + f64_min[i]) / 2.0 for i in range(3)]

    best_error = float('inf')
    best_result = ((0, 1, 2), (1, 1, 1), 1.0)

    for perm in permutations(range(3)):
        # For this axis permutation, compute scale per F64 axis
        scales = []
        for f_axis, d_axis in enumerate(perm):
            if dae_ranges[d_axis] > 0.001:
                scales.append(f64_ranges[f_axis] / dae_ranges[d_axis])
            else:
                scales.append(0.0)

        if not all(s > 0.001 for s in scales):
            continue

        avg_scale = sum(scales) / len(scales)
        error = sum((s - avg_scale) ** 2 for s in scales)

        if error < best_error:
            # Determine signs from center alignment
            signs = []
            for f_axis, d_axis in enumerate(perm):
                if abs(dae_centers[d_axis] * avg_scale - f64_centers[f_axis]) < \
                   abs(dae_centers[d_axis] * avg_scale + f64_centers[f_axis]):
                    signs.append(1)
                else:
                    signs.append(-1)
            best_error = error
            best_result = (perm, tuple(signs), avg_scale)

    perm, signs, scale = best_result
    axis_names = 'xyz'
    mapping_str = ', '.join(
        f'F64.{axis_names[i]}={("" if signs[i]>0 else "-")}DAE.{axis_names[perm[i]]}*{scale:.1f}'
        for i in range(3)
    )
    print(f'  Axis mapping: {mapping_str}')
    return best_result


def assign_fast64_to_bones(f64_positions, f64_normals, f64_triangles,
                           dae_positions, dae_bone_assignments,
                           bone_inv_world, scale):
    """Assign Fast64 vertices to bones using nearest-neighbor matching against .dae vertices.

    Returns: List[MeshPart] split by bone, with positions in bone-local space.
    """
    # Auto-detect axis mapping between Fast64 and DAE coordinate spaces
    axis_map, signs, f64_scale = _detect_axis_transform(f64_positions, dae_positions)

    # Convert Fast64 positions to DAE space for matching
    def f64_to_dae(pos):
        """Convert Fast64 position to DAE coordinate space"""
        # Inverse of: F64[i] = signs[i] * DAE[axis_map[i]] * f64_scale
        # So: DAE[axis_map[i]] = F64[i] / (signs[i] * f64_scale)
        result = [0.0, 0.0, 0.0]
        for i in range(3):
            result[axis_map[i]] = pos[i] / (signs[i] * f64_scale)
        return tuple(result)

    f64_in_dae_space = [f64_to_dae(p) for p in f64_positions]

    def _nearest_bone(pos, dae_pos, dae_bones):
        best_dist = float('inf')
        best_bone = 0
        for i, dp in enumerate(dae_pos):
            dx = pos[0] - dp[0]
            dy = pos[1] - dp[1]
            dz = pos[2] - dp[2]
            d = dx*dx + dy*dy + dz*dz
            if d < best_dist:
                best_dist = d
                best_bone = dae_bones[i]
        return best_bone

    # Assign each Fast64 vertex to a bone (matching in DAE space)
    print(f'  Matching {len(f64_positions)} Fast64 verts to {len(dae_positions)} DAE verts...')
    per_vertex_bones = []
    for i, pos in enumerate(f64_in_dae_space):
        bone = _nearest_bone(pos, dae_positions, dae_bone_assignments)
        per_vertex_bones.append(bone)

    # Split triangles by bone (majority vote)
    bone_tris = {}
    for a, b, c in f64_triangles:
        bones_abc = [per_vertex_bones[a], per_vertex_bones[b], per_vertex_bones[c]]
        bone = Counter(bones_abc).most_common(1)[0][0]
        bone_tris.setdefault(bone, []).append((a, b, c))

    # Convert F64 normals to DAE space too
    def f64_norm_to_dae(n):
        result = [0.0, 0.0, 0.0]
        for i in range(3):
            result[axis_map[i]] = n[i] * signs[i]  # no scale for normals, just axis+sign
        length = math.sqrt(sum(x*x for x in result))
        if length > 0.0001:
            return tuple(x / length for x in result)
        return (0.0, 0.0, 1.0)

    f64_norms_dae = [f64_norm_to_dae(n) for n in f64_normals]

    # Build MeshParts per bone (positions in DAE bone-local space)
    meshes = []
    for bone_idx, tri_list in sorted(bone_tris.items()):
        used = set()
        for a, b, c in tri_list:
            used.update([a, b, c])
        sorted_v = sorted(used)
        remap = {v: i for i, v in enumerate(sorted_v)}

        inv_world = bone_inv_world.get(bone_idx)

        new_pos, new_norm, new_uv = [], [], []
        for vi in sorted_v:
            p = f64_in_dae_space[vi]  # positions already in DAE world space
            n = f64_norms_dae[vi]
            if inv_world:
                p = _mat4_mul_vec3(inv_world, p)
                n = _mat4_mul_dir3(inv_world, n)
            new_pos.append(p)
            new_norm.append(n)
            new_uv.append((0.0, 0.0))  # no UVs for now

        new_tris = [(remap[a], remap[b], remap[c]) for a, b, c in tri_list]

        meshes.append(MeshPart(
            limb_index=bone_idx,
            positions=new_pos,
            normals=new_norm,
            uvs=new_uv,
            triangles=new_tris,
        ))

    print(f'  Split into {len(meshes)} limb mesh parts')
    return meshes

@dataclass
class AnimData:
    name: str
    start_time: int
    end_time: int
    channels: Dict[str, Dict[str, List[Tuple[float, float]]]]
    # channels[bone_name][attr] = [(time, value), ...]

# ── COLLADA Parser ───────────────────────────────────────────────────────────

class ColladaParser:
    def __init__(self, filepath):
        global NS
        self.tree = ET.parse(filepath)
        self.root = self.tree.getroot()
        # Auto-detect COLLADA namespace
        tag = self.root.tag
        if NS_2008 in tag:
            NS = {'c': NS_2008}
        elif NS_2005 in tag:
            NS = {'c': NS_2005}
        else:
            # Try without namespace
            NS = {'c': tag.split('}')[0].lstrip('{') if '}' in tag else NS_2008}
        self.ns_uri = NS['c']
        self.bones: List[Bone] = []
        self.bone_map: Dict[str, int] = {}
        self.meshes: List[MeshPart] = []

    def parse(self):
        self._parse_skeleton()
        self._build_limb_tree()
        self._compute_world_transforms()
        self._parse_geometry()
        return self.bones, self.meshes

    def _find(self, element, path):
        return element.find(path, NS)

    def _findall(self, element, path):
        return element.findall(path, NS)

    def _parse_skeleton(self):
        """Extract bone hierarchy from visual_scene"""
        vis_scene = self._find(self.root, './/c:library_visual_scenes/c:visual_scene')
        if vis_scene is None:
            print("ERROR: No visual_scene found in COLLADA")
            return

        # Find root JOINT node
        for node in self._findall(vis_scene, './/c:node[@type="JOINT"]'):
            parent = self._find_parent_joint(vis_scene, node)
            if parent is None:
                self._parse_bone_recursive(node, -1)
                break

    def _find_parent_joint(self, root, target):
        """Find parent JOINT of a given node"""
        for node in root.iter(f'{{{self.ns_uri}}}node'):
            if node.get('type') == 'JOINT':
                for child in node:
                    tag = child.tag.replace(f'{{{self.ns_uri}}}', '')
                    if tag == 'node' and child is target:
                        return node
        return None

    def _parse_bone_recursive(self, node, parent_idx):
        """Recursively parse bone hierarchy"""
        name = node.get('name') or node.get('sid') or f'bone_{len(self.bones)}'

        # Parse local transform
        pos = [0.0, 0.0, 0.0]
        rot_z, rot_y, rot_x = 0.0, 0.0, 0.0

        for child in node:
            tag = child.tag.split('}')[-1] if '}' in child.tag else child.tag

            if tag == 'translate':
                vals = [float(v) for v in child.text.split()]
                pos = vals[:3]

            elif tag == 'rotate':
                vals = [float(v) for v in child.text.split()]
                if len(vals) == 4:
                    axis = vals[:3]
                    angle = vals[3]
                    # BrawlCrate exports Z, Y, X rotations in sequence
                    if abs(axis[2]) > 0.9:    rot_z = angle
                    elif abs(axis[1]) > 0.9:  rot_y = angle
                    elif abs(axis[0]) > 0.9:  rot_x = angle

        idx = len(self.bones)
        bone = Bone(
            name=name,
            index=idx,
            parent_index=parent_idx,
            local_pos=tuple(pos),
            local_rot_deg=(rot_x, rot_y, rot_z),
        )
        self.bones.append(bone)
        self.bone_map[name] = idx

        if parent_idx >= 0:
            self.bones[parent_idx].children.append(idx)

        # Parse child JOINT nodes
        for child in node:
            tag = child.tag.split('}')[-1] if '}' in child.tag else child.tag
            if tag == 'node' and child.get('type') == 'JOINT':
                self._parse_bone_recursive(child, idx)

    def _build_limb_tree(self):
        """Convert parent-child tree to OOT child/sibling format"""
        for bone in self.bones:
            if bone.children:
                bone.child_limb = bone.children[0]
                # Set sibling chain
                for i in range(len(bone.children) - 1):
                    self.bones[bone.children[i]].sibling_limb = bone.children[i + 1]

    def _compute_world_transforms(self):
        """Compute bone world transforms matching OOT's Matrix_TranslateRotateZYX order.
        This ensures vertex local-space positions are consistent with OOT rendering."""
        self._bone_world = {}
        self.bone_inv_world = {}
        for bone in self.bones:
            local = _mat4_from_translate_rotate_zyx(bone.local_pos, bone.local_rot_deg)
            if bone.parent_index >= 0:
                world = _mat4_mul(self._bone_world[bone.parent_index], local)
            else:
                world = local
            self._bone_world[bone.index] = world
            self.bone_inv_world[bone.index] = _mat4_affine_inverse(world)

    def _parse_geometry(self):
        """Parse mesh geometry and skin bindings"""
        # Parse skin controllers to get per-vertex bone assignments
        skin_data = self._parse_skin_controllers()

        lib_geom = self._find(self.root, './/c:library_geometries')
        if lib_geom is None:
            return

        for geom in self._findall(lib_geom, 'c:geometry'):
            geom_id = geom.get('id')
            mesh = self._find(geom, 'c:mesh')
            if mesh is None:
                continue

            positions, normals, uvs = [], [], []
            sources = {}

            # Parse sources
            for source in self._findall(mesh, 'c:source'):
                sid = source.get('id')
                float_array = self._find(source, 'c:float_array')
                if float_array is not None and float_array.text:
                    data = [float(v) for v in float_array.text.split()]
                    accessor = self._find(source, './/c:accessor')
                    stride = int(accessor.get('stride', '1')) if accessor is not None else 1
                    sources[sid] = (data, stride)

            # Parse vertices element to get position source
            vertices = self._find(mesh, 'c:vertices')
            vert_source = None
            if vertices is not None:
                inp = self._find(vertices, 'c:input[@semantic="POSITION"]')
                if inp is not None:
                    vert_source = inp.get('source', '').lstrip('#')

            # Parse triangles
            for tri in self._findall(mesh, 'c:triangles') + self._findall(mesh, 'c:polylist'):
                inputs = {}
                max_offset = 0
                for inp in self._findall(tri, 'c:input'):
                    sem = inp.get('semantic')
                    offset = int(inp.get('offset', '0'))
                    src = inp.get('source', '').lstrip('#')
                    # VERTEX semantic maps to the vertices element's source
                    if sem == 'VERTEX' and vert_source:
                        src = vert_source
                    inputs[sem] = (offset, src)
                    max_offset = max(max_offset, offset)

                stride = max_offset + 1

                p_elem = self._find(tri, 'c:p')
                if p_elem is None or not p_elem.text:
                    continue
                indices = [int(v) for v in p_elem.text.split()]

                # Build vertex list for this geometry
                pos_data = sources.get(inputs.get('VERTEX', inputs.get('POSITION', (0, '')))[1])
                norm_src = inputs.get('NORMAL', (0, ''))[1]
                norm_data = sources.get(norm_src)
                uv_src = inputs.get('TEXCOORD', (0, ''))[1]
                uv_data = sources.get(uv_src)

                # Collect unique vertices
                vert_map = {}
                verts_pos, verts_norm, verts_uv = [], [], []
                vert_pos_indices = []  # track COLLADA position index per unique vertex
                tris = []

                for t in range(0, len(indices), stride * 3):
                    tri_verts = []
                    for v in range(3):
                        base = t + v * stride
                        if base + stride > len(indices):
                            break

                        pi = indices[base + inputs.get('VERTEX', inputs.get('POSITION', (0, '')))[0]]
                        ni = indices[base + inputs['NORMAL'][0]] if 'NORMAL' in inputs else 0
                        ui = indices[base + inputs['TEXCOORD'][0]] if 'TEXCOORD' in inputs else 0
                        key = (pi, ni, ui)

                        if key not in vert_map:
                            vert_map[key] = len(verts_pos)
                            if pos_data:
                                d, s = pos_data
                                verts_pos.append(tuple(d[pi*s:pi*s+3]))
                            else:
                                verts_pos.append((0, 0, 0))
                            if norm_data:
                                d, s = norm_data
                                verts_norm.append(tuple(d[ni*s:ni*s+3]))
                            else:
                                verts_norm.append((0, 0, 1))
                            if uv_data:
                                d, s = uv_data
                                verts_uv.append(tuple(d[ui*s:ui*s+2]))
                            else:
                                verts_uv.append((0, 0))
                            vert_pos_indices.append(pi)

                        tri_verts.append(vert_map[key])

                    if len(tri_verts) == 3:
                        tris.append(tuple(tri_verts))

                # Accumulate raw geometry for --collada-skin mode (ALL geometries including eyes)
                if verts_pos:
                    if not hasattr(self, 'raw_verts_pos'):
                        self.raw_verts_pos = []
                        self.raw_verts_norm = []
                        self.raw_verts_uv = []
                        self.raw_triangles = []
                        self.raw_vert_pos_indices = []
                        self._raw_geom_ids = []
                        self._raw_pos_idx_offset = 0  # Accumulated offset for position indices
                    offset = len(self.raw_verts_pos)
                    self.raw_verts_pos.extend(verts_pos)
                    self.raw_verts_norm.extend(verts_norm)
                    self.raw_verts_uv.extend(verts_uv)
                    self.raw_triangles.extend((a + offset, b + offset, c + offset) for a, b, c in tris)
                    # CRITICAL: offset position indices so each geometry maps to the correct
                    # weights in the combined weight array. Without this, eye verts would
                    # get body weights (both start at index 0).
                    self.raw_vert_pos_indices.extend(pi + self._raw_pos_idx_offset for pi in vert_pos_indices)
                    # Track max position index from this geometry to offset the next one
                    if vert_pos_indices:
                        self._raw_pos_idx_offset += max(vert_pos_indices) + 1
                    self._raw_geom_ids.append(geom_id)
                    # Track per-geometry triangle counts for body/eye DL separation
                    if not hasattr(self, '_raw_geom_tri_counts'):
                        self._raw_geom_tri_counts = {}
                    self._raw_geom_tri_counts[geom_id] = len(tris)

                if verts_pos:
                    sd = skin_data.get(geom_id)
                    if sd and sd['per_vertex_bones']:
                        # Split triangles by primary bone of their vertices
                        bone_tris = {}
                        for ti, (a, b, c) in enumerate(tris):
                            bones_abc = []
                            for vi in (a, b, c):
                                pi_idx = vert_pos_indices[vi]
                                if pi_idx < len(sd['per_vertex_bones']):
                                    bones_abc.append(sd['per_vertex_bones'][pi_idx])
                                else:
                                    bones_abc.append(0)
                            # Majority vote for which bone owns this triangle
                            bone = Counter(bones_abc).most_common(1)[0][0]
                            bone_tris.setdefault(bone, []).append(ti)

                        # Create a MeshPart per bone
                        for bone_idx, tri_list in bone_tris.items():
                            used = set()
                            for ti in tri_list:
                                used.update(tris[ti])
                            sorted_v = sorted(used)
                            remap = {v: i for i, v in enumerate(sorted_v)}

                            # Use our computed inverse world transform (matches OOT ZYX order)
                            inv_world = self.bone_inv_world.get(bone_idx)

                            new_pos, new_norm, new_uv = [], [], []
                            for vi in sorted_v:
                                p = verts_pos[vi]
                                n = verts_norm[vi]
                                if inv_world:
                                    p = _mat4_mul_vec3(inv_world, p)
                                    n = _mat4_mul_dir3(inv_world, n)
                                new_pos.append(p)
                                new_norm.append(n)
                                new_uv.append(verts_uv[vi])

                            new_tris = [(remap[a], remap[b], remap[c])
                                        for ti in tri_list
                                        for a, b, c in [tris[ti]]]

                            self.meshes.append(MeshPart(
                                limb_index=bone_idx,
                                positions=new_pos,
                                normals=new_norm,
                                uvs=new_uv,
                                triangles=new_tris,
                            ))
                    else:
                        # No skin data — assign all to root bone
                        self.meshes.append(MeshPart(
                            limb_index=0,
                            positions=verts_pos,
                            normals=verts_norm,
                            uvs=verts_uv,
                            triangles=tris,
                        ))

    def _parse_skin_controllers(self) -> Dict[str, dict]:
        """Parse skin controllers to get per-vertex bone assignments and inverse bind matrices.

        Returns dict mapping geometry_id -> {
            'per_vertex_bones': List[int],   # bone index for each position vertex
            'bone_inv_bind': Dict[int, mat], # inverse bind matrix per bone index
            'bind_shape_matrix': mat or None # bind shape matrix (usually identity)
        }
        """
        skin_data = {}
        lib_ctrl = self._find(self.root, './/c:library_controllers')
        if lib_ctrl is None:
            return skin_data

        for ctrl in self._findall(lib_ctrl, 'c:controller'):
            skin = self._find(ctrl, 'c:skin')
            if skin is None:
                continue

            geom_ref = skin.get('source', '').lstrip('#')

            # Parse bind shape matrix
            bsm_elem = self._find(skin, 'c:bind_shape_matrix')
            bind_shape_matrix = None
            if bsm_elem is not None and bsm_elem.text:
                vals = [float(v) for v in bsm_elem.text.split()]
                if len(vals) == 16:
                    bind_shape_matrix = [vals[i:i+4] for i in range(0, 16, 4)]
                    # Check if it's identity — skip if so
                    is_identity = all(
                        abs(bind_shape_matrix[r][c] - (1.0 if r == c else 0.0)) < 1e-6
                        for r in range(4) for c in range(4)
                    )
                    if is_identity:
                        bind_shape_matrix = None

            # Parse all source arrays in this skin
            sources = {}
            for source in self._findall(skin, 'c:source'):
                sid = source.get('id')
                name_array = self._find(source, 'c:Name_array')
                float_array = self._find(source, 'c:float_array')
                if name_array is not None and name_array.text:
                    sources[sid] = ('names', name_array.text.split())
                elif float_array is not None and float_array.text:
                    data = [float(v) for v in float_array.text.split()]
                    accessor = self._find(source, './/c:accessor')
                    stride = int(accessor.get('stride', '1')) if accessor is not None else 1
                    sources[sid] = ('floats', data, stride)

            # Get joint names and inverse bind matrices from <joints>
            joint_names = []
            inv_bind_matrices = []
            for inp in self._findall(skin, './/c:joints/c:input'):
                sem = inp.get('semantic')
                src_id = inp.get('source', '').lstrip('#')
                if sem == 'JOINT' and src_id in sources:
                    joint_names = sources[src_id][1]
                elif sem == 'INV_BIND_MATRIX' and src_id in sources:
                    _, data, stride = sources[src_id]
                    for i in range(0, len(data), 16):
                        mat = [data[i+r:i+r+4] for r in range(0, 16, 4)]
                        inv_bind_matrices.append(mat)

            # Parse <vertex_weights>
            vw = self._find(skin, './/c:vertex_weights')
            if vw is None:
                continue

            # Get input offsets
            joint_offset = 0
            weight_offset = 1
            weight_src_id = None
            for inp in self._findall(vw, 'c:input'):
                sem = inp.get('semantic')
                offset = int(inp.get('offset', '0'))
                if sem == 'JOINT':
                    joint_offset = offset
                elif sem == 'WEIGHT':
                    weight_offset = offset
                    weight_src_id = inp.get('source', '').lstrip('#')

            # Get weight values array
            weight_values = []
            if weight_src_id and weight_src_id in sources:
                weight_values = sources[weight_src_id][1]

            # Parse vcount and v
            vcount_elem = self._find(vw, 'c:vcount')
            v_elem = self._find(vw, 'c:v')
            if vcount_elem is None or v_elem is None:
                continue

            vcounts = [int(x) for x in vcount_elem.text.split()]
            v_data = [int(x) for x in v_elem.text.split()]
            vstride = max(joint_offset, weight_offset) + 1

            # Build per-vertex bone assignments (highest weight wins for single-bone mode)
            # Also store multi-weight data for skinning mode
            per_vertex_bones = []
            per_vertex_weights = []  # List of List[(bone_index, weight_float)]
            v_idx = 0
            for vc in vcounts:
                influences = []
                for inf in range(vc):
                    base = v_idx + inf * vstride
                    ji = v_data[base + joint_offset]
                    wi = v_data[base + weight_offset]
                    w = weight_values[wi] if wi < len(weight_values) else 1.0
                    jname = joint_names[ji] if ji < len(joint_names) else ''
                    bone_idx = self.bone_map.get(jname, 0)
                    if w > 0.001:
                        influences.append((bone_idx, w))
                v_idx += vc * vstride

                # Sort by weight descending, keep ALL influences (no truncation)
                influences.sort(key=lambda x: x[1], reverse=True)
                per_vertex_weights.append(influences)

                # Single-bone: pick the highest weight
                best_bone = influences[0][0] if influences else 0
                per_vertex_bones.append(best_bone)

            # Build inverse bind matrix lookup by skeleton bone index
            bone_inv_bind = {}
            for ji, jname in enumerate(joint_names):
                bi = self.bone_map.get(jname, -1)
                if bi >= 0 and ji < len(inv_bind_matrices):
                    bone_inv_bind[bi] = inv_bind_matrices[ji]

            skin_data[geom_ref] = {
                'per_vertex_bones': per_vertex_bones,
                'per_vertex_weights': per_vertex_weights,
                'bone_inv_bind': bone_inv_bind,
                'bind_shape_matrix': bind_shape_matrix,
                'joint_names': joint_names,
            }

            print(f'  Skin controller for "{geom_ref}": {len(per_vertex_bones)} vertices, '
                  f'{len(bone_inv_bind)} bones with inv_bind matrices')

        return skin_data


# ── Maya .anim Parser ────────────────────────────────────────────────────────

class AnimParser:
    def __init__(self, filepath):
        self.filepath = filepath

    def parse(self) -> AnimData:
        with open(self.filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        start_time = 0
        end_time = 0
        channels: Dict[str, Dict[str, List[Tuple[float, float]]]] = {}
        name = os.path.splitext(os.path.basename(self.filepath))[0]

        i = 0
        # Parse header
        while i < len(lines):
            line = lines[i].strip().rstrip(';')
            if line.startswith('startTime'):
                start_time = int(line.split()[1])
            elif line.startswith('endTime'):
                end_time = int(line.split()[1])
            elif line.startswith('anim '):
                break
            i += 1

        # Parse animation channels
        while i < len(lines):
            line = lines[i].strip().rstrip(';')
            if not line.startswith('anim '):
                i += 1
                continue

            parts = line.replace(';', '').split()
            if len(parts) < 4:
                i += 1
                continue

            attr = parts[2]      # rotateX, translateY, scaleZ, etc.
            bone_name = parts[3]

            if bone_name not in channels:
                channels[bone_name] = {}

            # Skip to animData block
            i += 1
            while i < len(lines) and 'animData' not in lines[i]:
                i += 1
            if i >= len(lines):
                break

            # Skip to keys block
            i += 1
            while i < len(lines) and 'keys' not in lines[i]:
                i += 1
            if i >= len(lines):
                break

            # Parse keyframes
            i += 1  # skip 'keys {'
            keyframes = []
            while i < len(lines):
                kl = lines[i].strip()
                if kl == '}':
                    i += 1
                    break
                kparts = kl.replace(';', '').split()
                if len(kparts) >= 2:
                    keyframes.append((float(kparts[0]), float(kparts[1])))
                i += 1

            channels[bone_name][attr] = keyframes

            # Skip closing braces
            while i < len(lines) and lines[i].strip() == '}':
                i += 1

        return AnimData(
            name=name,
            start_time=start_time,
            end_time=end_time,
            channels=channels,
        )


# ── OOT Skeleton Generator ──────────────────────────────────────────────────

class OotSkelGenerator:
    def __init__(self, bones: List[Bone], meshes: List[MeshPart], name: str, scale: float):
        self.bones = bones
        self.meshes = meshes
        self.name = name
        self.scale = scale
        # Group meshes by limb
        self.limb_meshes: Dict[int, List[MeshPart]] = {}
        for m in meshes:
            self.limb_meshes.setdefault(m.limb_index, []).append(m)

    def generate_header(self) -> str:
        lines = [
            f'#ifndef {self.name.upper()}_SKEL_H',
            f'#define {self.name.upper()}_SKEL_H',
            '',
            '#include "z64.h"',
            '',
            f'#define {self.name.upper()}_NUM_LIMBS {len(self.bones)}',
            '',
        ]

        # Extern declarations for DLs
        for i, bone in enumerate(self.bones):
            if i in self.limb_meshes:
                safe = self._safe_name(bone.name)
                lines.append(f'extern Gfx {self.name}_{safe}_dl[];')

        lines.extend([
            '',
            f'extern FlexSkeletonHeader {self.name}_skeleton;',
            '',
            f'#endif // {self.name.upper()}_SKEL_H',
        ])

        return '\n'.join(lines) + '\n'

    def generate_source(self) -> str:
        lines = [
            f'#include "expansions/ssbb/characters/{self.name}_skel.h"',
            '',
        ]

        dl_count = 0

        # Generate mesh data per limb
        for i, bone in enumerate(self.bones):
            if i not in self.limb_meshes:
                continue

            safe = self._safe_name(bone.name)
            meshparts = self.limb_meshes[i]

            # Merge all meshparts for this limb
            all_pos, all_norm, all_uv, all_tris = [], [], [], []
            for mp in meshparts:
                offset = len(all_pos)
                all_pos.extend(mp.positions)
                all_norm.extend(mp.normals)
                all_uv.extend(mp.uvs)
                all_tris.extend([(a+offset, b+offset, c+offset) for a,b,c in mp.triangles])

            # Generate vertex array
            lines.append(f'static Vtx {self.name}_{safe}_vtx[{len(all_pos)}] = {{')
            for j, (p, n, uv) in enumerate(zip(all_pos, all_norm, all_uv)):
                px = int(p[0] * self.scale)
                py = int(p[1] * self.scale)
                pz = int(p[2] * self.scale)
                nx = max(-128, min(127, int(n[0] * 127)))
                ny = max(-128, min(127, int(n[1] * 127)))
                nz = max(-128, min(127, int(n[2] * 127)))
                u = int(uv[0] * 1024) if uv else 0
                v = int((1.0 - uv[1]) * 1024) if uv else 0
                lines.append(f'    {{{{{{ {px}, {py}, {pz} }}, 0, {{ {u}, {v} }}, {{ {nx}, {ny}, {nz}, 0xFF }}}}}},')
            lines.append('};')
            lines.append('')

            # Generate display list with vertex batching
            lines.append(f'Gfx {self.name}_{safe}_dl[] = {{')

            # Process triangles in batches of MAX_VTX_LOAD vertices
            if len(all_pos) <= MAX_VTX_LOAD:
                lines.append(f'    gsSPVertex({self.name}_{safe}_vtx, {len(all_pos)}, 0),')
                for t in range(0, len(all_tris) - 1, 2):
                    a0, b0, c0 = all_tris[t]
                    a1, b1, c1 = all_tris[t + 1]
                    lines.append(f'    gsSP2Triangles({a0}, {b0}, {c0}, 0, {a1}, {b1}, {c1}, 0),')
                if len(all_tris) % 2 == 1:
                    a, b, c = all_tris[-1]
                    lines.append(f'    gsSP1Triangle({a}, {b}, {c}, 0),')
            else:
                # Batch vertices
                self._generate_batched_dl(lines, all_tris, len(all_pos), safe)

            lines.append('    gsSPEndDisplayList(),')
            lines.append('};')
            lines.append('')
            dl_count += 1

        # Generate limb definitions
        lines.append('// ── Limb Definitions ─────────────────────────────────────────────────────')
        for i, bone in enumerate(self.bones):
            safe = self._safe_name(bone.name)
            px = int(bone.local_pos[0] * self.scale)
            py = int(bone.local_pos[1] * self.scale)
            pz = int(bone.local_pos[2] * self.scale)
            child = bone.child_limb if bone.child_limb != LIMB_NONE else 255
            sibling = bone.sibling_limb if bone.sibling_limb != LIMB_NONE else 255
            dl_name = f'{self.name}_{safe}_dl' if i in self.limb_meshes else 'NULL'
            lines.append(f'static StandardLimb {self.name}_limb_{i:03d} = '
                        f'{{ {{ {px}, {py}, {pz} }}, {child}, {sibling}, {dl_name} }};')

        lines.append('')

        # Limb table
        lines.append(f'static void* {self.name}_limb_table[{len(self.bones)}] = {{')
        for i in range(len(self.bones)):
            comma = ',' if i < len(self.bones) - 1 else ''
            lines.append(f'    &{self.name}_limb_{i:03d}{comma}')
        lines.append('};')
        lines.append('')

        # FlexSkeletonHeader = { SkeletonHeader { void** segment, u8 limbCount, u8 skeletonType }, u8 dListCount }
        lines.append(f'FlexSkeletonHeader {self.name}_skeleton = {{ {{ {self.name}_limb_table, {len(self.bones)}, 0 }}, {dl_count} }};')

        return '\n'.join(lines) + '\n'

    def _generate_batched_dl(self, lines, all_tris, num_verts, safe_name):
        """Generate DL with vertex batching for meshes > 32 verts.

        Uses greedy triangle batching: groups triangles so each batch uses
        at most MAX_VTX_LOAD unique vertex indices. Vertices shared across
        batches are loaded multiple times (via indexed gsSPVertex calls).
        No triangles are dropped.
        """
        # Greedy batching: accumulate triangles until adding one would exceed 32 unique verts
        batches = []  # each batch = (set_of_vert_indices, list_of_tris)
        cur_verts = set()
        cur_tris = []

        for tri in all_tris:
            a, b, c = tri
            new_verts = {a, b, c} - cur_verts
            if len(cur_verts) + len(new_verts) > MAX_VTX_LOAD:
                # Flush current batch
                if cur_tris:
                    batches.append((cur_verts, cur_tris))
                cur_verts = {a, b, c}
                cur_tris = [tri]
            else:
                cur_verts.update(new_verts)
                cur_tris.append(tri)

        if cur_tris:
            batches.append((cur_verts, cur_tris))

        for batch_verts, batch_tris in batches:
            # Sort vertex indices so we can load them in order
            sorted_v = sorted(batch_verts)
            # Build remap: original vertex index → position in vertex buffer (0..N-1)
            remap = {v: j for j, v in enumerate(sorted_v)}

            # Load vertices one-by-one or in contiguous runs
            # Find contiguous runs to minimize gsSPVertex calls
            runs = []
            run_start = sorted_v[0]
            run_end = sorted_v[0]
            for v in sorted_v[1:]:
                if v == run_end + 1:
                    run_end = v
                else:
                    runs.append((run_start, run_end))
                    run_start = v
                    run_end = v
            runs.append((run_start, run_end))

            # Emit gsSPVertex for each contiguous run
            buf_offset = 0
            for run_start, run_end in runs:
                count = run_end - run_start + 1
                lines.append(f'    gsSPVertex(&{self.name}_{safe_name}_vtx[{run_start}], {count}, {buf_offset}),')
                buf_offset += count

            # Draw triangles with remapped indices
            remapped = [(remap[a], remap[b], remap[c]) for a, b, c in batch_tris]
            for t in range(0, len(remapped) - 1, 2):
                a0, b0, c0 = remapped[t]
                a1, b1, c1 = remapped[t + 1]
                lines.append(f'    gsSP2Triangles({a0}, {b0}, {c0}, 0, {a1}, {b1}, {c1}, 0),')
            if len(remapped) % 2 == 1:
                a, b, c = remapped[-1]
                lines.append(f'    gsSP1Triangle({a}, {b}, {c}, 0),')

    def _safe_name(self, name):
        """Convert bone name to C-safe identifier"""
        return ''.join(c if c.isalnum() or c == '_' else '_' for c in name)


# ── OOT Animation Generator ─────────────────────────────────────────────────

class OotAnimGenerator:
    def __init__(self, anim: AnimData, bones: List[Bone], name: str, scale: float = 1.0):
        self.anim = anim
        self.bones = bones
        self.name = name
        self.scale = scale
        self.total_frames = anim.end_time - anim.start_time + 1

    def _get_value_at_frame(self, keyframes, frame):
        """Linear interpolation (matching OOT's approach for stored data)"""
        if not keyframes:
            return 0.0
        if len(keyframes) == 1:
            return keyframes[0][1]
        if frame <= keyframes[0][0]:
            return keyframes[0][1]
        if frame >= keyframes[-1][0]:
            return keyframes[-1][1]

        for i in range(len(keyframes) - 1):
            t0, v0 = keyframes[i]
            t1, v1 = keyframes[i + 1]
            if t0 <= frame <= t1:
                if t1 == t0:
                    return v0
                frac = (frame - t0) / (t1 - t0)
                return v0 + (v1 - v0) * frac
        return keyframes[-1][1]

    def _deg_to_s16(self, degrees):
        """Convert degrees to OOT s16 rotation value"""
        val = int(degrees * DEG_TO_S16) & 0xFFFF
        if val > 32767:
            val -= 65536
        return val

    def generate_header(self) -> str:
        anim_name = f'{self.name}_{self.anim.name}'
        return '\n'.join([
            f'#ifndef {anim_name.upper()}_H',
            f'#define {anim_name.upper()}_H',
            '',
            '#include "z64.h"',
            '',
            f'extern AnimationHeader {anim_name}_anim;',
            '',
            f'#endif // {anim_name.upper()}_H',
        ]) + '\n'

    def generate_source(self) -> str:
        anim_name = f'{self.name}_{self.anim.name}'
        num_limbs = len(self.bones)

        # OOT jointTable layout (num_limbs+1 entries):
        #   jointTable[0] = root TRANSLATION (x,y,z position)
        #   jointTable[1] = root ROTATION (limb 0)
        #   jointTable[2] = limb 1 rotation
        #   ...
        #   jointTable[N] = limb N-1 rotation
        # So jointIndices needs num_limbs+1 entries total.

        num_entries = num_limbs + 1  # CRITICAL: OOT needs limbCount+1
        all_values = []  # [entry_idx][channel][frame] → s16 value

        for li, bone in enumerate(self.bones):
            ch = self.anim.channels.get(bone.name, {})

            if li == 0:
                # Entry 0: Root translation
                trans_channels = []
                for attr in ['translateX', 'translateY', 'translateZ']:
                    kf = ch.get(attr, [])
                    frame_vals = []
                    for f in range(self.total_frames):
                        t = self.anim.start_time + f
                        v = self._get_value_at_frame(kf, t)
                        v_scaled = int(v * self.scale) & 0xFFFF
                        if v_scaled > 32767:
                            v_scaled -= 65536
                        frame_vals.append(v_scaled)
                    trans_channels.append(frame_vals)
                all_values.append(trans_channels)

                # Entry 1: Root rotation (limb 0)
                rot_channels = []
                for attr in ['rotateX', 'rotateY', 'rotateZ']:
                    kf = ch.get(attr, [])
                    frame_vals = []
                    for f in range(self.total_frames):
                        t = self.anim.start_time + f
                        v = self._get_value_at_frame(kf, t)
                        frame_vals.append(self._deg_to_s16(v))
                    rot_channels.append(frame_vals)
                all_values.append(rot_channels)
            else:
                # Entry li+1: Limb rotation
                limb_channels = []
                for attr in ['rotateX', 'rotateY', 'rotateZ']:
                    kf = ch.get(attr, [])
                    frame_vals = []
                    for f in range(self.total_frames):
                        t = self.anim.start_time + f
                        v = self._get_value_at_frame(kf, t)
                        frame_vals.append(self._deg_to_s16(v))
                    limb_channels.append(frame_vals)
                all_values.append(limb_channels)

        assert len(all_values) == num_entries, \
            f"Expected {num_entries} entries, got {len(all_values)}"

        # Build indexed compression
        frame_data = []  # s16 values
        joint_indices = []  # (x_idx, y_idx, z_idx) per entry
        static_max = 0

        # First pass: find static values (constant across all frames)
        static_values = []
        dynamic_channels = []

        for ei in range(num_entries):
            indices = []
            for ci in range(3):
                vals = all_values[ei][ci]
                if all(v == vals[0] for v in vals):
                    idx = len(static_values)
                    static_values.append(vals[0])
                    indices.append(idx)
                else:
                    dynamic_channels.append((ei, ci, vals))
                    indices.append(-1)
            joint_indices.append(indices)

        static_max = len(static_values)

        # Build frame_data: static values first, then dynamic per-frame
        frame_data = list(static_values)

        # Second pass: assign dynamic indices
        for ei in range(num_entries):
            for ci in range(3):
                if joint_indices[ei][ci] == -1:
                    joint_indices[ei][ci] = len(frame_data)
                    for dei, dci, dvals in dynamic_channels:
                        if dei == ei and dci == ci:
                            frame_data.extend(dvals)
                            break

        # Generate C source
        lines = [
            f'#include "expansions/ssbb/characters/{anim_name}.h"',
            '',
            f'static s16 {anim_name}_frame_data[{len(frame_data)}] = {{',
        ]

        # Write frame data in rows of 14
        for i in range(0, len(frame_data), 14):
            chunk = frame_data[i:i+14]
            hex_vals = ', '.join(f'0x{v & 0xFFFF:04X}' for v in chunk)
            lines.append(f'    {hex_vals},')
        lines.append('};')
        lines.append('')

        # Joint indices (num_limbs+1 entries: root pos + root rot + limb rotations)
        lines.append(f'static JointIndex {anim_name}_joint_indices[{num_entries}] = {{')
        for ei in range(num_entries):
            x, y, z = joint_indices[ei]
            lines.append(f'    {{ 0x{x:04X}, 0x{y:04X}, 0x{z:04X} }},')
        lines.append('};')
        lines.append('')

        # Animation header
        lines.append(f'AnimationHeader {anim_name}_anim = {{')
        lines.append(f'    {{ {self.total_frames} }},')
        lines.append(f'    {anim_name}_frame_data,')
        lines.append(f'    {anim_name}_joint_indices,')
        lines.append(f'    {static_max}')
        lines.append('};')

        return '\n'.join(lines) + '\n'


# ── Skin Generator (Weighted Vertex Skinning) ──────────────────────────────

class OotSkinGenerator:
    """Generate weighted skin mesh data for CPU skinning.

    Outputs SSBBSkinVertex[], SSBBSkinWeight[], MtxF[] (invBind),
    Gfx[] (single DL with segment 0x08 vertex refs), and SSBBSkinMesh struct.
    """

    def __init__(self, name: str, f64_positions, f64_normals, f64_uvs, f64_alphas,
                 f64_triangles, collada_parser, scale: float):
        self.name = name
        self.scale = scale
        self.vertex_count = len(f64_positions)
        self.triangle_count = len(f64_triangles)

        # Positions are in F64/OOT space (already scaled by parse_fast64_c dividing by scale).
        # For the skin system, we need them in DAE space (the bind pose space).
        # The DaeToOot transform at runtime converts DAE→OOT, so store as DAE coords.
        # F64 coords = DaeToOot(DAE coords), so we need to invert:
        #   oot.x = +dae.y * scale, oot.y = -dae.x * scale, oot.z = -dae.z * scale
        # Inverse: dae.x = -oot.y / scale, dae.y = +oot.x / scale, dae.z = -oot.z / scale
        # But positions from parse_fast64_c are already divided by --scale, so they're in
        # "Brawl units" = DAE×100 space (same as the skeleton joint positions).
        # Actually, the F64 file positions are in OOT/F64 space. parse_fast64_c divides by
        # the --scale arg to get "Brawl units" — but the axis mapping is still F64's.
        # We need to undo the DaeToOot axis swap to get back to DAE space.
        #
        # DaeToOot: oot.x = +dae.y * f64_scale, oot.y = -dae.x * f64_scale, oot.z = -dae.z * f64_scale
        # So in Brawl units (divided by --scale, which == f64_scale for Pikachu):
        #   brawl.x = +dae.y, brawl.y = -dae.x, brawl.z = -dae.z (axis_map=(1,0,2), signs=(1,-1,-1))
        # Inverse: dae.x = -brawl.y, dae.y = +brawl.x, dae.z = -brawl.z

        # Use the COLLADA parser's DAE world positions and skin data to match F64 verts
        # to their COLLADA counterparts and get proper weights
        skin_data = None
        for geom_id, sd in collada_parser._parse_skin_controllers().items():
            skin_data = sd
            break

        if skin_data is None:
            raise ValueError("No skin controller found in COLLADA file")

        collada_weights = skin_data['per_vertex_weights']
        collada_inv_bind = skin_data['bone_inv_bind']  # COLLADA's authoritative inv_bind matrices
        collada_bind_shape = skin_data.get('bind_shape_matrix')  # may be None (identity)

        # Build DAE world-space positions from raw geometry position data.
        # per_vertex_weights is indexed by COLLADA position vertex index (pi),
        # so we need raw positions in the same order.
        # Parse raw position data from the first geometry in COLLADA.
        dae_raw_positions = []
        lib_geom = collada_parser._find(collada_parser.root, './/c:library_geometries')
        if lib_geom is not None:
            for geom in collada_parser._findall(lib_geom, 'c:geometry'):
                mesh_elem = collada_parser._find(geom, 'c:mesh')
                if mesh_elem is None:
                    continue
                # Find the position source
                vertices_elem = collada_parser._find(mesh_elem, 'c:vertices')
                pos_src_id = None
                if vertices_elem is not None:
                    pos_inp = collada_parser._find(vertices_elem, 'c:input[@semantic="POSITION"]')
                    if pos_inp is not None:
                        pos_src_id = pos_inp.get('source', '').lstrip('#')
                if pos_src_id:
                    for source in collada_parser._findall(mesh_elem, 'c:source'):
                        if source.get('id') == pos_src_id:
                            fa = collada_parser._find(source, 'c:float_array')
                            if fa is not None and fa.text:
                                data = [float(v) for v in fa.text.split()]
                                for i in range(0, len(data), 3):
                                    dae_raw_positions.append(tuple(data[i:i+3]))
                            break
                break  # Only first geometry (polygon0 is the main body mesh)

        if not dae_raw_positions:
            raise ValueError("Could not extract raw COLLADA positions")

        print(f'  COLLADA raw positions: {len(dae_raw_positions)}, '
              f'skin weights: {len(collada_weights)}')

        # Detect axis transform between F64 and DAE positions
        axis_map, signs, f64_scale = _detect_axis_transform(f64_positions, dae_raw_positions)

        # For each F64 vertex, find nearest DAE raw position to get its skin weights
        self.skin_weights = []  # List of List[(bone_index, u8_weight)]
        used_bones = set()
        multi_bone_count = 0

        for fi in range(self.vertex_count):
            fp = f64_positions[fi]
            # Compare in F64 space: transform DAE positions to F64 space
            best_dist = float('inf')
            best_di = 0
            for di, dp in enumerate(dae_raw_positions):
                fp_from_dae = tuple(signs[a] * dp[axis_map[a]] * f64_scale for a in range(3))
                dx = fp[0] - fp_from_dae[0]
                dy = fp[1] - fp_from_dae[1]
                dz = fp[2] - fp_from_dae[2]
                dist = dx*dx + dy*dy + dz*dz
                if dist < best_dist:
                    best_dist = dist
                    best_di = di

            # Get the COLLADA weights for this vertex (indexed by position vertex index)
            if best_di < len(collada_weights):
                influences = collada_weights[best_di]
            else:
                influences = [(0, 1.0)]

            if len(influences) > 1:
                multi_bone_count += 1

            # Normalize to u8 (sum = 255)
            total = sum(w for _, w in influences)
            if total < 0.001:
                influences = [(0, 1.0)]
                total = 1.0

            u8_weights = []
            remaining = 255
            for i, (bi, w) in enumerate(influences):
                if i == len(influences) - 1:
                    u8w = remaining
                else:
                    u8w = max(1, int(round(w / total * 255.0)))
                    u8w = min(u8w, remaining)
                remaining -= u8w
                u8_weights.append((bi, u8w))
                used_bones.add(bi)

            self.skin_weights.append(u8_weights)

        self.max_influences = max((len(w) for w in self.skin_weights), default=1)
        print(f'  Vertices with multiple bone influences: {multi_bone_count}/{self.vertex_count}')
        print(f'  Max influences per vertex: {self.max_influences}')

        # Build bone list (all bones that any vertex references)
        self.bone_indices = sorted(used_bones)
        self.bone_count = max(self.bone_indices) + 1 if self.bone_indices else 0

        # ── Coordinate space strategy ──
        # Store vertex positions in F64/OOT space (directly from Fast64 C file).
        # Compute inv_bind matrices that work in OOT space by transforming the
        # skeleton bind-pose through the same DaeToOot + scale pipeline.
        # At runtime, bone matrices are computed in DAE space then converted to OOT space,
        # so combined = boneWorldOOT × invBindOOT produces correct results.

        # Build DaeToOot+scale matrix matching the C code's pipeline:
        # 1. Scale by def->scale (render_scale)
        # 2. DaeToOot: oot.x = +dae.y*1.4899, oot.y = -dae.x*1.4899, oot.z = -dae.z*1.4899
        # Combined: daeToOot_scaled
        # But actually, we need the full transform from DAE space to F64/OOT integer space.
        # The auto-detected mapping gives us exactly this:
        # f64[f] = signs[f] * dae[axis_map[f]] * f64_scale
        # This is the DaeToF64 transform (F64 integers = Blender/OOT export space).

        # Build the DaeToF64 matrix (4x4, row-major)
        dae_to_f64 = [[0]*4 for _ in range(4)]
        for f_axis in range(3):
            d_axis = axis_map[f_axis]
            dae_to_f64[f_axis][d_axis] = signs[f_axis] * f64_scale
        dae_to_f64[3][3] = 1.0
        self.dae_to_f64 = dae_to_f64  # Store for output to C
        f64_to_dae = _mat4_general_inverse(dae_to_f64)
        self.f64_to_dae = f64_to_dae  # Store pre-computed inverse for output to C

        # Use COLLADA's AUTHORITATIVE inverse bind matrices (same as Three.js uses).
        # Transform them to F64/OOT space: invBind_f64 = invBind_collada × inverse(DaeToF64)
        # This ensures exact match with the HTML viewer's skinning.
        #
        # Skinning equation in COLLADA: v_world = Σ(w × boneWorld × invBind × v_mesh)
        # Our equation: v_f64 = Σ(w × boneWorld_f64 × invBind_f64 × v_f64)
        # Since boneWorld_f64 = DaeToF64 × boneWorld_dae, and v_f64 = DaeToF64 × v_mesh:
        #   invBind_f64 = invBind_collada × inverse(DaeToF64)
        self.inv_bind_matrices = {}
        self.bone_local_positions = {}  # float positions matching inv_bind precision
        for bone in collada_parser.bones:
            if bone.index in collada_inv_bind:
                # Use COLLADA's authoritative inv_bind, transformed to F64 space
                self.inv_bind_matrices[bone.index] = _mat4_mul(collada_inv_bind[bone.index], f64_to_dae)
            else:
                # Fallback: compute from bone world matrix
                bone_world_dae = collada_parser._bone_world.get(bone.index, _mat4_identity())
                bone_world_oot = _mat4_mul(dae_to_f64, bone_world_dae)
                self.inv_bind_matrices[bone.index] = _mat4_general_inverse(bone_world_oot)
            # Store float local position in DAE space (matches what inv_bind was computed from)
            self.bone_local_positions[bone.index] = bone.local_pos

        # Store vertex data in F64/OOT space (no axis conversion needed)
        self.vertices = []
        for i in range(self.vertex_count):
            fp = f64_positions[i]
            # Positions directly from Fast64 C file (F64/OOT integers)
            pos = (fp[0], fp[1], fp[2])

            # Normals in F64/OOT space (already parsed by parse_fast64_c)
            fn = f64_normals[i]
            norm = tuple(max(-127, min(127, int(round(x * 127)))) for x in fn)

            uv = f64_uvs[i] if i < len(f64_uvs) else (0, 0)
            alpha = f64_alphas[i] if i < len(f64_alphas) else 255

            self.vertices.append((pos, norm, uv, alpha))

        self.triangles = f64_triangles

    def generate_header(self) -> str:
        lines = [
            f'#ifndef {self.name.upper()}_SKIN_H',
            f'#define {self.name.upper()}_SKIN_H',
            '',
            '#include "expansions/ssbb/ssbb_skin.h"',
            '',
            f'extern SSBBSkinMesh {self.name}_skin_mesh;',
            '',
            f'#endif // {self.name.upper()}_SKIN_H',
        ]
        return '\n'.join(lines) + '\n'

    def generate_source(self) -> str:
        lines = [
            f'// Auto-generated weighted skin mesh for {self.name}',
            f'// {self.vertex_count} vertices, {self.triangle_count} triangles, {self.bone_count} bones',
            '',
            f'#include "expansions/ssbb/characters/{self.name}_skin.h"',
            '',
        ]

        # ── SSBBSkinVertex array ──
        lines.append(f'static SSBBSkinVertex {self.name}_skin_vertices[{self.vertex_count}] = {{')
        for i, (pos, norm, uv, alpha) in enumerate(self.vertices):
            lines.append(f'    {{ {pos[0]:.6f}f, {pos[1]:.6f}f, {pos[2]:.6f}f, '
                        f'{norm[0]}, {norm[1]}, {norm[2]}, '
                        f'{uv[0]}, {uv[1]}, {alpha} }}, // {i}')
        lines.append('};')
        lines.append('')

        # ── SSBBSkinWeight array ──
        # Always pad to SSBB_MAX_INFLUENCES (4) to match the C struct
        PAD = 4
        lines.append(f'static SSBBSkinWeight {self.name}_skin_weights[{self.vertex_count}] = {{')
        for i, weights in enumerate(self.skin_weights):
            bones = [0] * PAD
            wvals = [0] * PAD
            for j, (bi, w) in enumerate(weights[:PAD]):
                bones[j] = bi
                wvals[j] = w
            bones_str = ', '.join(str(b) for b in bones)
            wvals_str = ', '.join(str(w) for w in wvals)
            lines.append(f'    {{ {{ {bones_str} }}, {{ {wvals_str} }} }}, // {i}')
        lines.append('};')
        lines.append('')

        # ── Inverse bind matrices (MtxF, column-major for OOT) ──
        # COLLADA stores row-major, OOT's MtxF is column-major: mf[col][row]
        lines.append(f'static MtxF {self.name}_skin_inv_bind[{self.bone_count}] = {{')
        for bi in range(self.bone_count):
            mat = self.inv_bind_matrices.get(bi)
            if mat is None:
                # Identity for bones without inv_bind data
                lines.append('    { .mf = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, '
                            '{ 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } } },')
            else:
                # COLLADA is row-major: mat[row][col]
                # OOT MtxF is column-major: mf[col][row]
                # So mf[c][r] = mat[r][c]
                cols = []
                for c in range(4):
                    col = ', '.join(f'{mat[r][c]:.6f}f' for r in range(4))
                    cols.append(f'{{ {col} }}')
                lines.append(f'    {{ .mf = {{ {", ".join(cols)} }} }},')
        lines.append('};')
        lines.append('')

        # ── Bone local positions (float, matches inv_bind precision) ──
        lines.append(f'static SSBBSkinBonePos {self.name}_skin_bone_pos[{self.bone_count}] = {{')
        for bi in range(self.bone_count):
            lp = self.bone_local_positions.get(bi)
            if lp is None:
                lines.append('    { 0.0f, 0.0f, 0.0f },')
            else:
                lines.append(f'    {{ {lp[0]:.6f}f, {lp[1]:.6f}f, {lp[2]:.6f}f }},')
        lines.append('};')
        lines.append('')

        # ── Display List (single DL, vertices via segment 0x08) ──
        # Segment 0x08 addresses need LSB=1 for SoH's SegAddr detection
        lines.append(f'static Gfx {self.name}_skin_dl[] = {{')

        # Emit gsSPVertex + triangle commands in batches of MAX_VTX_LOAD (32)
        # First, build a list of which vertices each triangle uses
        # and batch triangles so each batch uses at most 32 unique verts
        batches = self._batch_triangles()

        for batch_verts, batch_tris in batches:
            # Sort verts for deterministic output
            vert_list = sorted(batch_verts)
            vert_remap = {v: i for i, v in enumerate(vert_list)}
            count = len(vert_list)
            first = vert_list[0]

            # Segment 0x08 offset = first_vertex * sizeof(Vtx) = first * 16
            # LSB=1 for SoH segmented address detection
            seg_addr = (0x08000000 | (first * 16)) | 1
            lines.append(f'    gsSPVertex(0x{seg_addr:08X}, {count}, 0),')

            # Emit triangles
            tri_list = [(vert_remap[a], vert_remap[b], vert_remap[c]) for a, b, c in batch_tris]
            i = 0
            while i < len(tri_list):
                if i + 1 < len(tri_list):
                    a0, b0, c0 = tri_list[i]
                    a1, b1, c1 = tri_list[i + 1]
                    lines.append(f'    gsSP2Triangles({a0}, {b0}, {c0}, 0, {a1}, {b1}, {c1}, 0),')
                    i += 2
                else:
                    a, b, c = tri_list[i]
                    lines.append(f'    gsSP1Triangle({a}, {b}, {c}, 0),')
                    i += 1

        lines.append('    gsSPEndDisplayList(),')
        lines.append('};')
        lines.append('')

        # ── SSBBSkinMesh struct ──
        lines.append(f'SSBBSkinMesh {self.name}_skin_mesh = {{')
        lines.append(f'    .vertexCount = {self.vertex_count},')
        lines.append(f'    .boneCount = {self.bone_count},')
        lines.append(f'    .vertices = {self.name}_skin_vertices,')
        lines.append(f'    .weights = {self.name}_skin_weights,')
        lines.append(f'    .invBindMatrices = {self.name}_skin_inv_bind,')
        lines.append(f'    .bonePositions = {self.name}_skin_bone_pos,')
        # DaeToF64 matrix (row-major Python → column-major MtxF)
        mat = self.dae_to_f64
        cols = []
        for c in range(4):
            col = ', '.join(f'{mat[r][c]:.6f}f' for r in range(4))
            cols.append(f'{{ {col} }}')
        lines.append(f'    .daeToF64 = {{ .mf = {{ {", ".join(cols)} }} }},')
        # Pre-computed inverse(DaeToF64) — avoids runtime SkinMatrix_Invert
        inv_mat = self.f64_to_dae
        inv_cols = []
        for c in range(4):
            col = ', '.join(f'{inv_mat[r][c]:.6f}f' for r in range(4))
            inv_cols.append(f'{{ {col} }}')
        lines.append(f'    .f64ToDae = {{ .mf = {{ {", ".join(inv_cols)} }} }},')
        lines.append(f'    .displayList = {self.name}_skin_dl,')
        lines.append('    .vtxBuf = { NULL, NULL },')
        lines.append('    .bufIndex = 0,')
        lines.append('};')

        return '\n'.join(lines) + '\n'

    def _batch_triangles(self):
        """Split triangles into batches where each batch uses at most MAX_VTX_LOAD unique vertices.
        Returns list of (set_of_vert_indices, list_of_triangles)."""
        batches = []
        remaining = list(self.triangles)

        while remaining:
            batch_verts = set()
            batch_tris = []
            next_remaining = []

            for tri in remaining:
                new_verts = set(tri) - batch_verts
                if len(batch_verts) + len(new_verts) <= MAX_VTX_LOAD:
                    batch_verts.update(tri)
                    batch_tris.append(tri)
                else:
                    next_remaining.append(tri)

            if batch_tris:
                batches.append((batch_verts, batch_tris))
            remaining = next_remaining

        return batches


# ── SSBB Animation Generator (translate + rotate + scale) ──────────────────
# Generates SSBBAnim format with per-bone translate+rotate+scale per frame.
# This replaces OOT's AnimationHeader (rotation only) for the skin path.

class SSBBAnimGenerator:
    """Generate SSBBAnim data from Maya .anim file. Stores T+R+S per bone per frame."""

    def __init__(self, anim: AnimData, bones: List[Bone], name: str):
        self.anim = anim
        self.bones = bones
        self.name = name
        self.total_frames = anim.end_time - anim.start_time + 1
        self.num_bones = len(bones)

    def _get_value(self, keyframes, frame):
        if not keyframes: return 0.0
        if len(keyframes) == 1: return keyframes[0][1]
        if frame <= keyframes[0][0]: return keyframes[0][1]
        if frame >= keyframes[-1][0]: return keyframes[-1][1]
        for i in range(len(keyframes) - 1):
            t0, v0 = keyframes[i]
            t1, v1 = keyframes[i + 1]
            if t0 <= frame <= t1:
                if t1 == t0: return v0
                frac = (frame - t0) / (t1 - t0)
                return v0 + (v1 - v0) * frac
        return keyframes[-1][1]

    def generate_header(self) -> str:
        anim_name = f'{self.name}_{self.anim.name}_ssbb'
        return '\n'.join([
            f'#ifndef {anim_name.upper()}_H',
            f'#define {anim_name.upper()}_H',
            '',
            '#include "expansions/ssbb/ssbb_anim.h"',
            '',
            f'extern const struct SSBBAnim {anim_name}_anim;',
            '',
            f'#endif // {anim_name.upper()}_H',
        ]) + '\n'

    def generate_source(self) -> str:
        anim_name = f'{self.name}_{self.anim.name}_ssbb'
        lines = [
            f'// Auto-generated SSBB animation: {self.anim.name}',
            f'// {self.total_frames} frames, {self.num_bones} bones, translate+rotate+scale per bone',
            '',
            f'#include "expansions/ssbb/characters/{anim_name}.h"',
            '',
        ]

        # Generate frame data: [numFrames * numBones] SSBBBoneFrame
        lines.append(f'static const SSBBBoneFrame {anim_name}_frames[{self.total_frames * self.num_bones}] = {{')

        for f in range(self.total_frames):
            t = self.anim.start_time + f
            lines.append(f'    // Frame {f}')
            for bi, bone in enumerate(self.bones):
                ch = self.anim.channels.get(bone.name, {})

                # Translation: from .anim or bind pose
                tx = self._get_value(ch.get('translateX'), t) if 'translateX' in ch else bone.local_pos[0]
                ty = self._get_value(ch.get('translateY'), t) if 'translateY' in ch else bone.local_pos[1]
                tz = self._get_value(ch.get('translateZ'), t) if 'translateZ' in ch else bone.local_pos[2]

                # Rotation: from .anim (degrees, ZYX Euler)
                rx = self._get_value(ch.get('rotateX'), t) if 'rotateX' in ch else 0.0
                ry = self._get_value(ch.get('rotateY'), t) if 'rotateY' in ch else 0.0
                rz = self._get_value(ch.get('rotateZ'), t) if 'rotateZ' in ch else 0.0

                # Scale: from .anim or 1.0
                sx = self._get_value(ch.get('scaleX'), t) if 'scaleX' in ch else 1.0
                sy = self._get_value(ch.get('scaleY'), t) if 'scaleY' in ch else 1.0
                sz = self._get_value(ch.get('scaleZ'), t) if 'scaleZ' in ch else 1.0

                lines.append(f'    {{ {tx:.6f}f, {ty:.6f}f, {tz:.6f}f, '
                            f'{rx:.6f}f, {ry:.6f}f, {rz:.6f}f, '
                            f'{sx:.6f}f, {sy:.6f}f, {sz:.6f}f }}, // [{bi}] {bone.name}')

        lines.append('};')
        lines.append('')

        # SSBBAnim struct
        lines.append(f'const struct SSBBAnim {anim_name}_anim = {{')
        lines.append(f'    .name = "{self.anim.name}",')
        lines.append(f'    .numFrames = {self.total_frames},')
        lines.append(f'    .numBones = {self.num_bones},')
        lines.append(f'    .frameRate = 60.0f,')
        lines.append(f'    .frames = {anim_name}_frames,')
        lines.append('};')

        return '\n'.join(lines) + '\n'


# ── COLLADA Direct Skin Generator ──────────────────────────────────────────
# Uses COLLADA mesh geometry directly. No Fast64, no nearest-neighbor matching.
# Weights are 1:1 mapped by COLLADA position vertex index.

class ColladaSkinGenerator:
    """Generate SSBBSkinMesh data directly from COLLADA geometry + skin controller.
    Produces: DL + SSBBSkinVertex[] + SSBBSkinWeight[] + invBind[] + bone positions.
    """

    MAX_VTX_LOAD = 32

    def __init__(self, name: str, collada_parser, scale: float, axis_map_str: str = '+y,-z,+x'):
        self.name = name
        self.scale = scale

        # ── 1. Get raw mesh data from ColladaParser ──
        if not hasattr(collada_parser, 'raw_verts_pos'):
            raise ValueError("ColladaParser has no raw geometry — call parse() first")

        dae_positions = collada_parser.raw_verts_pos
        dae_normals = collada_parser.raw_verts_norm
        dae_uvs = collada_parser.raw_verts_uv
        triangles = collada_parser.raw_triangles
        vert_pos_indices = collada_parser.raw_vert_pos_indices

        self.vertex_count = len(dae_positions)
        self.triangle_count = len(triangles)
        self.triangles = triangles

        # ── Split body/eyes triangle ranges ──
        # polygon0 = body, polygon1-4 = eyes
        self.body_tri_count = self.triangle_count  # default: all body
        if hasattr(collada_parser, '_raw_geom_tri_counts'):
            gtc = collada_parser._raw_geom_tri_counts
            first_geom = collada_parser._raw_geom_ids[0] if collada_parser._raw_geom_ids else None
            if first_geom and first_geom in gtc:
                self.body_tri_count = gtc[first_geom]
                eye_tri_count = self.triangle_count - self.body_tri_count
                print(f'  Body triangles: {self.body_tri_count}, Eye triangles: {eye_tri_count}')

        print(f'  COLLADA mesh: {self.vertex_count} vertices, {self.triangle_count} triangles')

        # ── 2. Build DaeToF64 matrix from axis mapping string ──
        # Format: "+y,-z,+x" means F64.x=+DAE.y*scale, F64.y=-DAE.z*scale, F64.z=+DAE.x*scale
        dae_to_f64 = [[0]*4 for _ in range(4)]
        axis_names = {'x': 0, 'y': 1, 'z': 2}
        parts = [p.strip() for p in axis_map_str.split(',')]
        if len(parts) != 3:
            raise ValueError(f"Bad axis mapping '{axis_map_str}', expected 3 parts like '+y,-z,+x'")
        for f_axis, part in enumerate(parts):
            sign = -1 if part[0] == '-' else 1
            d_axis = axis_names.get(part[-1])
            if d_axis is None:
                raise ValueError(f"Bad axis '{part[-1]}' in mapping '{axis_map_str}'")
            dae_to_f64[f_axis][d_axis] = sign * scale
        dae_to_f64[3][3] = 1.0
        self.dae_to_f64 = dae_to_f64
        self.f64_to_dae = _mat4_general_inverse(dae_to_f64)

        mapping_str = ', '.join(
            f'F64.{"xyz"[i]}={"+" if dae_to_f64[i][j]>0 else "-"}DAE.{"xyz"[j]}*{abs(dae_to_f64[i][j]):.1f}'
            for i in range(3) for j in range(3) if abs(dae_to_f64[i][j]) > 0.001
        )
        print(f'  Axis mapping: {mapping_str}')

        # ── 3. Get skin weights from ALL COLLADA skin controllers ──
        # Combine weights from polygon0-4 (body + eyes)
        all_skin_data = collada_parser._parse_skin_controllers()
        if not all_skin_data:
            raise ValueError("No skin controller found in COLLADA file")

        # Use the first controller's inv_bind (all share the same skeleton)
        first_sd = next(iter(all_skin_data.values()))
        collada_inv_bind = first_sd['bone_inv_bind']

        # Build a combined weight lookup: geometry_id → per_vertex_weights
        # The raw_vert_pos_indices are accumulated across geometries in order
        all_weights_by_geom = {}
        for gid, sd in all_skin_data.items():
            all_weights_by_geom[gid] = sd['per_vertex_weights']

        # Build combined weights list matching the accumulated raw_verts order
        collada_weights = []
        geom_ids_seen = list(all_skin_data.keys())
        # Each geometry contributes verts in order. Track which geometry each vert_pos_index comes from.
        # Since raw_verts accumulates all geometries, and each geometry has its own position indices 0..N,
        # we need to know which geometry each raw vert belongs to.
        # The position indices reset per geometry, so we track accumulated counts.
        if hasattr(collada_parser, '_raw_geom_ids'):
            geom_vert_counts = {}
            for gid in collada_parser._raw_geom_ids:
                if gid not in geom_vert_counts:
                    geom_vert_counts[gid] = 0

        # Flatten: for each geometry in order, its weights are at position indices 0..N
        # We'll build the combined weights by iterating through geom_ids in order
        combined_weights_flat = []
        for gid in geom_ids_seen:
            if gid in all_weights_by_geom:
                combined_weights_flat.extend(all_weights_by_geom[gid])
        collada_weights = combined_weights_flat
        print(f'  Combined weights from {len(all_skin_data)} skin controllers: {len(collada_weights)} entries')

        self.skin_weights = []
        used_bones = set()
        multi_bone_count = 0
        missing_weight_count = 0

        for vi in range(self.vertex_count):
            pi = vert_pos_indices[vi]
            if pi < len(collada_weights):
                influences = collada_weights[pi]
            else:
                influences = [(0, 1.0)]
                missing_weight_count += 1

            if len(influences) > 1:
                multi_bone_count += 1

            # Truncate to 4 influences max (SSBB_MAX_INFLUENCES=4), then renormalize
            if len(influences) > 4:
                influences = influences[:4]

            # Normalize to u8 (sum = 255)
            total = sum(w for _, w in influences)
            if total < 0.001:
                influences = [(0, 1.0)]
                total = 1.0

            u8_weights = []
            remaining = 255
            for i, (bi, w) in enumerate(influences):
                if i == len(influences) - 1:
                    u8w = remaining
                else:
                    u8w = max(1, int(round(w / total * 255.0)))
                    u8w = min(u8w, remaining)
                remaining -= u8w
                u8_weights.append((bi, u8w))
                used_bones.add(bi)

            self.skin_weights.append(u8_weights)

        self.max_influences = max((len(w) for w in self.skin_weights), default=1)
        self.bone_indices = sorted(used_bones)
        self.bone_count = max(self.bone_indices) + 1 if self.bone_indices else 0

        print(f'  Direct 1:1 weight mapping: {self.vertex_count} vertices (NO nearest-neighbor)')
        print(f'  Multi-bone vertices: {multi_bone_count}/{self.vertex_count}')
        print(f'  Max influences: {self.max_influences}, bones used: {len(used_bones)}')
        if missing_weight_count > 0:
            print(f'  WARNING: {missing_weight_count} vertices had no COLLADA weight data!')

        # ── 4. Transform positions + normals from DAE to F64 space ──
        self.vertices = []
        for i in range(self.vertex_count):
            dp = dae_positions[i]
            dn = dae_normals[i]
            du = dae_uvs[i]

            # Position: DAE → F64
            fp = _mat4_mul_vec3(dae_to_f64, dp)

            # Normal: rotate only (no translation), renormalize
            fn = [0.0, 0.0, 0.0]
            for r in range(3):
                for c in range(3):
                    fn[r] += dae_to_f64[r][c] * dn[c]
            nlen = (fn[0]**2 + fn[1]**2 + fn[2]**2) ** 0.5
            if nlen > 0.001:
                fn = [x / nlen for x in fn]
            norm = tuple(max(-127, min(127, int(round(x * 127)))) for x in fn)

            # UV: COLLADA (0-1, V-flipped for N64) → s10.5
            # N64 s10.5 texture coordinates: value = UV * textureSize * 32
            # COLLADA UVs are 0-1 normalized. Store raw s10.5 = UV * 32 * 32 = UV * 1024
            # (32x32 texture; mask=5 wraps at 32 texels)
            texS = int(round(du[0] * 1024.0))
            texT = int(round((1.0 - du[1]) * 1024.0))

            self.vertices.append(((fp[0], fp[1], fp[2]), norm, (texS, texT), 255))

        # ── 5. Inverse bind matrices: invBind_f64 = invBind_collada × inv(DaeToF64) ──
        self.inv_bind_matrices = {}
        self.bone_local_positions = {}
        for bone in collada_parser.bones:
            if bone.index in collada_inv_bind:
                self.inv_bind_matrices[bone.index] = _mat4_mul(collada_inv_bind[bone.index], self.f64_to_dae)
            else:
                bone_world_dae = collada_parser._bone_world.get(bone.index, _mat4_identity())
                bone_world_f64 = _mat4_mul(dae_to_f64, bone_world_dae)
                self.inv_bind_matrices[bone.index] = _mat4_general_inverse(bone_world_f64)
            self.bone_local_positions[bone.index] = bone.local_pos

        print(f'  InvBind matrices: {len(self.inv_bind_matrices)} bones')

    def generate_header(self) -> str:
        lines = [
            f'#ifndef {self.name.upper()}_SKIN_H',
            f'#define {self.name.upper()}_SKIN_H',
            '',
            '#include "expansions/ssbb/ssbb_skin.h"',
            '',
            f'extern SSBBSkinMesh {self.name}_skin_mesh;',
            '',
            f'#endif // {self.name.upper()}_SKIN_H',
        ]
        return '\n'.join(lines) + '\n'

    def generate_source(self) -> str:
        # ── Flatten batches: each batch gets its OWN contiguous vertex block ──
        # gsSPVertex loads N contiguous vertices. Vertices shared between batches
        # are DUPLICATED so each batch is self-contained.
        batches = self._batch_triangles()

        flat_vertices = []
        flat_weights = []
        flat_batches = []  # (start_idx, count, [(local_a, local_b, local_c)])

        for batch_verts, batch_tris in batches:
            vert_list = sorted(batch_verts)
            old_to_local = {v: i for i, v in enumerate(vert_list)}
            start = len(flat_vertices)
            count = len(vert_list)
            for v in vert_list:
                flat_vertices.append(self.vertices[v])
                flat_weights.append(self.skin_weights[v])
            local_tris = [(old_to_local[a], old_to_local[b], old_to_local[c])
                          for a, b, c in batch_tris]
            flat_batches.append((start, count, local_tris))

        actual_vert_count = len(flat_vertices)
        reordered_vertices = flat_vertices
        reordered_weights = flat_weights
        print(f'  Flattened: {actual_vert_count} vertices ({actual_vert_count - self.vertex_count} duplicated for batching)')

        lines = [
            f'// Auto-generated weighted skin mesh for {self.name}',
            f'// Generated from COLLADA geometry (direct 1:1 weights, no Fast64 matching)',
            f'// {actual_vert_count} vertices, {self.triangle_count} triangles, '
            f'{self.bone_count} bones, max {self.max_influences} influences',
            '',
            f'#include "expansions/ssbb/characters/{self.name}_skin.h"',
            '',
        ]

        # ── SSBBSkinVertex array (reordered by batch) ──
        lines.append(f'static SSBBSkinVertex {self.name}_skin_vertices[{actual_vert_count}] = {{')
        for i, (pos, norm, uv, alpha) in enumerate(reordered_vertices):
            lines.append(f'    {{ {pos[0]:.6f}f, {pos[1]:.6f}f, {pos[2]:.6f}f, '
                        f'{norm[0]}, {norm[1]}, {norm[2]}, '
                        f'{uv[0]}, {uv[1]}, {alpha} }}, // {i}')
        lines.append('};')
        lines.append('')

        # ── SSBBSkinWeight array (same order as vertices) ──
        PAD = 4
        lines.append(f'static SSBBSkinWeight {self.name}_skin_weights[{actual_vert_count}] = {{')
        for i, weights in enumerate(reordered_weights):
            bones = [0] * PAD
            wvals = [0] * PAD
            for j, (bi, w) in enumerate(weights[:PAD]):
                bones[j] = bi
                wvals[j] = w
            bones_str = ', '.join(str(b) for b in bones)
            wvals_str = ', '.join(str(w) for w in wvals)
            lines.append(f'    {{ {{ {bones_str} }}, {{ {wvals_str} }} }}, // {i}')
        lines.append('};')
        lines.append('')

        # ── Inverse bind matrices ──
        lines.append(f'static MtxF {self.name}_skin_inv_bind[{self.bone_count}] = {{')
        for bi in range(self.bone_count):
            mat = self.inv_bind_matrices.get(bi)
            if mat is None:
                lines.append('    { .mf = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, '
                            '{ 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } } },')
            else:
                cols = []
                for c in range(4):
                    col = ', '.join(f'{mat[r][c]:.6f}f' for r in range(4))
                    cols.append(f'{{ {col} }}')
                lines.append(f'    {{ .mf = {{ {", ".join(cols)} }} }},')
        lines.append('};')
        lines.append('')

        # ── Bone local positions ──
        lines.append(f'static SSBBSkinBonePos {self.name}_skin_bone_pos[{self.bone_count}] = {{')
        for bi in range(self.bone_count):
            lp = self.bone_local_positions.get(bi)
            if lp is None:
                lines.append('    { 0.0f, 0.0f, 0.0f },')
            else:
                lines.append(f'    {{ {lp[0]:.6f}f, {lp[1]:.6f}f, {lp[2]:.6f}f }},')
        lines.append('};')
        lines.append('')

        # ── Display List: body (polygon0) + eyes (polygon1-4) ──
        # The batching already separates body from eyes naturally since body tris
        # come first. We insert a material switch marker via gsSPDisplayList(segment 0x09)
        # between body and eye batches. At runtime, segment 0x09 points to the eye material DL.
        body_batch_count = 0
        tri_counter = 0
        for start, count, local_tris in flat_batches:
            tri_counter += len(local_tris)
            body_batch_count += 1
            if tri_counter >= self.body_tri_count:
                break

        lines.append(f'static Gfx {self.name}_skin_dl[] = {{')
        for bi, (start, count, local_tris) in enumerate(flat_batches):
            # Insert eye material switch before the first eye batch
            if bi == body_batch_count and self.body_tri_count < self.triangle_count:
                lines.append(f'    // ── Eye material switch (segment 0x09) ──')
                lines.append(f'    gsSPDisplayList(0x09000001),')

            seg_addr = (0x08000000 | (start * 16)) | 1
            lines.append(f'    gsSPVertex(0x{seg_addr:08X}, {count}, 0),')
            i = 0
            while i < len(local_tris):
                if i + 1 < len(local_tris):
                    a0, b0, c0 = local_tris[i]
                    a1, b1, c1 = local_tris[i + 1]
                    lines.append(f'    gsSP2Triangles({a0}, {b0}, {c0}, 0, {a1}, {b1}, {c1}, 0),')
                    i += 2
                else:
                    a, b, c = local_tris[i]
                    lines.append(f'    gsSP1Triangle({a}, {b}, {c}, 0),')
                    i += 1
        lines.append('    gsSPEndDisplayList(),')
        lines.append('};')
        lines.append('')
        print(f'  DL: {body_batch_count} body batches + {len(flat_batches) - body_batch_count} eye batches')

        # ── SSBBSkinMesh struct ──
        lines.append(f'SSBBSkinMesh {self.name}_skin_mesh = {{')
        lines.append(f'    .vertexCount = {actual_vert_count},')
        lines.append(f'    .boneCount = {self.bone_count},')
        lines.append(f'    .vertices = {self.name}_skin_vertices,')
        lines.append(f'    .weights = {self.name}_skin_weights,')
        lines.append(f'    .invBindMatrices = {self.name}_skin_inv_bind,')
        lines.append(f'    .bonePositions = {self.name}_skin_bone_pos,')
        # DaeToF64 matrix
        mat = self.dae_to_f64
        cols = []
        for c in range(4):
            col = ', '.join(f'{mat[r][c]:.6f}f' for r in range(4))
            cols.append(f'{{ {col} }}')
        lines.append(f'    .daeToF64 = {{ .mf = {{ {", ".join(cols)} }} }},')
        # Pre-computed inverse
        inv_mat = self.f64_to_dae
        inv_cols = []
        for c in range(4):
            col = ', '.join(f'{inv_mat[r][c]:.6f}f' for r in range(4))
            inv_cols.append(f'{{ {col} }}')
        lines.append(f'    .f64ToDae = {{ .mf = {{ {", ".join(inv_cols)} }} }},')
        lines.append(f'    .displayList = {self.name}_skin_dl,')
        lines.append('    .vtxBuf = { NULL, NULL },')
        lines.append('    .bufIndex = 0,')
        lines.append('};')

        return '\n'.join(lines) + '\n'

    def _batch_triangles(self):
        return self._batch_triangles_from(self.triangles)

    def _batch_triangles_from(self, triangles):
        """Split triangles into batches where each batch uses at most MAX_VTX_LOAD unique vertices."""
        batches = []
        remaining = list(triangles)
        while remaining:
            batch_verts = set()
            batch_tris = []
            next_remaining = []
            for tri in remaining:
                new_verts = set(tri) - batch_verts
                if len(batch_verts) + len(new_verts) <= self.MAX_VTX_LOAD:
                    batch_verts.update(tri)
                    batch_tris.append(tri)
                else:
                    next_remaining.append(tri)
            if batch_tris:
                batches.append((batch_verts, batch_tris))
            remaining = next_remaining
        return batches


# ── Bone Mapping Report ─────────────────────────────────────────────────────

def print_bone_mapping(bones: List[Bone], anim: Optional[AnimData]):
    print(f'\n{"="*60}')
    print(f'BONE HIERARCHY ({len(bones)} bones)')
    print(f'{"="*60}')

    def print_tree(idx, depth=0):
        bone = bones[idx]
        indent = '  ' * depth
        has_anim = anim and bone.name in anim.channels
        anim_mark = ' [ANIM]' if has_anim else ''
        pos = f'({bone.local_pos[0]:.2f}, {bone.local_pos[1]:.2f}, {bone.local_pos[2]:.2f})'
        print(f'{indent}[{idx:2d}] {bone.name} pos={pos} '
              f'child={bone.child_limb} sib={bone.sibling_limb}{anim_mark}')
        for ci in bone.children:
            print_tree(ci, depth + 1)

    # Find root
    for b in bones:
        if b.parent_index < 0:
            print_tree(b.index)
            break

    if anim:
        matched = sum(1 for bn in anim.channels if bn in {b.name for b in bones})
        total = len(anim.channels)
        print(f'\nAnimation "{anim.name}": {matched}/{total} bones matched')

        unmatched = [bn for bn in anim.channels if bn not in {b.name for b in bones}]
        if unmatched:
            print(f'UNMATCHED: {", ".join(unmatched)}')


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Convert Smash Bros Brawl assets (.dae + .anim) to OOT SkelAnime format')
    parser.add_argument('--dae', help='COLLADA model file (.dae)')
    parser.add_argument('--anim', action='append', default=[], help='Maya animation file(s) (.anim), can specify multiple')
    parser.add_argument('--anim-dir', help='Directory containing .anim files (adds all .anim files found)')
    parser.add_argument('--name', required=True, help='Character name (used for C identifiers)')
    parser.add_argument('--scale', type=float, default=1.0, help='Scale factor (Brawl units → OOT units)')
    parser.add_argument('--out', default='.', help='Output directory')
    parser.add_argument('--no-mesh', action='store_true', help='Skip mesh/DL generation')
    parser.add_argument('--fast64-c', help='Fast64-exported C file (.c) — uses its decimated mesh split by bone for per-limb DLs')
    parser.add_argument('--render-scale', type=float, default=1.0, help='Scale applied at draw time (stored in SSBBCharacterDef.scale)')
    parser.add_argument('--info', action='store_true', help='Print bone info without generating files')
    parser.add_argument('--skin', action='store_true', help='Generate weighted skin mesh (multi-bone vertex blending)')
    parser.add_argument('--collada-skin', action='store_true',
                        help='Generate skin mesh directly from COLLADA geometry (no Fast64, 1:1 weights)')
    parser.add_argument('--axis-map', default='+y,-z,+x',
                        help='DAE→F64 axis mapping, e.g. "+y,-z,+x" means F64.x=+DAE.y, F64.y=-DAE.z, F64.z=+DAE.x (default: %(default)s)')

    args = parser.parse_args()

    if not args.dae and not args.anim:
        parser.error('Must specify at least --dae or --anim')

    os.makedirs(args.out, exist_ok=True)

    bones = []
    meshes = []

    # Parse COLLADA
    if args.dae:
        print(f'Parsing COLLADA: {args.dae}')
        collada = ColladaParser(args.dae)
        bones, meshes = collada.parse()
        print(f'  Found {len(bones)} bones, {len(meshes)} mesh parts')

        if args.no_mesh:
            meshes = []

    # Replace mesh with Fast64 decimated mesh if provided
    if args.fast64_c and bones:
        print(f'Parsing Fast64 C file: {args.fast64_c}')
        f64_pos, f64_norm, f64_uvs, f64_alphas, f64_tris = parse_fast64_c(args.fast64_c, args.scale)

        # Collect .dae world-space positions and bone assignments from the COLLADA skin
        # Re-parse skin to get per_vertex_bones for position vertices
        collada_parser = ColladaParser(args.dae)
        collada_parser.parse()

        # Get all world-space positions and their bone assignments from the .dae
        dae_positions = []
        dae_bone_assignments = []
        for mp in collada_parser.meshes:
            # MeshPart positions are in bone-local space — transform back to world
            bone_world = collada_parser._bone_world.get(mp.limb_index, _mat4_identity())
            for p in mp.positions:
                wp = _mat4_mul_vec3(bone_world, p)
                dae_positions.append(wp)
                dae_bone_assignments.append(mp.limb_index)

        meshes = assign_fast64_to_bones(
            f64_pos, f64_norm, f64_tris,
            dae_positions, dae_bone_assignments,
            collada_parser.bone_inv_world, args.scale,
        )

    # Collect animation paths (--anim + --anim-dir)
    anim_paths = list(args.anim)
    if args.anim_dir:
        import glob
        dir_anims = sorted(glob.glob(os.path.join(args.anim_dir, '*.anim')))
        # Avoid duplicates
        existing = set(os.path.abspath(p) for p in anim_paths)
        for p in dir_anims:
            if os.path.abspath(p) not in existing:
                anim_paths.append(p)
        print(f'Found {len(dir_anims)} .anim files in {args.anim_dir}')

    # Parse animations
    anims = []
    for anim_path in anim_paths:
        print(f'Parsing animation: {anim_path}')
        anim_parser = AnimParser(anim_path)
        anim = anim_parser.parse()
        anims.append(anim)
        frames = anim.end_time - anim.start_time + 1
        print(f'  {len(anim.channels)} bones, {frames} frames')

    # Info mode
    if args.info:
        print_bone_mapping(bones, anims[0] if anims else None)
        return

    # Generate skeleton
    if bones:
        print_bone_mapping(bones, anims[0] if anims else None)

        skel_gen = OotSkelGenerator(bones, meshes, args.name, args.scale)

        header_path = os.path.join(args.out, f'{args.name}_skel.h')
        with open(header_path, 'w', encoding='utf-8') as f:
            f.write(skel_gen.generate_header())
        print(f'\nWrote skeleton header: {header_path}')

        source_path = os.path.join(args.out, f'{args.name}_skel.c')
        with open(source_path, 'w', encoding='utf-8') as f:
            f.write(skel_gen.generate_source())
        print(f'Wrote skeleton source: {source_path}')

    # Generate weighted skin mesh if --skin and --fast64-c provided
    if args.skin and args.fast64_c and bones:
        print(f'\nGenerating weighted skin mesh...')
        # Re-parse COLLADA for skin data (need full parser state)
        skin_collada = ColladaParser(args.dae)
        skin_collada.parse()

        skin_gen = OotSkinGenerator(
            args.name, f64_pos, f64_norm, f64_uvs, f64_alphas, f64_tris,
            skin_collada, args.scale
        )

        skin_header_path = os.path.join(args.out, f'{args.name}_skin.h')
        with open(skin_header_path, 'w', encoding='utf-8') as f:
            f.write(skin_gen.generate_header())
        print(f'Wrote skin header: {skin_header_path}')

        skin_source_path = os.path.join(args.out, f'{args.name}_skin.c')
        with open(skin_source_path, 'w', encoding='utf-8') as f:
            f.write(skin_gen.generate_source())
        print(f'Wrote skin source: {skin_source_path}')

    # Generate COLLADA direct skin mesh if --collada-skin
    if args.collada_skin and args.dae and bones:
        print(f'\nGenerating COLLADA direct skin mesh (1:1 weights, no matching)...')
        skin_collada = ColladaParser(args.dae)
        skin_collada.parse()

        skin_gen = ColladaSkinGenerator(
            args.name, skin_collada, args.scale, args.axis_map
        )

        skin_header_path = os.path.join(args.out, f'{args.name}_skin.h')
        with open(skin_header_path, 'w', encoding='utf-8') as f:
            f.write(skin_gen.generate_header())
        print(f'Wrote skin header: {skin_header_path}')

        skin_source_path = os.path.join(args.out, f'{args.name}_skin.c')
        with open(skin_source_path, 'w', encoding='utf-8') as f:
            f.write(skin_gen.generate_source())
        print(f'Wrote skin source: {skin_source_path}')

    # Generate animations
    for anim in anims:
        if not bones:
            print(f'WARNING: No skeleton loaded, cannot generate animation for {anim.name}')
            continue

        anim_gen = OotAnimGenerator(anim, bones, args.name, args.scale)

        header_path = os.path.join(args.out, f'{args.name}_{anim.name}.h')
        with open(header_path, 'w', encoding='utf-8') as f:
            f.write(anim_gen.generate_header())
        print(f'Wrote anim header: {header_path}')

        source_path = os.path.join(args.out, f'{args.name}_{anim.name}.c')
        with open(source_path, 'w', encoding='utf-8') as f:
            f.write(anim_gen.generate_source())
        print(f'Wrote anim source: {source_path}')

    # Generate SSBB animations (translate+rotate+scale) when --collada-skin
    ssbb_anims_generated = []
    if args.collada_skin and anims and bones:
        print(f'\nGenerating SSBB animations (translate+rotate+scale)...')
        for anim in anims:
            ssbb_gen = SSBBAnimGenerator(anim, bones, args.name)

            header_path = os.path.join(args.out, f'{args.name}_{anim.name}_ssbb.h')
            with open(header_path, 'w', encoding='utf-8') as f:
                f.write(ssbb_gen.generate_header())

            source_path = os.path.join(args.out, f'{args.name}_{anim.name}_ssbb.c')
            with open(source_path, 'w', encoding='utf-8') as f:
                f.write(ssbb_gen.generate_source())
            print(f'Wrote SSBB anim: {anim.name} ({ssbb_gen.total_frames} frames)')
            ssbb_anims_generated.append(anim)

    # Generate registration helper
    if bones and anims:
        reg_path = os.path.join(args.out, f'{args.name}_register.h')
        with open(reg_path, 'w', encoding='utf-8') as f:
            f.write(_generate_registration(args.name, anims, bones, args.render_scale,
                                           has_skin=args.skin or args.collada_skin,
                                           ssbb_anims=ssbb_anims_generated))
        print(f'Wrote registration helper: {reg_path}')

    print(f'\nDone! Generated {2 + len(anims) * 2 + (1 if anims else 0)} files in {args.out}/')
    print(f'Add .c files to your Visual Studio solution to compile.')


def _generate_registration(name: str, anims: List[AnimData], bones: List[Bone],
                           render_scale: float = 1.0, has_skin: bool = False,
                           ssbb_anims: List[AnimData] = None) -> str:
    """Generate a helper header for registering the character with SSBB system"""
    if ssbb_anims is None:
        ssbb_anims = []

    lines = [
        f'#ifndef {name.upper()}_REGISTER_H',
        f'#define {name.upper()}_REGISTER_H',
        '',
        '// Auto-generated SSBB character registration',
        '// Include this file and call the register function to use this character',
        '',
        '#include "expansions/ssbb/ssbb_character.h"',
        f'#include "expansions/ssbb/characters/{name}_skel.h"',
    ]

    if has_skin:
        lines.append(f'#include "expansions/ssbb/characters/{name}_skin.h"')

    for anim in anims:
        lines.append(f'#include "expansions/ssbb/characters/{name}_{anim.name}.h"')

    for anim in ssbb_anims:
        lines.append(f'#include "expansions/ssbb/characters/{name}_{anim.name}_ssbb.h"')

    # OOT format anims array
    lines.extend([
        '',
        f'static AnimationHeader* {name}_anims[] = {{',
    ])
    for anim in anims:
        lines.append(f'    &{name}_{anim.name}_anim,')
    lines.extend(['};', ''])

    # SSBB format anims array (translate+rotate+scale)
    if ssbb_anims:
        lines.append(f'static const struct SSBBAnim* {name}_ssbb_anims[] = {{')
        for anim in ssbb_anims:
            lines.append(f'    &{name}_{anim.name}_ssbb_anim,')
        lines.extend(['};', ''])

    lines.extend([
        f'static SSBBCharacterDef {name}_def = {{',
        f'    .name = "{name}",',
        f'    .skeleton = &{name}_skeleton,',
        f'    .anims = {name}_anims,',
        f'    .ssbbAnims = {name + "_ssbb_anims" if ssbb_anims else "NULL"},',
        f'    .numAnims = {len(anims)},',
        f'    .numSSBBAnims = {len(ssbb_anims)},',
        f'    .scale = {render_scale}f,',
        f'    .numLimbs = {len(bones)},',
        f'    .rotOrder = SSBB_ROT_ORDER_ZYX,',
        f'    .skinMesh = {"&" + name + "_skin_mesh" if has_skin else "NULL"},',
        '};',
        '',
        f'static inline s32 {name}_Register(void) {{',
        f'    return SSBBChar_Register(&{name}_def);',
        '}',
        '',
        f'#endif // {name.upper()}_REGISTER_H',
    ])

    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    main()
