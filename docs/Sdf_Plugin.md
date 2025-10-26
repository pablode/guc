# Sdf Plugin

An Sdf plugin allows the OpenUSD runtime to transparently read and write foreign file formats (depending on support).

guc comes with its own plugin, _usdGlTF_, which needs to be enabled with the `GUC_BUILD_USDGLTF` CMake option.

After building, the plugin has to be copied into the USD installation tree.

```
cmake --install . --component usdGlTF --config Release --prefix <USD_INSTALL_DIR>/plugin/usd
```

## Using the Sdf Plugin

### File Format Arguments

When Sdf is used, the user can specificy arguments that are passed to the plugin.

| Name | Type | Default Value |
| --- | --- | --- |
| emitMtlx | bool | false |

### Tooling Support

With the plugin installed, glTF files can now be opened with native USD tooling such as _usdview_ and _usdcat_.

Arguments can be passed as part of the path: `usdview Asset.gltf:SDF_FORMAT_ARGS:emitMtlx=true`

### C++ and Python

When opening stages using the API, arguments can be passed in the same way as above.
```c++
UsdStageRefPtr stage = UsdStage::Open("Asset.gltf:SDF_FORMAT_ARGS:emitMtlx=true")
stage->Export("Asset.usd")
```

### Composition of USD Files

glTF files can further be natively referenced by USD files, with arguments defined as documented in [Dynamic File Formats](https://openusd.org/dev/api/_usd__page__dynamic_file_format.html).

```usda
#usda 1.0
 
def "Root" (
    references = </Params>
    payload = @./Asset.gltf@
)
{
}
 
def "Params" (
    emitMtlx = 1
)
{
}
```
