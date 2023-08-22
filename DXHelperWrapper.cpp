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
      m_height(1.0f),
      m_width(1.0f) {
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


  ComPtr<IDXGIFactory1> dxgiFactory;
  winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

  ComPtr<IDXGIAdapter> dxgiAdapter;
  winrt::check_hresult(dxgiFactory->EnumAdapters(0, &dxgiAdapter));

  winrt::check_hresult(dxgiAdapter->EnumOutputs(0, &m_dxgiOutput));

  m_loadingComplete = true;
}

void DXHelperWrapper::CreateSizeDependentResources() {
  m_renderTargetWidth = m_width * m_compositionScaleX;
  m_renderTargetHeight = m_height * m_compositionScaleY;

  // If the swap chain already exists, then resize it.
  if (m_swapChain != nullptr) {
    if (m_renderTarget) {
      m_renderTarget.Reset();
    }
    HRESULT hr = m_swapChain->ResizeBuffers(
        2, static_cast<UINT>(m_renderTargetWidth),
        static_cast<UINT>(m_renderTargetHeight), DXGI_FORMAT_B8G8R8A8_UNORM, 0);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
      OnDeviceLost();
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
    swapChainDesc.BufferUsage =
        DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;
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

    ThrowIfFailed(dxgiDevice->SetMaximumFrameLatency(1));

    Dispatcher->RunAsync(
        CoreDispatcherPriority::Normal,
        ref new DispatchedHandler(
            [=]() {
              // Get backing native interface for SwapChainPanel.
              ComPtr<ISwapChainPanelNative> panelNative;
              ThrowIfFailed(reinterpret_cast<IUnknown*>(this)->QueryInterface(
                  IID_PPV_ARGS(&panelNative)));

              ThrowIfFailed(panelNative->SetSwapChain(m_swapChain.Get()));
            },
            CallbackContext::Any));
  } 

  ComPtr<IDXGISurface> dxgiBackbuffer;
  m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackbuffer));

  D2D1_RENDER_TARGET_PROPERTIES renderTargetProps =
      D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
                                   D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                     D2D1_ALPHA_MODE_IGNORE));

  winrt::check_hresult(m_d2dFactory->CreateDxgiSurfaceRenderTarget(
      dxgiBackbuffer.Get(), &renderTargetProps, m_renderTarget.GetAddressOf()));
   
}

void DXHelperWrapper::Render() {
  if (!m_loadingComplete) {
    return;
  }

  m_renderTarget->BeginDraw();
  m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
  m_renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));
  m_renderTarget->EndDraw();

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
    OnDeviceLost();
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

  m_renderTarget = nullptr;
  m_renderTarget->Flush();
  m_renderTarget.Reset();

  m_swapChain = nullptr;

  // Make sure the rendering state has been released.
  m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);

  m_d2dDevice = nullptr;

  m_d3dContext->Flush();

  CreateDeviceResources();
  CreateSizeDependentResources();

  Render();
}


ComPtr<ID2D1Bitmap> DXHelper::CreateBitmapFromVideoSample(
    IMFSample* pSample, const UINT32& width, const UINT32& height) {
  ComPtr<IMFMediaBuffer> buffer;
  winrt::check_hresult(pSample->ConvertToContiguousBuffer(&buffer));

  BYTE* data = nullptr;
  DWORD maxDataLength = 0;
  DWORD currentDataLength = 0;

  winrt::check_hresult(buffer->Lock(&data, &maxDataLength, &currentDataLength));

  UINT32 pitch = width * sizeof(UINT32);

  D2D1_BITMAP_PROPERTIES bitmapProperties = {};
  ZeroMemory(&bitmapProperties, sizeof(bitmapProperties));
  bitmapProperties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;

  ComPtr<ID2D1Bitmap> bitmap;
  winrt::check_hresult(m_renderTarget->CreateBitmap(
      D2D1::SizeU(width, height), data, pitch, bitmapProperties, &bitmap));

  winrt::check_hresult(buffer->Unlock());

  return bitmap;
}

void DXHelper::RenderBitmapOnWindow(ComPtr<ID2D1Bitmap> pBitmap) {
  m_renderTarget->BeginDraw();
  m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
  m_renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

  D2D1_SIZE_F window = m_renderTarget->GetSize();
  D2D1_SIZE_F picture = pBitmap->GetSize();

  float windowAR = window.width / window.height;
  float pictureAR = picture.width / picture.height;

  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  float scale = 0.0f;

  if (windowAR > pictureAR) {
    scale = window.height / 1080.0f;
    float newWidth = 1920 * scale;
    left = (window.width - newWidth) * 0.5f;
    right = left + newWidth;
    bottom = window.height;
  } else {
    scale = window.width / 1920.0f;
    float newHeight = scale * 1080;
    top = (window.height - newHeight) * 0.5f;
    bottom = top + newHeight;
    right = window.width;
  }

  D2D1_RECT_F destinationRect = D2D1::RectF(left, top, right, bottom);

  m_renderTarget->DrawBitmap(pBitmap.Get(), destinationRect);

  m_renderTarget->EndDraw();

  winrt::check_hresult(m_swapChain->Present(1, 0));
}
