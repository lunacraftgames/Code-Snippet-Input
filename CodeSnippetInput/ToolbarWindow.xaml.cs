using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using CodeSnippetInput.Services;

namespace CodeSnippetInput;

public partial class ToolbarWindow : Window
{
    private readonly TemplateStore _store = new();
    private readonly DispatcherTimer _refreshTimer;
    private readonly DispatcherTimer _visibilityTimer;
    private readonly ObservableCollection<string> _contexts = [];
    private MainWindow? _managerWindow;
    private bool _refreshing;
    private bool _initialized;

    public ToolbarWindow()
    {
        InitializeComponent();
        ContextCombo.ItemsSource = _contexts;
        _refreshTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _refreshTimer.Tick += async (_, _) => await RefreshContextsAsync();
        _visibilityTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        _visibilityTimer.Tick += (_, _) => UpdateInputMethodVisibility();
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        if (_initialized) return;
        _initialized = true;
        Left = Math.Max(SystemParameters.WorkArea.Left, SystemParameters.WorkArea.Right - Width - 24);
        Top = Math.Max(SystemParameters.WorkArea.Top, SystemParameters.WorkArea.Bottom - Height - 24);
        await RefreshContextsAsync();
        _refreshTimer.Start();
        _visibilityTimer.Start();
        UpdateInputMethodVisibility();
    }

    private void UpdateInputMethodVisibility()
    {
        if (_store.LoadInputMethodActive())
        {
            Opacity = 1;
            if (!IsVisible) Show();
        }
        else
        {
            Opacity = 0;
            if (IsVisible) Hide();
        }
    }

    private async Task RefreshContextsAsync()
    {
        if (_refreshing) return;
        _refreshing = true;
        try
        {
            LocalizationService.ReloadIfChanged();
            var config = await _store.LoadConfigAsync();
            var names = config.Contexts
                .Concat(config.Templates.Select(template => template.Context))
                .Where(context => !string.IsNullOrWhiteSpace(context))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(context => context, StringComparer.OrdinalIgnoreCase)
                .ToList();
            var activeValue = _store.LoadActiveContext();
            var activeDisplay = activeValue == TemplateStore.AllContextsValue ? MainWindow.AllContextsDisplay : activeValue;
            var expected = new[] { MainWindow.AllContextsDisplay }.Concat(names).ToList();
            if (!_contexts.SequenceEqual(expected, StringComparer.OrdinalIgnoreCase))
            {
                _contexts.Clear();
                foreach (var context in expected) _contexts.Add(context);
            }
            ContextCombo.SelectedItem = _contexts.FirstOrDefault(context =>
                string.Equals(context, activeDisplay, StringComparison.OrdinalIgnoreCase)) ?? MainWindow.AllContextsDisplay;
        }
        catch
        {
            if (_contexts.Count == 0) _contexts.Add(MainWindow.AllContextsDisplay);
        }
        finally
        {
            _refreshing = false;
        }
    }

    private void ContextCombo_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (_refreshing || ContextCombo.SelectedItem is not string context) return;
        _store.SaveActiveContext(context == MainWindow.AllContextsDisplay ? TemplateStore.AllContextsValue : context);
    }

    private void OpenManager_Click(object sender, RoutedEventArgs e)
    {
        if (_managerWindow is null)
        {
            _managerWindow = new MainWindow();
            _managerWindow.Closed += (_, _) => _managerWindow = null;
            _managerWindow.Show();
        }
        else
        {
            if (_managerWindow.WindowState == WindowState.Minimized) _managerWindow.WindowState = WindowState.Normal;
            _managerWindow.Activate();
        }
    }

    private void DragHandle_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.LeftButton == MouseButtonState.Pressed) DragMove();
    }

    private void Window_Closed(object? sender, EventArgs e)
    {
        _refreshTimer.Stop();
        _visibilityTimer.Stop();
    }
}
