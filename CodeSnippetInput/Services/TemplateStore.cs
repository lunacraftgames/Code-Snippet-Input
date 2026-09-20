using System.Text.Json;
using System.Diagnostics;
using CodeSnippetInput.Models;

namespace CodeSnippetInput.Services;

public sealed class TemplateStore
{
    public const string AllContextsValue = "*";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true
    };

    public string DefaultPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "CodeSnippetInput", "templates.json");

    public string TsfBridgePath => Path.ChangeExtension(DefaultPath, ".tsf");
    public string ActiveContextPath => Path.Combine(Path.GetDirectoryName(DefaultPath)!, "active-context.txt");
    public string InputMethodStatePath => Path.Combine(Path.GetDirectoryName(DefaultPath)!, "ime-active.state");

    public bool LoadInputMethodActive()
    {
        try
        {
            if (!File.Exists(InputMethodStatePath)) return false;
            var values = File.ReadAllText(InputMethodStatePath).Trim().Split('\t');
            if (values.Length < 2 || values[0] != "1" || !int.TryParse(values[1], out var processId)) return false;
            using var process = Process.GetProcessById(processId);
            return !process.HasExited;
        }
        catch
        {
            return false;
        }
    }

    public async Task<SnippetConfig> LoadConfigAsync()
    {
        if (!File.Exists(DefaultPath))
        {
            var examples = CreateExamples();
            return new SnippetConfig
            {
                Contexts = examples.Select(template => template.Context).Distinct(StringComparer.OrdinalIgnoreCase).ToList(),
                Templates = examples
            };
        }

        await using var stream = File.OpenRead(DefaultPath);
        var config = await JsonSerializer.DeserializeAsync<SnippetConfig>(stream, JsonOptions);
        config ??= new SnippetConfig();
        foreach (var template in config.Templates)
            template.Context = string.IsNullOrWhiteSpace(template.Context) ? "General" : template.Context;
        config.Contexts = config.Contexts
            .Concat(config.Templates.Select(template => template.Context))
            .Where(context => !string.IsNullOrWhiteSpace(context))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(context => context, StringComparer.OrdinalIgnoreCase)
            .ToList();
        return config;
    }

    public async Task<List<SnippetTemplate>> LoadAsync() => (await LoadConfigAsync()).Templates;

    public async Task SaveAsync(IEnumerable<SnippetTemplate> templates, IEnumerable<string>? contexts = null)
    {
        var snapshot = templates.ToList();
        await SaveToAsync(DefaultPath, snapshot, contexts);
        await SaveTsfBridgeAsync(snapshot);
    }

    public async Task SaveToAsync(string filePath, IEnumerable<SnippetTemplate> templates, IEnumerable<string>? contexts = null)
    {
        var snapshot = templates.ToList();
        var folder = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrWhiteSpace(folder)) Directory.CreateDirectory(folder);

        await using var stream = File.Create(filePath);
        var contextList = (contexts ?? snapshot.Select(template => template.Context))
            .Concat(snapshot.Select(template => template.Context))
            .Where(context => !string.IsNullOrWhiteSpace(context))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(context => context, StringComparer.OrdinalIgnoreCase)
            .ToList();
        await JsonSerializer.SerializeAsync(stream, new SnippetConfig { Contexts = contextList, Templates = snapshot }, JsonOptions);
    }

    private async Task SaveTsfBridgeAsync(IEnumerable<SnippetTemplate> templates)
    {
        var folder = Path.GetDirectoryName(TsfBridgePath);
        if (!string.IsNullOrWhiteSpace(folder)) Directory.CreateDirectory(folder);

        static string Encode(string value) => Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(value ?? string.Empty));
        var lines = templates.Select(template => string.Join("\t",
            Encode(template.Abbreviation),
            Encode(template.Body),
            template.IsEnabled ? "1" : "0",
            Encode(string.Join("\n", template.Variables.OrderBy(pair => pair.Key).Select(pair => $"{pair.Key}={pair.Value}"))),
            Encode(template.Description),
            Encode(template.Context)));

        var temporaryPath = $"{TsfBridgePath}.{Guid.NewGuid():N}.tmp";
        await File.WriteAllLinesAsync(temporaryPath, lines, new System.Text.UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        File.Move(temporaryPath, TsfBridgePath, overwrite: true);
    }

    public string LoadActiveContext()
    {
        try
        {
            if (!File.Exists(ActiveContextPath)) return AllContextsValue;
            var value = File.ReadAllText(ActiveContextPath).Trim();
            return string.IsNullOrWhiteSpace(value) ? AllContextsValue : value;
        }
        catch
        {
            return AllContextsValue;
        }
    }

    public void SaveActiveContext(string? context)
    {
        var value = string.IsNullOrWhiteSpace(context) ? AllContextsValue : context.Trim();
        var folder = Path.GetDirectoryName(ActiveContextPath)!;
        Directory.CreateDirectory(folder);
        var temporaryPath = $"{ActiveContextPath}.{Guid.NewGuid():N}.tmp";
        File.WriteAllText(temporaryPath, value, new System.Text.UTF8Encoding(false));
        File.Move(temporaryPath, ActiveContextPath, overwrite: true);
    }
    public async Task<List<SnippetTemplate>> ImportJsonAsync(string filePath)
    {
        await using var stream = File.OpenRead(filePath);
        var config = await JsonSerializer.DeserializeAsync<SnippetConfig>(stream, JsonOptions)
            ?? throw new InvalidDataException(LocalizationService.Get("ErrorInvalidConfig"));
        return config.Templates;
    }

    private static List<SnippetTemplate> CreateExamples() =>
    [
        new()
        {
            Context = "C#",
            Abbreviation = "fori",
            Description = LocalizationService.Get("ExampleForLoop"),
            Body = "for (int i = 0; i < $LIMIT$; i++)\r\n{\r\n    $END$\r\n}",
            Variables = new() { ["LIMIT"] = "length" }
        },
        new()
        {
            Context = "C#",
            Abbreviation = "prop",
            Description = LocalizationService.Get("ExampleProperty"),
            Body = "public $TYPE$ $NAME$ { get; set; }$END$",
            Variables = new() { ["TYPE"] = "string", ["NAME"] = "Name" }
        }
    ];
}
