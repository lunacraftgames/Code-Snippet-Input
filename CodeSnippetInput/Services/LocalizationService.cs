using System.Globalization;
using System.Windows;

namespace CodeSnippetInput.Services;

public sealed record UiLanguageOption(string Code, string DisplayName);

public static class LocalizationService
{
    private const string Chinese = "zh-CN";
    private const string English = "en-US";

    public static IReadOnlyList<UiLanguageOption> SupportedLanguages { get; } =
    [
        new(Chinese, "简体中文"),
        new(English, "English")
    ];

    public static string CurrentLanguage { get; private set; } = Chinese;
    public static event EventHandler? LanguageChanged;

    private static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "CodeSnippetInput", "ui-language.txt");

    public static void Initialize()
    {
        var fallback = CultureInfo.CurrentUICulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase)
            ? Chinese
            : English;
        var language = fallback;
        try
        {
            if (File.Exists(SettingsPath)) language = File.ReadAllText(SettingsPath).Trim();
        }
        catch
        {
            // A read-only profile should not prevent the application from starting.
        }
        SetLanguage(language, persist: false);
    }

    public static void ReloadIfChanged()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return;
            var language = File.ReadAllText(SettingsPath).Trim();
            if (!string.Equals(language, CurrentLanguage, StringComparison.OrdinalIgnoreCase))
                SetLanguage(language, persist: false);
        }
        catch
        {
            // Another process may be replacing the settings file; retry on the next refresh.
        }
    }

    public static void SetLanguage(string? language, bool persist = true)
    {
        var normalized = SupportedLanguages.Any(item => string.Equals(item.Code, language, StringComparison.OrdinalIgnoreCase))
            ? SupportedLanguages.First(item => string.Equals(item.Code, language, StringComparison.OrdinalIgnoreCase)).Code
            : English;

        var dictionary = new ResourceDictionary
        {
            Source = new Uri($"Resources/Strings.{normalized}.xaml", UriKind.Relative)
        };
        var dictionaries = Application.Current.Resources.MergedDictionaries;
        var existingIndex = -1;
        for (var index = 0; index < dictionaries.Count; index++)
        {
            if (dictionaries[index].Source?.OriginalString.Contains("Resources/Strings.", StringComparison.OrdinalIgnoreCase) == true)
            {
                existingIndex = index;
                break;
            }
        }
        if (existingIndex >= 0) dictionaries[existingIndex] = dictionary;
        else dictionaries.Insert(0, dictionary);

        CurrentLanguage = normalized;
        CultureInfo.CurrentUICulture = new CultureInfo(normalized);
        if (persist) SaveLanguage(normalized);
        LanguageChanged?.Invoke(null, EventArgs.Empty);
    }

    public static string Get(string key) =>
        Application.Current?.TryFindResource(key) as string ?? key;

    public static string Format(string key, params object[] arguments) =>
        string.Format(CultureInfo.CurrentUICulture, Get(key), arguments);

    private static void SaveLanguage(string language)
    {
        try
        {
            var folder = Path.GetDirectoryName(SettingsPath)!;
            Directory.CreateDirectory(folder);
            var temporaryPath = $"{SettingsPath}.{Guid.NewGuid():N}.tmp";
            File.WriteAllText(temporaryPath, language, new System.Text.UTF8Encoding(false));
            File.Move(temporaryPath, SettingsPath, overwrite: true);
        }
        catch
        {
            // The active language still applies for this process if persistence fails.
        }
    }
}
