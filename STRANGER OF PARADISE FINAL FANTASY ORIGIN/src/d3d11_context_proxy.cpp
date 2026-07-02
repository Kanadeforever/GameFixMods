#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS
#include "d3d11_context_proxy.h"
#include "hooks.h"
#include <d3d11_1.h>
#include <cstring>

struct WrappedD3D11DeviceContext
{
    const ID3D11DeviceContextVtbl *lpVtbl;
    volatile LONG ref;
    ID3D11DeviceContext *orig;
    ID3D11DeviceContext1 *orig1;
    ID3D11Device *wrappedDevice;
    ID3D11PixelShader *currentPS;
};

static WrappedD3D11DeviceContext *Ctx(ID3D11DeviceContext *This)
{
    return reinterpret_cast<WrappedD3D11DeviceContext*>(This);
}

static WrappedD3D11DeviceContext *Ctx1(ID3D11DeviceContext1 *This)
{
    return reinterpret_cast<WrappedD3D11DeviceContext*>(This);
}

static ULONG STDMETHODCALLTYPE Ctx_AddRef(ID3D11DeviceContext *This);

static bool ShouldSkipBloomDraw(WrappedD3D11DeviceContext *ctx, const char *name)
{
    CheckToggleKey();
    ID3D11PixelShader *ps = ctx ? ctx->currentPS : nullptr;
    if (!ps) ps = g_currentPS;
    if (g_bloomEnabled || !ps) return false;
    if (!IsRememberedBloomShader(ps)) return false;
    static LONG skipped = 0;
    LONG n = InterlockedIncrement(&skipped);
    if (n <= 32 || (n % 1000) == 0) {
        DB_LOGF("[DBloom] skipped bloom draw #%ld via %s ps=%p", n, name, ps);
    }
    return true;
}

static HRESULT STDMETHODCALLTYPE Ctx_QueryInterface(ID3D11DeviceContext * This, REFIID riid, void **ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) ||
        IsEqualGUID(riid, IID_ID3D11DeviceChild) ||
        IsEqualGUID(riid, IID_ID3D11DeviceContext)) {
        *ppvObject = This;
        Ctx_AddRef(This);
        DB_LOGF("[DBloom] Context::QI returned wrapped ID3D11DeviceContext");
        return S_OK;
    }
    if (IsEqualGUID(riid, IID_ID3D11DeviceContext1)) {
        WrappedD3D11DeviceContext *ctx = Ctx(This);
        if (!ctx->orig1) {
            *ppvObject = nullptr;
            DB_LOGF("[DBloom] Context::QI ID3D11DeviceContext1 -> E_NOINTERFACE");
            return E_NOINTERFACE;
        }
        *ppvObject = This;
        Ctx_AddRef(This);
        DB_LOGF("[DBloom] Context::QI returned wrapped ID3D11DeviceContext1");
        return S_OK;
    }
    return ID3D11DeviceContext_QueryInterface(Ctx(This)->orig, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE Ctx_AddRef(ID3D11DeviceContext * This)
{
    return (ULONG)InterlockedIncrement(&Ctx(This)->ref);
}

static ULONG STDMETHODCALLTYPE Ctx_Release(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ULONG ref = (ULONG)InterlockedDecrement(&ctx->ref);
    if (!ref) {
        if (ctx->wrappedDevice) ID3D11Device_Release(ctx->wrappedDevice);
        if (ctx->orig1) ID3D11DeviceContext1_Release(ctx->orig1);
        if (ctx->orig) ID3D11DeviceContext_Release(ctx->orig);
        delete ctx;
    }
    return ref;
}

static void STDMETHODCALLTYPE Ctx_GetDevice(ID3D11DeviceContext * This, ID3D11Device **ppDevice)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ppDevice && ctx->wrappedDevice) {
        *ppDevice = ctx->wrappedDevice;
        ID3D11Device_AddRef(ctx->wrappedDevice);
        return;
    }
    ID3D11DeviceContext_GetDevice(ctx->orig, ppDevice);
}

static HRESULT STDMETHODCALLTYPE Ctx_GetPrivateData(ID3D11DeviceContext * This, REFGUID guid, UINT *pDataSize, void *pData)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_GetPrivateData(ctx->orig, guid, pDataSize, pData);
}

static HRESULT STDMETHODCALLTYPE Ctx_SetPrivateData(ID3D11DeviceContext * This, REFGUID guid, UINT DataSize, const void *pData)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_SetPrivateData(ctx->orig, guid, DataSize, pData);
}

static HRESULT STDMETHODCALLTYPE Ctx_SetPrivateDataInterface(ID3D11DeviceContext * This, REFGUID guid, const IUnknown *pData)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_SetPrivateDataInterface(ctx->orig, guid, pData);
}

static void STDMETHODCALLTYPE Ctx_VSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_PSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_PSSetShader(ID3D11DeviceContext * This, ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ctx->currentPS = pPixelShader;
    g_currentPS = pPixelShader;
    ID3D11DeviceContext_PSSetShader(ctx->orig, pPixelShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_PSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_VSSetShader(ID3D11DeviceContext * This, ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSSetShader(ctx->orig, pVertexShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_DrawIndexed(ID3D11DeviceContext * This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawIndexed")) return;
    ID3D11DeviceContext_DrawIndexed(ctx->orig, IndexCount, StartIndexLocation, BaseVertexLocation);
}

static void STDMETHODCALLTYPE Ctx_Draw(ID3D11DeviceContext * This, UINT VertexCount, UINT StartVertexLocation)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "Draw")) return;
    ID3D11DeviceContext_Draw(ctx->orig, VertexCount, StartVertexLocation);
}

static HRESULT STDMETHODCALLTYPE Ctx_Map(ID3D11DeviceContext * This, ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_Map(ctx->orig, pResource, Subresource, MapType, MapFlags, pMappedResource);
}

static void STDMETHODCALLTYPE Ctx_Unmap(ID3D11DeviceContext * This, ID3D11Resource *pResource, UINT Subresource)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_Unmap(ctx->orig, pResource, Subresource);
}

static void STDMETHODCALLTYPE Ctx_PSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_IASetInputLayout(ID3D11DeviceContext * This, ID3D11InputLayout *pInputLayout)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IASetInputLayout(ctx->orig, pInputLayout);
}

static void STDMETHODCALLTYPE Ctx_IASetVertexBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IASetVertexBuffers(ctx->orig, StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

static void STDMETHODCALLTYPE Ctx_IASetIndexBuffer(ID3D11DeviceContext * This, ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->orig, pIndexBuffer, Format, Offset);
}

static void STDMETHODCALLTYPE Ctx_DrawIndexedInstanced(ID3D11DeviceContext * This, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawIndexedInstanced")) return;
    ID3D11DeviceContext_DrawIndexedInstanced(ctx->orig, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

static void STDMETHODCALLTYPE Ctx_DrawInstanced(ID3D11DeviceContext * This, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawInstanced")) return;
    ID3D11DeviceContext_DrawInstanced(ctx->orig, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

static void STDMETHODCALLTYPE Ctx_GSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_GSSetShader(ID3D11DeviceContext * This, ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSSetShader(ctx->orig, pShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_IASetPrimitiveTopology(ID3D11DeviceContext * This, D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->orig, Topology);
}

static void STDMETHODCALLTYPE Ctx_VSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_VSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_Begin(ID3D11DeviceContext * This, ID3D11Asynchronous *pAsync)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_Begin(ctx->orig, pAsync);
}

static void STDMETHODCALLTYPE Ctx_End(ID3D11DeviceContext * This, ID3D11Asynchronous *pAsync)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_End(ctx->orig, pAsync);
}

static HRESULT STDMETHODCALLTYPE Ctx_GetData(ID3D11DeviceContext * This, ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_GetData(ctx->orig, pAsync, pData, DataSize, GetDataFlags);
}

static void STDMETHODCALLTYPE Ctx_SetPredication(ID3D11DeviceContext * This, ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_SetPredication(ctx->orig, pPredicate, PredicateValue);
}

static void STDMETHODCALLTYPE Ctx_GSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_GSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_OMSetRenderTargets(ID3D11DeviceContext * This, UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->orig, NumViews, ppRenderTargetViews, pDepthStencilView);
}

static void STDMETHODCALLTYPE Ctx_OMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext * This, UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(ctx->orig, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}

static void STDMETHODCALLTYPE Ctx_OMSetBlendState(ID3D11DeviceContext * This, ID3D11BlendState *pBlendState, const FLOAT BlendFactor[ 4 ], UINT SampleMask)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMSetBlendState(ctx->orig, pBlendState, BlendFactor, SampleMask);
}

static void STDMETHODCALLTYPE Ctx_OMSetDepthStencilState(ID3D11DeviceContext * This, ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->orig, pDepthStencilState, StencilRef);
}

static void STDMETHODCALLTYPE Ctx_SOSetTargets(ID3D11DeviceContext * This, UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_SOSetTargets(ctx->orig, NumBuffers, ppSOTargets, pOffsets);
}

static void STDMETHODCALLTYPE Ctx_DrawAuto(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawAuto")) return;
    ID3D11DeviceContext_DrawAuto(ctx->orig);
}

static void STDMETHODCALLTYPE Ctx_DrawIndexedInstancedIndirect(ID3D11DeviceContext * This, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawIndexedInstancedIndirect")) return;
    ID3D11DeviceContext_DrawIndexedInstancedIndirect(ctx->orig, pBufferForArgs, AlignedByteOffsetForArgs);
}

static void STDMETHODCALLTYPE Ctx_DrawInstancedIndirect(ID3D11DeviceContext * This, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    if (ShouldSkipBloomDraw(ctx, "DrawInstancedIndirect")) return;
    ID3D11DeviceContext_DrawInstancedIndirect(ctx->orig, pBufferForArgs, AlignedByteOffsetForArgs);
}

static void STDMETHODCALLTYPE Ctx_Dispatch(ID3D11DeviceContext * This, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_Dispatch(ctx->orig, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

static void STDMETHODCALLTYPE Ctx_DispatchIndirect(ID3D11DeviceContext * This, ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DispatchIndirect(ctx->orig, pBufferForArgs, AlignedByteOffsetForArgs);
}

static void STDMETHODCALLTYPE Ctx_RSSetState(ID3D11DeviceContext * This, ID3D11RasterizerState *pRasterizerState)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSSetState(ctx->orig, pRasterizerState);
}

static void STDMETHODCALLTYPE Ctx_RSSetViewports(ID3D11DeviceContext * This, UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSSetViewports(ctx->orig, NumViewports, pViewports);
}

static void STDMETHODCALLTYPE Ctx_RSSetScissorRects(ID3D11DeviceContext * This, UINT NumRects, const D3D11_RECT *pRects)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSSetScissorRects(ctx->orig, NumRects, pRects);
}

static void STDMETHODCALLTYPE Ctx_CopySubresourceRegion(ID3D11DeviceContext * This, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CopySubresourceRegion(ctx->orig, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}

static void STDMETHODCALLTYPE Ctx_CopyResource(ID3D11DeviceContext * This, ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CopyResource(ctx->orig, pDstResource, pSrcResource);
}

static void STDMETHODCALLTYPE Ctx_UpdateSubresource(ID3D11DeviceContext * This, ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_UpdateSubresource(ctx->orig, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

static void STDMETHODCALLTYPE Ctx_CopyStructureCount(ID3D11DeviceContext * This, ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CopyStructureCount(ctx->orig, pDstBuffer, DstAlignedByteOffset, pSrcView);
}

static void STDMETHODCALLTYPE Ctx_ClearRenderTargetView(ID3D11DeviceContext * This, ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[ 4 ])
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ClearRenderTargetView(ctx->orig, pRenderTargetView, ColorRGBA);
}

static void STDMETHODCALLTYPE Ctx_ClearUnorderedAccessViewUint(ID3D11DeviceContext * This, ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[ 4 ])
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx->orig, pUnorderedAccessView, Values);
}

static void STDMETHODCALLTYPE Ctx_ClearUnorderedAccessViewFloat(ID3D11DeviceContext * This, ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[ 4 ])
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ClearUnorderedAccessViewFloat(ctx->orig, pUnorderedAccessView, Values);
}

static void STDMETHODCALLTYPE Ctx_ClearDepthStencilView(ID3D11DeviceContext * This, ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ClearDepthStencilView(ctx->orig, pDepthStencilView, ClearFlags, Depth, Stencil);
}

static void STDMETHODCALLTYPE Ctx_GenerateMips(ID3D11DeviceContext * This, ID3D11ShaderResourceView *pShaderResourceView)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GenerateMips(ctx->orig, pShaderResourceView);
}

static void STDMETHODCALLTYPE Ctx_SetResourceMinLOD(ID3D11DeviceContext * This, ID3D11Resource *pResource, FLOAT MinLOD)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_SetResourceMinLOD(ctx->orig, pResource, MinLOD);
}

static FLOAT STDMETHODCALLTYPE Ctx_GetResourceMinLOD(ID3D11DeviceContext * This, ID3D11Resource *pResource)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_GetResourceMinLOD(ctx->orig, pResource);
}

static void STDMETHODCALLTYPE Ctx_ResolveSubresource(ID3D11DeviceContext * This, ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ResolveSubresource(ctx->orig, pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}

static void STDMETHODCALLTYPE Ctx_ExecuteCommandList(ID3D11DeviceContext * This, ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_ExecuteCommandList(ctx->orig, pCommandList, RestoreContextState);
}

static void STDMETHODCALLTYPE Ctx_HSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_HSSetShader(ID3D11DeviceContext * This, ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSSetShader(ctx->orig, pHullShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_HSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_HSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_DSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_DSSetShader(ID3D11DeviceContext * This, ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSSetShader(ctx->orig, pDomainShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_DSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_DSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_CSSetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSSetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_CSSetUnorderedAccessViews(ID3D11DeviceContext * This, UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx->orig, StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}

static void STDMETHODCALLTYPE Ctx_CSSetShader(ID3D11DeviceContext * This, ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSSetShader(ctx->orig, pComputeShader, ppClassInstances, NumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_CSSetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSSetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_CSSetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_VSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_PSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_PSGetShader(ID3D11DeviceContext * This, ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSGetShader(ctx->orig, ppPixelShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_PSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_VSGetShader(ID3D11DeviceContext * This, ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSGetShader(ctx->orig, ppVertexShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_PSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_PSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_IAGetInputLayout(ID3D11DeviceContext * This, ID3D11InputLayout **ppInputLayout)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IAGetInputLayout(ctx->orig, ppInputLayout);
}

static void STDMETHODCALLTYPE Ctx_IAGetVertexBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IAGetVertexBuffers(ctx->orig, StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

static void STDMETHODCALLTYPE Ctx_IAGetIndexBuffer(ID3D11DeviceContext * This, ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IAGetIndexBuffer(ctx->orig, pIndexBuffer, Format, Offset);
}

static void STDMETHODCALLTYPE Ctx_GSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_GSGetShader(ID3D11DeviceContext * This, ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSGetShader(ctx->orig, ppGeometryShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_IAGetPrimitiveTopology(ID3D11DeviceContext * This, D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx->orig, pTopology);
}

static void STDMETHODCALLTYPE Ctx_VSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_VSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_VSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_GetPredication(ID3D11DeviceContext * This, ID3D11Predicate **ppPredicate, BOOL *pPredicateValue)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GetPredication(ctx->orig, ppPredicate, pPredicateValue);
}

static void STDMETHODCALLTYPE Ctx_GSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_GSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_GSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_OMGetRenderTargets(ID3D11DeviceContext * This, UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMGetRenderTargets(ctx->orig, NumViews, ppRenderTargetViews, ppDepthStencilView);
}

static void STDMETHODCALLTYPE Ctx_OMGetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext * This, UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(ctx->orig, NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
}

static void STDMETHODCALLTYPE Ctx_OMGetBlendState(ID3D11DeviceContext * This, ID3D11BlendState **ppBlendState, FLOAT BlendFactor[ 4 ], UINT *pSampleMask)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMGetBlendState(ctx->orig, ppBlendState, BlendFactor, pSampleMask);
}

static void STDMETHODCALLTYPE Ctx_OMGetDepthStencilState(ID3D11DeviceContext * This, ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx->orig, ppDepthStencilState, pStencilRef);
}

static void STDMETHODCALLTYPE Ctx_SOGetTargets(ID3D11DeviceContext * This, UINT NumBuffers, ID3D11Buffer **ppSOTargets)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_SOGetTargets(ctx->orig, NumBuffers, ppSOTargets);
}

static void STDMETHODCALLTYPE Ctx_RSGetState(ID3D11DeviceContext * This, ID3D11RasterizerState **ppRasterizerState)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSGetState(ctx->orig, ppRasterizerState);
}

static void STDMETHODCALLTYPE Ctx_RSGetViewports(ID3D11DeviceContext * This, UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSGetViewports(ctx->orig, pNumViewports, pViewports);
}

static void STDMETHODCALLTYPE Ctx_RSGetScissorRects(ID3D11DeviceContext * This, UINT *pNumRects, D3D11_RECT *pRects)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_RSGetScissorRects(ctx->orig, pNumRects, pRects);
}

static void STDMETHODCALLTYPE Ctx_HSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_HSGetShader(ID3D11DeviceContext * This, ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSGetShader(ctx->orig, ppHullShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_HSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_HSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_HSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_DSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_DSGetShader(ID3D11DeviceContext * This, ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSGetShader(ctx->orig, ppDomainShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_DSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_DSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_DSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_CSGetShaderResources(ID3D11DeviceContext * This, UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSGetShaderResources(ctx->orig, StartSlot, NumViews, ppShaderResourceViews);
}

static void STDMETHODCALLTYPE Ctx_CSGetUnorderedAccessViews(ID3D11DeviceContext * This, UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(ctx->orig, StartSlot, NumUAVs, ppUnorderedAccessViews);
}

static void STDMETHODCALLTYPE Ctx_CSGetShader(ID3D11DeviceContext * This, ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSGetShader(ctx->orig, ppComputeShader, ppClassInstances, pNumClassInstances);
}

static void STDMETHODCALLTYPE Ctx_CSGetSamplers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSGetSamplers(ctx->orig, StartSlot, NumSamplers, ppSamplers);
}

static void STDMETHODCALLTYPE Ctx_CSGetConstantBuffers(ID3D11DeviceContext * This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_CSGetConstantBuffers(ctx->orig, StartSlot, NumBuffers, ppConstantBuffers);
}

static void STDMETHODCALLTYPE Ctx_ClearState(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ctx->currentPS = nullptr;
    g_currentPS = nullptr;
    ID3D11DeviceContext_ClearState(ctx->orig);
}

static void STDMETHODCALLTYPE Ctx_Flush(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    ID3D11DeviceContext_Flush(ctx->orig);
}

static D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE Ctx_GetType(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_GetType(ctx->orig);
}

static UINT STDMETHODCALLTYPE Ctx_GetContextFlags(ID3D11DeviceContext * This)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_GetContextFlags(ctx->orig);
}

static HRESULT STDMETHODCALLTYPE Ctx_FinishCommandList(ID3D11DeviceContext * This, BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList)
{
    WrappedD3D11DeviceContext *ctx = Ctx(This);
    return ID3D11DeviceContext_FinishCommandList(ctx->orig, RestoreDeferredContextState, ppCommandList);
}

static void STDMETHODCALLTYPE Ctx_CopySubresourceRegion1(ID3D11DeviceContext1 *This, ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox, UINT CopyFlags)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_CopySubresourceRegion1(ctx->orig1, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags);
}

static void STDMETHODCALLTYPE Ctx_UpdateSubresource1(ID3D11DeviceContext1 *This, ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_UpdateSubresource1(ctx->orig1, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
}

static void STDMETHODCALLTYPE Ctx_DiscardResource(ID3D11DeviceContext1 *This, ID3D11Resource *pResource)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_DiscardResource(ctx->orig1, pResource);
}

static void STDMETHODCALLTYPE Ctx_DiscardView(ID3D11DeviceContext1 *This, ID3D11View *pResourceView)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_DiscardView(ctx->orig1, pResourceView);
}

static void STDMETHODCALLTYPE Ctx_VSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_VSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_HSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_HSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_DSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_DSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_GSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_GSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_PSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_PSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_CSSetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_CSSetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_VSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_VSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_HSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_HSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_DSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_DSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_GSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_GSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_PSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_PSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_CSGetConstantBuffers1(ID3D11DeviceContext1 *This, UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_CSGetConstantBuffers1(ctx->orig1, StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}

static void STDMETHODCALLTYPE Ctx_SwapDeviceContextState(ID3D11DeviceContext1 *This, ID3DDeviceContextState *pState, ID3DDeviceContextState **ppPreviousState)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    ctx->currentPS = nullptr;
    g_currentPS = nullptr;
    if (ctx->orig1) ID3D11DeviceContext1_SwapDeviceContextState(ctx->orig1, pState, ppPreviousState);
}

static void STDMETHODCALLTYPE Ctx_ClearView(ID3D11DeviceContext1 *This, ID3D11View *pView, const FLOAT Color[4], const D3D11_RECT *pRect, UINT NumRects)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_ClearView(ctx->orig1, pView, Color, pRect, NumRects);
}

static void STDMETHODCALLTYPE Ctx_DiscardView1(ID3D11DeviceContext1 *This, ID3D11View *pResourceView, const D3D11_RECT *pRects, UINT NumRects)
{
    WrappedD3D11DeviceContext *ctx = Ctx1(This);
    if (ctx->orig1) ID3D11DeviceContext1_DiscardView1(ctx->orig1, pResourceView, pRects, NumRects);
}

struct ID3D11DeviceContext1VtblCompat
{
    ID3D11DeviceContextVtbl base;
    void (STDMETHODCALLTYPE *CopySubresourceRegion1)(ID3D11DeviceContext1*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*, UINT);
    void (STDMETHODCALLTYPE *UpdateSubresource1)(ID3D11DeviceContext1*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT, UINT);
    void (STDMETHODCALLTYPE *DiscardResource)(ID3D11DeviceContext1*, ID3D11Resource*);
    void (STDMETHODCALLTYPE *DiscardView)(ID3D11DeviceContext1*, ID3D11View*);
    void (STDMETHODCALLTYPE *VSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *HSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *DSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *GSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *PSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *CSSetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    void (STDMETHODCALLTYPE *VSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *HSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *DSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *GSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *PSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *CSGetConstantBuffers1)(ID3D11DeviceContext1*, UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
    void (STDMETHODCALLTYPE *SwapDeviceContextState)(ID3D11DeviceContext1*, ID3DDeviceContextState*, ID3DDeviceContextState**);
    void (STDMETHODCALLTYPE *ClearView)(ID3D11DeviceContext1*, ID3D11View*, const FLOAT[4], const D3D11_RECT*, UINT);
    void (STDMETHODCALLTYPE *DiscardView1)(ID3D11DeviceContext1*, ID3D11View*, const D3D11_RECT*, UINT);
};

static const ID3D11DeviceContext1VtblCompat g_contextVtbl =
{
    {
    Ctx_QueryInterface,
    Ctx_AddRef,
    Ctx_Release,
    Ctx_GetDevice,
    Ctx_GetPrivateData,
    Ctx_SetPrivateData,
    Ctx_SetPrivateDataInterface,
    Ctx_VSSetConstantBuffers,
    Ctx_PSSetShaderResources,
    Ctx_PSSetShader,
    Ctx_PSSetSamplers,
    Ctx_VSSetShader,
    Ctx_DrawIndexed,
    Ctx_Draw,
    Ctx_Map,
    Ctx_Unmap,
    Ctx_PSSetConstantBuffers,
    Ctx_IASetInputLayout,
    Ctx_IASetVertexBuffers,
    Ctx_IASetIndexBuffer,
    Ctx_DrawIndexedInstanced,
    Ctx_DrawInstanced,
    Ctx_GSSetConstantBuffers,
    Ctx_GSSetShader,
    Ctx_IASetPrimitiveTopology,
    Ctx_VSSetShaderResources,
    Ctx_VSSetSamplers,
    Ctx_Begin,
    Ctx_End,
    Ctx_GetData,
    Ctx_SetPredication,
    Ctx_GSSetShaderResources,
    Ctx_GSSetSamplers,
    Ctx_OMSetRenderTargets,
    Ctx_OMSetRenderTargetsAndUnorderedAccessViews,
    Ctx_OMSetBlendState,
    Ctx_OMSetDepthStencilState,
    Ctx_SOSetTargets,
    Ctx_DrawAuto,
    Ctx_DrawIndexedInstancedIndirect,
    Ctx_DrawInstancedIndirect,
    Ctx_Dispatch,
    Ctx_DispatchIndirect,
    Ctx_RSSetState,
    Ctx_RSSetViewports,
    Ctx_RSSetScissorRects,
    Ctx_CopySubresourceRegion,
    Ctx_CopyResource,
    Ctx_UpdateSubresource,
    Ctx_CopyStructureCount,
    Ctx_ClearRenderTargetView,
    Ctx_ClearUnorderedAccessViewUint,
    Ctx_ClearUnorderedAccessViewFloat,
    Ctx_ClearDepthStencilView,
    Ctx_GenerateMips,
    Ctx_SetResourceMinLOD,
    Ctx_GetResourceMinLOD,
    Ctx_ResolveSubresource,
    Ctx_ExecuteCommandList,
    Ctx_HSSetShaderResources,
    Ctx_HSSetShader,
    Ctx_HSSetSamplers,
    Ctx_HSSetConstantBuffers,
    Ctx_DSSetShaderResources,
    Ctx_DSSetShader,
    Ctx_DSSetSamplers,
    Ctx_DSSetConstantBuffers,
    Ctx_CSSetShaderResources,
    Ctx_CSSetUnorderedAccessViews,
    Ctx_CSSetShader,
    Ctx_CSSetSamplers,
    Ctx_CSSetConstantBuffers,
    Ctx_VSGetConstantBuffers,
    Ctx_PSGetShaderResources,
    Ctx_PSGetShader,
    Ctx_PSGetSamplers,
    Ctx_VSGetShader,
    Ctx_PSGetConstantBuffers,
    Ctx_IAGetInputLayout,
    Ctx_IAGetVertexBuffers,
    Ctx_IAGetIndexBuffer,
    Ctx_GSGetConstantBuffers,
    Ctx_GSGetShader,
    Ctx_IAGetPrimitiveTopology,
    Ctx_VSGetShaderResources,
    Ctx_VSGetSamplers,
    Ctx_GetPredication,
    Ctx_GSGetShaderResources,
    Ctx_GSGetSamplers,
    Ctx_OMGetRenderTargets,
    Ctx_OMGetRenderTargetsAndUnorderedAccessViews,
    Ctx_OMGetBlendState,
    Ctx_OMGetDepthStencilState,
    Ctx_SOGetTargets,
    Ctx_RSGetState,
    Ctx_RSGetViewports,
    Ctx_RSGetScissorRects,
    Ctx_HSGetShaderResources,
    Ctx_HSGetShader,
    Ctx_HSGetSamplers,
    Ctx_HSGetConstantBuffers,
    Ctx_DSGetShaderResources,
    Ctx_DSGetShader,
    Ctx_DSGetSamplers,
    Ctx_DSGetConstantBuffers,
    Ctx_CSGetShaderResources,
    Ctx_CSGetUnorderedAccessViews,
    Ctx_CSGetShader,
    Ctx_CSGetSamplers,
    Ctx_CSGetConstantBuffers,
    Ctx_ClearState,
    Ctx_Flush,
    Ctx_GetType,
    Ctx_GetContextFlags,
    Ctx_FinishCommandList
    },
    Ctx_CopySubresourceRegion1,
    Ctx_UpdateSubresource1,
    Ctx_DiscardResource,
    Ctx_DiscardView,
    Ctx_VSSetConstantBuffers1,
    Ctx_HSSetConstantBuffers1,
    Ctx_DSSetConstantBuffers1,
    Ctx_GSSetConstantBuffers1,
    Ctx_PSSetConstantBuffers1,
    Ctx_CSSetConstantBuffers1,
    Ctx_VSGetConstantBuffers1,
    Ctx_HSGetConstantBuffers1,
    Ctx_DSGetConstantBuffers1,
    Ctx_GSGetConstantBuffers1,
    Ctx_PSGetConstantBuffers1,
    Ctx_CSGetConstantBuffers1,
    Ctx_SwapDeviceContextState,
    Ctx_ClearView,
    Ctx_DiscardView1
};

ID3D11DeviceContext *WrapContext(ID3D11DeviceContext *orig, ID3D11Device *wrappedDevice)
{
    if (!orig) return nullptr;
    WrappedD3D11DeviceContext *ctx = new WrappedD3D11DeviceContext();
    ctx->lpVtbl = reinterpret_cast<const ID3D11DeviceContextVtbl*>(&g_contextVtbl);
    ctx->ref = 1;
    ctx->orig = orig;
    ctx->orig1 = nullptr;
    ctx->wrappedDevice = wrappedDevice;
    ctx->currentPS = nullptr;
    HRESULT hr1 = ID3D11DeviceContext_QueryInterface(orig, IID_ID3D11DeviceContext1, (void**)&ctx->orig1);
    if (ctx->wrappedDevice) ID3D11Device_AddRef(ctx->wrappedDevice);
    DB_LOGF("[DBloom] WrappedD3D11DeviceContext=%p original=%p original1=%p hr1=0x%08lx",
            ctx, orig, ctx->orig1, (unsigned long)hr1);
    return reinterpret_cast<ID3D11DeviceContext*>(ctx);
}

ID3D11DeviceContext1 *WrapContext1(ID3D11DeviceContext1 *orig, ID3D11Device *wrappedDevice)
{
    if (!orig) return nullptr;
    WrappedD3D11DeviceContext *ctx = new WrappedD3D11DeviceContext();
    ctx->lpVtbl = reinterpret_cast<const ID3D11DeviceContextVtbl*>(&g_contextVtbl);
    ctx->ref = 1;
    ctx->orig = nullptr;
    ctx->orig1 = orig;
    ctx->wrappedDevice = wrappedDevice;
    ctx->currentPS = nullptr;
    HRESULT hr0 = ID3D11DeviceContext1_QueryInterface(orig, IID_ID3D11DeviceContext, (void**)&ctx->orig);
    if (ctx->wrappedDevice) ID3D11Device_AddRef(ctx->wrappedDevice);
    DB_LOGF("[DBloom] WrappedD3D11DeviceContext1=%p original1=%p original=%p hr0=0x%08lx",
            ctx, orig, ctx->orig, (unsigned long)hr0);
    return reinterpret_cast<ID3D11DeviceContext1*>(ctx);
}
