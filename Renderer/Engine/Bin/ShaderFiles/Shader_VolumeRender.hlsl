
#include "Engine_Shader_Defines.hpp"

matrix			g_WorldMatrix, g_ViewMatrix, g_ProjMatrix;
matrix			g_ProjMatrixInv;
matrix			g_ViewMatrixInv;
matrix			g_PrevViewProj;

vector			g_vCamPosition;

texture3D		g_ShapeTexture;
texture3D		g_DetailTexture;

texture2D		g_BlueNoiseTexture;
texture2D		g_CurlNoiseTexture;

texture2D		g_PrevFrameTexture;

texture2D		g_TransLUTTexture;
texture3D		g_AerialLUTTexture;

float3			g_vLightPos;

float			g_fMaxHeight = 2000.0f;
float			g_fMinHeight = 1000.0f;

uint			g_iMaxStep;

uint			g_iSunStep;
float			g_fSunStepLength;

float			g_fAbsorption;
float			g_fDetailNoiseScale;

int				g_iUpdatePixel;
int				g_iGridSize;

uint				g_iWinSizeX;
uint				g_iWinSizeY;

float			g_fAerialMaxDepth = 60000.0f;

bool			g_bAerial;

float			g_fCoverage;

cbuffer AtmosphereParams : register(b0)
{
	float4	vScatterRayleigh;
	float	fHDensityRayleigh;

	float	fScatterMie;
	float	fPhaseMieG;
	float	fExtinctionMie;
	float	fHDensityMie;

	float	fEarthRadius;
	float	fAtmosphereRadius;

	float	fSunIlluminance;

	float4	vAbsorbOzone;
 	float4	vOzone;

	float	fMultiScatFactor;
}

struct VS_IN
{
	float3		vPosition : POSITION;
	float2		vTexcoord : TEXCOORD0;
};

struct VS_OUT
{	
	float4		vPosition : SV_POSITION;
	float2		vTexcoord : TEXCOORD0;
};

void LutTransmittanceParamsToUv(in float fViewHeight, in float fViewZenithCosAngle, out float2 vUV)
{
	float fH = sqrt(max(0.0f, fAtmosphereRadius * fAtmosphereRadius - fEarthRadius * fEarthRadius));
	float fRho = sqrt(max(0.0f, fViewHeight * fViewHeight - fEarthRadius * fEarthRadius));

	float fDiscriminant = fViewHeight * fViewHeight * (fViewZenithCosAngle * fViewZenithCosAngle - 1.0f) + fAtmosphereRadius * fAtmosphereRadius;
	float fD = max(0.0f, (-fViewHeight * fViewZenithCosAngle + sqrt(fDiscriminant)));

	float fMin = fAtmosphereRadius - fViewHeight;
	float fMax = fRho + fH;
	float fX = (fD - fMin) / (fMax - fMin);
	float fY = fRho / fH;

	vUV = float2(fX, fY);
}

VS_OUT VS_MAIN(VS_IN In)
{
	VS_OUT			Out = (VS_OUT)0;
	
	matrix			matWV, matWVP;

	matWV = mul(g_WorldMatrix, g_ViewMatrix);
	matWVP = mul(matWV, g_ProjMatrix);

    Out.vTexcoord = In.vTexcoord;
	Out.vPosition = mul(float4(In.vPosition, 1.f), matWVP);

	return Out;
}

struct PS_IN
{
	float4		vPosition : SV_POSITION;
	float2		vTexcoord : TEXCOORD0;
};


struct PS_OUT
{
	float4	vColor : SV_TARGET0;
};

float remap(float fValue, float fIn1, float fIn2, float fOut1, float fOut2)
{
    return fOut1 + (fValue - fIn1) * (fOut2 - fOut1) / (fIn2 - fIn1);
}

float Blue_Noise(float2 vTexcoord, float fStepLength)
{
	return g_BlueNoiseTexture.Sample(LinearSampler, vTexcoord).x * (fStepLength * 0.5f);
}

float3 Ray_Sphere_Intersection(float3 vOrigin, float3 vRayDir, float3 vCenter, float fRadius)
{
    float3 l = vOrigin - vCenter;
	float a = 1.0f;
	float b = 2.0f * dot(vRayDir, l);
	float c = dot(l, l) - pow(fRadius, 2);
	float D = pow(b, 2) - 4.0f * a * c;
	
    if (D < 0.0f)
		return vOrigin;
	else if (abs(D) - 0.00005f <= 0.0f)
		return vOrigin + vRayDir * (-0.5f * b / a);
	else
	{
		float q = 0.0f;
		if (b > 0.0f) 
            q = -0.5f * (b + sqrt(D));
		else 
            q = -0.5f * (b - sqrt(D));
		
		float h1 = q / a;
		float h2 = c / q;
		float2 t = float2(min(h1, h2), max(h1, h2));

		if (t.x < 0.0f) 
        {
			t.x = t.y;
			
            if (t.x < 0.0f)
				return vOrigin;
		}
        
		return vOrigin + t.x * vRayDir;
	}
}


float Height_Fraction(float3 vWorldPos)
{
	float3 vEarthCenter = float3(0.0f, -fEarthRadius, 0.0f);
	return  clamp((distance(vWorldPos,  vEarthCenter) - (g_fMinHeight + fEarthRadius)) / (g_fMaxHeight - g_fMinHeight), 0.0f, 1.0f);
}


float Beer_Law(float fDensity)
{
	float fD = fDensity * -g_fAbsorption;
	return max(exp(fD), exp(fD * 0.5f) * 0.7f);
}

float Beer_Lambert_Law(float fDensity)
{
    return exp(fDensity * -g_fAbsorption);
}

float Henyey_Greenstein_Phase(float fCos, float fG)
{
	float fG2 = fG * fG;
	return ((1.0f - fG2) / pow(1.0f + fG2 - 2.0f * fG * fCos, 1.5f)) / (4.0f * 3.1415f);
}

float Powder_Effect(float fDensity, float fCos)
{
	float fPowder = 1.0f - exp(-fDensity * 2.0f);
	return lerp(1.0f, fPowder, clamp((-fCos * 0.5f) + 0.5f, 0.0f, 1.0f));
}

float Calculate_Light_Energy(float fDensity, float fPowderDensity) 
{ 
	float fBeer = 2.0f * Beer_Law(fDensity);
	return fBeer;
}

float Sample_CloudDensity(float3 vWorldPos)
{
	float3 vTexcoord = vWorldPos * 0.0001f;

	float fHeightFraction = Height_Fraction(vWorldPos);


	float4 vSample = g_ShapeTexture.SampleLevel(CloudSampler, vTexcoord, 0.0f);

	float fWfbm = vSample.y * 0.625f + vSample.z * 0.25f + vSample.w * 0.125f;

    float fDensity = remap(vSample.x, fWfbm - 1.0f, 1.0f, 0.0f, 1.0f);


	fDensity *= saturate(remap(fHeightFraction, 0.0f, 0.2f, 0.0f, 1.0f))
           * saturate(remap(fHeightFraction, 0.75f, 1.0f, 1.0f, 0.0f));


	float fCoverage = lerp(g_fCoverage, 1.0f, fHeightFraction);

    fDensity = remap(fDensity, fCoverage, 1.0f, 0.0f, 1.0f);
	fDensity *= 0.1f;
	
 	if (fHeightFraction < 0.2f)
 	{
 		fDensity *= fHeightFraction / 0.2f;
 	}

	float4 vDetail = g_DetailTexture.SampleLevel(CloudSampler, vTexcoord * 14.0f, 0.0f);
 	float fDetailfbm = vDetail.x * 0.625f + vDetail.y * 0.25f + vDetail.z * 0.125f;
 
 	fDensity -= saturate(fDetailfbm) * g_fDetailNoiseScale;
   
	return saturate(fDensity);
}


float Calculate_LightDensity(float3 vStart)
{
	float fSunDensity = 0.0f;

	float3 vDir = normalize(g_vLightPos - vStart);
	
	for (int i = 0; i < g_iSunStep; ++i)
	{
		vStart += vDir * g_fSunStepLength;

		float fSampleDensity = Sample_CloudDensity(vStart);
			
		fSunDensity += fSampleDensity * g_fSunStepLength;
	}
	

	return fSunDensity;
}


float4 RayMarch(float3 vStartPos, float3 vRayDir, float fMaxStepLength, float3 vOrigin, out float fAerialDepth)
{
	float fAccum_Transmittance = 1.0f;
	
	float3 vResultColor = float3(0.0f, 0.0f, 0.0f);
	float fTotalDensity = 0.0f;
	float fTotalTrans = 0.0f;
	float fTotalTransDepth = 0.0f;
	float fStepLength = fMaxStepLength;
	float fMinStepLength = 30.0f;

	float3 vLightDir = normalize(g_vLightPos - vStartPos);
	float fCos = dot(vRayDir, vLightDir);
	float fHG = lerp(Henyey_Greenstein_Phase(fCos, 0.8f), Henyey_Greenstein_Phase(fCos, -0.5f), 0.5f);

	for (int i = 0; i < g_iMaxStep; ++i)
	{
		vStartPos += vRayDir * fStepLength;
		

		if (vStartPos.y > 10.0f)
		{
			float fSampleDensity = Sample_CloudDensity(vStartPos);
			
			float fStep_Transmittance = Beer_Lambert_Law(fSampleDensity * fStepLength);

			if (fSampleDensity > 0.0f)
			{
 				if (fStepLength == fMaxStepLength && fStepLength > fMinStepLength)
 				{
 					--i;
 					vStartPos -= vRayDir * fStepLength;
 					fStepLength = fMinStepLength;
 					continue;
 				}

				
				
				
				float fSunDensity = Calculate_LightDensity(vStartPos);
			
				float3 vScatteredLight = Calculate_Light_Energy(fSunDensity, fSampleDensity * fStepLength) * fHG;

				float3 vWorldPos = vStartPos + float3(0.0f, fEarthRadius, 0.0f);

				float fViewHeight = length(vWorldPos);
				const float3 vUpVector = vWorldPos / fViewHeight;
				float fSunZenithCosAngle = dot(vLightDir, vUpVector);
				float2 vUV;

				LutTransmittanceParamsToUv(fViewHeight, fSunZenithCosAngle, vUV);
				const float3 vTrans = g_TransLUTTexture.SampleLevel(LinearClampSampler, vUV, 0).rgb;
				
				vResultColor += vScatteredLight * vTrans * fAccum_Transmittance * fSampleDensity * fStepLength;
				fAccum_Transmittance *= fStep_Transmittance;
			}
			else
			{
				fStepLength = fMaxStepLength;
			}

 			if (fAccum_Transmittance < 0.0001f)
 			{
 				break;
 			}

			fTotalTrans += (1.0f - fStep_Transmittance);
			fTotalTransDepth += (1.0f - fStep_Transmittance) * (distance(vStartPos, vOrigin) / g_fAerialMaxDepth);
		}

	}

	vResultColor *= fSunIlluminance;
	float fAlpha = (1.0f - fAccum_Transmittance);

	fAerialDepth = fTotalTrans > 0.0f ? fTotalTransDepth / fTotalTrans : 0.0f;

	return float4(vResultColor, fAlpha);
}

float4 Get_Cloud(float3 vWorldPos, float3 vRayDir, float2 vTexcoord)
{
	float3		vEarthCenter = float3(0.0f, -fEarthRadius, 0.0f);

	float3		vStart = Ray_Sphere_Intersection(vWorldPos, vRayDir, vEarthCenter, fEarthRadius + g_fMinHeight);
	float3		vEnd = Ray_Sphere_Intersection(vWorldPos, vRayDir, vEarthCenter, fEarthRadius + g_fMaxHeight);

	float		fLength = distance(vStart, vEnd);
	float		fStepLength = fLength / (float)g_iMaxStep;

	float		fAerialDepth = 0.0f;

 	if (distance(vEnd, g_vCamPosition) < 50000.0f)
 	{
		float3 vWorldPos = vStart + vRayDir * Blue_Noise(vTexcoord, fStepLength);
		float4 vCloud = RayMarch(vWorldPos, vRayDir, fStepLength, vStart, fAerialDepth);

		if (g_bAerial && fAerialDepth > 0.0f)
		{	
 			const float4 vAP = g_AerialLUTTexture.SampleLevel(LinearClampSampler, float3(vTexcoord, fAerialDepth * 20.0f), 0);
 			vCloud.rgb += (vAP.rgb * fSunIlluminance);
		}

 		return vCloud;
 	}
	
	return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

PS_OUT PS_MAIN_CLOUD(PS_IN In)
{
	PS_OUT		Out = (PS_OUT)0;

	int iX = In.vTexcoord.x * g_iWinSizeX;
	int iY = In.vTexcoord.y * g_iWinSizeY;
	int iPixelIndex = ((iX % g_iGridSize) * g_iGridSize) + iY % g_iGridSize;

	vector		vClipPos;

	vClipPos.x = In.vTexcoord.x * 2.f - 1.f;
	vClipPos.y = In.vTexcoord.y * -2.f + 1.f;
	vClipPos.z = 1.0f;
	vClipPos.w = 1.f;

	vClipPos = vClipPos * 1000.0f;
	vClipPos = mul(vClipPos, g_ProjMatrixInv);

	vClipPos = mul(vClipPos, g_ViewMatrixInv);

	float3		vWorldPos = vClipPos.xyz;


	if (g_iUpdatePixel == iPixelIndex)
	{
		float3		vRayDir = normalize(vWorldPos - g_vCamPosition.xyz);
		vWorldPos = g_vCamPosition.xyz;

		Out.vColor = Get_Cloud(vWorldPos, vRayDir, In.vTexcoord);
	}
	else
	{
		float4 vPrevWorldPos = mul(vClipPos, g_PrevViewProj);
		float3 vProjCoord = vPrevWorldPos.xyz / vPrevWorldPos.w;

		vProjCoord.x = vProjCoord.x * 0.5f + 0.5f;
		vProjCoord.y = vProjCoord.y * -0.5f + 0.5f;

		if (vProjCoord.x < 0.0f || vProjCoord.x > 1.0f || vProjCoord.y < 0.0f || vProjCoord.y > 1.0f)
		{
			float3		vRayDir = normalize(vWorldPos - g_vCamPosition.xyz);
			vWorldPos = g_vCamPosition.xyz;

			Out.vColor = Get_Cloud(vWorldPos, vRayDir, In.vTexcoord);
		}
		else
		{
			Out.vColor = g_PrevFrameTexture.Sample(ReProjectionSampler, float2(vProjCoord.xy));
		}
	}
	
	return Out;
}


PS_OUT PS_MAIN_PERLINWORLEY2D(PS_IN In)
{
	PS_OUT		Out = (PS_OUT)0;

	float4 vSample = g_ShapeTexture.Sample(CloudSampler, float3(In.vTexcoord.x * 2.0f, In.vTexcoord.y * 2.0f, 0.0f));

	float fWfbm = vSample.y * 0.625f + vSample.z * 0.25f + vSample.w * 0.125f;

    float fDensity = remap(vSample.x, fWfbm - 1.0f, 1.0f, 0.0f, 1.0f);

    fDensity = remap(fDensity, 0.8f, 1.0f, 0.0f, 1.0f);
	fDensity *= 0.5f;

	Out.vColor = float4(fDensity, fDensity, fDensity, 1.0f);

	return Out;
}

technique11 DefaultTechnique
{
	pass CloudRender
	{
		SetRasterizerState(RS_Default);
		SetDepthStencilState(DSS_None, 0);
		SetBlendState(BS_Default, float4(0.f, 0.f, 0.f, 1.f), 0xffffffff);

		VertexShader = compile vs_5_0 VS_MAIN();
		GeometryShader = NULL;
		HullShader = NULL;
		DomainShader = NULL;
		PixelShader = compile ps_5_0 PS_MAIN_CLOUD();
	}

	pass PerlinWorley2D
	{
		SetRasterizerState(RS_Default);
		SetDepthStencilState(DSS_None, 0);
		SetBlendState(BS_AlphaBlend, float4(0.f, 0.f, 0.f, 1.f), 0xffffffff);

		VertexShader = compile vs_5_0 VS_MAIN();
		GeometryShader = NULL;
		HullShader = NULL;
		DomainShader = NULL;
		PixelShader = compile ps_5_0 PS_MAIN_PERLINWORLEY2D();
	}
}




