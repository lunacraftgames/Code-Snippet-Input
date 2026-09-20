using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using CodeSnippetInput.Models;

namespace CodeSnippetInput.Services;

public sealed class GlobalExpansionService : IDisposable
{
    private const uint VkBack = 0x08;
    private const uint VkTab = 0x09;
    private const uint VkReturn = 0x0D;
    private const uint VkEscape = 0x1B;
    private const uint VkLeft = 0x25;
    private const uint VkUp = 0x26;
    private const uint VkRight = 0x27;
    private const uint VkDown = 0x28;
    private const uint VkShift = 0x10;
    private const uint VkControl = 0x11;
    private const uint VkMenu = 0x12;
    private const uint VkLwin = 0x5B;
    private const uint VkRwin = 0x5C;
    private const uint KeyeventfKeyup = 0x0002;
    private const uint KeyeventfUnicode = 0x0004;

    private readonly Func<string, SnippetTemplate?> _findTemplate;
    private readonly TemplateEngine _templateEngine;
    private readonly Dispatcher _dispatcher;
    private readonly StringBuilder _typedAbbreviation = new();
    private NativeKeyboardHook? _hook;

    public GlobalExpansionService(Func<string, SnippetTemplate?> findTemplate, TemplateEngine templateEngine, Dispatcher dispatcher)
    {
        _findTemplate = findTemplate;
        _templateEngine = templateEngine;
        _dispatcher = dispatcher;
    }

    public bool IsRunning => _hook is not null;

    public void Start()
    {
        if (_hook is not null) return;
        _hook = new NativeKeyboardHook();
        _hook.KeyDown += OnKeyDown;
    }

    public void Stop()
    {
        if (_hook is null) return;
        _hook.KeyDown -= OnKeyDown;
        _hook.Dispose();
        _hook = null;
        _typedAbbreviation.Clear();
    }

    private void OnKeyDown(object? sender, GlobalKeyEventArgs args)
    {
        if (args.IsInjected) return;

        if (args.VirtualKey == VkTab)
        {
            var template = _findTemplate(_typedAbbreviation.ToString());
            _typedAbbreviation.Clear();
            if (template is null) return;

            args.Handled = true;
            var expansion = _templateEngine.Expand(template);
            _dispatcher.BeginInvoke(() => InsertExpansion(template.Abbreviation.Length, expansion), DispatcherPriority.Input);
            return;
        }

        if (args.VirtualKey == VkBack)
        {
            if (_typedAbbreviation.Length > 0) _typedAbbreviation.Length--;
            return;
        }

        if (args.VirtualKey is VkEscape or VkReturn or VkLeft or VkRight or VkUp or VkDown)
        {
            _typedAbbreviation.Clear();
            return;
        }

        if (args.VirtualKey is VkShift or VkControl or VkMenu or VkLwin or VkRwin || IsModifierDown(VkControl) || IsModifierDown(VkMenu) || IsModifierDown(VkLwin) || IsModifierDown(VkRwin))
        {
            return;
        }

        var character = GetCharacter(args.VirtualKey);
        if (character is char value && (char.IsLetterOrDigit(value) || value == '_'))
        {
            _typedAbbreviation.Append(value);
            if (_typedAbbreviation.Length > 64) _typedAbbreviation.Remove(0, _typedAbbreviation.Length - 64);
        }
        else if (character is not null)
        {
            _typedAbbreviation.Clear();
        }
    }

    private static bool IsModifierDown(uint virtualKey) => (GetAsyncKeyState((int)virtualKey) & 0x8000) != 0;

    private static char? GetCharacter(uint virtualKey)
    {
        var keyboardState = new byte[256];
        if (!GetKeyboardState(keyboardState)) return null;
        var buffer = new StringBuilder(4);
        var result = ToUnicodeEx(virtualKey, 0, keyboardState, buffer, buffer.Capacity, 0, GetKeyboardLayout(0));
        return result == 1 ? buffer[0] : null;
    }

    private static void InsertExpansion(int abbreviationLength, ExpansionResult expansion)
    {
        SendVirtualKey(VkBack, abbreviationLength);
        SendText(expansion.Text);
        SendVirtualKey(VkLeft, expansion.CursorMoves);
    }

    private static void SendText(string text)
    {
        for (var index = 0; index < text.Length; index++)
        {
            if (text[index] is '\r' or '\n')
            {
                if (text[index] == '\r' && index + 1 < text.Length && text[index + 1] == '\n') index++;
                SendVirtualKey(VkReturn, 1);
                continue;
            }
            SendUnicode(text[index]);
        }
    }

    private static void SendVirtualKey(uint virtualKey, int count)
    {
        if (count <= 0) return;
        var inputs = new INPUT[count * 2];
        for (var index = 0; index < count; index++)
        {
            inputs[index * 2] = new INPUT { Type = 1, Data = new InputUnion { Keyboard = new KEYBDINPUT { VirtualKey = (ushort)virtualKey } } };
            inputs[index * 2 + 1] = new INPUT { Type = 1, Data = new InputUnion { Keyboard = new KEYBDINPUT { VirtualKey = (ushort)virtualKey, Flags = KeyeventfKeyup } } };
        }
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    private static void SendUnicode(char character)
    {
        var inputs = new[]
        {
            new INPUT { Type = 1, Data = new InputUnion { Keyboard = new KEYBDINPUT { ScanCode = character, Flags = KeyeventfUnicode } } },
            new INPUT { Type = 1, Data = new InputUnion { Keyboard = new KEYBDINPUT { ScanCode = character, Flags = KeyeventfUnicode | KeyeventfKeyup } } }
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    public void Dispose() => Stop();

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint Type;
        public InputUnion Data;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public KEYBDINPUT Keyboard;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort VirtualKey;
        public ushort ScanCode;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, INPUT[] inputs, int size);

    [DllImport("user32.dll")]
    private static extern short GetAsyncKeyState(int virtualKey);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetKeyboardState(byte[] keyboardState);

    [DllImport("user32.dll")]
    private static extern int ToUnicodeEx(uint virtualKey, uint scanCode, byte[] keyboardState, StringBuilder buffer, int bufferLength, uint flags, IntPtr keyboardLayout);

    [DllImport("user32.dll")]
    private static extern IntPtr GetKeyboardLayout(uint threadId);
}
