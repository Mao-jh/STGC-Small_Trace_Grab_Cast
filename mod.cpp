#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <dwmapi.h>
#include <mmsystem.h>
using namespace Gdiplus;
using namespace std;
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
//windres resource.rc -O coff -o resource.res
//g++ -fexec-charset=GBK -finput-charset=UTF-8 mod.cpp resource.res -o STGC.exe -lgdiplus -lgdi32 -luser32 -lwinmm -mwindows
// 所有常量定义
constexpr const char* APP_WINDOW_CLASS = "MouseFollowWindowClass";
constexpr const char* FONT_NAME = "宋体";
constexpr COLORREF WINDOW_BG_COLOR = RGB(45, 45, 45);
constexpr int SMART_MARGIN = 20;
constexpr UINT_PTR CHECK_TIMER_ID = 2;
constexpr DWORD CHECK_INTERVAL = 500;
constexpr DWORD CLICK_TIMEOUT = 500;

// 在常量定义区域添加
constexpr UINT_PTR MOVE_TIMER_ID = 1;    // 移动定时器ID
constexpr DWORD MOVE_TIMER_INTERVAL = 1;  // 提高定时器频率到最大

// 在常量定义区域添加图标ID
#define IDI_MYICON 101

// 函数前向声明
void CheckAndFixImageDisplay(HWND hwnd);

// 窗口类名和控件ID常量
// 添加新的常量和结构体
struct Position {
    double x;
    double y;
    Position(double _x = 0, double _y = 0) : x(_x), y(_y) {}
};

// 全局变量
bool g_isFollowEnabled = false;
HWND g_mainWindow = NULL;
HFONT g_hFont = NULL;  // 添加字体句柄
bool g_isMouseOver = false; // 添加鼠标是否在窗口上的标志

// 鼠标钩子句柄
HHOOK g_mouseHook = NULL;

// 添加中键点击计数和时间记录
int g_middleClickCount = 0;
DWORD g_lastClickTime = 0;
// 添加全局变量
Position g_currentPos(0, 0);
Position g_targetPos(0, 0);
RECT g_screenRect;

// 添加跟踪状态标志
bool g_isTracking = false;

// 添加智能位置计算函数
Position calculateOptimalPosition(int mouseX, int mouseY, int windowWidth, int windowHeight) {
    // 使用直接赋值代替构造函数
    Position optimal;
    
    // 避免重复计算
    optimal.x = mouseX + 5;
    optimal.y = mouseY - windowHeight - 5;
    
    // 使用位运算优化边界检查
    optimal.x = (optimal.x + windowWidth > g_screenRect.right) ? 
        mouseX - windowWidth - 5 : optimal.x;
    optimal.y = (optimal.y < g_screenRect.top) ? 
        mouseY + 5 : optimal.y;
    
    return optimal;
}

// 修改平滑移动函数
void smoothMove(HWND hwnd) {
    // 直接设置窗口位置到目标位置
    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        static_cast<int>(g_targetPos.x),
        static_cast<int>(g_targetPos.y),
        0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE
    );
    
    // 更新当前位置
    g_currentPos = g_targetPos;
}

// 鼠标钩子回调函数
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_MBUTTONDOWN) {
            DWORD currentTime = GetTickCount();
            if (currentTime - g_lastClickTime <= CLICK_TIMEOUT) {
                g_middleClickCount++;
                if (g_middleClickCount >= 3) {
                    // 连续三次中键点击，关闭程序
                    DestroyWindow(g_mainWindow);
                    return 1;
                }
            } else {
                g_middleClickCount = 1;
            }
            g_lastClickTime = currentTime;
            
            g_isFollowEnabled = !g_isFollowEnabled;  // 在钩子中处理中键点击
            if (!g_isFollowEnabled) {
                SetWindowPos(g_mainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, 
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                // 重新启动检测定时器
                SetTimer(g_mainWindow, CHECK_TIMER_ID, CHECK_INTERVAL, NULL);
            } else {
                // 停止检测定时器
                KillTimer(g_mainWindow, CHECK_TIMER_ID);
                // 执行一次测
                CheckAndFixImageDisplay(g_mainWindow);
            }
        }
        
        if (g_isFollowEnabled) {
            MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
            if (g_mainWindow) {
                if (!g_isTracking) {
                    g_isTracking = true;
                    // 设置窗口为完全穿透
                    SetWindowLong(g_mainWindow, GWL_EXSTYLE, 
                        GetWindowLong(g_mainWindow, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
                }
                
                RECT windowRect;
                GetWindowRect(g_mainWindow, &windowRect);
                int width = windowRect.right - windowRect.left;
                int height = windowRect.bottom - windowRect.top;
                
                // 计算最优位置并直接设置窗口位置
                Position optimal = calculateOptimalPosition(
                    pMouseStruct->pt.x, 
                    pMouseStruct->pt.y,
                    width,
                    height
                );
                
                // 直接移动窗口到目标位置
                SetWindowPos(
                    g_mainWindow,
                    HWND_TOPMOST,
                    static_cast<int>(optimal.x),
                    static_cast<int>(optimal.y),
                    0, 0,
                    SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW
                );
            }
        } else if (g_isTracking) {
            g_isTracking = false;
            // 恢复窗口正常状态
            SetWindowLong(g_mainWindow, GWL_EXSTYLE, 
                GetWindowLong(g_mainWindow, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

// 添加全局变量
Image* g_pImage = nullptr;
bool g_hasImage = false;

// 添加全局变量来存储缩放比例
float g_scaleRatio = 1.0f;
bool g_isResizing = false;
POINT g_lastMousePos = {0, 0};
int g_originalWidth = 0;
int g_originalHeight = 0;

// 添加全局变量
// 添加图片检测函数
void CheckAndFixImageDisplay(HWND hwnd) {
    if (!g_hasImage || !g_pImage) return;
    
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    int currentWindowWidth = windowRect.right - windowRect.left;
    int currentWindowHeight = windowRect.bottom - windowRect.top;
    
    // 获取窗口边框尺寸
    RECT frameRect = {0, 0, 0, 0};
    AdjustWindowRectEx(&frameRect, WS_POPUP | WS_VISIBLE, FALSE, 
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_ACCEPTFILES);
    int frameWidth = frameRect.right - frameRect.left;
    int frameHeight = frameRect.bottom - frameRect.top;
    
    // 计算当前客户区大小
    int currentClientWidth = currentWindowWidth - frameWidth;
    int currentClientHeight = currentWindowHeight - frameHeight;
    
    // 计算期望的客户区大小（保持原始宽高比）
    float originalRatio = (float)g_originalWidth / g_originalHeight;
    float currentRatio = (float)currentClientWidth / currentClientHeight;
    
    // 如果比例差异超过阈值，进行调整
    if (std::abs(currentRatio - originalRatio) > 0.01f) {
        int newClientWidth, newClientHeight;
        
        if (currentRatio > originalRatio) {
            // 以高度为基准调整宽度
            newClientHeight = currentClientHeight;
            newClientWidth = (int)(currentClientHeight * originalRatio);
        } else {
            // 以宽度为基准调整高度
            newClientWidth = currentClientWidth;
            newClientHeight = (int)(currentClientWidth / originalRatio);
        }
        
        // 计算新的窗口大小（加上边框）
        int newWindowWidth = newClientWidth + frameWidth;
        int newWindowHeight = newClientHeight + frameHeight;
        
        // 保持窗口中心位置不变
        int centerX = (windowRect.left + windowRect.right) / 2;
        int centerY = (windowRect.top + windowRect.bottom) / 2;
        int newLeft = centerX - newWindowWidth / 2;
        int newTop = centerY - newWindowHeight / 2;
        
        // 设置新的窗口位置和大小
        SetWindowPos(hwnd, NULL, 
            newLeft, newTop,
            newWindowWidth, newWindowHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
        
        // 更新缩放比例
        g_scaleRatio = (float)newClientWidth / g_originalWidth;
        
        // 强制重绘
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

// 添加新的全局变量（在其他全局变量声明处添加）
UINT_PTR REFRESH_TIMER_ID = 3;  // 刷新定时器ID
constexpr DWORD REFRESH_DELAY = 300;  // 刷新延迟时间（毫秒）

// 窗口过程函数
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            // 创建字体
            g_hFont = CreateFontA(
                16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                FONT_NAME
            );
            
            // 安装鼠标钩子
            g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, 
                GetModuleHandle(NULL), 0);
                
            // 设置窗口字体
            if(g_hFont) {
                SendMessage(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            }

            // 设置窗口透明
            SetLayeredWindowAttributes(hwnd, 0, 128, LWA_ALPHA);

            // 初始化位置
            POINT pt;
            GetCursorPos(&pt);
            RECT windowRect;
            GetWindowRect(hwnd, &windowRect);
            int height = windowRect.bottom - windowRect.top;
            g_currentPos = Position(pt.x + 5, pt.y - height - 5);
            g_targetPos = g_currentPos;
            
            // 获取屏幕尺寸
            SystemParametersInfo(SPI_GETWORKAREA, 0, &g_screenRect, 0);

            // 启用拖放
            DragAcceptFiles(hwnd, TRUE);

            // 启动检测定时器
            SetTimer(hwnd, CHECK_TIMER_ID, CHECK_INTERVAL, NULL);

            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_isTracking) {
                g_isTracking = false;  // 重置跟踪状态
                return 0;  // 忽略鼠标移动事件
            }
            
            if (!g_isMouseOver) {
                g_isMouseOver = true;
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
                
                // 显示边框
                LONG style = GetWindowLong(hwnd, GWL_STYLE);
                SetWindowLong(hwnd, GWL_STYLE, style | WS_BORDER);
                SetWindowPos(hwnd, NULL, 0, 0, 0, 0, 
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            g_isMouseOver = false;
            SetLayeredWindowAttributes(hwnd, 0, 128, LWA_ALPHA);
            
            // 隐藏边框
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_BORDER);
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0, 
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            
            return 0;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSCROLLBAR:
            SetBkColor((HDC)wp, WINDOW_BG_COLOR);
            SetTextColor((HDC)wp, RGB(40, 40, 40));  // 设置按钮文字为白色
            return (LRESULT)CreateSolidBrush(WINDOW_BG_COLOR);

        case WM_DESTROY: {
            KillTimer(hwnd, CHECK_TIMER_ID);  // 确保清理定时器
            KillTimer(hwnd, MOVE_TIMER_ID);
            if (g_pImage) {
                delete g_pImage;
                g_pImage = nullptr;
            }
            if(g_mouseHook) {
                UnhookWindowsHookEx(g_mouseHook);
                g_mouseHook = NULL;
            }
            if(g_hFont) {
                DeleteObject(g_hFont);
                g_hFont = NULL;
            }
            PostQuitMessage(0);
            return 0;
        }

        case WM_TIMER: {
            if (wp == CHECK_TIMER_ID && !g_isFollowEnabled) {
                CheckAndFixImageDisplay(hwnd);
            } else if (wp == REFRESH_TIMER_ID) {
                KillTimer(hwnd, REFRESH_TIMER_ID);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }

        case WM_DROPFILES: {
            if (g_isFollowEnabled) return 0; // 追踪时禁用拖放
            
            HDROP hDrop = (HDROP)wp;
            CHAR szFileName[MAX_PATH];
            DragQueryFileA(hDrop, 0, szFileName, MAX_PATH);
            DragFinish(hDrop);
            
            // 清理旧图片
            if (g_pImage) {
                delete g_pImage;
                g_pImage = nullptr;
            }
            
            // 将 ANSI 字符串转换为 WCHAR 字符串
            WCHAR wszFileName[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, szFileName, -1, wszFileName, MAX_PATH);
            
            // 加载新图片
            g_pImage = Image::FromFile(wszFileName);
            if (g_pImage && g_pImage->GetLastStatus() == Ok) {
                g_hasImage = true;
                
                // 保存原始尺寸
                g_originalWidth = g_pImage->GetWidth();
                g_originalHeight = g_pImage->GetHeight();
                
                // 用上次的缩放比例如果存在）
                int width = (int)(g_originalWidth * g_scaleRatio);
                int height = (int)(g_originalHeight * g_scaleRatio);
                
                // 限制最大尺寸
                const int MAX_SIZE = 800;
                if (width > MAX_SIZE || height > MAX_SIZE) {
                    float ratio = (std::min)((float)MAX_SIZE / width, (float)MAX_SIZE / height);
                    width = (int)(width * ratio);
                    height = (int)(height * ratio);
                    g_scaleRatio = ratio;  // 重置缩放比例
                }
                
                // 考虑窗口边框调整实际窗口大小
                RECT rect = {0, 0, width, height};
                AdjustWindowRectEx(&rect, WS_POPUP | WS_VISIBLE, FALSE, 
                    WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_ACCEPTFILES);
                
                SetWindowPos(hwnd, NULL, 0, 0, 
                    rect.right - rect.left,
                    rect.bottom - rect.top, 
                    SWP_NOMOVE | SWP_NOZORDER);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (g_hasImage && g_pImage) {
                // 获取客户区大小
                RECT rect;
                GetClientRect(hwnd, &rect);
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;
                
                // 创建双缓冲用的内存DC和位图
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
                HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
                
                // 在内存DC上绘制
                Graphics graphics(memDC);
                
                // 清除背景
                graphics.Clear(Color(45, 45, 45));  // 使用窗口背景色
                
                // 设置高质量绘制
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetSmoothingMode(SmoothingModeHighQuality);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                
                // 计算绘制域以保持宽高比
                float imageRatio = (float)g_pImage->GetWidth() / g_pImage->GetHeight();
                float windowRatio = (float)width / height;
                
                int drawWidth = width;
                int drawHeight = height;
                int drawX = 0;
                int drawY = 0;
                
                if (windowRatio > imageRatio) {
                    drawWidth = (int)(height * imageRatio);
                    drawX = (width - drawWidth) / 2;
                } else {
                    drawHeight = (int)(width / imageRatio);
                    drawY = (height - drawHeight) / 2;
                }
                
                // 绘制图片
                graphics.DrawImage(g_pImage, 
                    Rect(drawX, drawY, drawWidth, drawHeight),
                    0, 0, g_pImage->GetWidth(), g_pImage->GetHeight(),
                    UnitPixel);
                
                // 将内存DC的内容复制到窗口DC
                BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
                
                // 清理资源
                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
                
                // 在双缓冲完成后，绘制拖拽图标
                if (!g_isFollowEnabled) {
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    int width = rect.right - rect.left;
                    int height = rect.bottom - rect.top;
                    
                    // 绘制拖拽图
                    HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
                    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                    
                    // 绘制三条对角线
                    for (int i = 0; i < 3; i++) {
                        int offset = i * 4;
                        MoveToEx(hdc, width - 12 + offset, height - 3, NULL);
                        LineTo(hdc, width - 3, height - 12 + offset);
                    }
                    
                    SelectObject(hdc, oldPen);
                    DeleteObject(pen);
                }
            } else {
                // 原有的背景绘制
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, CreateSolidBrush(WINDOW_BG_COLOR));
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            if (g_isFollowEnabled) {
                return HTTRANSPARENT;
            }
            
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rect;
            GetWindowRect(hwnd, &rect);
            
            // 转换为客户区坐标
            pt.x -= rect.left;
            pt.y -= rect.top;
            
            // 只检查右下角
            const int RESIZE_AREA = 16;  // 增大拖拽区域
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            if (pt.x >= width - RESIZE_AREA && pt.y >= height - RESIZE_AREA) {
                SetCursor(LoadCursor(NULL, IDC_SIZENWSE));  // 设置拖拽光标
                return HTBOTTOMRIGHT;
            }
            
            return HTCLIENT;
        }

        case WM_SIZING: {
            if (g_hasImage && g_pImage && wp == WMSZ_BOTTOMRIGHT) {
                RECT* rect = (RECT*)lp;
                
                // 获取窗口边框尺寸
                RECT frameRect = {0, 0, 0, 0};
                AdjustWindowRectEx(&frameRect, WS_POPUP | WS_VISIBLE, FALSE, 
                    WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_ACCEPTFILES);
                int frameWidth = frameRect.right - frameRect.left;
                int frameHeight = frameRect.bottom - frameRect.top;
                
                // 计算客户区大小
                int clientWidth = (rect->right - rect->left) - frameWidth;
                int clientHeight = (rect->bottom - rect->top) - frameHeight;
                
                // 确保最小尺寸
                const int MIN_SIZE = 50;
                if (clientWidth < MIN_SIZE) {
                    clientWidth = MIN_SIZE;
                    rect->right = rect->left + clientWidth + frameWidth;
                }
                if (clientHeight < MIN_SIZE) {
                    clientHeight = MIN_SIZE;
                    rect->bottom = rect->top + clientHeight + frameHeight;
                }
                
                // 计算并保持宽高比
                float originalRatio = (float)g_originalWidth / g_originalHeight;
                float currentRatio = (float)clientWidth / clientHeight;
                
                if (currentRatio > originalRatio) {
                    // 以高度为基准调整宽度
                    clientWidth = (int)(clientHeight * originalRatio);
                    rect->right = rect->left + clientWidth + frameWidth;
                } else {
                    // 以宽度为基准调整高度
                    clientHeight = (int)(clientWidth / originalRatio);
                    rect->bottom = rect->top + clientHeight + frameHeight;
                }
                
                // 更新放比例
                g_scaleRatio = (float)clientWidth / g_originalWidth;

                // 重置刷新定时器
                KillTimer(hwnd, REFRESH_TIMER_ID);
                SetTimer(hwnd, REFRESH_TIMER_ID, REFRESH_DELAY, NULL);
            }
            return TRUE;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// 注册窗类
bool RegisterAppWindow(HINSTANCE hInstance) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = APP_WINDOW_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(WINDOW_BG_COLOR);
    
    // 只设置主图标
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_MYICON));
    
    return RegisterClassA(&wc);
}

// 创建主窗口
HWND CreateAppWindow(HINSTANCE hInstance) {
    return CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_ACCEPTFILES,
        APP_WINDOW_CLASS,
        "鼠标跟随窗口 (按鼠标中键开关,连续按三次退出)",
        WS_POPUP | WS_VISIBLE,  // 移除 WS_THICKFRAME
        CW_USEDEFAULT, CW_USEDEFAULT,
        200, 100,
        NULL,
        NULL,
        hInstance,
        NULL
    );
}

// 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    if(!RegisterAppWindow(hInstance)) {
        MessageBoxA(NULL, "窗口注册失败", "错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_mainWindow = CreateAppWindow(hInstance);
    if(!g_mainWindow) {
        MessageBoxA(NULL, "创建窗口失败", "错", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_mainWindow, nCmdShow);
    UpdateWindow(g_mainWindow);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 在消息循环结束后清理
    if (g_pImage) {
        delete g_pImage;
    }
    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

