using System.Runtime.InteropServices;

namespace CodeSnippetInput.Services;

internal sealed class NativeKeyboardHook : IDisposable
{
    private const int WhKeyboardLl = 13;
    private const int WmKeyDown = 0x0100;
    private const int WmSysKeyDown = 0x0104;
    private const uint LlkhfInjected = 0x10;

    private readonly HookProc _callback;
    private IntPtr _handle;

    public NativeKeyboardHook()
    {
        _callback = HookCallback;
        _handle = SetWindowsHookEx(WhKeyboardLl, _callback, IntPtr.Zero, 0);
        if (_handle == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    }

    public event EventHandler<GlobalKeyEventArgs>? KeyDown;

    private IntPtr HookCallback(int code, IntPtr message, IntPtr data)
    {
        if (code >= 0 && (message == (IntPtr)WmKeyDown || message == (IntPtr)WmSysKeyDown))
        {
            var details = Marshal.PtrToStructure<KbdLlHookStruct>(data);
            var args = new GlobalKeyEventArgs((uint)details.VirtualKeyCode, (details.Flags & LlkhfInjected) != 0);
            KeyDown?.Invoke(this, args);
            if (args.Handled) return (IntPtr)1;
        }
        return CallNextHookEx(_handle, code, message, data);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        UnhookWindowsHookEx(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    private delegate IntPtr HookProc(int code, IntPtr message, IntPtr data);

    [StructLayout(LayoutKind.Sequential)]
    private struct KbdLlHookStruct
    {
        public uint VirtualKeyCode;
        public uint ScanCode;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int idHook, HookProc callback, IntPtr module, uint threadId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(IntPtr hook);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr message, IntPtr data);
}

internal sealed class GlobalKeyEventArgs(uint virtualKey, bool isInjected) : EventArgs
{
    public uint VirtualKey { get; } = virtualKey;
    public bool IsInjected { get; } = isInjected;
    public bool Handled { get; set; }
}
