# Sdf Plugin

Sdf plugins allow OpenUSD to transparently read and write foreign file formats.
guc comes with its own plugin, _usdGlTF_, which needs to be enabled with the `GUC_BUILD_USDGLTF` CMake option.

After building, the plugin has to be copied to the USD installation tree:

```
cmake --install . --component usdGlTF --config Release --prefix <USD_INSTALL_DIR>/plugin/usd
```

Following file format arguments are exposed:

| Name | Type | Default Value |
| --- | --- | --- |
| emitMtlx | bool | false |
| skipValidation | bool | false |

## Transparent file I/O

With the plugin installed, glTF files can be opened with USD tooling such as _usdview_ and _usdcat_.

Arguments may be encoded into the path as follows: `usdview Asset.gltf:SDF_FORMAT_ARGS:emitMtlx=true`

## Composition arcs

glTF files can further be referenced by USD layers, optionally with arguments, as documented in [Dynamic File Formats](https://openusd.org/dev/api/_usd__page__dynamic_file_format.html).

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
