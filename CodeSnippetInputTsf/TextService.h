#pragma once

#include <windows.h>
#include <msctf.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Stable IDs: changing either GUID would create a second Windows input profile.
inline const CLSID CLSID_CodeSnippetInputTextService =
    { 0x20b13e38, 0xa7b1, 0x4ecf, { 0xa2, 0x63, 0x95, 0x68, 0x94, 0xf4, 0x18, 0x28 } };
inline const GUID GUID_Profile_CodeSnippetInput =
    { 0xd7a40e04, 0x3fd1, 0x4c93, { 0xb1, 0xab, 0xa5, 0x58, 0xec, 0x42, 0x63, 0xea } };
inline constexpr LANGID kCodeSnippetLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
inline constexpr wchar_t kProfileDescription[] = L"Code Snippet 输入法";

struct SnippetTemplate
{
    std::wstring abbreviation;
    std::wstring description;
    std::wstring context = L"General";
    std::wstring body;
    bool enabled = true;
    std::map<std::wstring, std::wstring> variables;
};

class CandidateWindow;
class CandidatePositionEditSession;

struct ExpandedSnippet
{
    std::wstring text;
    LONG cursorOffsetFromEnd = 0;
};

class TextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink
{
public:
    TextService();
    ~TextService();

    STDMETHODIMP QueryInterface(REFIID riid, void** result) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP ActivateEx(ITfThreadMgr* threadMgr, TfClientId clientId, DWORD flags) override;

    STDMETHODIMP OnSetFocus(BOOL foreground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wParam, LPARAM lParam, BOOL* eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

private:
    friend class CandidateWindow;
    friend class CandidatePositionEditSession;

    static LRESULT CALLBACK MessageHook(int code, WPARAM wParam, LPARAM lParam);
    void ProcessKeyboardMessage(MSG* message);
    bool HasMatchingTemplate(SnippetTemplate* result = nullptr) const;
    void UpdateCandidates();
    void HideCandidates();
    bool ChooseCandidate(size_t index);
    bool HandleCandidateKey(MSG* message);
    void QueueCandidatePosition();
    void SetCandidateAnchor(const RECT& anchor);
    HRESULT QueueExpansion(ITfContext* context, const SnippetTemplate& snippet);
    void TrackKey(WPARAM virtualKey);

    std::atomic<ULONG> references_{ 1 };
    ITfThreadMgr* threadMgr_ = nullptr;
    bool keyEventSinkAdvised_ = false;
    HHOOK messageHook_ = nullptr;
    HWND inputWindow_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    std::wstring typedAbbreviation_;
    std::wstring activeContext_ = L"*";
    std::vector<SnippetTemplate> currentCandidates_;
    size_t selectedCandidate_ = 0;
    WPARAM suppressedVirtualKey_ = 0;
    std::unique_ptr<CandidateWindow> candidateWindow_;
    static thread_local TextService* activeService_;
};

std::vector<SnippetTemplate> LoadSnippetTemplates();
ExpandedSnippet ExpandSnippet(const SnippetTemplate& snippet);
extern std::atomic<ULONG> g_serverLocks;
extern std::atomic<ULONG> g_objectCount;
extern HMODULE g_module;
