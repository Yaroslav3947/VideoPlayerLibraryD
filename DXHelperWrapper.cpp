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
  m_backgroundColor = ColorF(ColorF::AliceBlue);
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

  ComPtr<IDXGIFactory1> dxgiFactory;
  winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

  ComPtr<IDXGIAdapter> dxgiAdapter;
  winrt::check_hresult(dxgiFactory->EnumAdapters(0, &dxgiAdapter));

  winrt::check_hresult(dxgiAdapter->EnumOutputs(0, &m_dxgiOutput));

  m_loadingComplete = true;
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
    ThrowIfFailed(m_d3dDevice.As(&dxgiDevice));

    // Get adapter.
    ComPtr<IDXGIAdapter> dxgiAdapter;
    ThrowIfFailed(dxgiDevice->GetAdapter(&dxgiAdapter));

    // Get factory.
    ComPtr<IDXGIFactory2> dxgiFactory;
    ThrowIfFailed(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGISwapChain1> swapChain;
    // Create swap chain.
    ThrowIfFailed(dxgiFactory->CreateSwapChainForComposition(
        m_d3dDevice.Get(), &swapChainDesc, nullptr, &swapChain));
    swapChain.As(&m_swapChain);

    // Ensure that DXGI does not queue more than one frame at a time. This both
    // reduces latency and ensures that the application will only render after
    // each VSync, minimizing power consumption.
    ThrowIfFailed(dxgiDevice->SetMaximumFrameLatency(1));

    Dispatcher->RunAsync(
        CoreDispatcherPriority::Normal,
        ref new DispatchedHandler(
            [=]() {
              // Get backing native interface for SwapChainPanel.
              ComPtr<ISwapChainPanelNative> panelNative;
              ThrowIfFailed(reinterpret_cast<IUnknown*>(this)->QueryInterface(
                  IID_PPV_ARGS(&panelNative)));

              // Associate swap chain with SwapChainPanel.  This must be done on
              // the UI thread.
              ThrowIfFailed(panelNative->SetSwapChain(m_swapChain.Get()));
            },
            CallbackContext::Any));
  }

  // Ensure the physical pixel size of the swap chain takes into account both
  // the XAML SwapChainPanel's logical layout size and any cumulative
  // composition scale applied due to zooming, render transforms, or the
  // system's current scaling plateau. For example, if a 100x100 SwapChainPanel
  // has a cumulative 2x scale transform applied, we instead create a 200x200
  // swap chain to avoid artifacts from scaling it up by 2x, then apply an
  // inverse 1/2x transform to the swap chain to cancel out the 2x transform.
  DXGI_MATRIX_3X2_F inverseScale = {0};
  inverseScale._11 = 1.0f / m_compositionScaleX;
  inverseScale._22 = 1.0f / m_compositionScaleY;

  m_swapChain->SetMatrixTransform(&inverseScale);

  D2D1_BITMAP_PROPERTIES1 bitmapProperties = BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      m_dipsPerInch * m_compositionScaleX, m_dipsPerInch * m_compositionScaleY);

  // Direct2D needs the DXGI version of the backbuffer surface pointer.
  ComPtr<IDXGISurface> dxgiBackBuffer;
  winrt::check_hresult(
      m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer)));

  // Get a D2D surface from the DXGI back buffer to use as the D2D render
  // target.
  winrt::check_hresult(m_d2dContext->CreateBitmapFromDxgiSurface(
      dxgiBackBuffer.Get(), &bitmapProperties, &m_d2dTargetBitmap));

  m_d2dContext->SetDpi(m_dipsPerInch * m_compositionScaleX,
                       m_dipsPerInch * m_compositionScaleY);
  m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
}

void DXHelperWrapper::Render() {
  if (!m_loadingComplete || m_d2dContext == nullptr || m_swapChain == nullptr) {
    return;
  }

  m_d2dContext->BeginDraw();
  m_d2dContext->Clear(m_backgroundColor);

  ThrowIfFailed(m_d2dContext->EndDraw());

  Present();
}

void DXHelperWrapper::Present() {
  DXGI_PRESENT_PARAMETERS parameters = {0};
  parameters.DirtyRectsCount = 0;
  parameters.pDirtyRects = nullptr;
  parameters.pScrollRect = nullptr;
  parameters.pScrollOffset = nullptr;

  HRESULT hr = S_OK;

  hr = m_swapChain->Present1(1, 0, &parameters);

  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    return;
  } else {
    winrt::check_hresult(hr);
  }
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

void DXHelperWrapper::StopRenderLoop() { m_renderLoopWorker->Cancel(); }

void DXHelperWrapper::OnDeviceLost() {
  m_loadingComplete = false;

  m_swapChain = nullptr;

  // Make sure the rendering state has been released.
  m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);

  m_d2dContext->SetTarget(nullptr);
  m_d2dTargetBitmap = nullptr;

  m_d2dContext = nullptr;
  m_d2dDevice = nullptr;

  m_d3dContext->Flush();

  CreateDeviceResources();
  CreateSizeDependentResources();

  Render();
}
