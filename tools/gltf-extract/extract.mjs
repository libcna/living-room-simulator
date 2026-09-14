#!/usr/bin/env node
// Prepares a glTF/GLB for CNA's importer:
//   - decodes KHR_draco_mesh_compression (the extracted subsets are written
//     uncompressed so every consumer can read them);
//   - converts EXT_texture_webp / JPEG images to PNG (CNA has no WebP decoder);
//   - optionally keeps only the nodes whose names match a pattern, drops the
//     rest, bakes the node transforms and recentres the survivors so the
//     object's pivot is the centre of its footprint at floor level;
//   - reports the object's size in metres.
//
// Usage:
//   node extract.mjs <in.glb> [<out.glb>] [--select REGEX] [--exclude REGEX]
//                    [--recentre] [--flip-winding] [--scale F] [--info]
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { dedup, prune, textureCompress } from '@gltf-transform/functions';
import draco3d from 'draco3dgltf';
import sharp from 'sharp';

const args = process.argv.slice(2);
if (args.length < 1) {
  console.error('usage: extract.mjs <in.glb> [<out.glb>] [--select REGEX] [--exclude REGEX] [--recentre] [--flip-winding] [--scale F] [--info]');
  process.exit(2);
}
const input = args[0];
let output = null;
let select = null, exclude = null, recentre = false, info = false, flipWinding = false, scale = 1.0;
for (let i = 1; i < args.length; ++i) {
  const a = args[i];
  if (a === '--select') select = new RegExp(args[++i]);
  else if (a === '--exclude') exclude = new RegExp(args[++i]);
  else if (a === '--recentre') recentre = true;
  else if (a === '--flip-winding') flipWinding = true;
  else if (a === '--scale') scale = parseFloat(args[++i]);
  else if (a === '--info') info = true;
  else if (output === null) output = a;
  else { console.error('unknown argument', a); process.exit(2); }
}

const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({
    'draco3d.decoder': await draco3d.createDecoderModule(),
    'draco3d.encoder': await draco3d.createEncoderModule(),
  });

const document = await io.read(input);
const root = document.getRoot();

function fmt(x) { return (Math.round(x * 1000) / 1000).toString(); }

function boundsOfNode(n) {
  const m = n.getWorldMatrix();
  let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (const prim of n.getMesh().listPrimitives()) {
    const pos = prim.getAttribute('POSITION'); if (!pos) continue;
    const arr = pos.getArray();
    for (let i = 0; i < arr.length; i += 3) {
      const x = m[0]*arr[i] + m[4]*arr[i+1] + m[8]*arr[i+2] + m[12];
      const y = m[1]*arr[i] + m[5]*arr[i+1] + m[9]*arr[i+2] + m[13];
      const z = m[2]*arr[i] + m[6]*arr[i+1] + m[10]*arr[i+2] + m[14];
      min = [Math.min(min[0], x), Math.min(min[1], y), Math.min(min[2], z)];
      max = [Math.max(max[0], x), Math.max(max[1], y), Math.max(max[2], z)];
    }
  }
  return { min, max };
}

if (info) {
  console.log(`nodes ${root.listNodes().length} meshes ${root.listMeshes().length} materials ${root.listMaterials().length} textures ${root.listTextures().length}`);
  console.log('extensions:', root.listExtensionsUsed().map((e) => e.extensionName).join(', '));
  for (const n of root.listNodes()) {
    const mesh = n.getMesh();
    if (!mesh) continue;
    const bb = boundsOfNode(n);
    const tris = mesh.listPrimitives().reduce((s, p) => s + (p.getIndices() ? p.getIndices().getCount() / 3 : p.getAttribute('POSITION').getCount() / 3), 0);
    console.log(`  ${(n.getName() || '?').padEnd(30)} size ${fmt(bb.max[0]-bb.min[0])} x ${fmt(bb.max[1]-bb.min[1])} x ${fmt(bb.max[2]-bb.min[2])}  min ${bb.min.map(fmt)} max ${bb.max.map(fmt)}  tris ${tris}  mats ${mesh.listPrimitives().map((p)=>p.getMaterial()?.getName()).join('|')}`);
  }
  if (output === null) process.exit(0);
}

// Keep only the selected nodes (and their ancestors).
if (select || exclude) {
  const keep = new Set();
  for (const n of root.listNodes()) {
    const name = n.getName() || '';
    const wanted = (!select || select.test(name)) && (!exclude || !exclude.test(name));
    if (wanted && n.getMesh()) {
      let p = n;
      while (p && p.propertyType === 'Node') { keep.add(p); p = p.getParentNode(); }
    }
  }
  for (const n of root.listNodes()) {
    if (!keep.has(n)) {
      n.setMesh(null);
      n.setCamera(null);
    }
  }
  for (const n of root.listNodes()) {
    if (!keep.has(n) && n.listChildren().every((c) => !keep.has(c))) n.dispose();
  }
}

// Bake node transforms into the vertices so the extracted object stands at the
// origin, then recentre: pivot at the centre of the XZ footprint, at min Y.
if (recentre || scale !== 1.0 || flipWinding) {
  const scene = root.listScenes()[0];
  const worlds = new Map();
  for (const n of root.listNodes()) worlds.set(n, n.getWorldMatrix());
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  const seen = new Set();
  for (const n of root.listNodes()) {
    const mesh = n.getMesh();
    if (!mesh) continue;
    const m = worlds.get(n);
    const owned = seen.has(mesh) ? mesh.clone() : mesh;
    seen.add(owned);
    n.setMesh(owned);
    for (const prim of owned.listPrimitives()) {
      const pos = prim.getAttribute('POSITION');
      if (!pos) continue;
      const ownedPos = pos.clone(); prim.setAttribute('POSITION', ownedPos);
      const arr = ownedPos.getArray().slice();
      for (let i = 0; i < arr.length; i += 3) {
        const v0 = arr[i], v1 = arr[i+1], v2 = arr[i+2];
        const x = m[0]*v0 + m[4]*v1 + m[8]*v2 + m[12];
        const y = m[1]*v0 + m[5]*v1 + m[9]*v2 + m[13];
        const z = m[2]*v0 + m[6]*v1 + m[10]*v2 + m[14];
        arr[i] = x * scale; arr[i+1] = y * scale; arr[i+2] = z * scale;
        for (let k = 0; k < 3; ++k) { min[k] = Math.min(min[k], arr[i+k]); max[k] = Math.max(max[k], arr[i+k]); }
      }
      ownedPos.setArray(arr);
      const rotate = (semantic) => {
        const acc = prim.getAttribute(semantic);
        if (!acc) return;
        const own = acc.clone(); prim.setAttribute(semantic, own);
        const a = own.getArray().slice(); const n = own.getElementSize();
        for (let i = 0; i < a.length; i += n) {
          const x = m[0]*a[i] + m[4]*a[i+1] + m[8]*a[i+2];
          const y = m[1]*a[i] + m[5]*a[i+1] + m[9]*a[i+2];
          const z = m[2]*a[i] + m[6]*a[i+1] + m[10]*a[i+2];
          const l = Math.hypot(x, y, z) || 1;
          a[i] = x/l; a[i+1] = y/l; a[i+2] = z/l;
        }
        own.setArray(a);
      };
      rotate('NORMAL'); rotate('TANGENT');
      if (flipWinding) {
        const idx = prim.getIndices();
        if (idx) {
          const own = idx.clone(); prim.setIndices(own);
          const a = own.getArray().slice();
          for (let i = 0; i + 2 < a.length; i += 3) { const t = a[i+1]; a[i+1] = a[i+2]; a[i+2] = t; }
          own.setArray(a);
        }
      }
    }
    n.setMatrix([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);
  }
  // Flatten: every mesh node becomes a direct child of the scene.
  for (const n of root.listNodes()) {
    if (n.getMesh()) {
      const parent = n.getParentNode();
      if (parent) parent.removeChild(n);
      if (!scene.listChildren().includes(n)) scene.addChild(n);
    }
  }
  if (recentre && isFinite(min[0])) {
    const cx = (min[0] + max[0]) / 2, cz = (min[2] + max[2]) / 2, cy = min[1];
    for (const n of root.listNodes()) {
      const mesh = n.getMesh(); if (!mesh) continue;
      for (const prim of mesh.listPrimitives()) {
        const pos = prim.getAttribute('POSITION'); if (!pos) continue;
        const arr = pos.getArray().slice();
        for (let i = 0; i < arr.length; i += 3) { arr[i] -= cx; arr[i+1] -= cy; arr[i+2] -= cz; }
        pos.setArray(arr);
      }
    }
    console.log(`recentred: size ${fmt(max[0]-min[0])} x ${fmt(max[1]-min[1])} x ${fmt(max[2]-min[2])} m (x, height, z); pivot moved by ${fmt(-cx)}, ${fmt(-cy)}, ${fmt(-cz)}`);
  }
}

await document.transform(
  prune({ keepAttributes: true, keepLeaves: false }),
  dedup(),
  textureCompress({ encoder: sharp, targetFormat: 'png', formats: /webp|jpeg/ }),
);
// Extensions CNA refuses in extensionsRequired are dropped: the base
// metallic-roughness material remains and the extra lobe (sheen, clearcoat,
// iridescence, anisotropy, dispersion) is lost, which is what CNA would do
// with them anyway had the file not required them.
const dropped = ['KHR_draco_mesh_compression', 'EXT_texture_webp', 'KHR_materials_sheen',
  'KHR_materials_clearcoat', 'KHR_materials_iridescence', 'KHR_materials_anisotropy',
  'KHR_materials_dispersion', 'KHR_texture_basisu', 'EXT_mesh_gpu_instancing'];
for (const ext of root.listExtensionsUsed()) {
  if (dropped.includes(ext.extensionName)) ext.dispose();
  // CNA implements KHR_materials_specular but refuses files that REQUIRE it;
  // keep the data and downgrade the requirement to "used".
  else if (ext.extensionName === 'KHR_materials_specular' && ext.isRequired()) ext.setRequired(false);
}
await io.write(output, document);
console.log(`wrote ${output}: nodes ${root.listNodes().length} meshes ${root.listMeshes().length} materials ${root.listMaterials().length} textures ${root.listTextures().length}`);
