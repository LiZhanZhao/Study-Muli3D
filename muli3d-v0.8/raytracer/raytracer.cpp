
#include "raytracer.h"
#include "../libappframework/include/application.h"
#include "../libappframework/include/scene.h"
#include "../libappframework/include/texture.h"
#include "mycamera.h"

class CRaytracerVS : public IMuli3DVertexShader
{
public:
	// 传进来是 i_pInput 是4个点
	/*
	const float32 fLocalDepth = 1.0f / tanf( i_fFOVAngle * 0.5f );
	pDest->vDirection = vector3( -1, -1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( 1, -1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( -1, 1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( 1, 1, fLocalDepth );
	*/
	void Execute( const shaderreg *i_pInput, vector4 &o_vPosition, shaderreg *o_pOutput )
	{
		// 初始化 参数o_vPosition，作为VS的输出
		o_vPosition = i_pInput[0]; o_vPosition.z = 0;
		
		// rotate the direction vector to camera's world-space
		// matGetMatrix( 0 ) -> inverse view matrix
		// 因为上面的 i_pInput 是Camera Space 的
		vVector3TransformNormal( *(vector3 *)&o_pOutput[0], (vector3)i_pInput[0], matGetMatrix( 0 ) );
	}

	m3dshaderregtype GetOutputRegisters( uint32 i_iRegister )
	{
		switch( i_iRegister )
		{
		case 0: return m3dsrt_vector3;
		default: return m3dsrt_unused;
		}
	}
};

class CRaytracerPS : public IMuli3DPixelShader
{
private:
	inline float32 fTrace( const vector3 &i_vRayOrigin, const vector3 &i_vRayDir, vector4 *o_pColor, uint32 i_iLevel = 0 )
	{
		// 初始化碰撞距离
		float32 fCollisionDistance = FLT_MAX;
		if( i_iLevel >= MAX_RECURSION )
		{
			if( o_pColor ) *o_pColor = vector4( 0, 0, 0, 0 );
			return fCollisionDistance;
		}

		// Trace spheres ------------------------------------------------------
		int32 iCollsionSphere = -1;
		vector3 vCollisionPoint, vCollisionNormal;

		const float32 fSphereUStep = 1.0f / (float32)MAX_SPHERES;
		float32 fSphereU = fSphereUStep * 0.5f;
		//  fGetFloat( 0 ) -> 记录多少个sphere
		const uint32 iNumSpheres = ftol( fGetFloat( 0 ) );

		// 遍历所有的物体，判断ray是否碰到物体
		for( uint32 iSphere = 0; iSphere < iNumSpheres; ++iSphere, fSphereU += fSphereUStep )
		{
			vector4 vSphereData; 
			// 采样自己构造的SphereData纹理，注意这里是 m3dtf_point，采样texture是point的
			SampleTexture( vSphereData, 0, fSphereU, 0 );
			// 从SphereData纹理得到对应的sphereOrigin 和 SphereRadius数据
			const vector3 vSphereOrigin( vSphereData.r, vSphereData.g, vSphereData.b );
			const float32 fSphereRadius = vSphereData.a;

			// vDiff 是ray.origin 指向球心 的向量
			const vector3 vDiff = vSphereOrigin - i_vRayOrigin;
			//  vDiff 在 i_vRayDir的投影
			const float32 fV = fVector3Dot( vDiff, i_vRayDir );
			//  fVector3Dot( vDiff, vDiff )就是 vDiff 的长度的平方
			// （是ray.origin 指向球心 的向量的长度的平方）
			float32 fDist = fSphereRadius * fSphereRadius + fV * fV - fVector3Dot( vDiff, vDiff );
			// ray 碰不到 shpere的情况下
			if( fDist < 0.0f )
				continue;

			// 计算距离
			fDist = fV - sqrtf( fDist );
			if( fDist >= 0.0f )
			{
				// collision with sphere
				// 计算出ray 碰撞到的 最小的距离 的球
				if( fDist < fCollisionDistance )
				{
					fCollisionDistance = fDist;
					iCollsionSphere = iSphere;
					// 计算 交点 (World space)
					vCollisionPoint = i_vRayOrigin + i_vRayDir * fCollisionDistance;
					//  计算 法线 (World space)
					vCollisionNormal = vCollisionPoint - vSphereOrigin;
				}
			}
		}

		if( iCollsionSphere == -1 ) // no collision
		{
			if( o_pColor ) *o_pColor = vector4( 0, 0, 0, 0 );
			return fCollisionDistance;
		}
		
		if( o_pColor )
		{
			const vector3 vViewDir = -i_vRayDir;
			vCollisionNormal.normalize();

			// 计算 索引为 iCollsionSphere 的sphere 在 SphereData纹理 的纹理坐标 U
			const float32 fSphereU = fSphereUStep * ( 0.5f + (float32)iCollsionSphere );
			vector4 vSphereColor; 
			// 获得 每一个球的vector4( i_vColor.r, i_vColor.g, i_vColor.b, (float32)iSamplerIndex )
			SampleTexture( vSphereColor, 0, fSphereU, 1 );

			if( vSphereColor.a < c_iMaxTextureSamplers ) // check if a texture is associated with this sphere
			{
				// 计算 ray 碰撞 Sphere 的 交点的UV坐标
				const float32 fPhi = atan2f( vCollisionNormal.z, vCollisionNormal.x );
				float32 fU = -fPhi / (2.0f * M3D_PI); 
				if( fPhi >= 0.0f ) fU += 1.0f;
				const float32 fV = asinf( vCollisionNormal.y ) / M3D_PI + 0.5f;

				// 采样 Sphere 用到的纹理图
				const uint32 iSamplerIndex = ftol( vSphereColor.a );
				SampleTexture( vSphereColor, iSamplerIndex, 1.0f - fU, 1.0f - fV );
			}

			// Initialize output-color with reflection color ...
			const float32 fEpsilon = 0.01f; // offset
			// 反射向量
			const float32 fViewDirDotNormal = fVector3Dot( vCollisionNormal, vViewDir );
			const vector3 vReflection = ( vCollisionNormal * ( 2 * fViewDirDotNormal ) - vViewDir ).normalize();
			// 递归反射向量进行ray tracing, 就是先获得反射的颜色，在进行光照
			fTrace( vCollisionPoint + vReflection * fEpsilon, vReflection, o_pColor, i_iLevel + 1 );

			// Compute lighting ...
			const float32 fLightUStep = 1.0f / (float32)MAX_LIGHTS;
			float32 fLightU = fLightUStep * 0.5f;
			// fGetFloat( 1 ) -> 获得light number
			const uint32 iNumLights = ftol( fGetFloat( 1 ) );
			for( uint32 iLight = 0; iLight < iNumLights; ++iLight, fLightU += fLightUStep )
			{
				vector4 vLightColor; 
				// 采样 m_pLightData 纹理图，获得lightPos,和lightColor
				SampleTexture( vLightColor, 1, fLightU, 0 );
				const vector3 vLightPos = vLightColor;

				SampleTexture( vLightColor, 1, fLightU, 1 );
				vector3 vLightDir = vLightPos - vCollisionPoint;

				const float32 fDistToLight = vLightDir.length();
				vLightDir.normalize();
				
				// calc phong-lighting
				// vSphereColor 就是Sphere的纹理颜色
				// 这里就是 球的纹理颜色 * ambient light
				*o_pColor += vSphereColor * vGetVector( 1 ); // ambient light

				const float32 fDiffuse = fVector3Dot( vCollisionNormal, vLightDir );
				if( fDiffuse > 0.0f )
				{
					// check shadow
					// 这里就是计算阴影的，主要的就是判断 球与光源之间是否存在其他物体，如果有的话，
					// 这个vCollisionPoint 就属于阴影的一部分
					if( fTrace( vCollisionPoint + vLightDir * fEpsilon, vLightDir, 0 ) < fDistToLight )
						continue;

					float32 fSpecular = 0.0f;
					if( fDiffuse > 0.0f )
					{
						// 计算高光
						const float32 fLightDirDotNormal = fVector3Dot( vCollisionNormal, vLightDir );
						const vector3 vReflection = ( vCollisionNormal * ( 2 * fLightDirDotNormal ) - vLightDir ).normalize();
						fSpecular = powf( fSaturate( fVector3Dot( vReflection, vViewDir ) ), 16 );
					}

					// 这里就是计算 漫反射 与 高光 
					*o_pColor += vSphereColor * vLightColor * fDiffuse + vLightColor * fSpecular;
				}
			}
		}
		
		return fCollisionDistance;
	}

public:
	bool bMightKillPixels() { return false; }
	bool bExecute( const shaderreg *i_pInput, vector4 &io_vColor, float32 &io_fDepth )
	{
		// vGetVector( 0 ) -> camera position (World Pos)
		const vector3 vRayOrigin = vGetVector( 0 );
		// i_pInput[0] -> direction vector (World Pos)
		const vector3 vRayDir = ((vector3)i_pInput[0]).normalize();
		// rayTracing 的核心
		fTrace( vRayOrigin, vRayDir, &io_vColor );
		return true;
	}
};

m3dvertexelement VertexDeclaration[] =
{
	M3DVERTEXFORMATDECL( 0, m3dvet_vector3, 0 )
};

CRaytracer::CRaytracer( class CScene *i_pParent )
{
	m_pParent = i_pParent;

	m_pVertexFormat = 0;
	m_pVertexBuffer = 0;
	m_pVertexShader = 0;
	m_pPixelShader = 0;

	m_iNumSpheres = 0;
	m_pSphereData = 0;
	memset( m_hSphereTextures, 0, sizeof( HRESOURCE ) * MAX_SPHERES );
	m_iNumLights = 0;
	m_pLightData = 0;
}

CRaytracer::~CRaytracer()
{
	SAFE_RELEASE( m_pLightData );
	for( uint32 i = 0; i < m_iNumSpheres; i++ )
		m_pParent->pGetParent()->pGetResManager()->ReleaseResource( m_hSphereTextures[i] );
	SAFE_RELEASE( m_pSphereData );

	SAFE_RELEASE( m_pPixelShader );
	SAFE_RELEASE( m_pVertexShader );
	SAFE_RELEASE( m_pVertexBuffer );
	SAFE_RELEASE( m_pVertexFormat );
}

bool CRaytracer::bInitialize( float32 i_fFOVAngle )
{
	CGraphics *pGraphics = m_pParent->pGetParent()->pGetGraphics();
	CMuli3DDevice *pM3DDevice = pGraphics->pGetM3DDevice();

	// create CMuli3DVertexFormat
	if( FUNC_FAILED( pM3DDevice->CreateVertexFormat( &m_pVertexFormat, VertexDeclaration, sizeof( VertexDeclaration ) ) ) )
		return false;

	// create CMuli3DVertexBuffer
	if( FUNC_FAILED( pM3DDevice->CreateVertexBuffer( &m_pVertexBuffer, sizeof( vertexformat ) * 4 ) ) )
		return false;

	// 获得vertexBuffer 的 内存地址
	vertexformat *pDest = 0;
	if( FUNC_FAILED( m_pVertexBuffer->GetPointer( 0, (void **)&pDest ) ) )
		return false;

	// 这个就是求出z值(推导过程类似于 求near plane的距离)
	const float32 fLocalDepth = 1.0f / tanf( i_fFOVAngle * 0.5f );
	pDest->vDirection = vector3( -1, -1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( 1, -1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( -1, 1, fLocalDepth ); pDest++;
	pDest->vDirection = vector3( 1, 1, fLocalDepth );


	m_pVertexShader = new CRaytracerVS;
	m_pPixelShader = new CRaytracerPS;

	// Create the sphere and light textures ...
	// 创建 sphere 和 light 纹理
	// sphere纹理，width = MAX_SPHERES, height = 2
	if( FUNC_FAILED( pM3DDevice->CreateTexture( &m_pSphereData, MAX_SPHERES, 2, 1, m3dfmt_r32g32b32a32f ) ) )
		return false;

	// light纹理，width = MAX_LIGHTS, height = 2
	if( FUNC_FAILED( pM3DDevice->CreateTexture( &m_pLightData, MAX_LIGHTS, 2, 1, m3dfmt_r32g32b32a32f ) ) )
		return false;

	return true;
}

bool CRaytracer::bAddSphere( const vector3 &i_vOrigin, float32 i_fRadius, const vector4 &i_vColor, HRESOURCE i_hTexture )
{
	if( m_iNumSpheres >= MAX_SPHERES )
		return false;

	// 保存Texture
	m_hSphereTextures[m_iNumSpheres] = i_hTexture;

	// 这里从2开始，后面有解析，主要是还要传进入 m_pSphereData 和 m_pLightData
	uint32 iSamplerIndex = 2; // 2 is first available texture stage for spheres
	if( i_hTexture )
	{
		for( uint32 i = 0; i < m_iNumSpheres; ++i )
		{
			if( m_hSphereTextures[i] )
				++iSamplerIndex;
		}

		if( iSamplerIndex > c_iMaxTextureSamplers )
			iSamplerIndex = c_iMaxTextureSamplers;
	}
	else
		iSamplerIndex = c_iMaxTextureSamplers;

	// 这里这里是vector4*
	vector4 *pData = 0;
	if( FUNC_FAILED( m_pSphereData->LockRect( 0, (void **)&pData, 0 ) ) )
		return false;
	// 因为m_pSphereData 是一个width = MAX_SPHERES， height = 2的CMuli3DTexture，
	// 这里就是第一行就保存每一个球的vector4( i_vOrigin.x, i_vOrigin.y, i_vOrigin.z, i_fRadius );
	// 第二行就保存每一个球的vector4( i_vColor.r, i_vColor.g, i_vColor.b, (float32)iSamplerIndex )
	pData[m_iNumSpheres] = vector4( i_vOrigin.x, i_vOrigin.y, i_vOrigin.z, i_fRadius );
	pData[m_iNumSpheres + MAX_SPHERES] = vector4( i_vColor.r, i_vColor.g, i_vColor.b, (float32)iSamplerIndex ); // encode sampler index in alpha component of sphere color.

	m_pSphereData->UnlockRect( 0 );

	m_iNumSpheres++;

	return true;
}

bool CRaytracer::bAddLight( const vector3 &i_vOrigin, const vector4 &i_vColor )
{
	if( m_iNumLights >= MAX_LIGHTS )
		return false;

	// 注意，这里vector4*
	vector4 *pData = 0;
	if( FUNC_FAILED( m_pLightData->LockRect( 0, (void **)&pData, 0 ) ) )
		return false;

	// 因为m_pLightData 是一个width = MAX_LIGHTS, height = 2 的CMuli3DTexture，那么
	// 第一行保存的就是 i_vOrigin， 第二行就保存 i_vColor
	pData[m_iNumLights] = i_vOrigin;
	pData[m_iNumLights + MAX_LIGHTS] = i_vColor;

	m_pLightData->UnlockRect( 0 );

	m_iNumLights++;

	return true;
}

bool CRaytracer::bFrameMove()
{
	return false;
}

void CRaytracer::Render( uint32 i_iPass )
{
	switch( i_iPass )
	{
	case ePass_Default: break;
	}

	CGraphics *pGraphics = m_pParent->pGetParent()->pGetGraphics();

	pGraphics->SetVertexFormat( m_pVertexFormat );
	// 设置vertexBuffer,4个顶点
	pGraphics->SetVertexStream( 0, m_pVertexBuffer, 0, sizeof( vertexformat ) );

	pGraphics->SetVertexShader( m_pVertexShader );
	pGraphics->SetPixelShader( m_pPixelShader );

	// 为 PS 设置参数
	// set spheres
	m_pPixelShader->SetFloat( 0, (float32)m_iNumSpheres );
	pGraphics->SetTexture( 0, m_pSphereData );
	pGraphics->SetTextureSamplerState( 0, m3dtss_minfilter, m3dtf_point );
	pGraphics->SetTextureSamplerState( 0, m3dtss_magfilter, m3dtf_point );
	pGraphics->SetTextureSamplerState( 0, m3dtss_addressu, m3dta_clamp );
	pGraphics->SetTextureSamplerState( 0, m3dtss_addressv, m3dta_clamp );

	// set lights
	m_pPixelShader->SetFloat( 1, (float32)m_iNumLights );
	pGraphics->SetTexture( 1, m_pLightData );
	pGraphics->SetTextureSamplerState( 1, m3dtss_minfilter, m3dtf_point );
	pGraphics->SetTextureSamplerState( 1, m3dtss_magfilter, m3dtf_point );
	pGraphics->SetTextureSamplerState( 1, m3dtss_addressu, m3dta_clamp );
	pGraphics->SetTextureSamplerState( 1, m3dtss_addressv, m3dta_clamp );

	// set sphere-textures
	uint32 iSamplerIndex = 2;
	CResManager *pResManager = m_pParent->pGetParent()->pGetResManager();
	for( uint32 i = 0; i < m_iNumSpheres && iSamplerIndex < c_iMaxTextureSamplers; ++i )
	{
		if( !m_hSphereTextures[i] )
			continue;

		// 获得 earth.png, moon.png
		CTexture *pTexture = (CTexture *)pResManager->pGetResource( m_hSphereTextures[i] );
		// 绑定 iSamplerIndex 和 Texture，
		// iSamplerIndex从2开始，主要是因为
		// 一开始 pGraphics->SetTexture( 0, m_pSphereData ); 和 pGraphics->SetTexture( 1, m_pLightData );
		pGraphics->SetTexture( iSamplerIndex, pTexture->pGetTexture() );
		pGraphics->SetTextureSamplerState( iSamplerIndex, m3dtss_minfilter, m3dtf_linear );
		pGraphics->SetTextureSamplerState( iSamplerIndex, m3dtss_magfilter, m3dtf_linear );
		pGraphics->SetTextureSamplerState( iSamplerIndex, m3dtss_addressu, m3dta_wrap );
		pGraphics->SetTextureSamplerState( iSamplerIndex, m3dtss_addressv, m3dta_wrap );
		++iSamplerIndex;
	}

	CCamera *pCurCamera = pGraphics->pGetCurCamera();
	matrix44 matInvView; 
	matMatrix44Transpose( matInvView, pCurCamera->matGetViewMatrix() );
	// 这里是给vs，ps设置 属性
	m_pVertexShader->SetMatrix( 0, matInvView ); // inverse view matrix
	m_pPixelShader->SetVector( 0, pCurCamera->vGetPosition() ); // camera position
	m_pPixelShader->SetVector( 1, vector4( 0.15f, 0.15f, 0.15f, 1 ) ); // ambient light

	pGraphics->SetRenderState( m3drs_cullmode, m3dcull_none );

	pGraphics->pGetM3DDevice()->DrawPrimitive( m3dpt_trianglestrip, 0, 2 );
}
