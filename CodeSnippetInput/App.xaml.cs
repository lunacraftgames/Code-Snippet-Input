using System.Windows;
using CodeSnippetInput.Services;

namespace CodeSnippetInput;

public partial class App : Application
{
    private Mutex? _toolbarMutex;
    private bool _ownsToolbarMutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        LocalizationService.Initialize();
        if (e.Args.Any(argument => string.Equals(argument, "--toolbar", StringComparison.OrdinalIgnoreCase)))
        {
            _toolbarMutex = new Mutex(true, @"Local\CodeSnippetInput.Toolbar", out _ownsToolbarMutex);
            if (!_ownsToolbarMutex)
            {
                Shutdown();
                return;
            }
            new ToolbarWindow().Show();
            return;
        }
        new MainWindow().Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_ownsToolbarMutex) _toolbarMutex?.ReleaseMutex();
        _toolbarMutex?.Dispose();
        base.OnExit(e);
    }
}
