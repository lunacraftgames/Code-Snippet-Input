namespace CodeSnippetInput.Models;

public sealed class SnippetConfig
{
    public int Version { get; set; } = 2;
    public List<string> Contexts { get; set; } = ["General"];
    public List<SnippetTemplate> Templates { get; set; } = [];
}
