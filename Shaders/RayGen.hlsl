

RaytracingAccelerationStructure _AccelerationStructure : register(t0);
RWTexture2D<float4>             _ColorImage            : register(u1);

struct Payload
{
    [[vk::location(0)]] float3 hitValue;
};

struct Constants
{
    float4x4 _InverseMatrixV;
    float4x4 _InverseMatrixP;
};
[[vk::push_constant]] Constants gConstants;

[shader("raygeneration")]
void Main()
{
    uint3 dispatchRayID = DispatchRaysIndex();
    uint3 dispatchSize  = DispatchRaysDimensions();

    const float2 pixelCenter = float2(dispatchRayID.xy) + float2(0.5, 0.5);
    const float2 inUV = pixelCenter / float2(dispatchSize.xy);
    float2 d = inUV * 2.0 - 1.0;
    float4 target = mul(gConstants._InverseMatrixP, float4(d.x, d.y, 1, 1));

    RayDesc ray;
    {
        ray.Origin    = mul(gConstants._InverseMatrixV, float4(0, 0, 0, 1)).xyz;
        ray.Direction = mul(gConstants._InverseMatrixV, float4(normalize(target.xyz), 0)).xyz;
        ray.TMin      = 0.001;
        ray.TMax      = 10000.0;
    }

    Payload payload;
    TraceRay(_AccelerationStructure, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);

    _ColorImage[int2(dispatchRayID.xy)] = float4(payload.hitValue, 0.0);
}