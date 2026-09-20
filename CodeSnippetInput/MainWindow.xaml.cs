using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using CodeSnippetInput.Models;
using CodeSnippetInput.Services;

namespace CodeSnippetInput;

public partial class MainWindow : Window, INotifyPropertyChanged
{
    public static string AllContextsDisplay => LocalizationService.Get("AllContexts");

    private readonly TemplateStore _store = new();
    private readonly IntelliJTemplateCodec _intelliJCodec = new();
    private readonly TemplateEngine _templateEngine = new();
    private readonly HashSet<string> _knownContexts = new(StringComparer.OrdinalIgnoreCase);
    private GlobalExpansionService? _expansionService;
    private SnippetTemplate? _selectedTemplate;
    private ContextGroupViewModel? _selectedContextGroup;
    private string _activeContextDisplay = AllContextsDisplay;
    private string _statusText = LocalizationService.Get("StatusLoading");
    private bool _rebuildingContexts;
    private bool _settingLanguage;
    private bool _closeSaveInProgress;
    private bool _closeAfterSave;

    public ObservableCollection<SnippetTemplate> Templates { get; } = [];
    public ObservableCollection<ContextGroupViewModel> ContextGroups { get; } = [];
    public ObservableCollection<string> AvailableContexts { get; } = [];
    public ObservableCollection<string> ContextChoices { get; } = [];

    public SnippetTemplate? SelectedTemplate
    {
        get => _selectedTemplate;
        set
        {
            if (_selectedTemplate == value) return;
            _selectedTemplate = value;
            OnPropertyChanged(nameof(SelectedTemplate));
            OnPropertyChanged(nameof(VariableDefinitions));
            OnPropertyChanged(nameof(ContextDefinitions));
        }
    }

    public string ActiveContextDisplay
    {
        get => _activeContextDisplay;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? AllContextsDisplay : value;
            if (string.Equals(_activeContextDisplay, normalized, StringComparison.Ordinal)) return;
            _activeContextDisplay = normalized;
            OnPropertyChanged(nameof(ActiveContextDisplay));
            if (!_rebuildingContexts)
            {
                _store.SaveActiveContext(normalized == AllContextsDisplay ? TemplateStore.AllContextsValue : normalized);
                StatusText = normalized == AllContextsDisplay
                    ? L("StatusAllContexts")
                    : LF("StatusContextChanged", normalized);
            }
        }
    }

    public string VariableDefinitions
    {
        get => SelectedTemplate is null
            ? string.Empty
            : string.Join(Environment.NewLine, SelectedTemplate.Variables.OrderBy(pair => pair.Key).Select(pair => $"{pair.Key}={pair.Value}"));
        set
        {
            if (SelectedTemplate is null) return;
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in value.Replace("\r\n", "\n").Split('\n'))
            {
                var separator = line.IndexOf('=');
                if (separator <= 0) continue;
                var name = line[..separator].Trim();
                if (name.Length > 0) values[name] = line[(separator + 1)..];
            }
            SelectedTemplate.Variables = values;
        }
    }

    public string ContextDefinitions
    {
        get => SelectedTemplate is null ? string.Empty : string.Join(", ", SelectedTemplate.Contexts);
        set
        {
            if (SelectedTemplate is null) return;
            SelectedTemplate.Contexts = value.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries)
                .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }
    }

    public string StatusText
    {
        get => _statusText;
        private set { _statusText = value; OnPropertyChanged(nameof(StatusText)); }
    }

    public MainWindow()
    {
        InitializeComponent();
        DataContext = this;
        UiLanguageCombo.ItemsSource = LocalizationService.SupportedLanguages;
        _settingLanguage = true;
        UiLanguageCombo.SelectedValue = LocalizationService.CurrentLanguage;
        _settingLanguage = false;
    }

    private void UiLanguageCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_settingLanguage || UiLanguageCombo.SelectedValue is not string language) return;
        var wasAllContexts = string.Equals(_activeContextDisplay, AllContextsDisplay, StringComparison.Ordinal);
        LocalizationService.SetLanguage(language);
        if (wasAllContexts) _activeContextDisplay = AllContextsDisplay;
        RebuildContextGroups();
        UpdateExpansionButtonContent();
        StatusText = L("StatusLanguageChanged");
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        try
        {
            var config = await _store.LoadConfigAsync();
            foreach (var context in config.Contexts) AddKnownContext(context);
            foreach (var template in config.Templates)
            {
                NormalizeTemplate(template);
                Templates.Add(template);
                AddKnownContext(template.Context);
            }
            RebuildContextGroups();
            SelectedTemplate = Templates.FirstOrDefault();

            var activeContext = _store.LoadActiveContext();
            ActiveContextDisplay = activeContext == TemplateStore.AllContextsValue || !_knownContexts.Contains(activeContext)
                ? AllContextsDisplay
                : activeContext;
            await _store.SaveAsync(Templates, _knownContexts);
            _expansionService = new GlobalExpansionService(FindEnabledTemplate, _templateEngine, Dispatcher);
            StatusText = LF("StatusLoaded", Templates.Count, _knownContexts.Count);
        }
        catch (Exception exception)
        {
            StatusText = LF("ErrorLoad", exception.Message);
            MessageBox.Show(StatusText, L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private SnippetTemplate? FindEnabledTemplate(string abbreviation)
    {
        var activeContext = _store.LoadActiveContext();
        return Templates.FirstOrDefault(template => template.IsEnabled
            && (activeContext == TemplateStore.AllContextsValue || string.Equals(template.Context, activeContext, StringComparison.OrdinalIgnoreCase))
            && string.Equals(template.Abbreviation, abbreviation, StringComparison.OrdinalIgnoreCase));
    }

    private void ToggleExpansion_Click(object sender, RoutedEventArgs e)
    {
        if (_expansionService is null) return;
        try
        {
            if (_expansionService.IsRunning)
            {
                _expansionService.Stop();
                UpdateExpansionButtonContent();
                StatusText = L("StatusExpansionStopped");
            }
            else
            {
                _expansionService.Start();
                UpdateExpansionButtonContent();
                StatusText = L("StatusExpansionStarted");
            }
        }
        catch (Exception exception)
        {
            StatusText = LF("ErrorExpansion", exception.Message);
            MessageBox.Show(StatusText, L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void UpdateExpansionButtonContent() =>
        ToggleExpansionButton.Content = L(_expansionService?.IsRunning == true
            ? "ButtonDisableGlobalExpansion"
            : "ButtonEnableGlobalExpansion");

    private void AddContext_Click(object sender, RoutedEventArgs e)
    {
        var name = MakeUniqueContextName(L("NewContextName"));
        AddKnownContext(name);
        RebuildContextGroups();
        _selectedContextGroup = ContextGroups.First(group => string.Equals(group.Name, name, StringComparison.OrdinalIgnoreCase));
        SelectedTemplate = null;
        StatusText = LF("StatusContextCreated", name);
    }

    private async void DeleteContext_Click(object sender, RoutedEventArgs e)
    {
        var clickedGroup = (sender as FrameworkElement)?.Tag as ContextGroupViewModel;
        var context = clickedGroup?.Name ?? _selectedContextGroup?.Name ?? SelectedTemplate?.Context;
        if (string.IsNullOrWhiteSpace(context)) return;
        var affected = Templates.Where(template => string.Equals(template.Context, context, StringComparison.OrdinalIgnoreCase)).ToList();
        var confirmation = affected.Count > 0
            ? LF("ConfirmDeleteContextWithTemplates", context, affected.Count)
            : LF("ConfirmDeleteContextEmpty", context);
        if (MessageBox.Show(confirmation, L("DeleteContextTitle"), MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;

        foreach (var template in affected) Templates.Remove(template);
        _knownContexts.Remove(context);
        if (string.Equals(ActiveContextDisplay, context, StringComparison.OrdinalIgnoreCase))
            ActiveContextDisplay = AllContextsDisplay;
        SelectedTemplate = null;
        _selectedContextGroup = null;
        RebuildContextGroups();
        try
        {
            await _store.SaveAsync(Templates, _knownContexts);
            StatusText = LF("StatusContextDeleted", context);
        }
        catch (Exception exception)
        {
            StatusText = LF("ErrorContextDeleteSave", exception.Message);
            MessageBox.Show(StatusText, L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void AddTemplate_Click(object sender, RoutedEventArgs e)
    {
        var context = _selectedContextGroup?.Name
            ?? SelectedTemplate?.Context
            ?? (ActiveContextDisplay != AllContextsDisplay ? ActiveContextDisplay : AvailableContexts.FirstOrDefault())
            ?? "General";
        AddKnownContext(context);
        var template = new SnippetTemplate
        {
            Context = context,
            Abbreviation = MakeUniqueAbbreviation("new", context),
            Description = L("NewTemplateDescription"),
            Body = "$END$"
        };
        Templates.Add(template);
        SelectedTemplate = template;
        RebuildContextGroups();
        StatusText = LF("StatusTemplateCreated", context);
    }

    private void SelectAll_Click(object sender, RoutedEventArgs e)
    {
        SetAllTemplatesEnabled(true);
        StatusText = LF("StatusAllTemplatesEnabled", Templates.Count);
    }

    private void SelectNone_Click(object sender, RoutedEventArgs e)
    {
        SetAllTemplatesEnabled(false);
        StatusText = LF("StatusAllTemplatesDisabled", Templates.Count);
    }

    private void SetAllTemplatesEnabled(bool enabled)
    {
        foreach (var template in Templates) template.IsEnabled = enabled;
        foreach (var group in ContextGroups) group.NotifyEnabledStateChanged();
    }

    private void DeleteTemplate_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedTemplate is null) return;
        var name = SelectedTemplate.Abbreviation;
        var context = SelectedTemplate.Context;
        Templates.Remove(SelectedTemplate);
        SelectedTemplate = Templates.FirstOrDefault(template => string.Equals(template.Context, context, StringComparison.OrdinalIgnoreCase));
        RebuildContextGroups();
        StatusText = LF("StatusTemplateDeleted", name);
    }

    private async void Save_Click(object sender, RoutedEventArgs e) => await SaveDefaultAsync();

    private async Task SaveDefaultAsync()
    {
        try
        {
            await _store.SaveAsync(Templates, _knownContexts);
            RebuildContextGroups();
            StatusText = LF("StatusSaved", Templates.Count, _knownContexts.Count);
        }
        catch (Exception exception)
        {
            StatusText = LF("ErrorSave", exception.Message);
            MessageBox.Show(StatusText, L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private async void ImportConfig_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = L("FilterConfigOpen") };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            AddImportedTemplates(await _store.ImportJsonAsync(dialog.FileName));
            await SaveDefaultAsync();
        }
        catch (Exception exception)
        {
            MessageBox.Show(LF("ErrorImportConfig", exception.Message), L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private async void ImportIntelliJ_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = L("FilterTemplateOpen")
        };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var imported = string.Equals(Path.GetExtension(dialog.FileName), ".zip", StringComparison.OrdinalIgnoreCase)
                ? _intelliJCodec.ImportArchive(dialog.FileName)
                : _intelliJCodec.Import(dialog.FileName);
            AddImportedTemplates(imported);
            await SaveDefaultAsync();
        }
        catch (Exception exception)
        {
            MessageBox.Show(LF("ErrorImportTemplate", exception.Message), L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private async void ExportConfig_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = L("FilterConfigSave"), FileName = "templates.json" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            await _store.SaveToAsync(dialog.FileName, Templates, _knownContexts);
            StatusText = LF("StatusConfigExported", dialog.FileName);
        }
        catch (Exception exception)
        {
            MessageBox.Show(LF("ErrorExportConfig", exception.Message), L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void ExportIntelliJ_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = L("FilterTemplateSave"), FileName = "templates.zip" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            _intelliJCodec.ExportArchive(dialog.FileName, Templates, _knownContexts);
            StatusText = LF("StatusTemplateExported", dialog.FileName);
        }
        catch (Exception exception)
        {
            MessageBox.Show(LF("ErrorExportTemplate", exception.Message), L("AppName"), MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void AddImportedTemplates(IEnumerable<SnippetTemplate> imported)
    {
        var count = 0;
        foreach (var template in imported)
        {
            NormalizeTemplate(template);
            AddKnownContext(template.Context);
            template.Id = Guid.NewGuid().ToString("N");
            template.Abbreviation = MakeUniqueAbbreviation(template.Abbreviation, template.Context);
            Templates.Add(template);
            count++;
        }
        SelectedTemplate = Templates.LastOrDefault();
        RebuildContextGroups();
        StatusText = LF("StatusImported", count);
    }

    private void NormalizeTemplate(SnippetTemplate template)
    {
        template.Context = string.IsNullOrWhiteSpace(template.Context) ? "General" : template.Context;
        template.Abbreviation = template.Abbreviation?.Trim() ?? string.Empty;
        template.Description ??= string.Empty;
        template.Body ??= string.Empty;
        template.Contexts ??= [];
        template.Variables ??= new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    }

    private string MakeUniqueAbbreviation(string requested, string context)
    {
        var baseName = string.IsNullOrWhiteSpace(requested) ? "template" : requested.Trim();
        bool Exists(string value) => Templates.Any(template =>
            string.Equals(template.Context, context, StringComparison.OrdinalIgnoreCase)
            && string.Equals(template.Abbreviation, value, StringComparison.OrdinalIgnoreCase));
        if (!Exists(baseName)) return baseName;
        for (var suffix = 2; ; suffix++)
        {
            var candidate = $"{baseName}_{suffix}";
            if (!Exists(candidate)) return candidate;
        }
    }

    private string MakeUniqueContextName(string requested)
    {
        if (!_knownContexts.Contains(requested)) return requested;
        for (var suffix = 2; ; suffix++)
        {
            var candidate = $"{requested} {suffix}";
            if (!_knownContexts.Contains(candidate)) return candidate;
        }
    }

    private void AddKnownContext(string? context)
    {
        if (!string.IsNullOrWhiteSpace(context)) _knownContexts.Add(context.Trim());
    }

    private void RebuildContextGroups()
    {
        var selectedId = SelectedTemplate?.Id;
        foreach (var group in ContextGroups) group.Dispose();
        ContextGroups.Clear();
        foreach (var context in _knownContexts.OrderBy(value => value, StringComparer.OrdinalIgnoreCase))
        {
            ContextGroups.Add(new ContextGroupViewModel(
                context,
                Templates.Where(template => string.Equals(template.Context, context, StringComparison.OrdinalIgnoreCase)),
                ContextGroup_Renamed));
        }

        _rebuildingContexts = true;
        var active = _activeContextDisplay;
        AvailableContexts.Clear();
        ContextChoices.Clear();
        ContextChoices.Add(AllContextsDisplay);
        foreach (var context in _knownContexts.OrderBy(value => value, StringComparer.OrdinalIgnoreCase))
        {
            AvailableContexts.Add(context);
            ContextChoices.Add(context);
        }
        if (active != AllContextsDisplay && !_knownContexts.Contains(active)) active = AllContextsDisplay;
        _activeContextDisplay = active;
        OnPropertyChanged(nameof(ActiveContextDisplay));
        _rebuildingContexts = false;

        if (selectedId is not null) SelectedTemplate = Templates.FirstOrDefault(template => template.Id == selectedId);
    }

    private void ContextGroup_Renamed(ContextGroupViewModel group, string oldName, string newName)
    {
        _knownContexts.Remove(oldName);
        AddKnownContext(newName);
        if (string.Equals(_activeContextDisplay, oldName, StringComparison.OrdinalIgnoreCase))
        {
            _activeContextDisplay = newName;
            _store.SaveActiveContext(newName);
        }
        Dispatcher.BeginInvoke(RebuildContextGroups);
        StatusText = LF("StatusContextRenamed", oldName, newName);
    }

    private void TemplateTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (e.NewValue is SnippetTemplate template)
        {
            SelectedTemplate = template;
            _selectedContextGroup = ContextGroups.FirstOrDefault(group =>
                string.Equals(group.Name, template.Context, StringComparison.OrdinalIgnoreCase));
        }
        else if (e.NewValue is ContextGroupViewModel group)
        {
            _selectedContextGroup = group;
            SelectedTemplate = null;
        }
    }

    private void TemplateContext_LostFocus(object sender, RoutedEventArgs e)
    {
        if (SelectedTemplate is null) return;
        AddKnownContext(SelectedTemplate.Context);
        _selectedContextGroup = null;
        RebuildContextGroups();
    }

    private async void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (_closeAfterSave) return;
        e.Cancel = true;
        if (_closeSaveInProgress) return;
        _closeSaveInProgress = true;
        _expansionService?.Dispose();
        _expansionService = null;
        foreach (var group in ContextGroups) group.Dispose();
        try { await _store.SaveAsync(Templates, _knownContexts); }
        catch { /* Window shutdown should not be blocked by an inaccessible profile path. */ }
        finally
        {
            _closeSaveInProgress = false;
            _closeAfterSave = true;
            Close();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged(string propertyName) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    private static string L(string key) => LocalizationService.Get(key);
    private static string LF(string key, params object[] arguments) => LocalizationService.Format(key, arguments);
}
