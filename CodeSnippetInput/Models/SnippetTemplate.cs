using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace CodeSnippetInput.Models;

public sealed class SnippetTemplate : INotifyPropertyChanged
{
    private string _abbreviation = string.Empty;
    private string _description = string.Empty;
    private string _body = string.Empty;
    private string _context = "General";
    private bool _isEnabled = true;

    public string Id { get; set; } = Guid.NewGuid().ToString("N");

    public string Abbreviation
    {
        get => _abbreviation;
        set => SetField(ref _abbreviation, value);
    }

    public string Description
    {
        get => _description;
        set => SetField(ref _description, value);
    }

    /// <summary>The user-selectable template group, equivalent to IntelliJ templateSet/@group.</summary>
    public string Context
    {
        get => _context;
        set => SetField(ref _context, string.IsNullOrWhiteSpace(value) ? "General" : value.Trim());
    }

    /// <summary>Snippet text. Dollar-delimited text is emitted literally, including both dollar signs.</summary>
    public string Body
    {
        get => _body;
        set => SetField(ref _body, value);
    }

    public bool IsEnabled
    {
        get => _isEnabled;
        set => SetField(ref _isEnabled, value);
    }

    /// <summary>Imported IntelliJ language/application context option names, retained for round-trip information.</summary>
    public List<string> Contexts { get; set; } = [];

    /// <summary>Variable metadata retained for configuration import and export; expansion emits tokens literally.</summary>
    public Dictionary<string, string> Variables { get; set; } = new(StringComparer.OrdinalIgnoreCase);

    public event PropertyChangedEventHandler? PropertyChanged;

    private void SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
