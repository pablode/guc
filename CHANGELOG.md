
# Changelog

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

