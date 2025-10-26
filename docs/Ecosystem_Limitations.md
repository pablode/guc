# Ecosystem Limitations

This document clarifies limitations and potential problems relating to third-party technologies.
It assumes the latest software versions, which are USD v25.11 and MaterialX 1.39.4 at the time of writing.
See [here](https://github.com/pablode/guc/blob/2d48eadc23450acf6dc0f8c16e5fcc95d8a33ae4/docs/Ecosystem_Limitations.md) for a list of historical bugs.


## OpenUSD

- discussion: mention that guc converts to UsdPreviewSurface, but that results will be different because shading model is different
	- link to guc-tests as proof
	- if you aim for lossless look-transfer, MaterialX is your best bet.
	- however it maybe be more expensive to load and render, and support in rendering engines is lacking.
	- when both mtlx and preview surface are enabled, both are bound to 'preview' and 'full' render purposes
	- the host determines how bindings are treated. it may use a preference list provided by a Hydra renderer.

- usdz only supports PNG and JPEG


## MaterialX

#### Limitations of the MaterialX shading model and the glTF PBR implementation

As described in the [initial PR](https://github.com/AcademySoftwareFoundation/MaterialX/pull/861), certain properties of the glTF shading model do not map to MaterialX:
 * _thickness_: whether the material is thin-walled (thickness = 0) or volumetric (thickness > 0). Currently, only the volumetric case is implemented, and the input is ignored. (Issue [#1936](https://github.com/AcademySoftwareFoundation/MaterialX/issues/1936))
 * _occlusion_: this concept does not exist in MaterialX. SSAO or ray traced ambient occlusion have to be used instead.


## MaterialX in USD

#### Implicit vs. explicit primvar sampling

MaterialX shading networks require geometric properties (normals, texture coordinates, tangents). These are stored as USD primvars, and the renderer needs to provide them accordingly.

As an *implicit* mapping relies on non-standardized heuristics that do not cover multiple texture sets, vertex colors or tangents, guc always uses *explicit* reading of these primvars using `<geompropvalue>` nodes.

- in practice, this also means that .mtlx files can not be shown in MaterialXView, as it can not load USD files and their primvars.
- if you build guc in debug and specify XXX, files should render fine in MaterialXView but may not in an USD environment


#### Apple RealityKit and SceneKit compatibility

Apple's RealityKit (used for AR Quick Look) and SceneKit renderers [support a subset](https://developer.apple.com/documentation/realitykit/validating-usd-files) of USD's features. guc makes no compatibility efforts, and converted assets may not be displayed correctly.
