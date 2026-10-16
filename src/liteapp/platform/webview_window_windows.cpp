#include "liteapp/platform/webview_window.h"

#if defined(_WIN32)

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <WebView2.h>

#include <liteapp/embedded_webview2.h>

#include "liteapp/vfs/filesystem.h"

namespace liteapp::platform {
namespace {

constexpr std::wstring_view loader_version = L"1.0.4129.50";
constexpr UINT activation_message = WM_APP + 1U;

struct WindowContext {
    ICoreWebView2Controller* controller{nullptr};
    ICoreWebView2* webview{nullptr};
    std::wstring launch_url;
    std::wstring allowed_origin;
    int minimum_width{900};
    int minimum_height{600};
    std::string error;
};

Error window_error(std::string message) {
    return {"webview_window", std::move(message)};
}

bool valid_id(std::string_view id) {
    return !id.empty() && std::ranges::all_of(id, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' || character == '_';
    });
}

Result<std::wstring> wide(std::string_view value) {
    if (value.empty()) return std::wstring{};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(window_error("window text is too long"));
    }
    const auto size = static_cast<int>(value.size());
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
    if (required <= 0) return std::unexpected(window_error("window text is not valid UTF-8"));
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size,
                            result.data(), required) != required) {
        return std::unexpected(window_error("window text conversion failed"));
    }
    return result;
}

Result<std::filesystem::path> local_app_data() {
    PWSTR value = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &value);
    if (FAILED(result) || !value) {
        return std::unexpected(window_error("Windows local app data directory is unavailable"));
    }
    const std::filesystem::path path{value};
    CoTaskMemFree(value);
    return path;
}

bool file_matches(const std::filesystem::path& path, std::span<const std::byte> expected) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size != static_cast<decltype(size)>(expected.size())) return false;

    std::ifstream input{path, std::ios::binary};
    if (!input) return false;
    std::array<char, 8192> buffer{};
    std::size_t offset = 0;
    while (offset < expected.size()) {
        const auto chunk = std::min(buffer.size(), expected.size() - offset);
        input.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (input.gcount() != static_cast<std::streamsize>(chunk) ||
            std::memcmp(buffer.data(), expected.data() + offset, chunk) != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

Result<std::filesystem::path> ensure_loader(std::string_view app_id) {
    if (!valid_id(app_id)) return std::unexpected(window_error("app id is not a safe path component"));
    auto base = local_app_data();
    auto id = wide(app_id);
    if (!base) return std::unexpected(base.error());
    if (!id) return std::unexpected(id.error());
    const auto directory = *base / *id / L"runtime" / (L"webview2-" + std::wstring{loader_version});
    const auto loader = directory / L"WebView2Loader.dll";

    auto archive = vfs::FileSystem::from_zip(embedded_webview2::archive());
    if (!archive) return std::unexpected(archive.error());
    auto embedded = archive->read("WebView2Loader.dll");
    if (!embedded) return std::unexpected(embedded.error());
    if (!*embedded) return std::unexpected(window_error("embedded WebView2 loader is missing"));

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return std::unexpected(window_error("WebView2 runtime directory could not be created"));
    const auto bytes = (**embedded).bytes();
    if (file_matches(loader, bytes)) return loader;
    error.clear();
    auto staging = loader;
    staging += L".new";
    std::filesystem::remove(staging, error);
    error.clear();
    std::ofstream output{staging, std::ios::binary | std::ios::trunc};
    if (!output) return std::unexpected(window_error("WebView2 loader staging file could not be opened"));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) return std::unexpected(window_error("WebView2 loader staging file could not be written"));
    if (!MoveFileExW(staging.c_str(), loader.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(staging, error);
        return std::unexpected(window_error("WebView2 loader could not be installed"));
    }
    return loader;
}

WindowContext* context(HWND window) {
    return reinterpret_cast<WindowContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void resize_webview(HWND window) {
    const auto* window_context = context(window);
    if (!window_context || !window_context->controller) return;
    RECT bounds{};
    if (GetClientRect(window, &bounds)) {
        static_cast<void>(window_context->controller->put_Bounds(bounds));
    }
}

int resize_border() {
    return GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

LRESULT resize_hit_test(HWND window, LPARAM data) {
    RECT bounds{};
    if (!GetWindowRect(window, &bounds)) return HTCLIENT;
    const auto border = resize_border();
    const auto x = GET_X_LPARAM(data);
    const auto y = GET_Y_LPARAM(data);
    const bool left = x < bounds.left + border;
    const bool right = x >= bounds.right - border;
    const bool top = y < bounds.top + border;
    const bool bottom = y >= bounds.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

bool origin_allowed(std::wstring_view uri, std::wstring_view origin) {
    return uri == origin || (uri.starts_with(origin) && uri.size() > origin.size() && uri[origin.size()] == L'/');
}

template <typename Interface, const IID* InterfaceId>
class HandlerBase : public Interface {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, *InterfaceId)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }

protected:
    virtual ~HandlerBase() = default;

private:
    std::atomic<ULONG> references_{1};
};

class NavigationHandler final
    : public HandlerBase<ICoreWebView2NavigationStartingEventHandler,
                         &IID_ICoreWebView2NavigationStartingEventHandler> {
public:
    explicit NavigationHandler(WindowContext& window_context) : context_{window_context} {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* arguments) override {
        if (!arguments) return E_POINTER;
        LPWSTR uri = nullptr;
        const auto result = arguments->get_Uri(&uri);
        if (FAILED(result)) return result;
        const bool allowed = uri && origin_allowed(uri, context_.allowed_origin);
        CoTaskMemFree(uri);
        return allowed ? S_OK : arguments->put_Cancel(TRUE);
    }

private:
    WindowContext& context_;
};

class NewWindowHandler final
    : public HandlerBase<ICoreWebView2NewWindowRequestedEventHandler,
                         &IID_ICoreWebView2NewWindowRequestedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* arguments) override {
        return arguments ? arguments->put_Handled(TRUE) : E_POINTER;
    }
};

class PermissionHandler final
    : public HandlerBase<ICoreWebView2PermissionRequestedEventHandler,
                         &IID_ICoreWebView2PermissionRequestedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* arguments) override {
        return arguments ? arguments->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY) : E_POINTER;
    }
};

class DownloadHandler final
    : public HandlerBase<ICoreWebView2DownloadStartingEventHandler,
                         &IID_ICoreWebView2DownloadStartingEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* arguments) override {
        return arguments ? arguments->put_Cancel(TRUE) : E_POINTER;
    }
};

class WebMessageHandler final
    : public HandlerBase<ICoreWebView2WebMessageReceivedEventHandler,
                         &IID_ICoreWebView2WebMessageReceivedEventHandler> {
public:
    WebMessageHandler(HWND window, WindowContext& window_context)
        : window_{window}, context_{window_context} {}

    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* arguments) override {
        if (!arguments) return E_POINTER;
        LPWSTR source = nullptr;
        const auto source_result = arguments->get_Source(&source);
        if (FAILED(source_result)) return source_result;
        const bool allowed = source && origin_allowed(source, context_.allowed_origin);
        CoTaskMemFree(source);
        if (!allowed) return E_ACCESSDENIED;

        LPWSTR value = nullptr;
        const auto message_result = arguments->TryGetWebMessageAsString(&value);
        if (FAILED(message_result)) return message_result;
        const std::wstring_view command{value ? value : L""};
        if (command == L"window:drag") {
            ReleaseCapture();
            SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } else if (command == L"window:minimize") {
            ShowWindow(window_, SW_MINIMIZE);
        } else if (command == L"window:maximize") {
            ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
        } else if (command == L"window:close") {
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        CoTaskMemFree(value);
        return S_OK;
    }

private:
    HWND window_;
    WindowContext& context_;
};

Result<void> secure_webview(HWND window, WindowContext& window_context) {
    ICoreWebView2Settings* settings = nullptr;
    if (FAILED(window_context.webview->get_Settings(&settings)) || !settings) {
        return std::unexpected(window_error("WebView2 security settings are unavailable"));
    }
    const bool settings_secured =
        SUCCEEDED(settings->put_AreDevToolsEnabled(FALSE)) &&
        SUCCEEDED(settings->put_AreDefaultContextMenusEnabled(FALSE)) &&
        SUCCEEDED(settings->put_IsStatusBarEnabled(FALSE)) &&
        SUCCEEDED(settings->put_IsZoomControlEnabled(FALSE)) &&
        SUCCEEDED(settings->put_AreHostObjectsAllowed(FALSE)) &&
        SUCCEEDED(settings->put_IsWebMessageEnabled(TRUE)) &&
        SUCCEEDED(settings->put_AreDefaultScriptDialogsEnabled(FALSE));
    settings->Release();
    if (!settings_secured) {
        return std::unexpected(window_error("WebView2 security settings could not be applied"));
    }

    EventRegistrationToken token{};
    auto* navigation = new NavigationHandler{window_context};
    const auto navigation_result = window_context.webview->add_NavigationStarting(navigation, &token);
    navigation->Release();
    if (FAILED(navigation_result)) {
        return std::unexpected(window_error("WebView2 navigation policy could not be registered"));
    }
    auto* new_window = new NewWindowHandler{};
    const auto new_window_result = window_context.webview->add_NewWindowRequested(new_window, &token);
    new_window->Release();
    if (FAILED(new_window_result)) {
        return std::unexpected(window_error("WebView2 new-window policy could not be registered"));
    }
    auto* permission = new PermissionHandler{};
    const auto permission_result = window_context.webview->add_PermissionRequested(permission, &token);
    permission->Release();
    if (FAILED(permission_result)) {
        return std::unexpected(window_error("WebView2 permission policy could not be registered"));
    }
    auto* web_message = new WebMessageHandler{window, window_context};
    const auto web_message_result =
        window_context.webview->add_WebMessageReceived(web_message, &token);
    web_message->Release();
    if (FAILED(web_message_result)) {
        return std::unexpected(window_error("WebView2 window controls could not be registered"));
    }

    ICoreWebView2_4* webview4 = nullptr;
    if (FAILED(window_context.webview->QueryInterface(
            IID_ICoreWebView2_4, reinterpret_cast<void**>(&webview4))) || !webview4) {
        return std::unexpected(window_error("WebView2 download policy is unavailable"));
    }
    auto* download = new DownloadHandler{};
    const auto download_result = webview4->add_DownloadStarting(download, &token);
    download->Release();
    webview4->Release();
    if (FAILED(download_result)) {
        return std::unexpected(window_error("WebView2 download policy could not be registered"));
    }
    return {};
}

class ControllerHandler final
    : public HandlerBase<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                         &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    ControllerHandler(HWND window, WindowContext& window_context)
        : window_{window}, context_{window_context} {}

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error, ICoreWebView2Controller* controller) override {
        if (FAILED(error) || !controller) {
            context_.error = "WebView2 controller creation failed";
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return FAILED(error) ? error : E_FAIL;
        }
        controller->AddRef();
        context_.controller = controller;
        const auto result = controller->get_CoreWebView2(&context_.webview);
        if (FAILED(result) || !context_.webview) {
            context_.error = "WebView2 control creation failed";
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return FAILED(result) ? result : E_FAIL;
        }
        auto security = secure_webview(window_, context_);
        if (!security) {
            context_.error = std::move(security.error().message);
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return E_FAIL;
        }
        resize_webview(window_);
        ShowWindow(window_, SW_SHOW);
        const auto navigation = context_.webview->Navigate(context_.launch_url.c_str());
        if (FAILED(navigation)) {
            context_.error = "WebView2 navigation failed";
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return navigation;
        }
        return S_OK;
    }

private:
    HWND window_;
    WindowContext& context_;
};

class EnvironmentHandler final
    : public HandlerBase<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                         &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    EnvironmentHandler(HWND window, WindowContext& window_context)
        : window_{window}, context_{window_context} {}

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error, ICoreWebView2Environment* environment) override {
        if (FAILED(error) || !environment) {
            context_.error = "Evergreen WebView2 Runtime is unavailable";
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return FAILED(error) ? error : E_FAIL;
        }
        auto* handler = new ControllerHandler{window_, context_};
        const auto result = environment->CreateCoreWebView2Controller(window_, handler);
        handler->Release();
        if (FAILED(result)) {
            context_.error = "WebView2 controller startup failed";
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        return result;
    }

private:
    HWND window_;
    WindowContext& context_;
};

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    auto* window_context = context(window);
    if (message == WM_NCCALCSIZE && parameter == TRUE) {
        auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(data);
        if (IsZoomed(window)) {
            const auto border = resize_border();
            parameters->rgrc[0].left += border;
            parameters->rgrc[0].top += border;
            parameters->rgrc[0].right -= border;
            parameters->rgrc[0].bottom -= border;
        }
        return 0;
    }
    if (message == WM_NCHITTEST && !IsZoomed(window)) return resize_hit_test(window, data);
    if (message == WM_GETMINMAXINFO && window_context) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(data);
        limits->ptMinTrackSize.x = window_context->minimum_width;
        limits->ptMinTrackSize.y = window_context->minimum_height;
        return 0;
    }
    if (message == WM_SIZE) resize_webview(window);
    else if (message == activation_message) {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        return 0;
    } else if (message == WM_DESTROY) {
        if (window_context) {
            if (window_context->webview) window_context->webview->Release();
            if (window_context->controller) window_context->controller->Release();
            window_context->webview = nullptr;
            window_context->controller = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, parameter, data);
}

}  // namespace

bool webview_window_supported() noexcept { return true; }

Result<void> run_webview_window(WebViewWindowOptions options) {
    if (!valid_id(options.app_id) || options.width <= 0 || options.height <= 0 ||
        options.minimum_width <= 0 || options.minimum_height <= 0) {
        return std::unexpected(window_error("WebView window options are invalid"));
    }
    auto title = wide(options.title);
    auto launch_url = wide(options.launch_url);
    auto allowed_origin = wide(options.allowed_origin);
    auto loader_path = ensure_loader(options.app_id);
    if (!title) return std::unexpected(title.error());
    if (!launch_url) return std::unexpected(launch_url.error());
    if (!allowed_origin) return std::unexpected(allowed_origin.error());
    if (!loader_path) return std::unexpected(loader_path.error());

    const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) return std::unexpected(window_error("Windows COM initialization failed"));
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.lpszClassName = L"liteapp-webview-window";
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        CoUninitialize();
        return std::unexpected(window_error("Windows window class registration failed"));
    }

    WindowContext window_context{
        .controller = nullptr,
        .webview = nullptr,
        .launch_url = std::move(*launch_url),
        .allowed_origin = std::move(*allowed_origin),
        .minimum_width = options.minimum_width,
        .minimum_height = options.minimum_height,
        .error = {},
    };
    constexpr auto window_style = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const auto window = CreateWindowExW(
        0, window_class.lpszClassName, title->c_str(), window_style,
        CW_USEDEFAULT, CW_USEDEFAULT, options.width, options.height,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        CoUninitialize();
        return std::unexpected(window_error("Windows application window creation failed"));
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&window_context));
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) & ~WS_CAPTION);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    const auto loader = LoadLibraryExW(
        loader_path->c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) {
        DestroyWindow(window);
        CoUninitialize();
        return std::unexpected(window_error("WebView2 loader could not be loaded"));
    }
    using CreateEnvironmentFunction = HRESULT(STDAPICALLTYPE*)(
        PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    const auto create_environment = reinterpret_cast<CreateEnvironmentFunction>(
        GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create_environment) {
        FreeLibrary(loader);
        DestroyWindow(window);
        CoUninitialize();
        return std::unexpected(window_error("WebView2 loader API is unavailable"));
    }
    std::error_code directory_error;
    std::filesystem::create_directories(options.profile_directory, directory_error);
    if (directory_error) {
        FreeLibrary(loader);
        DestroyWindow(window);
        CoUninitialize();
        return std::unexpected(window_error("WebView2 profile directory could not be created"));
    }
    const auto profile = options.profile_directory.native();
    auto* handler = new EnvironmentHandler{window, window_context};
    const auto environment_result = create_environment(nullptr, profile.c_str(), nullptr, handler);
    handler->Release();
    if (FAILED(environment_result)) {
        FreeLibrary(loader);
        DestroyWindow(window);
        CoUninitialize();
        return std::unexpected(window_error("WebView2 environment startup failed"));
    }
    if (options.instance) {
        options.instance->set_activation_handler([window] {
            PostMessageW(window, activation_message, 0, 0);
        });
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (options.instance) options.instance->clear_activation_handler();
    FreeLibrary(loader);
    CoUninitialize();
    if (!window_context.error.empty()) return std::unexpected(window_error(std::move(window_context.error)));
    return {};
}

}  // namespace liteapp::platform

#endif
