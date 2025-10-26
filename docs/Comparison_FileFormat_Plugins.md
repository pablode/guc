# Comparison to Adobe's FileFormat Plugins

Adobe's [USD-Fileformat-plugins](https://github.com/adobe/USD-Fileformat-plugins) project is able to convert glTF assets to OpenUSD, making potential users ask the question: "What should I use, or when?"

This document lists differences and similarities.

### High Level Comparison

<table>
  <thead>
    <tr>
      <th></th>
      <th>guc</th>
      <th>Adobe plugin</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Stated mission</td>
      <td>Near-lossless glTF to USD conversion</td>
      <td>Interchange between USD and a number of file formats</td>
    </tr>
    <tr>
      <td>Conversion capability</td>
      <td>glTF → USD</td>
      <td>glTF ↔ USD</td>
    </tr>
    <tr>
      <td>USD Formats</td>
      <td>.usda, .usdc, .usdz</td>
      <td>.usda, .usdc</td>
    </tr>
    <tr>
      <td>File format plugin</td>
      <td>✅</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>C++ API</td>
      <td>✅</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>Executable</td>
      <td>✅</td>
      <td>usdcat with plugin</td>
    </tr>
    <tr>
      <td>Automated tests</td>
      <td>✅</td>
      <td>✅</td>
    </tr>
  </tbody>
</table>

### Feature Support

<table>
  <thead>
    <tr>
      <th></th>
      <th>guc</th>
      <th>Adobe plugin</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>UsdPreviewSurface</td>
      <td>✅</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>MaterialX</td>
      <td>✅ via the MaterialX <a href="https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/libraries/bxdf/gltf_pbr.mtlx">glTF PBR</a> implementation. It closely matches the math of the specification and gets continuously extended with extension support.</td>
      <td>🟡 via <a href="https://github.com/AcademySoftwareFoundation/OpenPBR">OpenPBR</a>. The material model is the successor of both the Autodesk Standard Surface and the Adobe Standard Material model. Some parts of it, such as the sheen (fuzz) layer location and implementation, as well as the diffuse roughness algorithm (EON) differ from the glTF specification.</td>
    </tr>
    <tr>
      <td>Animations</td>
      <td>❌ (planned)</td>
      <td>✅ (limited)</td>
    </tr>
    <tr>
      <td>Skinning</td>
      <td>❌ (planned)</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>Instancing</td>
      <td>✅</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>Scene Structure</td>
      <td>TODO</td>
      <td>TODO</td>
    </tr>
    <tr>
      <td>Tangents</td>
      <td>✅ MikkTSpace precomputed primvars</td>
      <td>🟡 authored without MikkTSpace and not referenced by shading network</td>
    </tr>
  </tbody>
</table>

### Extension Support

<table>
  <thead>
    <tr>
      <th></th>
      <th>guc</th>
      <th>Adobe plugin</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Multiple Adobe-specific extensions</td>
      <td>❌</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>EXT_meshopt_compression</td>
      <td>✅</td>
      <td>❌</td>
    </tr>
    <tr>
      <td>KHR_materials_anisotropy</td>
      <td>❌</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>KHR_materials_iridescence</td>
      <td>✅</td>
      <td>❌</td>
    </tr>
    <tr>
      <td>KHR_materials_pbrSpecularGlossiness</td>
      <td>❌ (by design)</td>
      <td>✅</td>
    </tr>
    <tr>
      <td>KHR_materials_transmission</td>
      <td>✅</td>
      <td>?</td>
    </tr>
    <tr>
      <td>KHR_materials_unlit</td>
      <td>✅</td>
      <td>❌</td>
    </tr>
    <tr>
      <td>KHR_materials_variants</td>
      <td>✅</td>
      <td>❌</td>
    </tr>
    <tr>
      <td>KHR_mesh_quantization</td>
      <td>✅</td>
      <td>❌</td>
    </tr>
  </tbody>
</table>
