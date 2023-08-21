#include "pch.h"

#include "DXHelperWrapper.h"

#include <concrt.h>
#include <windows.ui.xaml.media.dxinterop.h>


using Microsoft::WRL::ComPtr;
using namespace LibVDWUP::DXGraphics;
using namespace Windows::Foundation;

using namespace D2D1;
using namespace DX;
using namespace Platform;
using namespace Concurrency;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::Storage::Streams;
using namespace Windows::System::Threading;

static const float m_dipsPerInch = 96.0f;

DXHelperWrapper::DXHelperWrapper()
    : m_compositionScaleX(1.0f),
      m_compositionScaleY(1.0f),
      m_height(720.0f),
      m_width(1280.0f) {
  this->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler(
      this, &DXHelperWrapper::OnSizeChanged);
  this->CompositionScaleChanged +=
      ref new Windows::Foundation::TypedEventHandler<SwapChainPanel ^,
                                                     Object ^>(
          this, &DXHelperWrapper::OnCompositionScaleChanged);

  critical_section::scoped_lock lock(m_criticalSection);

  CreateDeviceIndependentResources();
  CreateDeviceResources();
  CreateSizeDependentResources();
}

void DXHelperWrapper::CreateDeviceIndependentResources() {
  D2D1_FACTORY_OPTIONS options;
  ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));

  winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                         __uuidof(ID2D1Factory2), &options,
                                         &m_d2dFactory));
}

void DXHelperWrapper::CreateDeviceResources() {

  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1};

  // Create the DX11 API device object, and get a corresponding context.
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  winrt::check_hresult(D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, creationFlags, featureLevels,
      ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &device, NULL, &context));

  // Get D3D11.1 device
  winrt::check_hresult(device.As(&m_d3dDevice));

  // Get D3D11.1 context
  winrt::check_hresult(context.As(&m_d3dContext));

  // Get underlying DXGI device of D3D device
  ComPtr<IDXGIDevice> dxgiDevice;
  winrt::check_hresult(m_d3dDevice.As(&dxgiDevice));

  // Get D2D device
  winrt::check_hresult(
      m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));

  // Get D2D context
  winrt::check_hresult(m_d2dDevice->CreateDeviceContext(
      D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext));

  m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void DXHelperWrapper::CreateSizeDependentResources() {
  // Ensure dependent objects have been released.
  m_d2dContext->SetTarget(nullptr);
  m_d2dTargetBitmap = nullptr;
  m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
  m_d3dContext->Flush();

  m_renderTargetWidth = m_width * m_compositionScaleX;
  m_renderTargetHeight = m_height * m_compositionScaleY;

  // If the swap chain already exists, then resize it.
  if (m_swapChain != nullptr) {
    HRESULT hr = m_swapChain->ResizeBuffers(
        2, static_cast<UINT>(m_renderTargetWidth),
        static_cast<UINT>(m_renderTargetHeight), DXGI_FORMAT_B8G8R8A8_UNORM, 0);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
      // If the device was removed for any reason, a new device and swap chain
      // will need to be created.
      return;

    } else {
      winrt::check_hresult(hr);
    }
  } else  // Otherwise, create a new one.
  {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
    swapChainDesc.Width = static_cast<UINT>(m_renderTargetWidth);
    swapChainDesc.Height = static_cast<UINT>(m_renderTargetHeight);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = false;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.Flags = 0;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    // Get underlying DXGI Device from D3D Device.
    ComPtr<IDXGIDevice1> dxgiDevice;
    winrt::check_hresult(m_d3dDevice.As(&dxgiDevice));

    // Get adapter.
    ComPtr<IDXGIAdapter> dxgiAdapter;
    winrt::check_hresult(dxgiDevice->GetAdapter(&dxgiAdapter));

    // Get factory.
    ComPtr<IDXGIFactory2> dxgiFactory;
    winrt::check_hresult(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGISwapChain1> swapChain;
    // Create swap chain.
    winrt::check_hresult(dxgiFactory->CreateSwapChainForComposition(
        m_d3dDevice.Get(), &swapChainDesc, nullptr, &swapChain));
    swapChain.As(&m_swapChain);

    winrt::check_hresult(dxgiDevice->SetMaximumFrameLatency(1));

    Dispatcher->RunAsync(
        CoreDispatcherPriority::Normal,
        ref new DispatchedHandler(
            [=]() {
              // Get backing native interface for SwapChainPanel.
              ComPtr<ISwapChainPanelNative> panelNative;
              winrt::check_hresult(
                  reinterpret_cast<IUnknown*>(this)->QueryInterface(
                      IID_PPV_ARGS(&panelNative)));

              winrt::check_hresult(
                  panelNative->SetSwapChain(m_swapChain.Get()));
            },
            CallbackContext::Any));


    //m_contentRect = D2D1::RectF(100, 100, 300, 300);
    //m_backgroundColor = ColorF(ColorF::AliceBlue);

    //winrt::check_hresult(m_d2dContext->CreateSolidColorBrush(ColorF(ColorF::Black), &m_strokeBrush));
    //winrt::check_hresult(m_d2dContext->CreateSolidColorBrush(ColorF(ColorF::Green), &m_fillBrush));

    //m_d2dContext->SetUnitMode(D2D1_UNIT_MODE::D2D1_UNIT_MODE_PIXELS);
  }
}

void DXHelperWrapper::Render() {

  m_d2dContext->BeginDraw();
  m_d2dContext->Clear(m_backgroundColor);

  //m_d2dContext->FillRectangle(m_contentRect, m_fillBrush.Get());
  //m_d2dContext->DrawRectangle(m_contentRect, m_strokeBrush.Get());

  winrt::check_hresult(m_d2dContext->EndDraw());

  Present();
}

void DXHelperWrapper::Present() {
  DXGI_PRESENT_PARAMETERS parameters = {0};
  parameters.DirtyRectsCount = 0;
  parameters.pDirtyRects = nullptr;
  parameters.pScrollRect = nullptr;
  parameters.pScrollOffset = nullptr;


  winrt::check_hresult(m_swapChain->Present1(1, 0, &parameters));
}

void DXHelperWrapper::OnSizeChanged(Object ^ sender, SizeChangedEventArgs ^ e) {
  if (m_width != e->NewSize.Width || m_height != e->NewSize.Height) {
    critical_section::scoped_lock lock(m_criticalSection);

    m_width = max(e->NewSize.Width, 1.0f);
    m_height = max(e->NewSize.Height, 1.0f);

    CreateSizeDependentResources();
  }
}

void DXHelperWrapper::OnCompositionScaleChanged(SwapChainPanel ^ sender,
                                                Object ^ args) {
  if (m_compositionScaleX != CompositionScaleX ||
      m_compositionScaleY != CompositionScaleY) {

    critical_section::scoped_lock lock(m_criticalSection);

    m_compositionScaleX = this->CompositionScaleX;
    m_compositionScaleY = this->CompositionScaleY;

    CreateSizeDependentResources();
  }
}

DXHelperWrapper::~DXHelperWrapper() { m_renderLoopWorker->Cancel(); }

void DXHelperWrapper::StartRenderLoop() {
  if (m_renderLoopWorker != nullptr &&
      m_renderLoopWorker->Status == AsyncStatus::Started) {
    return;
  }

  auto self = this;
  auto workItemHandler = ref new WorkItemHandler([self](IAsyncAction ^ action) {
    while (action->Status == AsyncStatus::Started) {
      self->m_timer.Tick([&]() {
        critical_section::scoped_lock lock(self->m_criticalSection);
        self->Render();
      });

      self->m_dxgiOutput->WaitForVBlank();
    }
  });

  m_renderLoopWorker = ThreadPool::RunAsync(
      workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
}

void DXHelperWrapper::StopRenderLoop() {
  m_renderLoopWorker->Cancel();
}

