#pragma once

#include "DX/DXHelper.h"
#include "pch.h"

#include "StepTimer.h"

namespace LibVDWUP {

namespace DXGraphics {
[Windows::Foundation::Metadata::WebHostHidden] public ref class DXHelperWrapper sealed
    : public Windows::UI::Xaml::Controls::SwapChainPanel {
 public:
  DXHelperWrapper();
  void Present();
  void Render();
  void StartRenderLoop();
  void StopRenderLoop();

 private
 protected:
  void CreateDeviceIndependentResources();
  void CreateDeviceResources();
  void CreateSizeDependentResources();

  void OnSizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e);
  void OnCompositionScaleChanged(Windows::UI::Xaml::Controls::SwapChainPanel ^ sender,
                                 Platform::Object ^ args);

  ComPtr<IDXGIOutput> m_dxgiOutput;
  Windows::Foundation::IAsyncAction ^ m_renderLoopWorker;

  DX::StepTimer m_timer;

  ComPtr<ID3D11Device1> m_d3dDevice;
  ComPtr<ID3D11DeviceContext1> m_d3dContext;
  ComPtr<IDXGISwapChain2> m_swapChain;

  ComPtr<ID2D1Factory2> m_d2dFactory;
  ComPtr<ID2D1Device> m_d2dDevice;
  ComPtr<ID2D1DeviceContext> m_d2dContext;
  ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;

  D2D1_RECT_F m_contentRect;
  D2D1_COLOR_F m_backgroundColor;

  ComPtr<ID2D1SolidColorBrush> m_fillBrush;
  ComPtr<ID2D1SolidColorBrush> m_strokeBrush;

  Concurrency::critical_section m_criticalSection;

  float m_renderTargetHeight;
  float m_renderTargetWidth;

  float m_compositionScaleX;
  float m_compositionScaleY;

  float m_height;
  float m_width;

 private:
  ~DXHelperWrapper();
};
}  // namespace DXGraphics
}  // namespace LibVDWUP
