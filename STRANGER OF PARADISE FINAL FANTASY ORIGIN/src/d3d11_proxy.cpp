#include "d3d11_proxy.h"
#include "d3d11_context_proxy.h"
#include "hooks.h"

static void GuidToText(REFIID riid, char *buf, size_t len)
{
    if (!buf || len == 0) return;
    wchar_t wbuf[64] = {};
    if (StringFromGUID2(riid, wbuf, 64) <= 0) {
        strcpy_s(buf, len, "<guid-error>");
        return;
    }
    WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, (int)len, nullptr, nullptr);
}

WrappedD3D11Device::WrappedD3D11Device(ID3D11Device *orig)
    : m_ref(1), m_orig(orig), m_orig1(nullptr)
{
    if (m_orig) {
        HRESULT hr = m_orig->QueryInterface(__uuidof(ID3D11Device1), (void**)&m_orig1);
        DB_LOGF("[DBloom] QI original ID3D11Device1 hr=0x%08lx ptr=%p", (unsigned long)hr, m_orig1);
    }
    DB_LOGF("[DBloom] WrappedD3D11Device=%p original=%p", this, orig);
}

WrappedD3D11Device::~WrappedD3D11Device()
{
    if (m_orig1) m_orig1->Release();
    if (m_orig) m_orig->Release();
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::QueryInterface(REFIID riid, void **ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device)) {
        *ppvObject = static_cast<ID3D11Device*>(this);
        AddRef();
        DB_LOGF("[DBloom] Device::QI returned wrapped ID3D11Device");
        return S_OK;
    }
    if (riid == __uuidof(ID3D11Device1)) {
        if (!m_orig1) {
            *ppvObject = nullptr;
            DB_LOGF("[DBloom] Device::QI ID3D11Device1 -> E_NOINTERFACE");
            return E_NOINTERFACE;
        }
        *ppvObject = static_cast<ID3D11Device1*>(this);
        AddRef();
        DB_LOGF("[DBloom] Device::QI returned wrapped ID3D11Device1");
        return S_OK;
    }
    HRESULT hr = m_orig->QueryInterface(riid, ppvObject);
    char guid[64] = {};
    GuidToText(riid, guid, sizeof(guid));
    DB_LOGF("[DBloom] Device::QI delegated riid=%s hr=0x%08lx ptr=%p",
            guid, (unsigned long)hr, ppvObject ? *ppvObject : nullptr);
    return hr;
}

ULONG STDMETHODCALLTYPE WrappedD3D11Device::AddRef()
{
    return (ULONG)InterlockedIncrement(&m_ref);
}

ULONG STDMETHODCALLTYPE WrappedD3D11Device::Release()
{
    ULONG ref = (ULONG)InterlockedDecrement(&m_ref);
    if (!ref) delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer)
{
    return m_orig->CreateBuffer(pDesc, pInitialData, ppBuffer);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D)
{
    return m_orig->CreateTexture1D(pDesc, pInitialData, ppTexture1D);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D)
{
    return m_orig->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D)
{
    return m_orig->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView)
{
    return m_orig->CreateShaderResourceView(pResource, pDesc, ppSRView);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView)
{
    return m_orig->CreateUnorderedAccessView(pResource, pDesc, ppUAView);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView)
{
    return m_orig->CreateRenderTargetView(pResource, pDesc, ppRTView);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView)
{
    return m_orig->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout)
{
    return m_orig->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader)
{
    return m_orig->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{
    return m_orig->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader)
{
    return m_orig->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader)
{
    CheckToggleKey();
    uint64_t hash = HashShaderBytecode(pShaderBytecode, BytecodeLength);
    static LONG shaderCount = 0;
    LONG current = InterlockedIncrement(&shaderCount);
    if (current <= 16 || IsTargetBloomHash(hash)) {
        DB_LOGF("[DBloom] CreatePixelShader #%ld hash=%016llx %s",
                current, (unsigned long long)hash,
                IsTargetBloomHash(hash) ? "<<< BLOOM <<<" : "");
    }

    HRESULT hr = m_orig->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
    if (SUCCEEDED(hr) && ppPixelShader && *ppPixelShader) {
        RememberPixelShader(*ppPixelShader, hash);
        if (IsTargetBloomHash(hash)) {
            DB_LOGF("[DBloom] remembered bloom pixel shader ps=%p hash=%016llx",
                    *ppPixelShader, (unsigned long long)hash);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader)
{
    return m_orig->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader)
{
    return m_orig->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader)
{
    return m_orig->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateClassLinkage(ID3D11ClassLinkage **ppLinkage)
{
    return m_orig->CreateClassLinkage(ppLinkage);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState)
{
    return m_orig->CreateBlendState(pBlendStateDesc, ppBlendState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState)
{
    return m_orig->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState)
{
    return m_orig->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState)
{
    return m_orig->CreateSamplerState(pSamplerDesc, ppSamplerState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
    return m_orig->CreateQuery(pQueryDesc, ppQuery);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc, ID3D11Predicate **ppPredicate)
{
    return m_orig->CreatePredicate(pPredicateDesc, ppPredicate);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter)
{
    return m_orig->CreateCounter(pCounterDesc, ppCounter);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext)
{
    HRESULT hr = m_orig->CreateDeferredContext(ContextFlags, ppDeferredContext);
    if (SUCCEEDED(hr) && ppDeferredContext && *ppDeferredContext) {
        *ppDeferredContext = WrapContext(*ppDeferredContext, static_cast<ID3D11Device*>(this));
        DB_LOGF("[DBloom] CreateDeferredContext returned wrapped context=%p", *ppDeferredContext);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource)
{
    return m_orig->OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport)
{
    return m_orig->CheckFormatSupport(Format, pFormatSupport);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels)
{
    return m_orig->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

void STDMETHODCALLTYPE WrappedD3D11Device::CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo)
{
    m_orig->CheckCounterInfo(pCounterInfo);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength)
{
    return m_orig->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize)
{
    return m_orig->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
    return m_orig->GetPrivateData(guid, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
    return m_orig->SetPrivateData(guid, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
    return m_orig->SetPrivateDataInterface(guid, pData);
}

D3D_FEATURE_LEVEL STDMETHODCALLTYPE WrappedD3D11Device::GetFeatureLevel()
{
    return m_orig->GetFeatureLevel();
}

UINT STDMETHODCALLTYPE WrappedD3D11Device::GetCreationFlags()
{
    return m_orig->GetCreationFlags();
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::GetDeviceRemovedReason()
{
    return m_orig->GetDeviceRemovedReason();
}

void STDMETHODCALLTYPE WrappedD3D11Device::GetImmediateContext(ID3D11DeviceContext **ppImmediateContext)
{
    if (!ppImmediateContext) return;
    ID3D11DeviceContext *origCtx = nullptr;
    m_orig->GetImmediateContext(&origCtx);
    if (!origCtx) {
        *ppImmediateContext = nullptr;
        return;
    }
    *ppImmediateContext = WrapContext(origCtx, static_cast<ID3D11Device*>(this));
    DB_LOGF("[DBloom] GetImmediateContext returned wrapped context=%p", *ppImmediateContext);
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::SetExceptionMode(UINT RaiseFlags)
{
    return m_orig->SetExceptionMode(RaiseFlags);
}

UINT STDMETHODCALLTYPE WrappedD3D11Device::GetExceptionMode()
{
    return m_orig->GetExceptionMode();
}

void STDMETHODCALLTYPE WrappedD3D11Device::GetImmediateContext1(ID3D11DeviceContext1 **ppImmediateContext)
{
    if (m_orig1) {
        ID3D11DeviceContext1 *origCtx = nullptr;
        m_orig1->GetImmediateContext1(&origCtx);
        if (ppImmediateContext) {
            *ppImmediateContext = WrapContext1(origCtx, static_cast<ID3D11Device*>(this));
            DB_LOGF("[DBloom] GetImmediateContext1 returned wrapped context1=%p", *ppImmediateContext);
        } else if (origCtx) {
            origCtx->Release();
        }
    } else if (ppImmediateContext) {
        *ppImmediateContext = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDeferredContext1(UINT ContextFlags, ID3D11DeviceContext1 **ppDeferredContext)
{
    HRESULT hr = m_orig1 ? m_orig1->CreateDeferredContext1(ContextFlags, ppDeferredContext) : E_NOINTERFACE;
    if (SUCCEEDED(hr) && ppDeferredContext && *ppDeferredContext) {
        *ppDeferredContext = WrapContext1(*ppDeferredContext, static_cast<ID3D11Device*>(this));
        DB_LOGF("[DBloom] CreateDeferredContext1 returned wrapped context1=%p", *ppDeferredContext);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateBlendState1(const D3D11_BLEND_DESC1 *pBlendStateDesc, ID3D11BlendState1 **ppBlendState)
{
    return m_orig1 ? m_orig1->CreateBlendState1(pBlendStateDesc, ppBlendState) : E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateRasterizerState1(const D3D11_RASTERIZER_DESC1 *pRasterizerDesc, ID3D11RasterizerState1 **ppRasterizerState)
{
    return m_orig1 ? m_orig1->CreateRasterizerState1(pRasterizerDesc, ppRasterizerState) : E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::CreateDeviceContextState(UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, REFIID EmulatedInterface, D3D_FEATURE_LEVEL *pChosenFeatureLevel, ID3DDeviceContextState **ppContextState)
{
    return m_orig1 ? m_orig1->CreateDeviceContextState(Flags, pFeatureLevels, FeatureLevels, SDKVersion, EmulatedInterface, pChosenFeatureLevel, ppContextState) : E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::OpenSharedResource1(HANDLE hResource, REFIID returnedInterface, void **ppResource)
{
    return m_orig1 ? m_orig1->OpenSharedResource1(hResource, returnedInterface, ppResource) : E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D11Device::OpenSharedResourceByName(LPCWSTR lpName, DWORD dwDesiredAccess, REFIID returnedInterface, void **ppResource)
{
    return m_orig1 ? m_orig1->OpenSharedResourceByName(lpName, dwDesiredAccess, returnedInterface, ppResource) : E_NOINTERFACE;
}

WrappedDXGISwapChain::WrappedDXGISwapChain(IDXGISwapChain *orig, ID3D11Device *wrappedDevice)
    : m_ref(1), m_orig(orig), m_wrappedDevice(wrappedDevice)
{
    if (m_wrappedDevice) m_wrappedDevice->AddRef();
    DB_LOGF("[DBloom] WrappedDXGISwapChain=%p original=%p", this, orig);
}

WrappedDXGISwapChain::~WrappedDXGISwapChain()
{
    if (m_wrappedDevice) m_wrappedDevice->Release();
    if (m_orig) m_orig->Release();
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::QueryInterface(REFIID riid, void **ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain)) {
        *ppvObject = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }
    return m_orig->QueryInterface(riid, ppvObject);
}

ULONG STDMETHODCALLTYPE WrappedDXGISwapChain::AddRef()
{
    return (ULONG)InterlockedIncrement(&m_ref);
}

ULONG STDMETHODCALLTYPE WrappedDXGISwapChain::Release()
{
    ULONG ref = (ULONG)InterlockedDecrement(&m_ref);
    if (!ref) delete this;
    return ref;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData)
{
    return m_orig->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown)
{
    return m_orig->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData)
{
    return m_orig->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetParent(REFIID riid, void **ppParent)
{
    return m_orig->GetParent(riid, ppParent);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetDevice(REFIID riid, void **ppDevice)
{
    if (!ppDevice) return E_POINTER;
    if (m_wrappedDevice) {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device)) {
            *ppDevice = m_wrappedDevice;
            m_wrappedDevice->AddRef();
            DB_LOGF("[DBloom] SwapChain::GetDevice returned wrapped ID3D11Device");
            return S_OK;
        }
        if (riid == __uuidof(ID3D11Device1)) {
            return m_wrappedDevice->QueryInterface(riid, ppDevice);
        }
    }
    return m_orig->GetDevice(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::Present(UINT SyncInterval, UINT Flags)
{
    CheckToggleKey();
    return m_orig->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetBuffer(UINT Buffer, REFIID riid, void **ppSurface)
{
    return m_orig->GetBuffer(Buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget)
{
    return m_orig->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget)
{
    return m_orig->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc)
{
    return m_orig->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    return m_orig->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters)
{
    return m_orig->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetContainingOutput(IDXGIOutput **ppOutput)
{
    return m_orig->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats)
{
    return m_orig->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetLastPresentCount(UINT *pLastPresentCount)
{
    return m_orig->GetLastPresentCount(pLastPresentCount);
}
