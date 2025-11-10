# Technology Limitations

This document clarifies limitations and open problems relating to third-party technologies.
It assumes the latest software versions, which are USD v25.11 and MaterialX 1.39.4 at the time of writing.

> [!IMPORTANT]
> It is advised to always use the latest versions.

## OpenUSD

#### Material and look interchange

Material interchange technologies are handled by OpenUSD in an agnostic way, assuming they assimilate into the UsdShade subsystem.
Universally, USD specifies the [UsdPreviewSurface](https://openusd.org/release/spec_usdpreviewsurface.html) standard, which is an intentionally simple (no SSS, anisotropy, etc.) BXDF intended for preview rendering during asset interchange.

Renderers can be assumed to support UsdPreviewSurface, but the differences to the glTF material model can not be overcome.
guc's [Grapical Test Overview](https://github.com/pablode/guc-tests/tree/main/tests#graphical-test-overview) makes an effort to showcase visual similarity and problematic cases.

To come as close as possible to the intended asset appearance, guc optionally uses the [MaterialX](https://github.com/AcademySoftwareFoundation/MaterialX) technology.
The standard implements the glTF PBR using a node graph (`gltf_pbr.mtlx`), which can be interpreted and rendered.
Many renderers and tools support MaterialX nowadays, including Omniverse, Karma, Storm, Renderman, V-Ray and macOS.

There are multiple downsides that need to be mentioned, however.
Due to the parsing of a node graph and subsequent dynamic code generation, the loading time likely increases.
As the BXDF has a higher fidelity and the code is generated rather than hand-optimized, one would furthermore expect a rendering performance penality compared to native glTF rendering.
Lastly, MaterialX is not trivial to support in a renderer, and some do not implement it.

The handling of multiple material bindings in OpenUSD is currently not optimal and may perhaps change in the future.
For example, during the conversion process, the generated MaterialX material is bound to the _full_ binding, whereas the UsdPreviewSurface material is bound to the _preview_ binding.
Upon rendering, the Hydra runtime matches the bindings against [static values](https://github.com/PixarAnimationStudios/OpenUSD/blob/8843f3b7b334bbcd8df014e63d1b8fad24fc6b6e/pxr/imaging/hd/renderDelegate.h#L418-L437) returned by the render delegate.
Depending on the renderer preferences, this can mean that the MaterialX material is never used, as in the case of Storm and _usdview_.

## MaterialX

#### Limitations of the standard and the glTF PBR implementation

As described in the [initial PR](https://github.com/AcademySoftwareFoundation/MaterialX/pull/861), certain properties of the glTF shading model do not map to MaterialX:
 * _thickness_: whether the material is thin-walled (thickness = 0) or volumetric (thickness > 0). Currently, only the volumetric case is implemented, and the input is ignored. (Issue [#1936](https://github.com/AcademySoftwareFoundation/MaterialX/issues/1936))
 * _occlusion_: this concept does not exist in MaterialX. SSAO or ray traced ambient occlusion have to be used instead.


## MaterialX in USD

#### Implicit vs. explicit primvar sampling

MaterialX shading networks require geometric properties (normals, texture coordinates, tangents). These are stored as USD primvars, and the renderer needs to provide them accordingly.

*Implicit* mapping makes use of MaterialX nodes intended to interface with the geometry streams, such as `<texcoord>` or `<tangent>`.
However, there is no specified relation between these nodes and primvar names, and usually error-prone heuristics are used.
Therefore, guc always uses *explicit* sampling of primvars using `<geompropvalue>` nodes.

In practice, this means that the generated .mtlx files can not be shown in MaterialXView, as the referenced geometry streams do not exist.
However, if you build guc in debug configuration and set the `GUC_ENABLE_MTLX_VIEWER_COMPAT` environment variable, there should be some degree of compatibility.


## Apple RealityKit and SceneKit

Apple's RealityKit (used for AR Quick Look) and SceneKit renderers support only a [subset](https://developer.apple.com/documentation/usd/validating-usd-files) of USD features. This can lead to converted assets not being displayed correctly.
