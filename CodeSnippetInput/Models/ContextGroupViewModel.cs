using System.Collections.ObjectModel;
using System.ComponentModel;

namespace CodeSnippetInput.Models;

public sealed class ContextGroupViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly Action<ContextGroupViewModel, string, string> _renamed;
    private string _name;

    public ContextGroupViewModel(
        string name,
        IEnumerable<SnippetTemplate> templates,
        Action<ContextGroupViewModel, string, string> renamed)
    {
        _name = string.IsNullOrWhiteSpace(name) ? "General" : name.Trim();
        _renamed = renamed;
        Templates = new ObservableCollection<SnippetTemplate>(templates.OrderBy(template => template.Abbreviation, StringComparer.OrdinalIgnoreCase));
        foreach (var template in Templates) template.PropertyChanged += Template_PropertyChanged;
    }

    public string Name
    {
        get => _name;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? "General" : value.Trim();
            if (string.Equals(_name, normalized, StringComparison.Ordinal)) return;
            var oldName = _name;
            _name = normalized;
            foreach (var template in Templates) template.Context = normalized;
            OnPropertyChanged(nameof(Name));
            _renamed(this, oldName, normalized);
        }
    }

    public ObservableCollection<SnippetTemplate> Templates { get; }

    public bool? IsEnabled
    {
        get
        {
            if (Templates.Count == 0) return false;
            var enabled = Templates.Count(template => template.IsEnabled);
            return enabled == 0 ? false : enabled == Templates.Count ? true : null;
        }
        set
        {
            var enabled = value == true;
            foreach (var template in Templates) template.IsEnabled = enabled;
            OnPropertyChanged(nameof(IsEnabled));
        }
    }

    public int Count => Templates.Count;

    public void NotifyEnabledStateChanged() => OnPropertyChanged(nameof(IsEnabled));

    private void Template_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(SnippetTemplate.IsEnabled)) OnPropertyChanged(nameof(IsEnabled));
    }

    public void Dispose()
    {
        foreach (var template in Templates) template.PropertyChanged -= Template_PropertyChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged(string propertyName) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
