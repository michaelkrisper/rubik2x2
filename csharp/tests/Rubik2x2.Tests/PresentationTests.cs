using System.Text.Json;
using Rubik2x2.Benchmarking;

namespace Rubik2x2.Tests;

/// <summary>The layers that are only about output: the net and the benchmark report.</summary>
public sealed class PresentationTests
{
    [Fact]
    public void ThePlainNetShowsEverySticker()
    {
        string net = CubeRenderer.Render(CubeState.Solved, ColorMode.None);
        Assert.Contains("UP", net, StringComparison.Ordinal);
        Assert.Contains("DOWN", net, StringComparison.Ordinal);
        Assert.DoesNotContain('\e', net);
        Assert.Equal(4, net.Count(character => character == 'Y')); // the UP face, and no label spells one
        foreach (char letter in CubeState.ColorChars.ToUpperInvariant())
        {
            Assert.Contains(letter, net);
        }
    }

    [Fact]
    public void ColouredNetUsesEscapeSequences()
    {
        Assert.Contains("\e[48;2;", CubeRenderer.Render(CubeState.Solved, ColorMode.TrueColor), StringComparison.Ordinal);
        Assert.Contains("\e[48;5;", CubeRenderer.Render(CubeState.Solved, ColorMode.Ansi256), StringComparison.Ordinal);
    }

    [Fact]
    public void BenchmarkJsonMatchesTheAgreedSchema()
    {
        BenchmarkReport report = Benchmark.Run([.. TestCubes.Scrambles(3)], workers: 2);
        using JsonDocument document = JsonDocument.Parse(report.ToJson());
        JsonElement root = document.RootElement;

        Assert.Equal("C#", root.GetProperty("language").GetString());
        Assert.False(string.IsNullOrWhiteSpace(root.GetProperty("version").GetString()));
        Assert.False(string.IsNullOrWhiteSpace(root.GetProperty("build").GetString()));

        JsonElement algorithms = root.GetProperty("algorithms");
        Assert.Equal(
            ["bfs", "ida", "table", "table-par"],
            algorithms.EnumerateArray().Select(entry => entry.GetProperty("name").GetString()));

        foreach (JsonElement entry in algorithms.EnumerateArray())
        {
            Assert.True(entry.GetProperty("setup_ms").GetDouble() >= 0);
            Assert.True(entry.GetProperty("avg_ms").GetDouble() >= 0);
            Assert.True(entry.GetProperty("worst_ms").GetDouble() >= 0);
            Assert.True(entry.GetProperty("peak_mb").GetDouble() >= 0);
            Assert.True(entry.GetProperty("optimal").GetBoolean());
        }

        Assert.Equal(2, algorithms[3].GetProperty("workers").GetInt32());
        Assert.False(algorithms[0].TryGetProperty("workers", out _));
    }
}
