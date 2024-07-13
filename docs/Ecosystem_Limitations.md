# Ecosystem Limitations

This document describes issues and known limitations that prevent truthful imaging of converted files.


## MaterialX

#### Limitations of the MaterialX shading model and the glTF PBR implementation
As described in the [initial PR](https://github.com/AcademySoftwareFoundation/MaterialX/pull/861), certain properties of the glTF shading model do not map to MaterialX:
 * _thickness_: whether the material is thin-walled (thickness = 0) or volumetric (thickness > 0). Currently, only the volumetric case is implemented, and the input is ignored. (Issue [#1936](https://github.com/AcademySoftwareFoundation/MaterialX/issues/1936))
 * _occlusion_: this concept does not exist in MaterialX. SSAO or ray traced ambient occlusion may be used instead.

#### Hardcoded `<normalmap>` node handedness
glTF tangents have four components, with the last component denoting the handedness of the calculated bitangent.
For normal mapping, we would ideally want to use MaterialX's `<normalmap>` node, however this node makes a hard assumption on the handedness of the bitangent.
A _bitangent_ input has been proposed (Issue [#945](https://github.com/AcademySoftwareFoundation/MaterialX/issues/945)), but as a fallback, guc injects a flattened, patched version of this node.

> [!IMPORTANT]  
> Resolved in MaterialX 1.39


## MaterialX in USD

#### Implicit vs. explicit primvar reading

MaterialX shading networks require geometric properties (normals, texture coordinates, tangents). These are stored as USD primvars, and the renderer needs to provide them accordingly.

As an *implicit* mapping relies on non-standardized heuristics that do not cover multiple texture sets, vertex colors or tangents, guc always uses *explicit* reading of these primvars using `<geompropvalue>` nodes.
However, this functionality is known to not be supported by select render delegates.

#### Image color spaces are not copied from Hydra shading networks to MaterialX documents

The HdMtlx USD library provides functions which aid Hydra render delegates in supporting shading networks via MaterialX. The `HdMtlxCreateMtlxDocumentFromHdNetwork` function is essential in that, as it translates Hydra shading networks to MaterialX documents. These documents are then processed by the renderer, and potentially used for code generation using a MaterialXGenShader backend.

One outstanding problem with the translation process is that color spaces are not copied from the Hydra network to the MaterialX document. This means that, for example, normal maps may be assumed to be encoded in the sRGB color space, leading to incorrect shading. (Issue [#1523](https://github.com/PixarAnimationStudios/USD/issues/1523))

This issue affects a number of render delegates, including Pixar's [HdStorm](https://github.com/PixarAnimationStudios/USD/blob/0c7b9a95f155c221ff7df9270a39a52e3b23af8b/pxr/imaging/hdSt/materialXFilter.cpp#L877).

> [!IMPORTANT]  
> Resolved in OpenUSD 23.11


## MaterialX in HdStorm

#### One and two channel sRGB texture formats are unsupported
Converted assets may use such textures, but HdStorm is not able to render them. ([Source code](https://github.com/PixarAnimationStudios/USD/blob/3abc46452b1271df7650e9948fef9f0ce602e3b2/pxr/imaging/hdSt/textureUtils.cpp#L341-L345))

> [!IMPORTANT]  
> Resolved in OpenUSD 24.08

#### Incorrect automatic sRGB image detection and decoding
In addition to the HdMtlx color space issue above, a heuristic defined by the [UsdPreviewSurface spec](https://graphics.pixar.com/usd/release/spec_usdpreviewsurface.html#texture-reader) is used to determine whether an image is to be interpreted as sRGB or not. This heuristic can not be disabled and incorrectly classifies normal maps and occlusion-roughness-metalness textures commonly used with glTF. (Issue [#1878](https://github.com/PixarAnimationStudios/USD/issues/1878))

> [!IMPORTANT]  
> Resolved in OpenUSD 23.11

#### Hardcoded transparent geometry detection
HdStorm is a rasterizer and therefore handles translucent geometry differently than solid geometry. In order to detect whether a geometry's material is translucent or not, a heuristic is used. However, it is not yet adjusted to the MaterialX glTF PBR. (Issue [#1882](https://github.com/PixarAnimationStudios/USD/issues/1882))

> [!IMPORTANT]  
> Resolved in OpenUSD 24.08

#### Materials with 1x1 images are not rendered correctly
Issue [#2140](https://github.com/PixarAnimationStudios/USD/issues/2140).

> [!IMPORTANT]  
> Resolved in MaterialX 1.38.8 / OpenUSD 24.05

#### EDF shader compilation error on Metal
Issue [#3049](https://github.com/PixarAnimationStudios/OpenUSD/issues/3049).

> [!IMPORTANT]  
> Resolved in OpenUSD 24.08

#### Tangent frame is not aligned to the UV space
This causes normal maps to render incorrectly, for instance for the Open Chess Set. (Issue [#2255](https://github.com/PixarAnimationStudios/OpenUSD/issues/2255))

> [!IMPORTANT]  
> Resolved in OpenUSD 24.03

## Apple RealityKit and SceneKit compatibility

Apple's RealityKit (used for AR Quick Look) and SceneKit renderers only [support a subset](https://developer.apple.com/documentation/realitykit/validating-usd-files) of USD's features. guc makes no compatibility efforts, and converted assets may not be displayed correctly.

