#pragma once

#include "DX/DXHelper.h"
#include "pch.h"
#include <ppltasks.h>

#include "StepTimer.h"

inline void ThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    // Set a breakpoint on this line to catch DirectX API errors.
    throw Platform::Exception::CreateException(hr);
  }
}

namespace LibVDWUP {

namespace DXGraphics {
[Windows::Foundation::Metadata::WebHostHidden] public ref class DXHelperWrapper sealed
    : public Windows::UI::Xaml::Controls::SwapChainPanel {
 public:
  DXHelperWrapper();
  void Render();
  void StartRenderLoop();
  void StopRenderLoop();


 private
 protected:
  void RenderBitmapOnWindow(ComPtr<ID2D1Bitmap> pBitmap);
  ComPtr<ID2D1Bitmap> CreateBitmapFromVideoSample(IMFSample  pSample, const UINT32& width,
                                                  const UINT32& height);
  void Present();
  void CreateDeviceIndependentResources();
  void CreateDeviceResources();
  void CreateSizeDependentResources();

  void OnDeviceLost();

  void OnSizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e);
  void OnCompositionScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel ^ sender,
                                 Platform::Object ^ args);

  ComPtr<IDXGIOutput> m_dxgiOutput;

  ComPtr<ID3D11Device1> m_d3dDevice;
  ComPtr<ID3D11DeviceContext1> m_d3dContext;
  ComPtr<IDXGISwapChain2> m_swapChain;

  ComPtr<ID2D1RenderTarget> m_renderTarget;

  ComPtr<ID2D1Factory2> m_d2dFactory;
  ComPtr<ID2D1Device> m_d2dDevice;

  Concurrency::critical_section m_criticalSection;

  float m_renderTargetHeight;
  float m_renderTargetWidth;

  float m_compositionScaleX;
  float m_compositionScaleY;

  float m_height;
  float m_width;

  bool m_loadingComplete = false;

  Windows::Foundation::IAsyncAction ^ m_renderLoopWorker;

  DX::StepTimer m_timer;

 private:
  ~DXHelperWrapper();
};
}  // namespace DXGraphics
}  // namespace LibVDWUP
