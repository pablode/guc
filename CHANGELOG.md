
# Changelog

## Version 0.4 - 2024-07-15

Added
* Asset resolver support (thanks, [@expenses](https://github.com/expenses)!)
* Command line option to print licenses
* Display name attribute for most glTF objects
* Dynamic _emitMtlx_ Sdf file format argument
* Explicit documentation of GUC_BUILD_USDGLTF to README (thanks, [@expenses](https://github.com/expenses)!)
* Help (-h / --help) command-line option
* Support for [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/blob/082d5a98f479c37dff18767982d1431fc6c014fd/extensions/2.0/Khronos/KHR_materials_unlit/README.md) extension
* Third-party licenses to LICENSE file

Changed
* Default input values are not authored anymore in MaterialX networks
* Default material binding is now used when MaterialX is disabled (thanks, [@lanxinger](https://github.com/lanxinger)!)
* Fallback tangents are now only generated if MaterialX is enabled
* Improved prim naming heuristics
* MaterialX 1.38.6 is now required
* Prebuilt binaries do not require a specific Python version anymore
* Primvar naming does not emit suffix for index 0 anymore
* Removed dependency on OpenImageIO (stb_image is used as a fallback)
* Removed explicit color space transformations
* Removed glTF PBR implementation options
* Renamed Sdf plugin id from 'glTF' to 'gltf' to conform with other projects
* Reorder command line arguments to conform with UNIX conventions

Fixed
* False-positive MSVC warning caused by Boost macros
* Incorrect UsdPreviewSurface Sdf value types
* Incorrect warning about greyscale texture alpha channel
* Material name not being preserved if it starts with an underscore
* Missing '.glb' extension in Sdf plugin definition
* Missing UsdLux extents
* Typo in Sdf plugin that could prevent file reading
* UsdLuxShapingAPI not being applied

## Version 0.3 - 2023-07-26

Added
* Binaries for USD v23.08 with MaterialX 1.38.7
* Metainfo that UsdGlTF plugin does not support writing
* Support for [KHR_materials_emissive_strength](https://github.com/KhronosGroup/glTF/blob/d3382c30eca18312bd9cc0b36d6a9ae60e1f1bae/extensions/2.0/Khronos/KHR_materials_emissive_strength/README.md) extension
* Support for [KHR_materials_iridescence](https://github.com/KhronosGroup/glTF/tree/d3382c30eca18312bd9cc0b36d6a9ae60e1f1bae/extensions/2.0/Khronos/KHR_materials_iridescence/README.md) extension
* Support for [KHR_materials_variants](https://github.com/KhronosGroup/glTF/blob/d3382c30eca18312bd9cc0b36d6a9ae60e1f1bae/extensions/2.0/Khronos/KHR_materials_variants/README.md) extension
* Support for [KHR_texture_transform](https://github.com/KhronosGroup/glTF/blob/d3382c30eca18312bd9cc0b36d6a9ae60e1f1bae/extensions/2.0/Khronos/KHR_texture_transform/README.md) extension
* Warnings about unsupported optional extensions

Changed
* Disabled file glTF PBR implementation option on USD v23.08+ due to an internal crash
* Renamed 'tangentSigns' primvar to 'bitangentSigns'
* Updated [Ecosystem Limitations](docs/Ecosystem_Limitations.md) document with latest USD/MaterialX versions

Fixed
* Normalmap UsdUVTexture:scale[3] not matching USD complianceChecker

## Version 0.2 - 2023-01-09

Added
* CMake config file installation for libguc
* Custom data for generated mesh attributes
* Display color estimation from material properties
* Documentation on [Ecosystem Limitations](docs/Ecosystem_Limitations.md) and [Structure Mapping](docs/Structure_Mapping.md)
* Extent computation
* GitHub Actions builds
* Omittance of MaterialX multiplication nodes with factor 1
* Option to export MaterialX glTF PBR as file
* Sdf file format plugin
* Support for glTF libraries (no scenes)
* Test suite with graphical tests
* Transmission as alpha-as-coverage to UsdPreviewSurfaces
* UsdModelAPI metadata
* USDZ export
* Version information to header, executable and converted assets
* Workaround for glTF assets referencing the wrong texture channel for alpha

Changed
* CMake 3.15 is now required
* MikkTSpace is now linked statically
* Refined output USD file structure
* Renamed `guc_params` struct to `guc_options`
* Updated USD version to 22.11
* Vertex colors are now stored as dedicated primvars (not as displayColors)
* Vertex opacities are not generated for opaque materials

Fixed
* Bug with base64 output buffer size estimation
* Erroneous C11 compiler requirement
* Fallback tangent generation
* Flat normals and tangents being attempted to generate for line and point topologies
* Flat normal generation not unindexing primvars
* Handling of MaterialX C++ exceptions
* Inactive scenes being visible
* Incorrect MaterialX glTF PBR default values
* Link errors with Boost and TBB (thanks, [@ix-dcourtois](https://github.com/ix-dcourtois)!)
* Location of MaterialX and OIIO libraries (thanks, [@ix-dcourtois](https://github.com/ix-dcourtois)!)
* MaterialX exception for very small or large float values
* MaterialX inputs having multiple data source attributes
* Missing MaterialX vertex color multiplication in case of missing base color texture
* Mtlx-as-UsdShade error with USD 22.11
* Multiple compiler warnings
* Normal map tangent handling
* Position independent code not being enabled for static library builds
* Shared library symbols not being exported on Windows (thanks, [@ix-dcourtois](https://github.com/ix-dcourtois)!)
* USD MaterialBindingAPI schema not being applied

## Version 0.1 - 2022-05-17

Initial release

