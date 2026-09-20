using System.IO.Compression;
using System.Text;
using System.Xml;
using System.Xml.Linq;
using CodeSnippetInput.Models;

namespace CodeSnippetInput.Services;

/// <summary>Imports and exports the portable subset of JetBrains Live Templates XML and ZIP bundles.</summary>
public sealed class IntelliJTemplateCodec
{
    public List<SnippetTemplate> Import(string filePath)
    {
        var document = XDocument.Load(filePath, LoadOptions.None);
        return ImportDocument(document, Path.GetFileNameWithoutExtension(filePath));
    }

    public List<SnippetTemplate> ImportArchive(string filePath)
    {
        var templates = new List<SnippetTemplate>();
        using var archive = ZipFile.OpenRead(filePath);
        foreach (var entry in archive.Entries
                     .Where(entry => entry.FullName.EndsWith(".xml", StringComparison.OrdinalIgnoreCase))
                     .OrderBy(entry => entry.FullName, StringComparer.OrdinalIgnoreCase))
        {
            using var stream = entry.Open();
            var document = XDocument.Load(stream, LoadOptions.None);
            templates.AddRange(ImportDocument(document, Path.GetFileNameWithoutExtension(entry.Name)));
        }
        return templates;
    }

    public void Export(string filePath, IEnumerable<SnippetTemplate> templates)
    {
        var snapshot = templates.ToList();
        var context = snapshot.Select(template => template.Context).FirstOrDefault() ?? "CodeSnippetInput";
        BuildDocument(context, snapshot).Save(filePath);
    }

    public void ExportArchive(string filePath, IEnumerable<SnippetTemplate> templates, IEnumerable<string>? contexts = null)
    {
        var snapshot = templates.ToList();
        var contextNames = (contexts ?? snapshot.Select(template => template.Context))
            .Concat(snapshot.Select(template => template.Context))
            .Where(context => !string.IsNullOrWhiteSpace(context))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .OrderBy(context => context, StringComparer.OrdinalIgnoreCase)
            .ToList();

        var directory = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        var temporaryPath = $"{filePath}.{Guid.NewGuid():N}.tmp";
        try
        {
            using (var archive = ZipFile.Open(temporaryPath, ZipArchiveMode.Create))
            {
                var usedNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var context in contextNames)
                {
                    var entryName = MakeUniqueEntryName(context, usedNames);
                    var entry = archive.CreateEntry(entryName, CompressionLevel.Optimal);
                    using var stream = entry.Open();
                    using var writer = XmlWriter.Create(stream, new XmlWriterSettings
                    {
                        Encoding = new UTF8Encoding(false),
                        Indent = true,
                        CloseOutput = false
                    });
                    var contextTemplates = snapshot.Where(template =>
                        string.Equals(template.Context, context, StringComparison.OrdinalIgnoreCase));
                    BuildDocument(context, contextTemplates).Save(writer);
                }
            }
            File.Move(temporaryPath, filePath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
        }
    }

    private static List<SnippetTemplate> ImportDocument(XDocument document, string fallbackContext)
    {
        var group = ((string?)document.Root?.Attribute("group"))?.Trim();
        if (string.IsNullOrWhiteSpace(group)) group = string.IsNullOrWhiteSpace(fallbackContext) ? "General" : fallbackContext;
        var templates = new List<SnippetTemplate>();

        foreach (var item in document.Descendants("template"))
        {
            var abbreviation = (string?)item.Attribute("name");
            var body = (string?)item.Attribute("value");
            if (string.IsNullOrWhiteSpace(abbreviation) || body is null) continue;

            var variables = item.Elements("variable")
                .Select(variable => new
                {
                    Name = (string?)variable.Attribute("name"),
                    Default = (string?)variable.Attribute("defaultValue") ?? string.Empty
                })
                .Where(variable => !string.IsNullOrWhiteSpace(variable.Name))
                .GroupBy(variable => variable.Name!, StringComparer.OrdinalIgnoreCase)
                .ToDictionary(group => group.Key, group => group.Last().Default, StringComparer.OrdinalIgnoreCase);

            var ideaContexts = item.Descendants("context").Elements("option")
                .Where(option => string.Equals((string?)option.Attribute("value"), "true", StringComparison.OrdinalIgnoreCase))
                .Select(option => (string?)option.Attribute("name"))
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Cast<string>()
                .ToList();

            templates.Add(new SnippetTemplate
            {
                Context = group,
                Abbreviation = abbreviation,
                Description = (string?)item.Attribute("description") ?? string.Empty,
                Body = body.Replace("\n", Environment.NewLine),
                Variables = variables,
                Contexts = ideaContexts
            });
        }
        return templates;
    }

    private static XDocument BuildDocument(string context, IEnumerable<SnippetTemplate> templates)
    {
        var root = new XElement("templateSet", new XAttribute("group", context));
        foreach (var template in templates)
        {
            var item = new XElement("template",
                new XAttribute("name", template.Abbreviation),
                new XAttribute("value", template.Body.Replace(Environment.NewLine, "\n")),
                new XAttribute("description", template.Description),
                new XAttribute("toReformat", "false"),
                new XAttribute("toShortenFQNames", "false"));

            foreach (var variable in template.Variables)
            {
                item.Add(new XElement("variable",
                    new XAttribute("name", variable.Key),
                    new XAttribute("expression", string.Empty),
                    new XAttribute("defaultValue", variable.Value),
                    new XAttribute("alwaysStopAt", "true")));
            }

            item.Add(new XElement("context", template.Contexts.Select(ideaContext =>
                new XElement("option", new XAttribute("name", ideaContext), new XAttribute("value", "true")))));
            root.Add(item);
        }
        return new XDocument(new XDeclaration("1.0", "UTF-8", "yes"), root);
    }

    private static string MakeUniqueEntryName(string context, HashSet<string> usedNames)
    {
        var invalid = Path.GetInvalidFileNameChars().ToHashSet();
        var safeName = new string(context.Select(character => invalid.Contains(character) || character is '/' or '\\' ? '_' : character).ToArray()).Trim().TrimEnd('.');
        if (safeName.Length == 0) safeName = "General";
        var candidate = $"{safeName}.xml";
        for (var suffix = 2; !usedNames.Add(candidate); suffix++) candidate = $"{safeName}_{suffix}.xml";
        return candidate;
    }
}
