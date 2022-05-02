﻿#pragma once

#include "../common/common_shared.h"

namespace shared {
    static constexpr float probToSampleEnvLight = 0.25f;

    static constexpr uint32_t numLightSubsets = 128;
    static constexpr uint32_t lightSubsetSize = 1024;
    static constexpr int tileSizeX = 8;
    static constexpr int tileSizeY = 8;



    struct GBufferRayType {
        enum Value {
            Primary,
            NumTypes
        } value;

        CUDA_DEVICE_FUNCTION constexpr GBufferRayType(Value v = Primary) : value(v) {}

        CUDA_DEVICE_FUNCTION operator uint32_t() const {
            return static_cast<uint32_t>(value);
        }
    };

    struct ReSTIRRayType {
        enum Value {
            Visibility,
            NumTypes
        } value;

        CUDA_DEVICE_FUNCTION constexpr ReSTIRRayType(Value v = Visibility) : value(v) {}

        CUDA_DEVICE_FUNCTION operator uint32_t() const {
            return static_cast<uint32_t>(value);
        }
    };

    constexpr uint32_t maxNumRayTypes = 1;



    struct PerspectiveCamera {
        float aspect;
        float fovY;
        float3 position;
        Matrix3x3 orientation;

        CUDA_COMMON_FUNCTION float2 calcScreenPosition(const float3 &posInWorld) const {
            Matrix3x3 invOri = inverse(orientation);
            float3 posInView = invOri * (posInWorld - position);
            float2 posAtZ1 = make_float2(posInView.x / posInView.z, posInView.y / posInView.z);
            float h = 2 * std::tan(fovY / 2);
            float w = aspect * h;
            return make_float2(1 - (posAtZ1.x + 0.5f * w) / w,
                               1 - (posAtZ1.y + 0.5f * h) / h);
        }
    };



    struct HitPointParams {
        float3 albedo;
        float3 positionInWorld;
        float3 prevPositionInWorld;
        float3 normalInWorld;
        float2 texCoord;
        uint32_t materialSlot;
    };



    CUDA_COMMON_FUNCTION CUDA_INLINE float convertToWeight(const float3 &color) {
        //return sRGB_calcLuminance(color);
        return (color.x + color.y + color.z) / 3;
    }



    struct LightSample {
        float3 emittance;
        float3 position;
        float3 normal;
        unsigned int atInfinity : 1;
    };

    struct PreSampledLight {
        LightSample sample;
        float areaPDensity;
    };

    using WeightSum = float;
    //using WeightSum = FloatSum;

    template <typename SampleType>
    class Reservoir {
        SampleType m_sample;
        WeightSum m_sumWeights;
        uint32_t m_streamLength;

    public:
        CUDA_COMMON_FUNCTION void initialize() {
            m_sumWeights = 0;
            m_streamLength = 0;
        }
        CUDA_COMMON_FUNCTION bool update(const SampleType &newSample, float weight, float u) {
            m_sumWeights += weight;
            bool accepted = u < weight / m_sumWeights;
            if (accepted)
                m_sample = newSample;
            ++m_streamLength;
            return accepted;
        }

        CUDA_COMMON_FUNCTION LightSample getSample() const {
            return m_sample;
        }
        CUDA_COMMON_FUNCTION float getSumWeights() const {
            return m_sumWeights;
        }
        CUDA_COMMON_FUNCTION uint32_t getStreamLength() const {
            return m_streamLength;
        }
        CUDA_COMMON_FUNCTION void setStreamLength(uint32_t length) {
            m_streamLength = length;
        }
    };

    struct ReservoirInfo {
        float recPDFEstimate;
        float targetDensity;
    };

    union SampleVisibility {
        uint32_t asUInt;
        struct {
            unsigned int newSample : 1;
            unsigned int newSampleOnTemporal : 1;
            unsigned int newSampleOnSpatiotemporal : 1;
            unsigned int temporalPassedHeuristic : 1;
            unsigned int temporalSample : 1;
            unsigned int temporalSampleOnCurrent : 1;
            unsigned int temporalSampleOnSpatiotemporal : 1;
            unsigned int spatiotemporalPassedHeuristic : 1;
            unsigned int spatiotemporalSample : 1;
            unsigned int spatiotemporalSampleOnCurrent : 1;
            unsigned int spatiotemporalSampleOnTemporal : 1;
            unsigned int selectedSample : 1;
        };

        CUDA_DEVICE_FUNCTION SampleVisibility() : asUInt(0) {}
    };



    struct PickInfo {
        uint32_t instSlot;
        uint32_t geomInstSlot;
        uint32_t primIndex;
        uint32_t matSlot;
        float3 positionInWorld;
        float3 normalInWorld;
        float3 albedo;
        float3 emittance;
        unsigned int hit : 1;
    };



    struct GBuffer0 {
        float3 positionInWorld;
        float texCoord_x;
    };

    struct GBuffer1 {
        float3 normalInWorld;
        float texCoord_y;
    };

    struct GBuffer2 {
        float2 motionVector;
        uint32_t materialSlot;
    };

    
    
    struct StaticPipelineLaunchParameters {
        int2 imageSize;
        int2 numTiles;

        // only for rearchitected ver.
        PCG32RNG* lightPreSamplingRngs;
        PreSampledLight* preSampledLights;

        optixu::NativeBlockBuffer2D<PCG32RNG> rngBuffer;

        optixu::NativeBlockBuffer2D<GBuffer0> GBuffer0[2];
        optixu::NativeBlockBuffer2D<GBuffer1> GBuffer1[2];
        optixu::NativeBlockBuffer2D<GBuffer2> GBuffer2[2];

        optixu::BlockBuffer2D<Reservoir<LightSample>, 0> reservoirBuffer[2];
        optixu::NativeBlockBuffer2D<ReservoirInfo> reservoirInfoBuffer[2];
        optixu::NativeBlockBuffer2D<SampleVisibility> sampleVisibilityBuffer[2];
        const float2* spatialNeighborDeltas; // only for rearchitected ver.

        const MaterialData* materialDataBuffer;
        const GeometryInstanceData* geometryInstanceDataBuffer;
        DiscreteDistribution1D lightInstDist;
        RegularConstantContinuousDistribution2D envLightImportanceMap;
        CUtexObject envLightTexture;

        optixu::NativeBlockBuffer2D<float4> beautyAccumBuffer;
        optixu::NativeBlockBuffer2D<float4> albedoAccumBuffer;
        optixu::NativeBlockBuffer2D<float4> normalAccumBuffer;
    };

    struct PerFramePipelineLaunchParameters {
        OptixTraversableHandle travHandle;
        uint32_t numAccumFrames;
        uint32_t frameIndex;

        const InstanceData* instanceDataBuffer;

        PerspectiveCamera camera;
        PerspectiveCamera prevCamera;

        float envLightPowerCoeff;
        float envLightRotation;

        float spatialNeighborRadius;
        float radiusThresholdForSpatialVisReuse; // only for rearchitected ver.

        int2 mousePosition;
        PickInfo* pickInfo;

        unsigned int log2NumCandidateSamples : 4;
        unsigned int numSpatialNeighbors : 4;
        unsigned int useLowDiscrepancyNeighbors : 1;
        unsigned int reuseVisibility : 1;
        unsigned int reuseVisibilityForTemporal : 1; // only for rearchitected ver.
        unsigned int reuseVisibilityForSpatiotemporal : 1; // only for rearchitected ver.
        unsigned int enableTemporalReuse : 1;
        unsigned int enableSpatialReuse : 1;
        unsigned int useUnbiasedEstimator : 1;
        unsigned int bufferIndex : 1;
        unsigned int resetFlowBuffer : 1;
        unsigned int enableJittering : 1;
        unsigned int enableEnvLight : 1;
        unsigned int enableBumpMapping : 1;

        uint32_t debugSwitches;
        void setDebugSwitch(int32_t idx, bool b) {
            debugSwitches &= ~(1 << idx);
            debugSwitches |= b << idx;
        }
        CUDA_COMMON_FUNCTION bool getDebugSwitch(int32_t idx) const {
            return (debugSwitches >> idx) & 0b1;
        }
    };
    
    struct PipelineLaunchParameters {
        StaticPipelineLaunchParameters* s;
        PerFramePipelineLaunchParameters* f;
        unsigned int currentReservoirIndex : 1;
        unsigned int spatialNeighborBaseIndex : 10;
    };



    enum class BufferToDisplay {
        NoisyBeauty = 0,
        Albedo,
        Normal,
        Flow,
        DenoisedBeauty,
    };



    using PrimaryRayPayloadSignature =
        optixu::PayloadSignature<shared::HitPointParams*, shared::PickInfo*>;
    using VisibilityRayPayloadSignature =
        optixu::PayloadSignature<float>;
}



#if defined(__CUDA_ARCH__) || defined(OPTIXU_Platform_CodeCompletion)

#include "../common/common_device.cuh"

#if defined(PURE_CUDA)
CUDA_CONSTANT_MEM shared::PipelineLaunchParameters plp;
#else
RT_PIPELINE_LAUNCH_PARAMETERS shared::PipelineLaunchParameters plp;
#endif

template <bool useSolidAngleSampling>
CUDA_DEVICE_FUNCTION CUDA_INLINE void sampleLight(
    const float3 &shadingPoint,
    float ul, bool sampleEnvLight, float u0, float u1,
    shared::LightSample* lightSample, float* areaPDensity) {
    CUtexObject texEmittance = 0;
    float3 emittance = make_float3(0.0f, 0.0f, 0.0f);
    float2 texCoord;
    if (sampleEnvLight) {
        float u, v;
        float uvPDF;
        plp.s->envLightImportanceMap.sample(u0, u1, &u, &v, &uvPDF);
        float phi = 2 * Pi * u;
        float theta = Pi * v;

        float posPhi = phi - plp.f->envLightRotation;
        posPhi = posPhi - floorf(posPhi / (2 * Pi)) * 2 * Pi;

        float3 direction = fromPolarYUp(posPhi, theta);
        float3 position = make_float3(direction.x, direction.y, direction.z);
        lightSample->position = position;
        lightSample->atInfinity = true;

        lightSample->normal = -position;

        // JP: テクスチャー空間中のPDFを面積に関するものに変換する。
        // EN: convert the PDF in texture space to one with respect to area.
        // The true value is: lim_{l to inf} uvPDF / (2 * Pi * Pi * std::sin(theta)) / l^2
        *areaPDensity = uvPDF / (2 * Pi * Pi * std::sin(theta));

        texEmittance = plp.s->envLightTexture;
        // JP: 環境マップテクスチャーの値に係数をかけて、通常の光源と同じように返り値を光束発散度
        //     として扱えるようにする。
        // EN: Multiply a coefficient to make the return value possible to be handled as luminous emittance.
        emittance = make_float3(Pi * plp.f->envLightPowerCoeff);
        texCoord.x = u;
        texCoord.y = v;
    }
    else {
        float lightProb = 1.0f;

        // JP: まずはインスタンスをサンプルする。
        // EN: First, sample an instance.
        float instProb;
        float uGeomInst;
        uint32_t instIndex = plp.s->lightInstDist.sample(ul, &instProb, &uGeomInst);
        lightProb *= instProb;
        const InstanceData &inst = plp.f->instanceDataBuffer[instIndex];

        // JP: 次にサンプルしたインスタンスに属するジオメトリインスタンスをサンプルする。
        // EN: Next, sample a geometry instance which belongs to the sampled instance.
        float geomInstProb;
        float uPrim;
        uint32_t geomInstIndexInInst = inst.lightGeomInstDist.sample(uGeomInst, &geomInstProb, &uPrim);
        uint32_t geomInstIndex = inst.geomInstSlots[geomInstIndexInInst];
        lightProb *= geomInstProb;
        const GeometryInstanceData &geomInst = plp.s->geometryInstanceDataBuffer[geomInstIndex];

        // JP: 最後に、サンプルしたジオメトリインスタンスに属するプリミティブをサンプルする。
        // EN: Finally, sample a primitive which belongs to the sampled geometry instance.
        float primProb;
        uint32_t primIndex = geomInst.emitterPrimDist.sample(uPrim, &primProb);
        lightProb *= primProb;

        //printf("%u-%u-%u: %g\n", instIndex, geomInstIndex, primIndex, lightProb);

        const MaterialData &mat = plp.s->materialDataBuffer[geomInst.materialSlot];

        const shared::Triangle &tri = geomInst.triangleBuffer[primIndex];
        const shared::Vertex (&v)[3] = {
            geomInst.vertexBuffer[tri.index0],
            geomInst.vertexBuffer[tri.index1],
            geomInst.vertexBuffer[tri.index2]
        };
        const float3 p[3] = {
            inst.transform * v[0].position,
            inst.transform * v[1].position,
            inst.transform * v[2].position,
        };

        float3 geomNormal = cross(p[1] - p[0], p[2] - p[0]);

        float t0, t1, t2;
        if constexpr (useSolidAngleSampling) {
            // Uniform sampling in solid angle subtended by the triangle for the shading point.
            float dist;
            float3 dir;
            float dirPDF;
            {
                const auto project = [](const float3 &vA, const float3 &vB) {
                    return normalize(vA - dot(vA, vB) * vB);
                };

                // TODO: ? compute in the local coordinates.
                float3 A = normalize(p[0] - shadingPoint);
                float3 B = normalize(p[1] - shadingPoint);
                float3 C = normalize(p[2] - shadingPoint);
                float3 cAB = normalize(cross(A, B));
                float3 cBC = normalize(cross(B, C));
                float3 cCA = normalize(cross(C, A));
                //float cos_a = dot(B, C);
                //float cos_b = dot(C, A);
                float cos_c = dot(A, B);
                float cosAlpha = -dot(cAB, cCA);
                float cosBeta = -dot(cBC, cAB);
                float cosGamma = -dot(cCA, cBC);
                float alpha = std::acos(cosAlpha);
                float sinAlpha = std::sqrt(1 - pow2(cosAlpha));
                float sphArea = alpha + std::acos(cosBeta) + std::acos(cosGamma) - Pi;

                float sphAreaHat = sphArea * u0;
                float s = std::sin(sphAreaHat - alpha);
                float t = std::cos(sphAreaHat - alpha);
                float uu = t - cosAlpha;
                float vv = s + sinAlpha * cos_c;
                float q = ((vv * t - uu * s) * cosAlpha - vv) / ((vv * s + uu * t) * sinAlpha);

                float3 cHat = q * A + std::sqrt(1 - pow2(q)) * project(C, A);
                float z = 1 - u1 * (1 - dot(cHat, B));
                float3 P = z * B + std::sqrt(1 - pow2(z)) * project(cHat, B);

                const auto restoreBarycentrics = [&geomNormal]
                (const float3 &org, const float3 &dir,
                 const float3 &pA, const float3 &pB, const float3 &pC,
                 float* dist, float* b1, float* b2) {
                     float3 eAB = pB - pA;
                     float3 eAC = pC - pA;
                     float3 pVec = cross(dir, eAC);
                     float recDet = 1.0f / dot(eAB, pVec);
                     float3 tVec = org - pA;
                     *b1 = dot(tVec, pVec) * recDet;
                     float3 qVec = cross(tVec, eAB);
                     *b2 = dot(dir, qVec) * recDet;
                     *dist = dot(eAC, qVec) * recDet;
                };
                dir = P;
                restoreBarycentrics(shadingPoint, dir, p[0], p[1], p[2], &dist, &t1, &t2);
                t0 = 1 - t1 - t2;
                dirPDF = 1 / sphArea;
            }

            geomNormal = normalize(geomNormal);
            float lpCos = -dot(dir, geomNormal);
            if (lpCos > 0 && isfinite(dirPDF))
                *areaPDensity = lightProb * (dirPDF * lpCos / pow2(dist));
            else
                *areaPDensity = 0.0f;
        }
        else {
            // Uniform sampling on unit triangle
            // A Low-Distortion Map Between Triangle and Square
            t0 = 0.5f * u0;
            t1 = 0.5f * u1;
            float offset = t1 - t0;
            if (offset > 0)
                t1 += offset;
            else
                t0 -= offset;
            t2 = 1 - (t0 + t1);

            float recArea = 2.0f / length(geomNormal);
            *areaPDensity = lightProb * recArea;
        }
        lightSample->position = t0 * p[0] + t1 * p[1] + t2 * p[2];
        lightSample->atInfinity = false;
        lightSample->normal = t0 * v[0].normal + t1 * v[1].normal + t2 * v[2].normal;
        lightSample->normal = normalize(inst.normalMatrix * lightSample->normal);

        if (mat.emittance) {
            texEmittance = mat.emittance;
            emittance = make_float3(1.0f, 1.0f, 1.0f);
            texCoord = t0 * v[0].texCoord + t1 * v[1].texCoord + t2 * v[2].texCoord;
        }
    }

    if (texEmittance) {
        float4 texValue = tex2DLod<float4>(texEmittance, texCoord.x, texCoord.y, 0.0f);
        emittance *= make_float3(texValue);
    }
    lightSample->emittance = emittance;
}

template <typename RayType, bool withVisibility>
CUDA_DEVICE_FUNCTION CUDA_INLINE float3 performDirectLighting(
    const float3 &shadingPoint, const float3 &vOutLocal, const ReferenceFrame &shadingFrame, const BSDF &bsdf,
    const shared::LightSample &lightSample) {
    float3 shadowRayDir = lightSample.atInfinity ?
        lightSample.position :
        (lightSample.position - shadingPoint);
    float dist2 = sqLength(shadowRayDir);
    float dist = std::sqrt(dist2);
    shadowRayDir /= dist;
    float3 shadowRayDirLocal = shadingFrame.toLocal(shadowRayDir);

    float lpCos = dot(-shadowRayDir, lightSample.normal);
    float spCos = shadowRayDirLocal.z;

    float visibility = 1.0f;
    if constexpr (withVisibility) {
        if (lightSample.atInfinity)
            dist = 1e+10f;
        optixu::trace<shared::VisibilityRayPayloadSignature>(
            plp.f->travHandle,
            shadingPoint, shadowRayDir, 0.0f, dist * 0.9999f, 0.0f,
            0xFF, OPTIX_RAY_FLAG_NONE,
            RayType::Visibility, shared::maxNumRayTypes, RayType::Visibility,
            visibility);
    }

    if (visibility > 0 && lpCos > 0) {
        float3 Le = lightSample.emittance / Pi; // assume diffuse emitter.
        float3 fsValue = bsdf.evaluate(vOutLocal, shadowRayDirLocal);
        float G = lpCos * std::fabs(spCos) / dist2;
        float3 ret = fsValue * Le * G;
        return ret;
    }
    else {
        return make_float3(0.0f, 0.0f, 0.0f);
    }
}

template <typename RayType>
CUDA_DEVICE_FUNCTION CUDA_INLINE bool evaluateVisibility(
    const float3 &shadingPoint, const shared::LightSample &lightSample) {
    float3 shadowRayDir = lightSample.atInfinity ?
        lightSample.position :
        (lightSample.position - shadingPoint);
    float dist2 = sqLength(shadowRayDir);
    float dist = std::sqrt(dist2);
    shadowRayDir /= dist;
    if (lightSample.atInfinity)
        dist = 1e+10f;

    float visibility = 1.0f;
    optixu::trace<shared::VisibilityRayPayloadSignature>(
        plp.f->travHandle,
        shadingPoint, shadowRayDir, 0.0f, dist * 0.9999f, 0.0f,
        0xFF, OPTIX_RAY_FLAG_NONE,
        RayType::Visibility, shared::maxNumRayTypes, RayType::Visibility,
        visibility);

    return visibility > 0.0f;
}

template <bool computeHypotheticalAreaPDensity, bool useSolidAngleSampling>
CUDA_DEVICE_FUNCTION CUDA_INLINE void computeSurfacePoint(
    const shared::InstanceData &inst,
    const shared::GeometryInstanceData &geomInst,
    uint32_t primIndex, float b1, float b2,
    const float3 &referencePoint,
    float3* positionInWorld, float3* shadingNormalInWorld, float3* texCoord0DirInWorld,
    float3* geometricNormalInWorld, float2* texCoord,
    float* hypAreaPDensity) {
    const Triangle &tri = geomInst.triangleBuffer[primIndex];
    const Vertex &v0 = geomInst.vertexBuffer[tri.index0];
    const Vertex &v1 = geomInst.vertexBuffer[tri.index1];
    const Vertex &v2 = geomInst.vertexBuffer[tri.index2];
    const float3 p[3] = {
        optixTransformPointFromObjectToWorldSpace(v0.position),
        optixTransformPointFromObjectToWorldSpace(v1.position),
        optixTransformPointFromObjectToWorldSpace(v2.position),
    };
    float b0 = 1 - (b1 + b2);

    // JP: ヒットポイントのローカル座標中の各値を計算する。
    // EN: Compute hit point properties in the local coordinates.
    float3 position = b0 * v0.position + b1 * v1.position + b2 * v2.position;
    float3 shadingNormal = b0 * v0.normal + b1 * v1.normal + b2 * v2.normal;
    float3 texCoord0Dir = b0 * v0.texCoord0Dir + b1 * v1.texCoord0Dir + b2 * v2.texCoord0Dir;
    float3 geometricNormal = cross(p[1] - p[0], p[2] - p[0]);
    float area;
    if constexpr (computeHypotheticalAreaPDensity && !useSolidAngleSampling)
        area = 0.5f * length(geometricNormal);
    else
        (void)area;
    *texCoord = b0 * v0.texCoord + b1 * v1.texCoord + b2 * v2.texCoord;

    // JP: ローカル座標中の値をワールド座標中の値へと変換する。
    // EN: Convert the local properties to ones in world coordinates.
    *positionInWorld = optixTransformPointFromObjectToWorldSpace(position);
    *shadingNormalInWorld = normalize(optixTransformNormalFromObjectToWorldSpace(shadingNormal));
    *texCoord0DirInWorld = normalize(optixTransformVectorFromObjectToWorldSpace(texCoord0Dir));
    *geometricNormalInWorld = normalize(geometricNormal);
    if (!allFinite(*shadingNormalInWorld)) {
        *shadingNormalInWorld = make_float3(0, 0, 1);
        *texCoord0DirInWorld = make_float3(1, 0, 0);
    }
    if (!allFinite(*texCoord0DirInWorld)) {
        float3 bitangent;
        makeCoordinateSystem(*shadingNormalInWorld, texCoord0DirInWorld, &bitangent);
    }

    if constexpr (computeHypotheticalAreaPDensity) {
        // JP: 交点をExplicit Light Samplingでサンプルする場合の仮想的な確率密度を求める。
        // EN: Compute a hypothetical probability density with which the intersection point
        //     is sampled by explicit light sampling.
        float lightProb = 1.0f;
        if (plp.s->envLightTexture && plp.f->enableEnvLight)
            lightProb *= (1 - probToSampleEnvLight);
        lightProb *= inst.lightGeomInstDist.integral() / plp.s->lightInstDist.integral();
        lightProb *= geomInst.emitterPrimDist.integral() / inst.lightGeomInstDist.integral();
        lightProb *= geomInst.emitterPrimDist.evaluatePMF(primIndex);
        if constexpr (useSolidAngleSampling) {
            // TODO: ? compute in the local coordinates.
            float3 A = normalize(p[0] - referencePoint);
            float3 B = normalize(p[1] - referencePoint);
            float3 C = normalize(p[2] - referencePoint);
            float3 cAB = normalize(cross(A, B));
            float3 cBC = normalize(cross(B, C));
            float3 cCA = normalize(cross(C, A));
            float cosAlpha = -dot(cAB, cCA);
            float cosBeta = -dot(cBC, cAB);
            float cosGamma = -dot(cCA, cBC);
            float sphArea = std::acos(cosAlpha) + std::acos(cosBeta) + std::acos(cosGamma) - Pi;
            float dirPDF = 1.0f / sphArea;
            float3 refDir = referencePoint - *positionInWorld;
            float dist2ToRefPoint = sqLength(refDir);
            refDir /= std::sqrt(dist2ToRefPoint);
            float lpCos = dot(refDir, *geometricNormalInWorld);
            if (lpCos > 0 && isfinite(dirPDF))
                *hypAreaPDensity = lightProb * (dirPDF * lpCos / dist2ToRefPoint);
            else
                *hypAreaPDensity = 0.0f;
        }
        else {
            *hypAreaPDensity = lightProb / area;
        }
    }
    else {
        (void)*hypAreaPDensity;
    }
}



struct HitPointParameter {
    float b1, b2;
    int32_t primIndex;

    CUDA_DEVICE_FUNCTION CUDA_INLINE static HitPointParameter get() {
        HitPointParameter ret;
        float2 bc = optixGetTriangleBarycentrics();
        ret.b1 = bc.x;
        ret.b2 = bc.y;
        ret.primIndex = optixGetPrimitiveIndex();
        return ret;
    }
};

struct HitGroupSBTRecordData {
    shared::GeometryInstanceData geomInstData;

    CUDA_DEVICE_FUNCTION CUDA_INLINE static const HitGroupSBTRecordData &get() {
        return *reinterpret_cast<HitGroupSBTRecordData*>(optixGetSbtDataPointer());
    }
};

template <bool testGeometry>
CUDA_DEVICE_FUNCTION CUDA_INLINE bool testNeighbor(
    uint32_t nbBufIdx, int2 nbCoord, float dist, const float3 &normalInWorld) {
    using namespace shared;
    if (nbCoord.x < 0 || nbCoord.x >= plp.s->imageSize.x ||
        nbCoord.y < 0 || nbCoord.y >= plp.s->imageSize.y)
        return false;

    GBuffer2 nbGBuffer2 = plp.s->GBuffer2[nbBufIdx].read(nbCoord);
    if (nbGBuffer2.materialSlot == 0xFFFFFFFF)
        return false;

    if constexpr (testGeometry) {
        GBuffer0 nbGBuffer0 = plp.s->GBuffer0[nbBufIdx].read(nbCoord);
        GBuffer1 nbGBuffer1 = plp.s->GBuffer1[nbBufIdx].read(nbCoord);
        float3 nbPositionInWorld = nbGBuffer0.positionInWorld;
        float3 nbNormalInWorld = nbGBuffer1.normalInWorld;
        float nbDist = length(plp.f->camera.position - nbPositionInWorld);
        if (abs(nbDist - dist) / dist > 0.1f || dot(normalInWorld, nbNormalInWorld) < 0.9f)
            return false;
    }

    return true;
}

#endif
