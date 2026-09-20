#include "TextService.h"

#include <new>
#include <string>

HMODULE g_module = nullptr;
std::atomic<ULONG> g_serverLocks{ 0 };
std::atomic<ULONG> g_objectCount{ 0 };

namespace
{
std::wstring GuidString(REFGUID guid)
{
    wchar_t value[64]{};
    StringFromGUID2(guid, value, ARRAYSIZE(value));
    return value;
}

HRESULT SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

std::wstring ModulePath()
{
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
    return length == 0 || length >= ARRAYSIZE(path) ? L"" : std::wstring(path, length);
}

HRESULT RegisterComClass()
{
    const std::wstring baseKey = L"CLSID\\" + GuidString(CLSID_CodeSnippetInputTextService);
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG result = RegCreateKeyExW(HKEY_CLASSES_ROOT, baseKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    const std::wstring displayName = L"Code Snippet Text Service";
    HRESULT hr = SetRegistryString(key, nullptr, displayName);
    RegCloseKey(key);
    if (FAILED(hr)) return hr;

    HKEY inproc = nullptr;
    result = RegCreateKeyExW(HKEY_CLASSES_ROOT, (baseKey + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &inproc, &disposition);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    const std::wstring modulePath = ModulePath();
    hr = modulePath.empty() ? E_FAIL : SetRegistryString(inproc, nullptr, modulePath);
    if (SUCCEEDED(hr)) hr = SetRegistryString(inproc, L"ThreadingModel", L"Apartment");
    RegCloseKey(inproc);
    return hr;
}

void UnregisterComClass()
{
    const std::wstring baseKey = L"CLSID\\" + GuidString(CLSID_CodeSnippetInputTextService);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, baseKey.c_str());
}

HRESULT RegisterProfile()
{
    ITfInputProcessorProfileMgr* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) return hr;

    const std::wstring modulePath = ModulePath();
    hr = profiles->RegisterProfile(
        CLSID_CodeSnippetInputTextService,
        kCodeSnippetLangId,
        GUID_Profile_CodeSnippetInput,
        kProfileDescription,
        static_cast<ULONG>(wcslen(kProfileDescription)),
        modulePath.empty() ? nullptr : modulePath.c_str(),
        static_cast<ULONG>(modulePath.size()),
        0,
        nullptr,
        0,
        TRUE,
        0);
    profiles->Release();
    if (FAILED(hr)) return hr;

    ITfCategoryMgr* categories = nullptr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (FAILED(hr)) return hr;
    hr = categories->RegisterCategory(CLSID_CodeSnippetInputTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_CodeSnippetInputTextService);
    categories->Release();
    return hr;
}

void UnregisterProfile()
{
    ITfInputProcessorProfileMgr* profiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles))))
    {
        profiles->UnregisterProfile(CLSID_CodeSnippetInputTextService, kCodeSnippetLangId, GUID_Profile_CodeSnippetInput, 0);
        profiles->Release();
    }
    ITfCategoryMgr* categories = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories))))
    {
        categories->UnregisterCategory(CLSID_CodeSnippetInputTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_CodeSnippetInputTextService);
        categories->Release();
    }
}

class ClassFactory final : public IClassFactory
{
public:
    ClassFactory() { ++g_objectCount; }
    ~ClassFactory() { --g_objectCount; }
    STDMETHODIMP QueryInterface(REFIID riid, void** result) override
    {
        if (result == nullptr) return E_INVALIDARG;
        *result = nullptr;
        if (riid != IID_IUnknown && riid != IID_IClassFactory) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** result) override
    {
        if (outer != nullptr) return CLASS_E_NOAGGREGATION;
        auto service = new (std::nothrow) TextService();
        if (service == nullptr) return E_OUTOFMEMORY;
        const HRESULT hr = service->QueryInterface(riid, result);
        service->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) ++g_serverLocks;
        else --g_serverLocks;
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{ 1 };
};
} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return g_serverLocks == 0 && g_objectCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* result)
{
    if (result == nullptr) return E_INVALIDARG;
    *result = nullptr;
    if (clsid != CLSID_CodeSnippetInputTextService) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, result);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer(void)
{
    const HRESULT initialize = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialize) && initialize != RPC_E_CHANGED_MODE) return initialize;
    HRESULT hr = RegisterComClass();
    if (SUCCEEDED(hr)) hr = RegisterProfile();
    if (FAILED(hr))
    {
        UnregisterProfile();
        UnregisterComClass();
    }
    if (SUCCEEDED(initialize)) CoUninitialize();
    return hr;
}

STDAPI DllUnregisterServer(void)
{
    const HRESULT initialize = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialize) && initialize != RPC_E_CHANGED_MODE) return initialize;
    UnregisterProfile();
    UnregisterComClass();
    if (SUCCEEDED(initialize)) CoUninitialize();
    return S_OK;
}
