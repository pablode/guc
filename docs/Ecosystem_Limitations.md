# Ecosystem Limitations

This document describes outstanding issues and known limitations that prevent truthful imaging of converted files.


## USD-MaterialX Integration

#### MaterialX geometric property support in renderers

MaterialX shading networks require geometric properties (normals, texture coordinates, tangents). These are stored as USD primvars, and the renderer needs to provide them accordingly. As this mapping is implicit, and primvar names for most purposes are not standardized, geometric nodes and their respective geometric properties (MaterialX 1.38 spec p.28) are currently poorly supported in renderers:

Geometric node   | HdStorm                                     | RadeonProRender
-----------------|---------------------------------------------|--------------------
normal           | ✅                                          | ⚠️ [only world space](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/blob/101208a8c7dbf100d050d88ca6124db4e260087e/deps/rprMtlxLoader/rprMtlxLoader.cpp#L872-L878)
texcoord (set 0)         | ⚠️ ['UV0'](https://github.com/PixarAnimationStudios/USD/blob/a0608bb8ae77de8468fb0d4b6bffbcd34979a1a8/pxr/usd/usdMtlx/parser.cpp#L220-L222), ['st' / custom primvar](https://github.com/PixarAnimationStudios/USD/blob/3b097e3ba8fabf1777a1256e241ea15df83f3065/pxr/usd/usdMtlx/parser.cpp#L53-L57)                        | ⚠️ ['st' + role-based detection](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/blob/0af4da6c160241dbd609fbf87402c60a45b90e0b/pxr/imaging/plugin/hdRpr/mesh.cpp#L389-L398)
texcoord (set N)            | ❌                                      | ❌
geomcolor            | ❌                                      | ❌
tangent          | ⚠️ [computed](https://github.com/PixarAnimationStudios/USD/blob/a0608bb8ae77de8468fb0d4b6bffbcd34979a1a8/pxr/imaging/hdSt/materialXShaderGen.cpp#L68)               | ⚠️ [computed, only world space](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/blob/101208a8c7dbf100d050d88ca6124db4e260087e/deps/rprMtlxLoader/rprMtlxLoader.cpp#L847-L856)
bitangent        | ❌                                         | ❌
geompropvalue    | ✅                                         | ❌

To work around the lack of support for authored geometric properties, guc reads most primvars directly using `<geompropvalue>` nodes.
Unfortunately, RPR [does not support](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/blob/101208a8c7dbf100d050d88ca6124db4e260087e/deps/rprMtlxLoader/rprMtlxLoader.cpp#L887) this feature, and it's [neither implemented](https://github.com/AcademySoftwareFoundation/MaterialX/blob/ef1b933b5c5192403aa4ae2d493a2b450670c934/source/MaterialXGenMdl/mdl/materialx/stdlib.mdl#L1170-L1285) in the MaterialX MDL backend. The [GeompropVis test](https://github.com/pablode/guc-tests/blob/main/tests/GeompropVis_mtlx.png) compares both approaches visually.

#### Image color spaces are not copied from USD Hydra networks to MaterialX documents

The HdMtlx USD library provides functions which aid Hydra render delegates in supporting shading networks via MaterialX. The `HdMtlxCreateMtlxDocumentFromHdNetwork` function is essential in that, as it translates Hydra shading networks to MaterialX documents. These documents are then processed by the renderer, and potentially used for code generation using a MaterialXGenShader backend.

One outstanding problem with the translation process is that color spaces are not copied from the Hydra network to the MaterialX document. This means that, for example, normal maps may be assumed to be encoded in the sRGB color space, leading to incorrect shading. (Issue [#1523](https://github.com/PixarAnimationStudios/USD/issues/1523))

This issue affects a number of render delegates, including Pixar's [HdStorm](https://github.com/PixarAnimationStudios/USD/blob/0c7b9a95f155c221ff7df9270a39a52e3b23af8b/pxr/imaging/hdSt/materialXFilter.cpp#L877).


## MaterialX

#### Hardcoded MaterialX `<normalmap>` node handedness
glTF tangents have four components, with the last component denoting the handedness of the calculated bitangent.
For normal mapping, we would ideally want to use MaterialX's `<normalmap>` node, however this node makes a hard assumption on the handedness of the bitangent.
I've proprosed a _bitangent_ input (Issue [#945](https://github.com/AcademySoftwareFoundation/MaterialX/issues/945)), but for now, guc injects a flattened, patched version of this node.

#### Limitations of the MaterialX shading model and the glTF PBR implementation
As described in the [initial PR](https://github.com/AcademySoftwareFoundation/MaterialX/pull/861), certain properties of the glTF shading model do not map to MaterialX:
 * _thickness_: whether the material is thin-walled (thickness = 0) or volumetric (thickness > 0). Currently, only the volumetric case is implemented, and the input is ignored. The addition of thin-walled materials to MaterialX is being discussed. (Issue [#864](https://github.com/AcademySoftwareFoundation/MaterialX/issues/864))
 * _occlusion_: this concept does not exist in MaterialX. SSAO or ray traced ambient occlusion may be used instead.


## HdStorm

HdStorm supports MaterialX pretty well, and Pixar is continously working on improving the integration. However, there are still some remaining issues that need to be addressed.

#### Incorrect automatic sRGB image detection and decoding
In addition to the HdMtlx color space issue above, a heuristic defined by the [UsdPreviewSurface spec](https://graphics.pixar.com/usd/release/spec_usdpreviewsurface.html#texture-reader) is used to determine whether an image is to be interpreted as sRGB or not. Unfortunately, this heuristic matches normal maps and occlusion-roughness-metalness textures commonly used with glTF. (Issue [#1878](https://github.com/PixarAnimationStudios/USD/issues/1878))

#### Hardcoded transparent geometry detection
HdStorm is a rasterizer and therefore handles translucent geometry differently than solid geometry. In order to detect whether a geometry's material is translucent or not, a heuristic is used. However, it is not yet adjusted to the MaterialX glTF PBR. (Issue [#1882](https://github.com/PixarAnimationStudios/USD/issues/1882))

#### One and two channel sRGB texture formats are unsupported
Converted assets may use such textures, but HdStorm is not able to render them. ([Source code](https://github.com/PixarAnimationStudios/USD/blob/3abc46452b1271df7650e9948fef9f0ce602e3b2/pxr/imaging/hdSt/textureUtils.cpp#L341-L345))

#### MaterialX materials with 1x1 images are not rendered correctly
Issue [#2140](https://github.com/PixarAnimationStudios/USD/issues/2140).


## Apple RealityKit and SceneKit compatibility

Apple's RealityKit (used for AR Quick Look) and SceneKit renderers only [support a subset](https://developer.apple.com/documentation/realitykit/validating-usd-files) of USD's features. guc makes no compatibility efforts, and converted assets are likely to not be displayed correctly.

