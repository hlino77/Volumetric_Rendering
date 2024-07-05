#pragma once

#include "Base.h"

/* Ŭ���̾���Ʈ���� ������ �پ��� ���ӿ�����Ʈ���� �θ𰡵ȴ�. */

BEGIN(Engine)

class ENGINE_DLL CCloud : public CBase
{
private:
	/* ������ ������ �� */
	CCloud(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
	/* �纻�� ������ �� */
	CCloud(const CCloud& rhs); /* ���� ������. */
	virtual ~CCloud() = default;


public:
	virtual HRESULT Initialize_Prototype();
	virtual HRESULT Initialize(void* pArg);
	virtual void PriorityTick(_float fTimeDelta);
	virtual void Tick(_float fTimeDelta);
	virtual void LateTick(_float fTimeDelta);
	virtual HRESULT Render();
	virtual HRESULT Render_LightDepth() { return S_OK; }

protected:
	ID3D11Device*			m_pDevice = { nullptr };
	ID3D11DeviceContext*	m_pContext = { nullptr };

	BoundingBox				m_tBoundingBox;

public:
	static CCloud* Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
	virtual void Free() override;
};

END