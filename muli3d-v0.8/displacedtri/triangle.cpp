
#include "triangle.h"
#include "../libappframework/include/application.h"
#include "../libappframework/include/scene.h"
#include "mycamera.h"

class CTriangleVS : public IMuli3DVertexShader
{
public:
	/*
	struct vertexformat
	{
	vector3 vPosition;
	vector3 vNormal;
	vector3 vTangent;
	vector2 vTexCoord0;
	};
	*/
	void Execute( const shaderreg *i_pInput, vector4 &o_vPosition, shaderreg *o_pOutput )
	{
		// offset position
		vector4 vTexNormal; 
		// i_pInput[3].xy -> vTexCoord0
		// 第1个参数1，就是要采样第几张texture，index从0开始
		// 这里就是UV采样TexNormal，采样出来的结果保存到vTexNormal
		SampleTexture( vTexNormal, 1, i_pInput[3].x, i_pInput[3].y );

		const float32 fHeight = 0.4f * vTexNormal.a;
		vector3 vNormal = i_pInput[1]; 
		vNormal.normalize(); // renormalize normal - length changed due to interpolation of vertices during subdivision

		// transform position
		o_vPosition = (i_pInput[0] + vNormal * fHeight) * matGetMatrix( m3dsc_wvpmatrix );

		// pass texcoord to pixelshader (uv)
		o_pOutput[0] = i_pInput[3];

		// build transformation matrix to tangent space
		vector3 vTangent = i_pInput[2]; vTangent.normalize();
		// vNormal = vNormal * m3dsc_worldmatrix
		vVector3TransformNormal( vNormal, vNormal, matGetMatrix( m3dsc_worldmatrix ) );
		vVector3TransformNormal( vTangent, vTangent, matGetMatrix( m3dsc_worldmatrix ) );
		vector3 vBinormal; vVector3Cross( vBinormal, vNormal, vTangent );

		// WorldToTangentSpace 世界坐标系 -> 切线空间 (转置)
		const matrix44 matWorldToTangentSpace(
			vTangent.x, vBinormal.x, vNormal.x, 0.0f,
			vTangent.y, vBinormal.y, vNormal.y, 0.0f,
			vTangent.z, vBinormal.z, vNormal.z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f );

		// transform light direction to tangent space
		const vector3 vWorldPosition = i_pInput[0] * matGetMatrix( m3dsc_worldmatrix );
		// (vector3)vGetVector( 1 ) 获得Light.pos
		vector3 vLightDir = (vector3)vGetVector( 1 ) - vWorldPosition;
		vector3 vLightDirTangentSpace; 
		// vLightDirTangentSpace = vLightDir * matWorldToTangentSpace
		vVector3TransformNormal( vLightDirTangentSpace, vLightDir, matWorldToTangentSpace );

		// pass LightDirTangentSpace to pixelshader
		o_pOutput[1] = vLightDirTangentSpace;

		// compute half vector and transform to tangent space
		// vGetVector( 0 ) camera.pos
		vector3 vViewDir = (vector3)vGetVector( 0 ) - vWorldPosition;
		const vector3 vHalf = ( vViewDir.normalize() + vLightDir.normalize() ) * 0.5f;
		vector3 vHalfTangentSpace; 
		// vVector3TransformNormal = vHalf * matWorldToTangentSpace
		vVector3TransformNormal( vHalfTangentSpace, vHalf, matWorldToTangentSpace );
		
		// pass HalfTangentSpace to pixelshader
		o_pOutput[2] = vHalfTangentSpace;
	}

	m3dshaderregtype GetOutputRegisters( uint32 i_iRegister )
	{
		switch( i_iRegister )
		{
		case 0: return m3dsrt_vector2;
		case 1: return m3dsrt_vector3;
		case 2: return m3dsrt_vector3;
		default: return m3dsrt_unused;
		}
	}
};

class CTrianglePS : public IMuli3DPixelShader
{
public:
	bool bMightKillPixels() { return false; }
	bool bExecute( const shaderreg *i_pInput, vector4 &io_vColor, float32 &io_fDepth )
	{
		// read normal from normalmap
		vector4 vTexNormal; SampleTexture( vTexNormal, 1, i_pInput[0].x, i_pInput[0].y, 0.0f );
		const vector3 vNormal( vTexNormal.x * 2.0f - 1.0f, vTexNormal.y * 2.0f - 1.0f, vTexNormal.z * 2.0f - 1.0f );

		// sample texture
		vector4 vTex;
		SampleTexture( vTex, 0, i_pInput[0].x, i_pInput[0].y, 0.0f );
		
		// renormalize interpolated light direction vector
		vector3 vLightDir = i_pInput[1]; vLightDir.normalize();

		// compute diffuse light
		float32 fDiffuse = fVector3Dot( vNormal, vLightDir );
		float32 fSpecular = 0.0f;
		if( fDiffuse >= 0.0f )
		{			
			// compute specular light
			vector3 vHalf = i_pInput[2]; vHalf.normalize();
			fSpecular = fVector3Dot( vNormal, vHalf );
			if( fSpecular < 0.0f )
				fSpecular = 0.0f;
			else
				fSpecular = powf( fSpecular, 128.0f );
		}
		else
			fDiffuse = 0.0f;

		const vector4 &vLightColor = vGetVector( 0 );
		io_vColor = vTex * vLightColor * fDiffuse + vLightColor * fSpecular; // += for additive blending with backbuffer, e.g. when there are multiple lights

		return true;
	}
};

m3dvertexelement VertexDeclaration[] =
{
	M3DVERTEXFORMATDECL( 0, m3dvet_vector3, 0 ),
	M3DVERTEXFORMATDECL( 0, m3dvet_vector3, 1 ),
	M3DVERTEXFORMATDECL( 0, m3dvet_vector3, 2 ),
	M3DVERTEXFORMATDECL( 0, m3dvet_vector2, 3 ),
};

CTriangle::CTriangle( class CScene *i_pParent )
{
	m_pParent = i_pParent;

	m_pVertexFormat = 0;
	m_pVertexBuffer = 0;
	m_pVertexShader = 0;
	m_pPixelShader = 0;

	m_hTexture = 0;
	m_hNormalmap = 0;
}

CTriangle::~CTriangle()
{
	m_pParent->pGetParent()->pGetResManager()->ReleaseResource( m_hNormalmap );
	m_pParent->pGetParent()->pGetResManager()->ReleaseResource( m_hTexture );

	SAFE_RELEASE( m_pPixelShader );
	SAFE_RELEASE( m_pVertexShader );
	SAFE_RELEASE( m_pVertexBuffer );
	SAFE_RELEASE( m_pVertexFormat );
}

bool CTriangle::bInitialize( const vertexformat *i_pVertices, string i_sTexture, string i_sNormalmap )
{
	CGraphics *pGraphics = m_pParent->pGetParent()->pGetGraphics();
	CMuli3DDevice *pM3DDevice = pGraphics->pGetM3DDevice();

	// create VertexFormat, VertexDeclaration 顶点属性声明(vertex怎么读入Shader相关)
	if( FUNC_FAILED( pM3DDevice->CreateVertexFormat( &m_pVertexFormat, VertexDeclaration, sizeof( VertexDeclaration ) ) ) )
		return false;

	// create VertexBuffer, 存储all Vertex 需要大多的内存(vertex的位置，UV，法线，切线)
	if( FUNC_FAILED( pM3DDevice->CreateVertexBuffer( &m_pVertexBuffer, sizeof( vertexformat ) * 3 ) ) )
		return false;

	// 获得vertexBuffer的地址
	vertexformat *pDest = 0;
	if( FUNC_FAILED( m_pVertexBuffer->GetPointer( 0, (void **)&pDest ) ) )
		return false;

	// 把外部的i_pVertices顶点数据塞到vertexbuffer中
	memcpy( pDest, i_pVertices, sizeof( vertexformat ) * 3 );

	// Calculate triangle normal ...
	vector3 v01 = pDest[1].vPosition - pDest[0].vPosition;
	vector3 v02 = pDest[2].vPosition - pDest[0].vPosition;
	vector3 vNormal; vVector3Cross( vNormal, v01, v02 );
	// all vertex normal is same
	pDest[0].vNormal = pDest[1].vNormal = pDest[2].vNormal = vNormal;

	// Calculate triangle tangent ...
	float32 fDeltaV[2] = {
		pDest[1].vTexCoord0.y - pDest[0].vTexCoord0.y,
		pDest[2].vTexCoord0.y - pDest[0].vTexCoord0.y };
	vector3 vTangent = (v01 * fDeltaV[1]) - (v02 * fDeltaV[0]);
	pDest[0].vTangent = pDest[1].vTangent = pDest[2].vTangent = vTangent.normalize();

	// 实例化CTriangleVS 和 CTrianglePS
	m_pVertexShader = new CTriangleVS;
	m_pPixelShader = new CTrianglePS;

	// Load texture -----------------------------------------------------------
	CResManager *pResManager = m_pParent->pGetParent()->pGetResManager();
	m_hTexture = pResManager->hLoadResource( i_sTexture );
	if( !m_hTexture )
		return false;

	// Load normalmap ---------------------------------------------------------
	m_hNormalmap = pResManager->hLoadResource( i_sNormalmap );
	if( !m_hNormalmap )
		return false;

	return true;
}

bool CTriangle::bFrameMove()
{
	return false;
}

void CTriangle::Render( uint32 i_iPass )
{
	switch( i_iPass )
	{
	case ePass_Lighting: break;
	}

	CGraphics *pGraphics = m_pParent->pGetParent()->pGetGraphics();
	// 获得 当前 Camera
	CCamera *pCurCamera = pGraphics->pGetCurCamera();
	// WorldMatrix 为单位矩阵
	matrix44 matWorld; matMatrix44Identity( matWorld );
	pCurCamera->SetWorldMatrix( matWorld );

	// 为 vs Shader设置 Materix
	m_pVertexShader->SetMatrix( m3dsc_worldmatrix, pCurCamera->matGetWorldMatrix() );
	m_pVertexShader->SetMatrix( m3dsc_viewmatrix, pCurCamera->matGetViewMatrix() );
	m_pVertexShader->SetMatrix( m3dsc_projectionmatrix, pCurCamera->matGetProjectionMatrix() );
	m_pVertexShader->SetMatrix( m3dsc_wvpmatrix, pCurCamera->matGetWorldMatrix() * pCurCamera->matGetViewMatrix() * pCurCamera->matGetProjectionMatrix() );

	// 为vs Shader 设置 vector (camera Pos)
	vector3 vCamPos = pCurCamera->vGetPosition();
	m_pVertexShader->SetVector( 0, vector4( vCamPos.x, vCamPos.y, vCamPos.z, 0 ) );

	// 获得当前 Light
	CLight *pLight = m_pParent->pGetCurrentLight();
	
	// 为vs Shader 设置 vector (light Pos)
	vector3 vLightPos = pLight->vGetPosition();
	m_pVertexShader->SetVector( 1, vector4( vLightPos.x, vLightPos.y, vLightPos.z, 0 ) );
	
	// 为fs Shader 设置 vector (light color)
	m_pPixelShader->SetVector( 0, pLight->vGetColor() );

	// Stack.top() 的 StateBlock 设置VertexFormat （设置device.m_RenderInfo.VSInputs）
	pGraphics->SetVertexFormat( m_pVertexFormat );
	// Stack.top() 的 StateBlock设置vertex stream
	pGraphics->SetVertexStream( 0, m_pVertexBuffer, 0, sizeof( vertexformat ) );
	// Stack.top() 的 StateBlock设置 vertexShader,(device 设置 vertexShader )
	pGraphics->SetVertexShader( m_pVertexShader );
	// Stack.top() 的 StateBlock设置 PixelShader,(device 设置 PixelShader )
	pGraphics->SetPixelShader( m_pPixelShader );

	CResManager *pResManager = m_pParent->pGetParent()->pGetResManager();

	// 获得 main Texture
	CTexture *pTexture = (CTexture *)pResManager->pGetResource( m_hTexture );

	// Stack.top() 的 StateBlock 存储 pTexture->pGetTexture()
	pGraphics->SetTexture( 0, pTexture->pGetTexture() );
	
	// 获得normal texture
	CTexture *pNormalmap = (CTexture *)pResManager->pGetResource( m_hNormalmap );

	// Stack.top() 的 StateBlock 存储 pNormalmap->pGetTexture()
	pGraphics->SetTexture( 1, pNormalmap->pGetTexture() );

	// Stack.top() 的 StateBlock 分别为 main normal,The samplers states.
	for( uint32 i = 0; i < 2; ++i )
	{
		pGraphics->SetTextureSamplerState( i, m3dtss_addressu, m3dta_clamp );
		pGraphics->SetTextureSamplerState( i, m3dtss_addressv, m3dta_clamp );
	}

	pGraphics->pGetM3DDevice()->DrawPrimitive( m3dpt_trianglelist, 0, 1 );
}
