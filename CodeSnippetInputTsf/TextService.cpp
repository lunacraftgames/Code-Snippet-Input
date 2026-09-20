#include "TextService.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <wincrypt.h>
#include <windowsx.h>

namespace
{
constexpr ULONG_PTR kInjectedInputMarker = 0x4353494D; // "CSIM"
constexpr wchar_t kCandidateWindowClass[] = L"CodeSnippetInput.CandidateWindow";
constexpr wchar_t kToolbarMutexName[] = L"Local\\CodeSnippetInput.Toolbar";
constexpr size_t kMaximumCandidateCount = 8;
constexpr UINT WM_CANDIDATE_REPOSITION = WM_APP + 0x341;

std::wstring ToUpper(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t value) { return static_cast<wchar_t>(towupper(value)); });
    return value;
}

std::wstring Base64ToWide(const std::string& encoded)
{
    if (encoded.empty()) return L"";
    DWORD byteCount = 0;
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, nullptr, &byteCount, nullptr, nullptr)) return L"";
    std::vector<BYTE> bytes(byteCount);
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, bytes.data(), &byteCount, nullptr, nullptr)) return L"";
    if (byteCount == 0) return L"";
    const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(bytes.data()), static_cast<int>(byteCount), nullptr, 0);
    if (wideCount <= 0) return L"";
    std::wstring value(wideCount, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(bytes.data()), static_cast<int>(byteCount), value.data(), wideCount);
    return value;
}

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> columns;
    size_t begin = 0;
    while (begin <= line.size())
    {
        const size_t end = line.find('\t', begin);
        columns.emplace_back(line.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return columns;
}

std::wstring BridgePath()
{
    std::array<wchar_t, 32768> appData{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(), static_cast<DWORD>(appData.size()));
    if (length == 0 || length >= appData.size()) return L"";
    return std::wstring(appData.data(), length) + L"\\CodeSnippetInput\\templates.tsf";
}

std::wstring ActiveContextPath()
{
    std::array<wchar_t, 32768> appData{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(), static_cast<DWORD>(appData.size()));
    if (length == 0 || length >= appData.size()) return L"";
    return std::wstring(appData.data(), length) + L"\\CodeSnippetInput\\active-context.txt";
}

std::wstring InputMethodStatePath()
{
    std::array<wchar_t, 32768> appData{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(), static_cast<DWORD>(appData.size()));
    if (length == 0 || length >= appData.size()) return L"";
    return std::wstring(appData.data(), length) + L"\\CodeSnippetInput\\ime-active.state";
}

void SaveInputMethodState(bool active)
{
    const auto statePath = InputMethodStatePath();
    if (statePath.empty()) return;

    const std::filesystem::path path(statePath);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    auto temporaryPath = path;
    temporaryPath += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(GetCurrentThreadId()) + L".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << (active ? "1\t" : "0\t") << GetCurrentProcessId();
    }

    bool replaced = false;
    for (int attempt = 0; attempt < 3 && !replaced; ++attempt)
    {
        replaced = MoveFileExW(temporaryPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!replaced) Sleep(2);
    }
    if (!replaced) std::filesystem::remove(temporaryPath, error);
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return L"";
    const int wideCount = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (wideCount <= 0) return L"";
    std::wstring result(wideCount, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), wideCount);
    if (!result.empty() && result.front() == 0xFEFF) result.erase(result.begin());
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || iswspace(result.back()))) result.pop_back();
    return result;
}

std::wstring LoadActiveContext()
{
    const auto path = ActiveContextPath();
    if (path.empty()) return L"*";
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) return L"*";
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto context = Utf8ToWide(text);
    return context.empty() ? L"*" : context;
}

void EnsureToolbarProcess()
{
    if (HANDLE existingToolbar = OpenMutexW(SYNCHRONIZE, FALSE, kToolbarMutexName); existingToolbar != nullptr)
    {
        CloseHandle(existingToolbar);
        return;
    }

    std::array<wchar_t, 32768> modulePath{};
    const DWORD length = GetModuleFileNameW(g_module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size()) return;
    const std::filesystem::path dllPath(std::wstring(modulePath.data(), length));
    const std::array<std::filesystem::path, 4> candidates{
        dllPath.parent_path().parent_path() / L"manager-context" / L"CodeSnippetInput.exe",
        dllPath.parent_path().parent_path() / L"manager-live" / L"CodeSnippetInput.exe",
        dllPath.parent_path() / L"CodeSnippetInput.exe",
        dllPath.parent_path().parent_path() / L"CodeSnippetInput.exe"
    };

    for (const auto& executable : candidates)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(executable, error)) continue;
        std::wstring command = L"\"" + executable.wstring() + L"\" --toolbar";
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            executable.parent_path().c_str(), &startup, &process))
        {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        return;
    }
}

std::wstring ClipboardText()
{
    if (!OpenClipboard(nullptr)) return L"";
    std::wstring result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT); handle != nullptr)
    {
        if (const auto text = static_cast<const wchar_t*>(GlobalLock(handle)); text != nullptr)
        {
            result = text;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

std::wstring CurrentDate()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
    return buffer;
}

std::wstring CurrentTime()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%02u:%02u", time.wHour, time.wMinute);
    return buffer;
}

wchar_t KeyToCharacter(WPARAM virtualKey)
{
    BYTE keyboardState[256]{};
    if (!GetKeyboardState(keyboardState)) return L'\0';
    wchar_t buffer[4]{};
    const int count = ToUnicodeEx(static_cast<UINT>(virtualKey), MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC), keyboardState, buffer, ARRAYSIZE(buffer), 0, GetKeyboardLayout(0));
    return count == 1 ? buffer[0] : L'\0';
}

void SendVirtualKey(WORD virtualKey, size_t count)
{
    if (count == 0) return;
    std::vector<INPUT> inputs;
    inputs.reserve(count * 2);
    for (size_t index = 0; index < count; ++index)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = virtualKey;
        down.ki.dwExtraInfo = kInjectedInputMarker;
        inputs.push_back(down);

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void SendUnicodeCharacter(wchar_t character)
{
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = character;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[0].ki.dwExtraInfo = kInjectedInputMarker;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

void SendExpandedText(const std::wstring& abbreviation, const ExpandedSnippet& expansion)
{
    SendVirtualKey(VK_BACK, abbreviation.size());
    for (size_t index = 0; index < expansion.text.size(); ++index)
    {
        if (expansion.text[index] == L'\r' || expansion.text[index] == L'\n')
        {
            if (expansion.text[index] == L'\r' && index + 1 < expansion.text.size() && expansion.text[index + 1] == L'\n') ++index;
            SendVirtualKey(VK_RETURN, 1);
        }
        else SendUnicodeCharacter(expansion.text[index]);
    }
    SendVirtualKey(VK_LEFT, static_cast<size_t>(expansion.cursorOffsetFromEnd));
}

LONG CursorMovesForText(std::wstring_view text)
{
    LONG moves = 0;
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == L'\r' && index + 1 < text.size() && text[index + 1] == L'\n') ++index;
        else if (IS_HIGH_SURROGATE(text[index]) && index + 1 < text.size() && IS_LOW_SURROGATE(text[index + 1])) ++index;
        ++moves;
    }
    return moves;
}

std::wstring CandidateDetail(const SnippetTemplate& snippet)
{
    if (!snippet.description.empty()) return snippet.description;
    std::wstring preview;
    preview.reserve(std::min<size_t>(snippet.body.size(), 96));
    bool previousWasSpace = false;
    for (const wchar_t character : snippet.body)
    {
        const bool whitespace = character == L'\r' || character == L'\n' || character == L'\t';
        if (whitespace)
        {
            if (!previousWasSpace && !preview.empty()) preview.push_back(L' ');
            previousWasSpace = true;
        }
        else
        {
            preview.push_back(character);
            previousWasSpace = false;
        }
        if (preview.size() >= 96) break;
    }
    return preview;
}

POINT CandidateAnchor(HWND inputWindow)
{
    GUITHREADINFO information{ sizeof(information) };
    if (GetGUIThreadInfo(GetCurrentThreadId(), &information) && information.hwndCaret != nullptr)
    {
        POINT point{ information.rcCaret.left, information.rcCaret.bottom };
        if (ClientToScreen(information.hwndCaret, &point)) return point;
    }

    HWND focused = GetFocus();
    POINT caret{};
    if (focused != nullptr && GetCaretPos(&caret) && ClientToScreen(focused, &caret))
    {
        caret.y += 24;
        return caret;
    }

    RECT target{};
    if (inputWindow != nullptr && GetWindowRect(inputWindow, &target))
        return POINT{ target.left + 16, target.top + 48 };

    POINT cursor{};
    GetCursorPos(&cursor);
    return cursor;
}

class ExpansionEditSession final : public ITfEditSession
{
public:
    ExpansionEditSession(ITfContext* context, std::wstring abbreviation, ExpandedSnippet expanded)
        : context_(context), abbreviation_(std::move(abbreviation)), expanded_(std::move(expanded))
    {
        context_->AddRef();
    }

    ~ExpansionEditSession() { context_->Release(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override
    {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession)
        {
            *result = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }

    STDMETHODIMP DoEditSession(TfEditCookie editCookie) override
    {
        TF_SELECTION current{};
        ULONG fetched = 0;
        HRESULT hr = context_->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &current, &fetched);
        if (FAILED(hr) || fetched != 1) return FAILED(hr) ? hr : E_FAIL;

        ITfRange* replacement = nullptr;
        hr = current.range->Clone(&replacement);
        current.range->Release();
        if (FAILED(hr)) return hr;

        LONG actualShift = 0;
        hr = replacement->ShiftStart(editCookie, -static_cast<LONG>(abbreviation_.size()), &actualShift, nullptr);
        if (FAILED(hr) || actualShift != -static_cast<LONG>(abbreviation_.size()))
        {
            replacement->Release();
            return FAILED(hr) ? hr : TF_E_NOLOCK;
        }

        hr = replacement->SetText(editCookie, 0, expanded_.text.c_str(), static_cast<LONG>(expanded_.text.size()));
        if (FAILED(hr))
        {
            replacement->Release();
            return hr;
        }

        ITfRange* caret = nullptr;
        hr = replacement->Clone(&caret);
        replacement->Release();
        if (FAILED(hr)) return hr;
        caret->Collapse(editCookie, TF_ANCHOR_END);
        if (expanded_.cursorOffsetFromEnd > 0)
        {
            LONG actualCursorShift = 0;
            caret->ShiftStart(editCookie, -expanded_.cursorOffsetFromEnd, &actualCursorShift, nullptr);
            caret->Collapse(editCookie, TF_ANCHOR_START);
        }

        TF_SELECTION output{};
        output.range = caret;
        output.style.ase = TF_AE_NONE;
        output.style.fInterimChar = FALSE;
        hr = context_->SetSelection(editCookie, 1, &output);
        caret->Release();
        return hr;
    }

private:
    std::atomic<ULONG> references_{ 1 };
    ITfContext* context_;
    std::wstring abbreviation_;
    ExpandedSnippet expanded_;
};
} // namespace

class CandidateWindow final
{
public:
    explicit CandidateWindow(TextService* owner) : owner_(owner) {}
    ~CandidateWindow()
    {
        if (window_ != nullptr) DestroyWindow(window_);
        if (titleFont_ != nullptr) DeleteObject(titleFont_);
        if (candidateFont_ != nullptr) DeleteObject(candidateFont_);
        if (detailFont_ != nullptr) DeleteObject(detailFont_);
        UnregisterClassW(kCandidateWindowClass, g_module);
    }

    void Show()
    {
        if (!EnsureWindow()) return;
        hasTsfAnchor_ = false;
        UpdateMetrics();
        Reposition();
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        InvalidateRect(window_, nullptr, FALSE);
        PostMessageW(window_, WM_CANDIDATE_REPOSITION, 0, 0);
    }

    void Hide()
    {
        if (window_ != nullptr) ShowWindow(window_, SW_HIDE);
    }

    void RefreshSelection()
    {
        if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
    }

    void SetTsfAnchor(const RECT& anchor)
    {
        tsfAnchor_ = anchor;
        hasTsfAnchor_ = true;
        Reposition();
    }

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        CandidateWindow* self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<CandidateWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);

        switch (message)
        {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->Paint();
            return 0;
        case WM_MOUSEMOVE:
            self->SelectRow(GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDOWN:
            self->ActivateRow(GET_Y_LPARAM(lParam));
            return 0;
        case WM_CANDIDATE_REPOSITION:
            self->owner_->QueueCandidatePosition();
            self->Reposition();
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    bool EnsureWindow()
    {
        if (window_ != nullptr) return true;

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        if (!GetClassInfoExW(g_module, kCandidateWindowClass, &windowClass))
        {
            windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
            windowClass.lpfnWndProc = WindowProcedure;
            windowClass.hInstance = g_module;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.lpszClassName = kCandidateWindowClass;
            if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        }

        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kCandidateWindowClass,
            L"Code Snippet 候选",
            WS_POPUP,
            0, 0, 0, 0,
            nullptr,
            nullptr,
            g_module,
            this);
        return window_ != nullptr;
    }

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    void UpdateMetrics()
    {
        const UINT newDpi = owner_->inputWindow_ == nullptr ? 96 : GetDpiForWindow(owner_->inputWindow_);
        dpi_ = newDpi == 0 ? 96 : newDpi;
        headerHeight_ = Scale(38);
        rowHeight_ = Scale(58);
        windowWidth_ = Scale(480);
        windowHeight_ = headerHeight_ + rowHeight_ * static_cast<int>(owner_->currentCandidates_.size()) + Scale(2);

        if (titleFont_ != nullptr) DeleteObject(titleFont_);
        if (candidateFont_ != nullptr) DeleteObject(candidateFont_);
        if (detailFont_ != nullptr) DeleteObject(detailFont_);
        titleFont_ = CreateFontW(-Scale(14), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        candidateFont_ = CreateFontW(-Scale(16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        detailFont_ = CreateFontW(-Scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }

    void Reposition()
    {
        if (window_ == nullptr || owner_->currentCandidates_.empty()) return;
        const POINT anchor = hasTsfAnchor_
            ? POINT{ tsfAnchor_.left, tsfAnchor_.bottom }
            : CandidateAnchor(owner_->inputWindow_);
        MONITORINFO monitor{ sizeof(monitor) };
        GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);

        int x = anchor.x;
        int y = anchor.y + Scale(6);
        if (x + windowWidth_ > monitor.rcWork.right) x = monitor.rcWork.right - windowWidth_;
        if (x < monitor.rcWork.left) x = monitor.rcWork.left;
        if (y + windowHeight_ > monitor.rcWork.bottom) y = anchor.y - windowHeight_ - Scale(6);
        if (y < monitor.rcWork.top) y = monitor.rcWork.top;

        SetWindowPos(window_, HWND_TOPMOST, x, y, windowWidth_, windowHeight_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        HRGN region = CreateRoundRectRgn(0, 0, windowWidth_ + 1, windowHeight_ + 1, Scale(10), Scale(10));
        if (SetWindowRgn(window_, region, TRUE) == 0) DeleteObject(region);
    }

    void DrawString(HDC target, const std::wstring& value, RECT bounds, UINT format) const
    {
        std::wstring copy = value;
        DrawTextW(target, copy.data(), static_cast<int>(copy.size()), &bounds, format | DT_NOPREFIX);
    }

    void Paint()
    {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        const HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);

        HBRUSH background = CreateSolidBrush(RGB(252, 253, 255));
        FillRect(buffer, &client, background);
        DeleteObject(background);

        RECT header{ 0, 0, client.right, headerHeight_ };
        HBRUSH headerBrush = CreateSolidBrush(RGB(244, 247, 251));
        FillRect(buffer, &header, headerBrush);
        DeleteObject(headerBrush);
        SelectObject(buffer, titleFont_);
        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, RGB(80, 91, 108));
        RECT headerText{ Scale(14), 0, client.right - Scale(14), headerHeight_ };
        const std::wstring contextLabel = owner_->activeContext_ == L"*" ? L"全部" : owner_->activeContext_;
        DrawString(buffer, L"代码片段  ·  " + contextLabel + L"  ·  " + owner_->typedAbbreviation_, headerText,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        for (size_t index = 0; index < owner_->currentCandidates_.size(); ++index)
        {
            const int top = headerHeight_ + static_cast<int>(index) * rowHeight_;
            RECT row{ 0, top, client.right, top + rowHeight_ };
            if (index == owner_->selectedCandidate_)
            {
                HBRUSH selection = CreateSolidBrush(RGB(232, 242, 255));
                FillRect(buffer, &row, selection);
                DeleteObject(selection);
                RECT indicator{ 0, top, Scale(4), top + rowHeight_ };
                HBRUSH accent = CreateSolidBrush(RGB(25, 103, 210));
                FillRect(buffer, &indicator, accent);
                DeleteObject(accent);
            }

            const auto& candidate = owner_->currentCandidates_[index];
            SelectObject(buffer, candidateFont_);
            SetTextColor(buffer, index == owner_->selectedCandidate_ ? RGB(15, 86, 180) : RGB(28, 35, 45));
            RECT title{ Scale(14), top + Scale(5), client.right - Scale(14), top + Scale(31) };
            DrawString(buffer, std::to_wstring(index + 1) + L"   " + candidate.abbreviation, title,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            SelectObject(buffer, detailFont_);
            SetTextColor(buffer, RGB(102, 112, 128));
            RECT detail{ Scale(48), top + Scale(30), client.right - Scale(14), top + rowHeight_ - Scale(5) };
            std::wstring detailText = CandidateDetail(candidate);
            if (owner_->activeContext_ == L"*") detailText = L"[" + candidate.context + L"]  " + detailText;
            DrawString(buffer, detailText, detail, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            if (index + 1 < owner_->currentCandidates_.size())
            {
                RECT separator{ Scale(48), top + rowHeight_ - 1, client.right - Scale(10), top + rowHeight_ };
                HBRUSH separatorBrush = CreateSolidBrush(RGB(229, 233, 239));
                FillRect(buffer, &separator, separatorBrush);
                DeleteObject(separatorBrush);
            }
        }

        HBRUSH border = CreateSolidBrush(RGB(199, 207, 218));
        FrameRect(buffer, &client, border);
        DeleteObject(border);
        BitBlt(target, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window_, &paint);
    }

    void SelectRow(int y)
    {
        if (y < headerHeight_) return;
        const size_t index = static_cast<size_t>((y - headerHeight_) / rowHeight_);
        if (index < owner_->currentCandidates_.size() && index != owner_->selectedCandidate_)
        {
            owner_->selectedCandidate_ = index;
            RefreshSelection();
        }
    }

    void ActivateRow(int y)
    {
        if (y < headerHeight_) return;
        const size_t index = static_cast<size_t>((y - headerHeight_) / rowHeight_);
        owner_->ChooseCandidate(index);
    }

    TextService* owner_;
    HWND window_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT candidateFont_ = nullptr;
    HFONT detailFont_ = nullptr;
    UINT dpi_ = 96;
    int headerHeight_ = 38;
    int rowHeight_ = 58;
    int windowWidth_ = 480;
    int windowHeight_ = 0;
    RECT tsfAnchor_{};
    bool hasTsfAnchor_ = false;
};

class CandidatePositionEditSession final : public ITfEditSession
{
public:
    CandidatePositionEditSession(TextService* owner, ITfContext* context) : owner_(owner), context_(context)
    {
        owner_->AddRef();
        context_->AddRef();
    }

    ~CandidatePositionEditSession()
    {
        context_->Release();
        owner_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override
    {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession)
        {
            *result = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }

    STDMETHODIMP DoEditSession(TfEditCookie editCookie) override
    {
        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT hr = context_->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched == 0) return FAILED(hr) ? hr : E_FAIL;

        selection.range->Collapse(editCookie, TF_ANCHOR_END);
        ITfContextView* view = nullptr;
        hr = context_->GetActiveView(&view);
        if (SUCCEEDED(hr))
        {
            RECT anchor{};
            BOOL clipped = FALSE;
            hr = view->GetTextExt(editCookie, selection.range, &anchor, &clipped);
            if (SUCCEEDED(hr)) owner_->SetCandidateAnchor(anchor);
            view->Release();
        }
        selection.range->Release();
        return hr;
    }

private:
    std::atomic<ULONG> references_{ 1 };
    TextService* owner_;
    ITfContext* context_;
};

std::vector<SnippetTemplate> LoadSnippetTemplates()
{
    std::vector<SnippetTemplate> templates;
    const auto path = BridgePath();
    if (!path.empty())
    {
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        std::string line;
        while (std::getline(input, line))
        {
            const auto columns = SplitTabs(line);
            if (columns.size() < 4) continue;
            SnippetTemplate snippet;
            snippet.abbreviation = Base64ToWide(columns[0]);
            snippet.body = Base64ToWide(columns[1]);
            snippet.enabled = columns[2] == "1";
            if (columns.size() >= 5) snippet.description = Base64ToWide(columns[4]);
            if (columns.size() >= 6)
            {
                snippet.context = Base64ToWide(columns[5]);
                if (snippet.context.empty()) snippet.context = L"General";
            }
            std::wistringstream variables(Base64ToWide(columns[3]));
            std::wstring variable;
            while (std::getline(variables, variable))
            {
                const auto separator = variable.find(L'=');
                if (separator != std::wstring::npos && separator > 0)
                    snippet.variables.emplace(ToUpper(variable.substr(0, separator)), variable.substr(separator + 1));
            }
            if (!snippet.abbreviation.empty()) templates.emplace_back(std::move(snippet));
        }
    }

    if (!templates.empty()) return templates;
    SnippetTemplate fallback;
    fallback.abbreviation = L"fori";
    fallback.description = L"整数索引 for 循环";
    fallback.context = L"General";
    fallback.body = L"for (int i = 0; i < length; i++)\r\n{\r\n    $END$\r\n}";
    templates.emplace_back(std::move(fallback));
    return templates;
}

ExpandedSnippet ExpandSnippet(const SnippetTemplate& snippet)
{
    ExpandedSnippet result;
    result.text = snippet.body;
    return result;
}

thread_local TextService* TextService::activeService_ = nullptr;

TextService::TextService() { ++g_objectCount; }
TextService::~TextService()
{
    Deactivate();
    --g_objectCount;
}

STDMETHODIMP TextService::QueryInterface(REFIID riid, void** result)
{
    if (result == nullptr) return E_INVALIDARG;
    *result = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx)
        *result = static_cast<ITfTextInputProcessorEx*>(this);
    else if (riid == IID_ITfKeyEventSink)
        *result = static_cast<ITfKeyEventSink*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ++references_; }
STDMETHODIMP_(ULONG) TextService::Release()
{
    const ULONG references = --references_;
    if (references == 0) delete this;
    return references;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* threadMgr, TfClientId clientId)
{
    return ActivateEx(threadMgr, clientId, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD)
{
    if (threadMgr == nullptr) return E_INVALIDARG;
    Deactivate();
    threadMgr_ = threadMgr;
    threadMgr_->AddRef();
    clientId_ = clientId;
    ITfKeystrokeMgr* keystrokeManager = nullptr;
    if (SUCCEEDED(threadMgr_->QueryInterface(IID_PPV_ARGS(&keystrokeManager))))
    {
        keyEventSinkAdvised_ = SUCCEEDED(keystrokeManager->AdviseKeyEventSink(clientId_, this, TRUE));
        keystrokeManager->Release();
    }
    candidateWindow_ = std::make_unique<CandidateWindow>(this);
    activeService_ = this;
    messageHook_ = SetWindowsHookExW(WH_GETMESSAGE, MessageHook, g_module, GetCurrentThreadId());
    if (messageHook_ == nullptr)
    {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Deactivate();
        return hr;
    }
    SaveInputMethodState(true);
    EnsureToolbarProcess();
    return S_OK;
}

STDMETHODIMP TextService::Deactivate()
{
    HideCandidates();
    candidateWindow_.reset();
    typedAbbreviation_.clear();
    suppressedVirtualKey_ = 0;
    inputWindow_ = nullptr;
    if (messageHook_ != nullptr)
    {
        UnhookWindowsHookEx(messageHook_);
        messageHook_ = nullptr;
    }
    if (keyEventSinkAdvised_ && threadMgr_ != nullptr)
    {
        ITfKeystrokeMgr* keystrokeManager = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_PPV_ARGS(&keystrokeManager))))
        {
            keystrokeManager->UnadviseKeyEventSink(clientId_);
            keystrokeManager->Release();
        }
        keyEventSinkAdvised_ = false;
    }
    if (activeService_ == this)
    {
        activeService_ = nullptr;
        SaveInputMethodState(false);
    }
    if (threadMgr_ != nullptr)
    {
        threadMgr_->Release();
        threadMgr_ = nullptr;
    }
    clientId_ = TF_CLIENTID_NULL;
    return S_OK;
}

LRESULT CALLBACK TextService::MessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    TextService* service = activeService_;
    if (code >= 0 && wParam == PM_REMOVE && service != nullptr)
        service->ProcessKeyboardMessage(reinterpret_cast<MSG*>(lParam));
    return CallNextHookEx(service == nullptr ? nullptr : service->messageHook_, code, wParam, lParam);
}

void TextService::ProcessKeyboardMessage(MSG* message)
{
    if (message == nullptr) return;
    const bool keyDown = message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN;
    const bool keyUp = message->message == WM_KEYUP || message->message == WM_SYSKEYUP;
    if (!keyDown && !keyUp) return;
    if (GetMessageExtraInfo() == static_cast<LPARAM>(kInjectedInputMarker)) return;

    if (keyUp)
    {
        if (message->wParam == suppressedVirtualKey_)
        {
            suppressedVirtualKey_ = 0;
            message->message = WM_NULL;
            message->wParam = 0;
            message->lParam = 0;
        }
        return;
    }

    if (message->hwnd != inputWindow_)
    {
        HideCandidates();
        inputWindow_ = message->hwnd;
        typedAbbreviation_.clear();
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0)
    {
        HideCandidates();
        typedAbbreviation_.clear();
        return;
    }

    if (HandleCandidateKey(message)) return;
    TrackKey(message->wParam);
    UpdateCandidates();
}

bool TextService::HasMatchingTemplate(SnippetTemplate* result) const
{
    if (typedAbbreviation_.empty()) return false;
    for (auto& snippet : LoadSnippetTemplates())
    {
        if (snippet.enabled && _wcsicmp(snippet.abbreviation.c_str(), typedAbbreviation_.c_str()) == 0)
        {
            if (result != nullptr) *result = std::move(snippet);
            return true;
        }
    }
    return false;
}

void TextService::UpdateCandidates()
{
    currentCandidates_.clear();
    selectedCandidate_ = 0;
    activeContext_ = LoadActiveContext();
    if (typedAbbreviation_.empty())
    {
        HideCandidates();
        return;
    }

    for (auto& snippet : LoadSnippetTemplates())
    {
        if (!snippet.enabled || snippet.abbreviation.size() < typedAbbreviation_.size()) continue;
        if (activeContext_ != L"*" && _wcsicmp(snippet.context.c_str(), activeContext_.c_str()) != 0) continue;
        if (_wcsnicmp(snippet.abbreviation.c_str(), typedAbbreviation_.c_str(), typedAbbreviation_.size()) == 0)
            currentCandidates_.emplace_back(std::move(snippet));
    }

    std::stable_sort(currentCandidates_.begin(), currentCandidates_.end(), [this](const auto& left, const auto& right)
    {
        const bool leftExact = left.abbreviation.size() == typedAbbreviation_.size();
        const bool rightExact = right.abbreviation.size() == typedAbbreviation_.size();
        if (leftExact != rightExact) return leftExact;
        if (left.abbreviation.size() != right.abbreviation.size()) return left.abbreviation.size() < right.abbreviation.size();
        return _wcsicmp(left.abbreviation.c_str(), right.abbreviation.c_str()) < 0;
    });
    if (currentCandidates_.size() > kMaximumCandidateCount)
        currentCandidates_.resize(kMaximumCandidateCount);

    if (currentCandidates_.empty())
    {
        HideCandidates();
        return;
    }
    if (candidateWindow_ == nullptr) candidateWindow_ = std::make_unique<CandidateWindow>(this);
    candidateWindow_->Show();
}

void TextService::HideCandidates()
{
    currentCandidates_.clear();
    selectedCandidate_ = 0;
    if (candidateWindow_ != nullptr) candidateWindow_->Hide();
}

bool TextService::ChooseCandidate(size_t index)
{
    if (index >= currentCandidates_.size()) return false;
    const SnippetTemplate snippet = currentCandidates_[index];
    const std::wstring abbreviation = typedAbbreviation_;
    typedAbbreviation_.clear();
    HideCandidates();
    SendExpandedText(abbreviation, ExpandSnippet(snippet));
    return true;
}

bool TextService::HandleCandidateKey(MSG* message)
{
    if (message == nullptr || currentCandidates_.empty()) return false;

    auto consume = [this, message]()
    {
        suppressedVirtualKey_ = message->wParam;
        message->message = WM_NULL;
        message->wParam = 0;
        message->lParam = 0;
    };

    if (message->wParam == VK_UP || message->wParam == VK_DOWN)
    {
        if (message->wParam == VK_UP)
            selectedCandidate_ = selectedCandidate_ == 0 ? currentCandidates_.size() - 1 : selectedCandidate_ - 1;
        else
            selectedCandidate_ = (selectedCandidate_ + 1) % currentCandidates_.size();
        candidateWindow_->RefreshSelection();
        consume();
        return true;
    }

    if (message->wParam == VK_ESCAPE)
    {
        typedAbbreviation_.clear();
        HideCandidates();
        consume();
        return true;
    }

    if (message->wParam == VK_TAB)
    {
        const size_t selected = selectedCandidate_;
        consume();
        ChooseCandidate(selected);
        return true;
    }

    size_t numericIndex = currentCandidates_.size();
    if (message->wParam >= L'1' && message->wParam <= L'8') numericIndex = message->wParam - L'1';
    else if (message->wParam >= VK_NUMPAD1 && message->wParam <= VK_NUMPAD8) numericIndex = message->wParam - VK_NUMPAD1;
    if (numericIndex < currentCandidates_.size())
    {
        consume();
        ChooseCandidate(numericIndex);
        return true;
    }
    return false;
}

void TextService::QueueCandidatePosition()
{
    if (threadMgr_ == nullptr || candidateWindow_ == nullptr || currentCandidates_.empty()) return;

    ITfDocumentMgr* document = nullptr;
    if (FAILED(threadMgr_->GetFocus(&document)) || document == nullptr) return;
    ITfContext* context = nullptr;
    const HRESULT topResult = document->GetTop(&context);
    document->Release();
    if (FAILED(topResult) || context == nullptr) return;

    auto session = new (std::nothrow) CandidatePositionEditSession(this, context);
    if (session != nullptr)
    {
        HRESULT sessionResult = E_FAIL;
        context->RequestEditSession(clientId_, session, TF_ES_ASYNCDONTCARE | TF_ES_READ, &sessionResult);
        session->Release();
    }
    context->Release();
}

void TextService::SetCandidateAnchor(const RECT& anchor)
{
    if (candidateWindow_ != nullptr && !currentCandidates_.empty()) candidateWindow_->SetTsfAnchor(anchor);
}

void TextService::TrackKey(WPARAM virtualKey)
{
    if (virtualKey == VK_BACK)
    {
        if (!typedAbbreviation_.empty()) typedAbbreviation_.pop_back();
        return;
    }
    if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || (virtualKey >= VK_LEFT && virtualKey <= VK_DOWN))
    {
        typedAbbreviation_.clear();
        return;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_CONTROL || virtualKey == VK_MENU || virtualKey == VK_LWIN || virtualKey == VK_RWIN) return;

    const wchar_t character = KeyToCharacter(virtualKey);
    if (iswalnum(character) || character == L'_')
    {
        typedAbbreviation_.push_back(static_cast<wchar_t>(towlower(character)));
        if (typedAbbreviation_.size() > 64) typedAbbreviation_.erase(0, typedAbbreviation_.size() - 64);
    }
    else if (character != L'\0') typedAbbreviation_.clear();
}

HRESULT TextService::QueueExpansion(ITfContext* context, const SnippetTemplate& snippet)
{
    auto session = new (std::nothrow) ExpansionEditSession(context, typedAbbreviation_, ExpandSnippet(snippet));
    if (session == nullptr) return E_OUTOFMEMORY;
    HRESULT sessionResult = E_FAIL;
    const HRESULT hr = context->RequestEditSession(clientId_, session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &sessionResult);
    session->Release();
    return FAILED(hr) ? hr : sessionResult;
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground)
{
    if (!foreground) HideCandidates();
    typedAbbreviation_.clear();
    SaveInputMethodState(foreground != FALSE);
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext*, WPARAM, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext*, WPARAM, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten)
{
    if (eaten == nullptr) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten)
{
    if (eaten == nullptr) return E_INVALIDARG;
    *eaten = FALSE;
    return S_OK;
}
