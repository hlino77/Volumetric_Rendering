#include "..\Public\Sky.h"
#include "GameInstance.h"
#include "TransmittanceLUT.h"
#include "Target_Manager.h"
#include "Renderer.h"
#include "MultiScatLUT.h"
#include "AerialLUT.h"
#include "Light_Manager.h"
#include "Cloud.h"

CSky::CSky(ID3D11Device * pDevice, ID3D11DeviceContext * pContext)
	: CGameObject(pDevice, pContext)
	, m_pTarget_Manager(CTarget_Manager::GetInstance())
{
	Safe_AddRef(m_pTarget_Manager);
}

CSky::CSky(const CSky& rhs)
	: CGameObject(rhs)
	, m_pTarget_Manager(CTarget_Manager::GetInstance())
	, m_WorldMatrix(rhs.m_WorldMatrix)
	, m_ViewMatrix(rhs.m_ViewMatrix)
	, m_ProjMatrix(rhs.m_ProjMatrix)
{
	Safe_AddRef(m_pTarget_Manager);
}

HRESULT CSky::Initialize_Prototype()
{
	m_WorldMatrix = XMMatrixIdentity();
	m_WorldMatrix._11 = m_iWinSizeX;
	m_WorldMatrix._22 = m_iWinSizeY;


	m_ViewMatrix = XMMatrixIdentity();
	m_ProjMatrix = XMMatrixOrthographicLH(m_iWinSizeX, m_iWinSizeY, 0.f, 1.f);

	m_strName = L"Sky";
	

	return S_OK;
}

HRESULT CSky::Initialize(void* pArg)
{
	m_tUnitAtmo = m_tUnitAtmo.ToStdUnit();
	
	if (FAILED(Ready_RenderTargets()))
	{
		return E_FAIL;
	}

	if (FAILED(Ready_AtmosphereBuffer()))
	{
		return E_FAIL;
	}

	if (FAILED(Ready_For_LUT()))
	{
		return E_FAIL;
	}

	if (FAILED(Ready_Components()))
	{
		return E_FAIL;
	}

	if (FAILED(Ready_Sun()))
	{
		return E_FAIL;
	}

	ZeroMemory(&m_SkyLUTViewPortDesc, sizeof(D3D11_VIEWPORT));
	m_SkyLUTViewPortDesc.TopLeftX = 0;
	m_SkyLUTViewPortDesc.TopLeftY = 0;
	m_SkyLUTViewPortDesc.Width = (_float)m_iSkyLUTX;
	m_SkyLUTViewPortDesc.Height = (_float)m_iSkyLUTY;
	m_SkyLUTViewPortDesc.MinDepth = 0.f;
	m_SkyLUTViewPortDesc.MaxDepth = 1.f;

	ZeroMemory(&m_MultiScatLUTViewPortDesc, sizeof(D3D11_VIEWPORT));
	m_MultiScatLUTViewPortDesc.TopLeftX = 0;
	m_MultiScatLUTViewPortDesc.TopLeftY = 0;
	m_MultiScatLUTViewPortDesc.Width = (_float)32;
	m_MultiScatLUTViewPortDesc.Height = (_float)32;
	m_MultiScatLUTViewPortDesc.MinDepth = 0.f;
	m_MultiScatLUTViewPortDesc.MaxDepth = 1.f;



	m_pCloud = CCloud::Create(m_pDevice, m_pContext, m_pRendererCom);
	if (m_pCloud == nullptr)
	{
		return E_FAIL;
	}

	CGameInstance* pGameInstance = GET_INSTANCE(CGameInstance);

	pGameInstance->Add_GPUTimer(L"SkyViewLUT");
	pGameInstance->Add_GPUTimer(L"AtmosphereRender");

	RELEASE_INSTANCE(CGameInstance);
	return S_OK;
}

void CSky::PriorityTick(_float fTimeDelta)
{
	
}

void CSky::Tick(_float fTimeDelta)
{
	Update_Sun(fTimeDelta);

	m_pCloud->Tick(fTimeDelta);
}

void CSky::LateTick(_float fTimeDelta)
{
	Update_Atmosphere();	

	m_pMultiScatLUT->Update_MultiScatteringLUT(&m_pAtmosphereBuffer, &m_pTransLUTSRV);


	m_pRendererCom->Add_RenderGroup(CRenderer::RG_SKY, this);
}

HRESULT CSky::Render()
{
	CGameInstance* pGameInstance = GET_INSTANCE(CGameInstance);

	D3D11_VIEWPORT		PrevVeiwPort;
	_uint				iNumViewports = 1;
	m_pContext->RSGetViewports(&iNumViewports, &PrevVeiwPort);

	
	//AerialLUT
	m_pContext->RSSetViewports(1, &PrevVeiwPort);

	ID3D11ShaderResourceView* pMultiScatLUT = m_pMultiScatLUT->Get_SRV();
	if (FAILED(m_pAerialLUT->Update_AerialLUT(&m_pAtmosphereBuffer, &m_pTransLUTSRV, m_vSunPos, &pMultiScatLUT)))
	{
		return E_FAIL;
	}

	//Sky_View LUT

	m_pContext->RSSetViewports(1, &m_SkyLUTViewPortDesc);

	if (FAILED(m_pTarget_Manager->Begin_MRT(m_pContext, L"MRT_SkyViewLUT")))
		return E_FAIL;

	pGameInstance->Start_GPUTimer(L"SkyViewLUT");

	if (FAILED(m_pShader->Bind_Matrix("g_WorldMatrix", &m_WorldMatrix)))
		return E_FAIL;
	if (FAILED(m_pShader->Bind_Matrix("g_ViewMatrix", &m_ViewMatrix)))
		return E_FAIL;
	if (FAILED(m_pShader->Bind_Matrix("g_ProjMatrix", &m_ProjMatrix)))
		return E_FAIL;

	CPipeLine* pPipeLine = GET_INSTANCE(CPipeLine);

	if (FAILED(m_pShader->Bind_Matrix("g_ViewMatrixInv", &pPipeLine->Get_Transform_Matrix_Inverse(CPipeLine::D3DTS_VIEW))))
		return E_FAIL;
	if (FAILED(m_pShader->Bind_Matrix("g_ProjMatrixInv", &pPipeLine->Get_Transform_Matrix_Inverse(CPipeLine::D3DTS_PROJ))))
		return E_FAIL;

	if (FAILED(m_pShader->Bind_RawValue("g_vCamPosition", &pPipeLine->Get_CamPosition(), sizeof(Vec3))))
		return E_FAIL;

	if (FAILED(m_pShader->Bind_RawValue("g_vSunPos", &m_vSunPos, sizeof(Vec3))))
		return E_FAIL;

	if (FAILED(m_pShader->Bind_Texture("g_TransLUTTexture", m_pTransLUTSRV)))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Bind_Texture("g_MultiScatLUTTexture", m_pMultiScatLUT->Get_SRV())))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Bind_RawValue("g_iWinSizeX", &m_iWinSizeX, sizeof(_uint))))
	{
		return E_FAIL;
	}
	if (FAILED(m_pShader->Bind_RawValue("g_iWinSizeY", &m_iWinSizeY, sizeof(_uint))))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Bind_ConstantBuffer("AtmosphereParams", m_pAtmosphereBuffer)))
	{
		return E_FAIL;
	}

	RELEASE_INSTANCE(CPipeLine);

	if (FAILED(m_pShader->Begin(1)))
		return E_FAIL;

	if (FAILED(m_pVIBuffer->Render()))
		return E_FAIL;

	pGameInstance->End_GPUTimer(L"SkyViewLUT");

	if (FAILED(m_pTarget_Manager->End_MRT(m_pContext)))
		return E_FAIL;

	m_pContext->RSSetViewports(1, &PrevVeiwPort);

	//Cloud
	if (FAILED(m_pCloud->Render(m_vSunPos, m_pAtmosphereBuffer, m_pTransLUTSRV, m_pAerialLUT->Get_SRV(), m_bAerial)))
		return E_FAIL;


	//Atmosphere

	if (FAILED(m_pTarget_Manager->Begin_MRT(m_pContext, L"MRT_Atmosphere")))
		return E_FAIL;

	pGameInstance->Start_GPUTimer(L"AtmosphereRender");

	if (FAILED(m_pShader->Bind_RawValue("g_fTest", &m_fTest, sizeof(_float))))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Bind_RawValue("g_bAerial", &m_bAerial, sizeof(_bool))))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Bind_Texture("g_AerialLUTTexture", m_pAerialLUT->Get_SRV())))
	{
		return E_FAIL;
	}

	if (FAILED(m_pTarget_Manager->Bind_SRV(m_pShader, TEXT("Target_Depth"), "g_DepthTexture")))
	{
		return E_FAIL;
	}

	if (FAILED(m_pTarget_Manager->Bind_SRV(m_pShader, L"Target_SkyViewLUT", "g_SkyViewLUTTexture")))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Begin(0)))
		return E_FAIL;

	if (FAILED(m_pVIBuffer->Render()))
		return E_FAIL;

	pGameInstance->End_GPUTimer(L"AtmosphereRender");

	if (FAILED(m_pTarget_Manager->Bind_SRV(m_pShader, m_pCloud->Get_RenderTargetTag(), "g_CloudTexture")))
	{
		return E_FAIL;
	}

	if (FAILED(m_pShader->Begin(3)))
		return E_FAIL;

	if (FAILED(m_pVIBuffer->Render()))
		return E_FAIL;


	if (FAILED(m_pTarget_Manager->End_MRT(m_pContext)))
		return E_FAIL;

	RELEASE_INSTANCE(CGameInstance);

	return S_OK;
}

void CSky::AfterRenderTick()
{
	m_tPerformance.fCloudRender = CGameInstance::GetInstance()->Compute_GPUTimer(L"CloudRendering");
	m_tPerformance.fAerialLUT = CGameInstance::GetInstance()->Compute_GPUTimer(L"AerialLUT");
	m_tPerformance.fMultiScatLUT = CGameInstance::GetInstance()->Compute_GPUTimer(L"MultiScattLUT");
	m_tPerformance.fSkyViewLUT = CGameInstance::GetInstance()->Compute_GPUTimer(L"SkyViewLUT");
	m_tPerformance.fAtmosphereRender = CGameInstance::GetInstance()->Compute_GPUTimer(L"AtmosphereRender");
}

CloudParams CSky::Get_CloudParams()
{
	return m_pCloud->Get_CloudParams();
}

void CSky::Set_CloudParams(const CloudParams& tCloud)
{
	m_pCloud->Set_CloudParams(tCloud);
}

HRESULT CSky::Ready_For_LUT()
{
	CTransmittanceLUT* pTransLUT = CTransmittanceLUT::Create(m_pDevice, m_pContext);

 	m_pTransLUTSRV = pTransLUT->Generate_TransmittanceLUT(m_tUnitAtmo);

	if (m_pTransLUTSRV == nullptr)
	{
		return E_FAIL;
	}

	Safe_Release(pTransLUT);

	CLight_Manager::GetInstance()->Set_TransLUT(m_pTransLUTSRV);


	m_pMultiScatLUT = CMultiScatLUT::Create(m_pDevice, m_pContext);

	if (m_pMultiScatLUT == nullptr)
	{
		return E_FAIL;
	}

	CLight_Manager::GetInstance()->Set_MultiScatLUT(m_pMultiScatLUT->Get_SRV());

	m_pAerialLUT = CAerialLUT::Create(m_pDevice, m_pContext);
	
	if (m_pAerialLUT == nullptr)
	{
		return E_FAIL;
	}

	return S_OK;
}

HRESULT CSky::Ready_Components()
{
	m_pShader = CShader::Create(m_pDevice, m_pContext, TEXT("../Bin/ShaderFiles/Shader_AtmosphereRender.hlsl"), VTXPOSTEX::Elements, VTXPOSTEX::iNumElements);
	if (nullptr == m_pShader)
		return E_FAIL;

	m_pVIBuffer = CVIBuffer_Rect::Create(m_pDevice, m_pContext);
	if (nullptr == m_pVIBuffer)
		return E_FAIL;

	/* Com_Renderer */
	if (FAILED(__super::Add_Component(0, TEXT("Prototype_Component_Renderer"),
		TEXT("Com_Renderer"), (CComponent**)&m_pRendererCom)))
		return E_FAIL;

	return S_OK;
}

HRESULT CSky::Ready_RenderTargets()
{
	if (FAILED(m_pTarget_Manager->Add_RenderTarget(m_pDevice, m_pContext, L"Target_SkyViewLUT",
		m_iSkyLUTX, m_iSkyLUTY, DXGI_FORMAT_R32G32B32A32_FLOAT, Vec4(0.0f, 0.0f, 0.0f, 0.f))))
		return E_FAIL;

	if (FAILED(m_pTarget_Manager->Add_MRT(L"MRT_SkyViewLUT", L"Target_SkyViewLUT")))
		return E_FAIL;

// 	if (FAILED(m_pTarget_Manager->Ready_Debug(TEXT("Target_SkyViewLUT"), m_iWinSizeX * 0.25f, m_iWinSizeY * 0.25f, m_iWinSizeX * 0.5f, m_iWinSizeY * 0.5f)))
// 		return E_FAIL;

	if (FAILED(m_pTarget_Manager->Add_RenderTarget(m_pDevice, m_pContext, L"Target_Atmosphere",
		m_iWinSizeX, m_iWinSizeY, DXGI_FORMAT_R32G32B32A32_FLOAT, Vec4(0.0f, 0.0f, 0.0f, 0.f))))
		return E_FAIL;

	if (FAILED(m_pTarget_Manager->Add_MRT(L"MRT_Atmosphere", L"Target_Atmosphere")))
		return E_FAIL;

	if (FAILED(m_pTarget_Manager->Add_RenderTarget(m_pDevice, m_pContext, L"Target_MultiScatLUT",
		32, 32, DXGI_FORMAT_R32G32B32A32_FLOAT, Vec4(0.0f, 0.0f, 0.0f, 0.f))))
		return E_FAIL;

	if (FAILED(m_pTarget_Manager->Add_MRT(L"MRT_MultiScatLUT", L"Target_MultiScatLUT")))
		return E_FAIL;

// 	if (FAILED(m_pTarget_Manager->Ready_Debug(TEXT("Target_MultiScatLUT"), 100.0f, m_iWinSizeY * 0.5f + 100.0f, 200.0f, 200.0f)))
// 		return E_FAIL;

	return S_OK;
}

HRESULT CSky::Ready_AtmosphereBuffer()
{
	D3D11_BUFFER_DESC tBufferDesc;
	ZeroMemory(&tBufferDesc, sizeof(D3D11_BUFFER_DESC));
	tBufferDesc.ByteWidth = sizeof(AtmosphereProperties);
	tBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	tBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	tBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA tData;
	ZeroMemory(&tData, sizeof(D3D11_SUBRESOURCE_DATA));
	tData.pSysMem = &m_tUnitAtmo;

	if (FAILED(m_pDevice->CreateBuffer(&tBufferDesc, &tData, &m_pAtmosphereBuffer)))
	{
		return E_FAIL;
	}

	CLight_Manager::GetInstance()->Set_AtmosphereBuffer(m_pAtmosphereBuffer);

	return S_OK;
}

HRESULT CSky::Ready_Sun()
{
	m_vSunPos = Vec3(-1.0f, 1.0f, -1.0f) * 6300e5;

	CLight_Manager::GetInstance()->Set_SunPos(m_vSunPos);

	Matrix LightView = XMMatrixLookAtLH(m_vSunPos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
	Matrix LightProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), m_iWinSizeX/(_float)m_iWinSizeY, 0.2f, 630010000.f);

	CPipeLine::GetInstance()->Set_Transform(CPipeLine::D3DTS_LIGHTVIEW, LightView);
	CPipeLine::GetInstance()->Set_Transform(CPipeLine::D3DTS_LIGHTPROJ, LightProj);
	
	return S_OK;
}

void CSky::Update_Sun(_float fTimeDelta)
{
	CGameInstance* pGameInstance = GET_INSTANCE(CGameInstance);

	_long	MouseMove = 0l;

	if (pGameInstance->Get_DIKeyState(DIK_LCONTROL) & 0x80)
	{
		if (MouseMove = pGameInstance->Get_DIMouseMove(CInput_Device::MMS_X))
		{
			Vec3 vUp(0.0f, 1.0f, 0.0f);

			Matrix RotateMatrix = Matrix::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(vUp, MouseMove * 0.001f));

			m_vSunPos = XMVector3Transform(m_vSunPos, RotateMatrix);
			CLight_Manager::GetInstance()->Set_SunPos(m_vSunPos);
		}

		if (MouseMove = pGameInstance->Get_DIMouseMove(CInput_Device::MMS_Y))
		{
			Vec3 vUp(0.0f, 1.0f, 0.0f);
			Vec3 vLook = m_vSunPos - Vec3(0.0f, -m_tUnitAtmo.fPlanetRadius, 0.0f);
			vLook.Normalize();
			Vec3 vRight = XMVector3Cross(vUp, vLook);

			Matrix RotateMatrix = Matrix::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(vRight, MouseMove * 0.001f));

			m_vSunPos = XMVector3Transform(m_vSunPos, RotateMatrix);
			CLight_Manager::GetInstance()->Set_SunPos(m_vSunPos);
		}
	}


	//Test
	if (pGameInstance->Get_DIKeyState(DIK_O) & 0x80)
	{
		m_fTest = max(0.0f, m_fTest - 0.0004f);
	}

	if (pGameInstance->Get_DIKeyState(DIK_P) & 0x80)
	{
		m_fTest = min(1.0f, m_fTest + 0.0004f);
	}


	Matrix LightView = XMMatrixLookAtLH(m_vSunPos, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
	CPipeLine::GetInstance()->Set_Transform(CPipeLine::D3DTS_LIGHTVIEW, LightView);

	RELEASE_INSTANCE(CGameInstance);

}

void CSky::Update_Atmosphere()
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));
	m_pContext->Map(m_pAtmosphereBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	memcpy(mappedResource.pData, &m_tUnitAtmo, sizeof(AtmosphereProperties));
	m_pContext->Unmap(m_pAtmosphereBuffer, 0);
}


CGameObject* CSky::Clone(void* pArg)
{
	CSky* pInstance = new CSky(*this);

	if (FAILED(pInstance->Initialize(pArg)))
	{
		MSG_BOX("Failed to Cloned : CTerrain");
		Safe_Release(pInstance);
	}

	return pInstance;
}

CSky* CSky::Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	CSky* pInstance = new CSky(pDevice, pContext);

	if (FAILED(pInstance->Initialize_Prototype()))
	{
		MSG_BOX("Failed to Created : CCloud");
		Safe_Release(pInstance);
	}

	return pInstance;
}

void CSky::Free()
{
	__super::Free();

	Safe_Release(m_pShader);
	Safe_Release(m_pMultiScatLUT);

	Safe_Release(m_pTransLUTSRV);

	Safe_Release(m_pDevice);
	Safe_Release(m_pContext);
}
