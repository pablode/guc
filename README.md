## guc

![USD v24.05](https://github.com/pablode/guc/actions/workflows/run-tests-usd2405.yml/badge.svg?branch=main)
![USD v24.03](https://github.com/pablode/guc/actions/workflows/run-tests-usd2403.yml/badge.svg?branch=main)

guc is a glTF to [Universal Scene Description](https://github.com/PixarAnimationStudios/USD) (USD) converter.

Unlike...
 - [gltf2usd](https://github.com/kcoley/gltf2usd), it aims to be more than a PoC
 - [usd_from_gltf](https://github.com/google/usd_from_gltf), it is not AR Quick Look centric
 - [Apple's USDZ Tools](https://developer.apple.com/augmented-reality/tools/), it is open-source and freely available

guc furthermore supports near-lossless material translation via the [MaterialX](https://github.com/AcademySoftwareFoundation/MaterialX) standard.

All glTF features except animation and skinning are implemented and get continuously tested in guc's [test suite](https://github.com/pablode/guc-tests).

<p align="middle">
  <img width=360 src="preview_hdStorm.png" />
  <img width=360 src="preview_glTFSampleViewer.png" />
</p>
<p align="middle">
  Wayfair's <a href="https://github.com/KhronosGroup/glTF-Sample-Models/tree/16e803435fca5b07dde3dbdc5bd0e9b8374b2750/2.0/IridescentDishWithOlives">Iridescent Dish with Olives</a> (<a href="https://creativecommons.org/licenses/by/4.0/">CC BY</a>) converted to USD+MaterialX with guc and rendered in hdStorm (left).
  The same model in Khronos's <a href="https://github.khronos.org/glTF-Sample-Viewer-Release/">glTF Sample Viewer</a> (right).
</p>

### Build

You need USD v23.11+ (e.g. <a href="https://github.com/PixarAnimationStudios/OpenUSD/releases/tag/v24.05">v24.05</a>) with MaterialX support enabled.

Do a recursive clone of the repository and set up a build folder:
```
git clone https://github.com/pablode/guc --recursive
mkdir guc/build && cd guc/build
```

Pass following parameters in the CMake generation phase:
```
cmake .. -Wno-dev -DCMAKE_BUILD_TYPE=Release
```

Build the executable:
```
cmake --build . -j8 --config Release
```

> Note: set `BUILD_SHARED_LIBS` for shared builds, and `CMAKE_MSVC_RUNTIME_LIBRARY` to USD's MSVC ABI.

### Usage

```
guc 0.5 - glTF to USD converter

Usage: guc [options] [--] <gltf_path> <usd_path>

Options:
  -m, --emit-mtlx                            Emit MaterialX materials in addition to UsdPreviewSurfaces
  -u, --mtlx-as-usdshade                     Convert and inline MaterialX materials into the USD layer using UsdMtlx
  -c, --hdstorm-compat                       Apply compatibility tweaks for the USD Storm Hydra render delegate
  -v, --default-material-variant=<index>     Index of the material variant that is selected by default
  -l, --licenses                             Print the license of guc and third-party libraries
  -h, --help                                 Show the command help
```

Both glTF and GLB file types are valid input. USDA, USDC and USDZ formats can be written.

An example asset conversion is described in the [Structure Mapping](docs/Structure_Mapping.md) document.

### Extension support

Name                                | Status&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
------------------------------------|----------
KHR_lights_punctual                 | ✅ Partial <sup>1</sup>
KHR_materials_clearcoat             | ✅ Complete
KHR_materials_emissive_strength     | ✅ Complete
KHR_materials_ior                   | ✅ Complete
KHR_materials_iridescence           | ✅ Complete
KHR_materials_sheen                 | ✅ Complete
KHR_materials_specular              | ✅ Complete
KHR_materials_transmission          | ✅ Complete
KHR_materials_unlit                 | ✅ Complete
KHR_materials_variants              | ✅ Complete
KHR_materials_volume                | ✅ Partial <sup>2</sup>
KHR_texture_transform               | ✅ Complete

<sup>\[1\]</sup> Spotlight cone falloff is ignored.  
<sup>\[2\]</sup> Thickness is <a href="https://github.com/AcademySoftwareFoundation/MaterialX/pull/861">not supported</a> by the MaterialX glTF PBR implementation.

### Sdf plugin

The _usdGlTF_ library implements USD's Sdf file format interface. Enable the `GUC_BUILD_USDGLTF` CMake option before building and install it as follows:
```
cmake --install . --component usdGlTF --config Release --prefix <USD_INSTALL_DIR>/plugin/usd
```

glTF files can now be referenced as layers and opened with USD tooling.
The _emitMtlx_ dynamic Sdf file format argument controls MaterialX material emission.

### License

```

   Copyright 2024 Pablo Delgado Krämer

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

```
