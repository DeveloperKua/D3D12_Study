//***************************************************************************************
// d3dApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "d3dApp.h"
#include <WindowsX.h>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

LRESULT CALLBACK
MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
    return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

D3DApp* D3DApp::mApp = nullptr;

D3DApp* D3DApp::GetApp()
{
    return mApp;
}

//생성자
D3DApp::D3DApp(HINSTANCE hInstance)
:	mhAppInst(hInstance)
{
    // Only one D3DApp can be constructed.
    assert(mApp == nullptr);
    mApp = this;
}

//소멸자(Com 인터페이스들 해제, 명령 대기열 비우기
D3DApp::~D3DApp()
{
	if(md3dDevice != nullptr)
		FlushCommandQueue();
}

//응용 프로그램 인스턴스 핸들의 복사본을 돌려주는 접근함수
HINSTANCE D3DApp::AppInst()const
{
	return mhAppInst;
}

//주 창 핸들의 복사본을 돌려주는 접근 함수
HWND D3DApp::MainWnd()const
{
	return mhMainWnd;
}

//후면 버퍼의 종횡비, 즉 높이에 대한 너비의 비율을 돌려줌
float D3DApp::AspectRatio()const
{
	return static_cast<float>(mClientWidth) / mClientHeight;
}

//4xMSAA 활성화 여부
bool D3DApp::Get4xMsaaState()const
{
    return m4xMsaaState;
}

//4xMSAA 활성화 / 비활성화 설정
void D3DApp::Set4xMsaaState(bool value)
{
    if(m4xMsaaState != value)
    {
        m4xMsaaState = value;

        // Recreate the swapchain and buffers with new multisample settings.
        CreateSwapChain();
        OnResize();
    }
}

//응용 프로그램이 메시지 루프를 감싸는 메서드
//만약 윈도우 메세지가 없다면 Update(), Draw()같은 게임 로직을 처리
int D3DApp::Run()
{
	//메세지 변수
	MSG msg = {0};
 
	//타이머 리셋
	mTimer.Reset();
	//루프 진행

	//윈도우 메세지가 있으면 처리함
	while(msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if(PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
		{
            TranslateMessage( &msg );
            DispatchMessage( &msg );
		}
		// Otherwise, do animation/game stuff.
		else
        {	
			mTimer.Tick();

			//앱이 실행중일 때
			if( !mAppPaused )
			{
				//프레임 계산
				CalculateFrameStats();
				//씬 업데이트
				Update(mTimer);	
				//씬 그리기
                Draw(mTimer);
			}
			//앱이 멈추면
			else
			{
				Sleep(100);
			}
        }
    }

	return (int)msg.wParam;
}

//각종 초기화 가상 메서드
bool D3DApp::Initialize()
{
	//메인 윈도우 초기화
	if(!InitMainWindow())
		return false;//실패시

	//메인 Dx 초기화
	if(!InitDirect3D())
		return false;//실패시

    // Do the initial resize code.
    OnResize();

	return true;
}
 
//RTV, DSV 뷰 힙 구조체 생성
void D3DApp::CreateRtvAndDsvDescriptorHeaps()
{
	//RTV 서술자 힙 구조체
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	//RTV 서술자를 담을 서술자 힙을 생성
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	//DSV 서술자 힙 구조체
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	//DSV 서술자를 담을 서술자 힙을 생성
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

//만약 클라이언트의 창 크기가 변한다면 재설정 해줘야 하는 멤버나 옵션들을 설정해주는 메서드
void D3DApp::OnResize()
{
	assert(md3dDevice);
	assert(mSwapChain);
    assert(mDirectCmdListAlloc);

	// Flush before changing any resources.
	FlushCommandQueue();

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Release the previous resources we will be recreating
	//버퍼들 리셋.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();
	
	// Resize the swap chain.
    ThrowIfFailed(mSwapChain->ResizeBuffers(
		SwapChainBufferCount, 
		mClientWidth, mClientHeight, 
		mBackBufferFormat, 
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrBackBuffer = 0;
 
	// RTV 서술자 힙에 저장된 RTV 서술자 핸들
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	// 스왑 체인의 버퍼 수만큼
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		// 스왑 체인의 i번째 버퍼를 얻는다.
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		// 그 버퍼에 대한 RTV를 생성한다.
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		// 힙의 다음 항목으로 넘어간다.
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	// 깊이 스텐실 버퍼와 뷰를 생성한다.
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;

	// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
	// the depth buffer.  Therefore, because we need to create two views to the same resource:
	//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
	//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
	// we need to create the depth buffer resource with a typeless format.  
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

	// 전체 자원이 밉맵 수준 0에 대한 서술자를,
	// 해당 자원의 픽셀 형식을 적용해서 생성한다.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	// 자원을 초기 상태에서 깊이 버퍼로 사용할 수 있는 상태로 전이한다.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
	
    // Execute the resize commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	FlushCommandQueue();

	// Update the viewport transform to cover the client area.
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width    = static_cast<float>(mClientWidth);
	mScreenViewport.Height   = static_cast<float>(mClientHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	//mCommandList->RSSetViewports(1, &mScreenViewport);

    mScissorRect = { mClientWidth / 4, mClientHeight / 4, mClientWidth / 2 + mClientWidth / 4, mClientHeight / 2 + mClientHeight / 4 };
	//mCommandList->RSSetScissorRects(1, &mScissorRect);
}
 
//응용 프로그램 주 창의 메세지 처리부(윈도우 프로시저 함수)를 넣음
LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg )
	{
	// WM_ACTIVATE is sent when the window is activated or deactivated.  
	// We pause the game when the window is deactivated and unpause it 
	// when it becomes active.  
	case WM_ACTIVATE:
		if( LOWORD(wParam) == WA_INACTIVE )
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;

	// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		mClientWidth  = LOWORD(lParam);
		mClientHeight = HIWORD(lParam);
		if( md3dDevice )
		{
			if( wParam == SIZE_MINIMIZED )
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if( wParam == SIZE_MAXIMIZED )
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if( wParam == SIZE_RESTORED )
			{
				
				// Restoring from minimized state?
				if( mMinimized )
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}

				// Restoring from maximized state?
				else if( mMaximized )
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if( mResizing )
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					OnResize();
				}
			}
		}
		return 0;

	// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing  = true;
		mTimer.Stop();
		return 0;

	// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
	// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		mAppPaused = false;
		mResizing  = false;
		mTimer.Start();
		OnResize();
		return 0;
 
	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

	// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200; 
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
    case WM_KEYUP:
        if(wParam == VK_ESCAPE)
        {
            PostQuitMessage(0);
        }
        else if((int)wParam == VK_F2)
            Set4xMsaaState(!m4xMsaaState);

        return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

//응용 프로그램의 주 창을 초기화
bool D3DApp::InitMainWindow()
{
	//윈도우 클래스 생성
	WNDCLASS wc;
	
	//클래스 속성 설정
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = MainWndProc; 
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = mhAppInst;
	wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = L"MainWnd";

	//윈도우 클래스 등록
	if( !RegisterClass(&wc) )
	{
		//등록 실패시 메세지 박스
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	// Window Size  Setting
	RECT R = { 0, 0, mClientWidth, mClientHeight };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width  = R.right - R.left;
	int height = R.bottom - R.top;

	//윈도우 생성
	mhMainWnd = CreateWindow(L"MainWnd", mMainWndCaption.c_str(), 
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, mhAppInst, 0); 
	//실패시
	if( !mhMainWnd )
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	//윈도우 출력
	ShowWindow(mhMainWnd, SW_SHOW);
	//윈도우 갱신
	UpdateWindow(mhMainWnd);

	return true;
}

//DirectX 초기화 구조체나 매크로 상수의 자세한 설명은 아래 블로그
//https://lipcoder.tistory.com/77
bool D3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) 
	//D3D12 디버그층 활성화
{
	ComPtr<ID3D12Debug> debugController;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
	debugController->EnableDebugLayer();
}
#endif

	//EnumWarpAdapter 호출 전에 mdxgiFactory를 생성
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

	//하드웨어 어댑터를 나타내는 장치를 생성해본다.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             //null = 기본 어댑터
		D3D_FEATURE_LEVEL_11_0, //응용프로그램이 요구하는 최소 기능 수준 (즉 최소 11수준의 Dx를 요구함)
		IID_PPV_ARGS(&md3dDevice));

	// 실패한다면 warp어댑터를 나타내는 장치를 생성
	if(FAILED(hardwareResult))
	{
		//warp 어댑터를 생성하기 전에 EnumWarpAdapter 메서도를 호출해 주어야함
		//이렇게 해야 디스플레이 어댑터 나열시 wrap어댑터가 나타남
		ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&md3dDevice)));
	}

	//펜스 객체 생성
	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&mFence)));

	//렌더 타겟 뷰(서술자) 크기
	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//깊이 스텐실 뷰(서술자)크기
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	//상수 버퍼 (뷰)서술자 크기
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Check 4X MSAA quality support for our back buffer format.
    // All Direct3D 11 capable devices support 4X MSAA for all render 
    // target formats, so we only need to check quality support.
	// 다음 코드를 통해 4X MSAA 지원 여부 명시적으로 점검
	
	//다중 샘플링 (MSAA) 설정 구조체
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = mBackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;

	//후면 버퍼 4X MSAA 지원 여부 검사
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

    m4xMsaaQuality = msQualityLevels.NumQualityLevels;

	//현재 장치의 기능 수준 (D3D_FEATURE_LEVEL)이 Direct3D12 이상임을 확인했다면 
	//4X MSAA가 항상 지원됨, 그러므로 반환된 품질 수준(m4xMssaQuality)은 항상 0보다 커야함
	//이를 아래 코드 assert를 통해 확인함
	assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
	
#ifdef _DEBUG
    LogAdapters();
#endif

	//명령에 필요한 객체들 생성
	CreateCommandObjects();
	
	//스왑 체인 생성
    CreateSwapChain();

	//서술자 힙 생성
    CreateRtvAndDsvDescriptorHeaps();

	return true;
}

//명령대기열, 명령 목록 할당자, 명령 목록
void D3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	//명령 큐(대기열) 인터페이스 생성
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	//명령 할당자 인터페이스 생성
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

	//명령 리스트 인터페이스 생성
	ThrowIfFailed(md3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(), // 연관된 명령 할당자
		nullptr,                   // 초기 파이프라인 상태 객체
		//(해당 예제는 어떤 그리기 명령도 제출하지 않으므로 유효한 파이프라인 상태 객체를 지정하지 않아도 됨

		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	// 단힌 상태로 시작. 이후 명령 리스트를 처음 참조할 때
	// Reset을 호출하는데, Reset를 호출하려면 명령 리스트가
	// 닫혀있어야 하기 때문임.
	mCommandList->Close();
}

//스왑 체인 생성
void D3DApp::CreateSwapChain()
{
    // Release the previous swapchain we will be recreating.
    mSwapChain.Reset();

	//교환 사슬 구조체 정의
	//BufferDesc - 후면버퍼의 속성(디스플레이 형식) 지정
	//SampleDesc - 다중 샘플링의 표본 개수와 품질을 지정
	//BufferUsage - 후면 버퍼에 대한 표면 사용 방식과 CPU 접근방법
	//BufferCount - 교환 사슬이 사용할 버퍼의 수를 지정
	//OutPutWindow - 출력 윈도우의 핸들을 지정(NULL이 될 수 없음)
	//Windowed - 윈도우 모드 또는 전체 화면 모드를 지정
	//SwapEffect - 스와핑 처리하는 선택사항 지정
	//Flags - 스왑 체인의 동작에 대한 선택사항을 지정(보통 0)
    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = mClientWidth;//버퍼의 가로 크기 픽셀단위로 지정
    sd.BufferDesc.Height = mClientHeight;//버퍼의 세로 크기를 픽셀단위로 지정

	//화면 갱신 비율 -헤르츠 단위로 지정(DXGI_RATIONAL 구조체 사용)
	sd.BufferDesc.RefreshRate.Numerator = 60;//분자
    sd.BufferDesc.RefreshRate.Denominator = 1;//분모
    sd.BufferDesc.Format = mBackBufferFormat;//버퍼 디스플레이 형식
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;//스캔 라인 그리기 모드 지정
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;//모니터 해상도에 맞게 확대하는 방법

    sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	//후면 버퍼에 렌더링 할 것이므로 DXGI_USAGE_RENDER_TARGET_OUTPUT
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = mhMainWnd;
    sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	//스왑 체인은 명령 리스트를 이용하여 방출(flush)을 수행함
    ThrowIfFailed(mdxgiFactory->CreateSwapChain(
		mCommandQueue.Get(),
		&sd, 
		mSwapChain.GetAddressOf()));
}

//GPU가 명령 대기열에 있는 모든 명령의 처리를 마칠때까지 CPU가 기다리게 함
void D3DApp::FlushCommandQueue()
{
	// Advance the fence value to mark commands up to this fence point.
    mCurrentFence++;

    // Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	// Wait until the GPU has completed commands up to this fence point.
    if(mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

        // Fire event when GPU hits current fence.  
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

        // Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
	}
}

//현재 후면 버퍼에 대한 ID3D12Resource를 돌려줌
ID3D12Resource* D3DApp::CurrentBackBuffer()const
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

//현재 후면 버퍼에 대한 RTV를 돌려줌
D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::CurrentBackBufferView()const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer,
		mRtvDescriptorSize);
}

//현재 후면 버퍼에 대한 DSV를 돌려줌
D3D12_CPU_DESCRIPTOR_HANDLE D3DApp::DepthStencilView()const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

//평균 프레임 수(FPS)와 평균 프레임 당 밀리초(ms)를 계산
void D3DApp::CalculateFrameStats()
{
	// 평균 FPS를 계산하며, 하나의 프레임을
	// 렌더링하는 데 걸리는 평균 시간도 계산
	// 또한 이 통계치들을 창의 제목줄에 추가
    
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;
	frameCnt++;

	// 1초동안 평균 프레임 수 계산
	if( (mTimer.TotalTime() - timeElapsed) >= 1.0f )
	{
		float fps = (float)frameCnt; // fps = frameCnt / 1
		float mspf = 1000.0f / fps;

        wstring fpsStr = to_wstring(fps);
        wstring mspfStr = to_wstring(mspf);
		wstring timeStr = to_wstring(mTimer.TotalTime());

        wstring windowText = mMainWndCaption +
            L"    fps: " + fpsStr +
            L"   mspf: " + mspfStr +
			L"   time: " + timeStr;

        SetWindowText(mhMainWnd, windowText.c_str());
		
		// Reset for next average.
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

//시스템의 모든 디스플레이 어댑터를 열거
void D3DApp::LogAdapters()
{
    UINT i = 0;
    IDXGIAdapter* adapter = nullptr;
    std::vector<IDXGIAdapter*> adapterList;
    while(mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        std::wstring text = L"***Adapter: ";
        text += desc.Description;
        text += L"\n";

        OutputDebugString(text.c_str());

        adapterList.push_back(adapter);
        
        ++i;
    }

    for(size_t i = 0; i < adapterList.size(); ++i)
    {
        LogAdapterOutputs(adapterList[i]);
        ReleaseCom(adapterList[i]);
    }
}

//주어진 어댑터와 연관된 모든 출력을 열거
void D3DApp::LogAdapterOutputs(IDXGIAdapter* adapter)
{
    UINT i = 0;
    IDXGIOutput* output = nullptr;
    while(adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);
        
        std::wstring text = L"***Output: ";
        text += desc.DeviceName;
        text += L"\n";
        OutputDebugString(text.c_str());

        LogOutputDisplayModes(output, mBackBufferFormat);

        ReleaseCom(output);

        ++i;
    }
}

//주어진 출력과 픽셀형식의 조합이 지원하는 모든 디스플레이 모드를 나열
void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
{
    UINT count = 0;
    UINT flags = 0;

    // Call with nullptr to get list count.
    output->GetDisplayModeList(format, flags, &count, nullptr);

    std::vector<DXGI_MODE_DESC> modeList(count);
    output->GetDisplayModeList(format, flags, &count, &modeList[0]);

    for(auto& x : modeList)
    {
        UINT n = x.RefreshRate.Numerator;
        UINT d = x.RefreshRate.Denominator;
        std::wstring text =
            L"Width = " + std::to_wstring(x.Width) + L" " +
            L"Height = " + std::to_wstring(x.Height) + L" " +
            L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
            L"\n";

        ::OutputDebugString(text.c_str());
    }
}