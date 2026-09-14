# Third-party assets

Every asset that did not originate in this repository is listed here with its licence. The code
of cna-room is MIT (see `LICENSE`); none of that licence extends to the assets below, which keep
their own. Files are fetched by `scripts/fetch-assets.sh` from `assets/external/manifest.json`
and are not committed; the manifest records the URL, SHA-256 and licence of every file.

## Licence texts

- CC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/legalcode
- CC BY 3.0: https://creativecommons.org/licenses/by/3.0/legalcode
- CC BY 4.0: https://creativecommons.org/licenses/by/4.0/legalcode

## Assets

Fetched from GitHub-hosted repositories only (the development environment cannot reach other
hosts); each licence was read from the source repository's own metadata: `metadata.json` beside
each Khronos model, the per-folder README of gkjohnson/3d-demo-data for the Bitterli/Blendswap
scenes. Attribution-required (CC BY) assets are credited here and in README.md.

The Bitterli scenes are whole rooms; `assets/external/extract-recipes.json` lists which node
groups become which object (e.g. `SofaLeather_0007-9` + cushions = `leather-sofa`), and
`scripts/extract-assets.sh` runs `tools/gltf-extract` (glTF-Transform: Draco decode, WebP to
PNG, subtree selection, transform baking, recentring) to produce them. No texture or geometry is
altered beyond decoding, recentring and the per-material roughness overrides named in
`src/Scene/RoomScene.cpp`.

| Asset | Author / holder | Source | Licence | Role | Modifications |
|---|---|---|---|---|---|
| SheenWoodLeatherSofa | Darmstadt Graphics Group GmbH | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenWoodLeatherSofa | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | main sofa | none beyond CNA's import |
| GlamVelvetSofa | Wayfair, LLC | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/GlamVelvetSofa | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | alternative sofa | none beyond CNA's import |
| SheenChair | Wayfair (published CC0 in the Khronos sample repository) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenChair | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | armchair | none beyond CNA's import |
| ChairDamaskPurplegold | Wayfair, LLC | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ChairDamaskPurplegold | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | accent chair | none beyond CNA's import |
| IridescenceLamp | Khronos Group / Wayfair (see model README) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescenceLamp | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | table lamp | none beyond CNA's import |
| AnisotropyBarnLamp | Wayfair, LLC (see model README) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | pendant/desk lamp | none beyond CNA's import |
| LightsPunctualLamp | Wayfair, LLC (see model README) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/LightsPunctualLamp | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | table lamp with punctual lights | none beyond CNA's import |
| GlassVaseFlowers | Wayfair (published CC0) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/GlassVaseFlowers | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | vase with flowers | none beyond CNA's import |
| DiffuseTransmissionPlant | Darmstadt Graphics Group GmbH | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionPlant | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | potted plant | none beyond CNA's import |
| IridescentDishWithOlives | Wayfair, LLC | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescentDishWithOlives | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | dish with olives | none beyond CNA's import |
| DiffuseTransmissionTeacup | Wayfair (published CC0) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionTeacup | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | teacup | none beyond CNA's import |
| SpecularSilkPouf | Wayfair, LLC | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularSilkPouf | [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/legalcode) | pouf | none beyond CNA's import |
| BoomBox | Microsoft | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BoomBox | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | small electronics | none beyond CNA's import |
| WaterBottle | Microsoft | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/WaterBottle | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | bottle | none beyond CNA's import |
| Lantern | Microsoft | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | lantern | none beyond CNA's import |
| ToyCar | Khronos Group (Guido Odendahl) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ToyCar | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | toy on a shelf | none beyond CNA's import |
| Avocado | Microsoft | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Avocado | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | fruit | none beyond CNA's import |
| ClearcoatWicker | Wayfair (published CC0) | https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ClearcoatWicker | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | wicker sphere | none beyond CNA's import |
| white-room.glb | Jay-Artist | https://github.com/gkjohnson/3d-demo-data/tree/main/models/bitterli-rendering-resources | [CC-BY-3.0](https://creativecommons.org/licenses/by/3.0/legalcode) | source of leather sofa, cushions, coffee table, TV, radio, books, magazine, candle holders, pictures, sockets, floor lamp | Draco decoded and WebP textures converted to PNG by tools/gltf-extract; furniture extracted per object by node-name prefix and recentred |
| grey-and-white-room.glb | Wig42 | https://github.com/gkjohnson/3d-demo-data/tree/main/models/bitterli-rendering-resources | [CC-BY-3.0](https://creativecommons.org/licenses/by/3.0/legalcode) | source of plants in pots, painting, mirror, bottle | Draco decoded and WebP textures converted to PNG by tools/gltf-extract; furniture extracted per object by node-name prefix and recentred |
| bedroom.glb | SlykDrako | https://github.com/gkjohnson/3d-demo-data/tree/main/models/bitterli-rendering-resources | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | source of curtains, vase, books, boxes, painting | Draco decoded and WebP textures converted to PNG by tools/gltf-extract; furniture extracted per object by node-name prefix and recentred |
| little-lamp.glb | UP3D | https://github.com/gkjohnson/3d-demo-data/tree/main/models/bitterli-rendering-resources | [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) | desk lamp | Draco decoded and WebP textures converted to PNG by tools/gltf-extract; furniture extracted per object by node-name prefix and recentred |

### Objects extracted from the scenes

| Object | Scene | Nodes |
|---|---|---|
| floor-lamp | bitterli/white-room.glb | `^(LampStand|LampshadeInner|LampshaderOuter)$` |
| leather-armchair-a | bitterli/white-room.glb | `^(SofaLeather_000[123]|Cushion)$` |
| leather-armchair-b | bitterli/white-room.glb | `^(SofaLeather_000[456]|Cushion3)$` |
| leather-sofa | bitterli/white-room.glb | `^(SofaLeather_000[789]|Cushion1_000[12])$` |
| coffee-table | bitterli/white-room.glb | `^(Table_000[12]|TableLegs|DrawerHandles_0002)$` |
| tv | bitterli/white-room.glb | `^(TvBevel|TvScreen)$` |
| tv-unit | bitterli/white-room.glb | `^(WhitePaint_00(09|1[012])|BlackMarble)$` |
| chest-of-drawers | bitterli/white-room.glb | `^(WhitePaint_000[1-5]|DrawerHandles_0001)$` |
| sideboard | bitterli/white-room.glb | `^(WhitePaint_000[678])$` |
| books-row-a | bitterli/white-room.glb | `^Books_00(0[1-5]|1[0-8])$` |
| books-row-b | bitterli/white-room.glb | `^Books_000[6-9]$` |
| magazine | bitterli/white-room.glb | `^Magazine$` |
| fruit-bowl | bitterli/white-room.glb | `^(Apple_000[1-4]|Ceramic_0001)$` |
| cup-and-saucer | bitterli/white-room.glb | `^(Dishes_000[12]|DullSteel)$` |
| teapot | bitterli/white-room.glb | `^Ceramic_000[23]$` |
| candle-holders | bitterli/white-room.glb | `^CandleHolders_000[123]$` |
| small-pictures | bitterli/white-room.glb | `^(SmallPicture[ABCD]|SmallPictureFrame_000[1-4])$` |
| large-picture | bitterli/white-room.glb | `^(LargePicture|PictureFrame_0001)$` |
| picture-medium | bitterli/white-room.glb | `^(Picture|PictureFrame_0002)$` |
| ceiling-lamp | bitterli/white-room.glb | `^(CeilingLampshade|CeilingShadeWire|LightFitting_000[123])$` |
| radio | bitterli/white-room.glb | `^(Radio[A-Za-z]*(_000[1-4])?|ChromeHandle)$` |
| rug | bitterli/white-room.glb | `^Carpet$` |
| cushion-a | bitterli/white-room.glb | `^Cushion$` |
| cushion-b | bitterli/white-room.glb | `^Cushion3$` |
| wood-stove | bitterli/white-room.glb | `^(BlackRaughtIron_000[1-7]|Steel_000[12])$` |
| potted-plant-a | bitterli/grey-and-white-room.glb | `^(PlantPot_0001|Dirt_0001|Branches_000[12]|Leaves_000[12])$` |
| potted-plant-b | bitterli/grey-and-white-room.glb | `^(PlantPot_0002|Dirt_0002|Branches_000[34]|Leaves_000[34])$` |
| painting-landscape | bitterli/grey-and-white-room.glb | `^(Painting|PaintingBack|MattePaint_0008|TableWood_000[12])$` |
| pedestal-table | bitterli/grey-and-white-room.glb | `^TableWood_000[345]$` |
| bottle-and-glasses | bitterli/grey-and-white-room.glb | `^(Glass_000[123]|BottleCap)$` |
| mirror | bitterli/grey-and-white-room.glb | `^(Mirror|Paneling_0012)$` |
| fabric-sofa | bitterli/grey-and-white-room.glb | `^(Sofa_000[1-6]|SofaLegs)$` |
| wall-sconce | bitterli/grey-and-white-room.glb | `^(Transluscent_0001|MattePaint_0001|BrushedStainlessSteel_0001)$` |
| candlestick | bitterli/grey-and-white-room.glb | `^BrushedStainlessSteel_0003$` |
| curtain-panel-a | bitterli/bedroom.glb | `^Curtains_0001$` |
| curtain-panel-b | bitterli/bedroom.glb | `^Curtains_0002$` |
| curtain-rod | bitterli/bedroom.glb | `^CurtainRod_0001$` |
| nightstand | bitterli/bedroom.glb | `^(WoodFurniture_0001|StainlessSmooth_0001|Aluminium_000[345])$` |
| bedside-lamp | bitterli/bedroom.glb | `^(LampMetal_0002|LampGlass_0001|LampGlass_0004|LampEmitter_0002|PlasticCable_0001)$` |
| pendant-metal | bitterli/bedroom.glb | `^(LampMetal_0001|LampGlass_0003|LampEmitter_0001)$` |
| vase-decor | bitterli/bedroom.glb | `^(Vase_000[12]|Glass|Rocks[123]|DecoPlant)$` |
| book-closed | bitterli/bedroom.glb | `^(BookCover|BookPages)$` |
| picture-frame-small | bitterli/bedroom.glb | `^(PictureFrame|PictureBacking|Picture)$` |
| low-cabinet | bitterli/bedroom.glb | `^(WoodFurniture_0004|StainlessSmooth_0003)$` |
| wardrobe | bitterli/bedroom.glb | `^(WoodFurniture_0003|StainlessSmooth_0002)$` |
| storage-boxes | bitterli/bedroom.glb | `^Boxes$` |
| folded-blankets | bitterli/bedroom.glb | `^Blankets_000[123]$` |
