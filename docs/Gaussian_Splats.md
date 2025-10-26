# Gaussian Splats

guc implements the glTF [_KHR_gaussian_splatting_](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting) draft extension using MaterialX, though proper imaging heavily depends on the viewer.
It builds on [early work](https://www.youtube.com/watch?v=we91xZbkNLc) from Oliver Markowski.

In the future, a USD-native solution using the [proposed](https://github.com/PixarAnimationStudios/OpenUSD/pull/3716/files) _GaussiansAPI_ and _SphericalHarmonicsAPI_ schemas will be alternatively provided.

## Splat Representation

Individual splats are either of type UsdGeomSphere or UsdGeomMesh, depending on the settings.
Spheres may be more efficient to render, although they not be universally supported.
The geometry is only defined once within the context of a point instancer.
All other splat properties such as color, opacity, and spherical haromics coefficients are too authored on the point instancer.

```usda
def Xform "splats"
{
    def PointInstancer "instances" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </Asset/Materials/MaterialX/Materials/GS_0>

        point3f[] positions = [...]
        quatf[] orientationsf = [...]
        float3[] scales = [...]
        vector3f[] primvars:color = [...] (interpolation = "varying")
        float[] primvars:opacity = [...] (interpolation = "varying")

        rel prototypes = <.../instances/ProtoGeom>
        int[] protoIndices = [0, 0, 0, ...]

        def Sphere "ProtoGeom"
        {
        }
    }
}
```

TODO: image of example splat without tranparency

## Shading Implementation

As Gaussian splat shading is of non-trivial complexity, the logic has been implemented using [ShadingLanguageX](https://github.com/jakethorn/ShadingLanguageX).
The following is the primary source code listing from which a helper `.mtlx` file is generated, which is referenced by the converted asset.

```
float GS_ShCoeff2(int idx)
{
  return switch(idx) {
    1.0925484305920792,
    -1.0925484305920792,
    0.31539156525252005,
    -1.0925484305920792,
    0.5462742152960396
  };
}

float GS_ShCoeff3(int idx)
{
  return switch(idx) {
    -0.5900435899266435,
    2.890611442640554,
    -0.4570457994644658,
    0.3731763325901154,
    -0.4570457994644658,
    1.445305721320277,
    -0.5900435899266435
  };
}

// https://github.com/graphdeco-inria/gaussian-splatting/blob/main/utils/sh_utils.py
float GS_SphericalHarmonics(int shDegree, vector3 dir)
{
  const float SH_C0 = 0.28209479177387814;
  const float SH_C1 = 0.4886025119029199;

  float xx = dir.x * dir.x;
  float yy = dir.y * dir.y;
  float zz = dir.z * dir.z;
  float xy = dir.x * dir.y;
  float yz = dir.y * dir.z;
  float xz = dir.x * dir.z;

  float result = SH_C0 * geompropvalue("sh_coeff0");

  result = if (shDegree >= 1)
  {
    result +
    - SH_C1 * dir.y * geompropvalue("sh_coeff1")
    + SH_C1 * dir.z * geompropvalue("sh_coeff2")
    - SH_C1 * dir.x * geompropvalue("sh_coeff3")
  };

  result = if (shDegree >= 2)
  {
    result +
    GS_ShCoeff2(0) * xy * geompropvalue("sh_coeff4") +
    GS_ShCoeff2(1) * yz * geompropvalue("sh_coeff5") +
    GS_ShCoeff2(2) * (2.0 * zz - xx - yy) * geompropvalue("sh_coeff6") +
    GS_ShCoeff2(3) * xz * geompropvalue("sh_coeff7") +
    GS_ShCoeff2(4) * (xx - yy) * geompropvalue("sh_coeff8")
  };

  result = if (shDegree >= 3)
  {
    result +
    GS_ShCoeff3(0) * dir.y * (3.0 * xx - yy) * geompropvalue("sh_coeff9") +
    GS_ShCoeff3(1) * dir.z * xy * geompropvalue("sh_coeff10") +
    GS_ShCoeff3(2) * dir.y * (4.0 * zz - xx - yy) * geompropvalue("sh_coeff11") +
    GS_ShCoeff3(3) * dir.z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * geompropvalue("sh_coeff12") +
    GS_ShCoeff3(4) * dir.x * (4.0 * zz - xx - yy) * geompropvalue("sh_coeff13") +
    GS_ShCoeff3(5) * dir.z * (xx - yy) * geompropvalue("sh_coeff14") +
    GS_ShCoeff3(6) * dir.x * (xx - 3.0 * yy) * geompropvalue("sh_coeff15")
  };

  return result;
}

surfaceshader GS_SurfaceShader(int shDegree)
{
  vector3 pos = position();
  float mag = magnitude(pos);
  float rxx = power(mag, 2.0);
  float rxxm = rxx * -2.5;
  float rexp = exp(rxxm);
  float opa = rexp * geompropvalue("opacity");

  vector3 dir = viewdirection("object");
  float sh = GS_SphericalHarmonics(shDegree, dir);

  color3 col = geompropvalue("color");
  col += color3(sh);

  return surface(
    edf = uniform_edf(col),
    opacity = opa
  );
}
```

The code listing does not define specific materials, but instead a custom surface shader implementation.
It is parameterized with an integer `shDegree`, which stands for the number of spherical harmonic degrees used.
Due to the value being set at compile time, this allows expensive branches to be optimized away.
guc generates materials that make use of `GS_SurfaceShader` depending on the spherical harmonic degrees needed.

TODO: side by side comparison between SH degree 0 and 3

## Limitations

For the converted assets to render correctly, a rasterizer with alpha blending and sorting-based transparency is needed.
Path tracing based renderers may give the impression of being correct, but may deviate from the expected result.
The rendering performance depends heavily on the renderer, but thousands of transparent objects combined with complex shading networks are usually on the expensive side.

TODO: comment about renderer support, e.g. Karma
