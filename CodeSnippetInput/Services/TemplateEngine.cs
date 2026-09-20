using CodeSnippetInput.Models;

namespace CodeSnippetInput.Services;

public sealed class TemplateEngine
{
    public ExpansionResult Expand(SnippetTemplate template)
        => new(template.Body ?? string.Empty, 0);
}

public sealed record ExpansionResult(string Text, int CursorMoves);
