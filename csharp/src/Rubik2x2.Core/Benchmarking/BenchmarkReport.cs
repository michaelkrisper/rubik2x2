using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Rubik2x2.Benchmarking;

/// <summary>What one algorithm cost on one workload.</summary>
public sealed record AlgorithmResult
{
    /// <summary>The algorithm's benchmark name, for example <c>table-par</c>.</summary>
    public required string Name { get; init; }

    /// <summary>One-time precomputation before the first solve, in milliseconds.</summary>
    public required double SetupMs { get; init; }

    /// <summary>Mean wall time per solve over all scrambles, in milliseconds.</summary>
    public required double AvgMs { get; init; }

    /// <summary>Wall time solving the antipodal position, in milliseconds.</summary>
    public required double WorstMs { get; init; }

    /// <summary>Memory the algorithm holds while solving, in megabytes; best effort.</summary>
    public required double PeakMb { get; init; }

    /// <summary>Whether every solution matched the distance table's distance.</summary>
    public required bool Optimal { get; init; }

    /// <summary>Threads used, for the parallel entry only.</summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Workers { get; init; }

    /// <summary>Setup plus <paramref name="solves"/> solves — the number that decides.</summary>
    /// <param name="solves">How many cubes the workload has.</param>
    public double TotalMs(int solves) => SetupMs + (solves * AvgMs);
}

/// <summary>One benchmark run, in the shape every implementation reports.</summary>
public sealed record BenchmarkReport
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true,
    };

    /// <summary>The implementation language.</summary>
    public required string Language { get; init; }

    /// <summary>The language runtime's version.</summary>
    public required string Version { get; init; }

    /// <summary>How this binary was built.</summary>
    public required string Build { get; init; }

    /// <summary>One entry per algorithm measured.</summary>
    public required IReadOnlyList<AlgorithmResult> Algorithms { get; init; }

    /// <summary>The report as the JSON object <c>tools/report.py</c> aggregates.</summary>
    public string ToJson() => JsonSerializer.Serialize(this, JsonOptions);

    /// <summary>The report as a table for a human.</summary>
    /// <param name="scrambles">How many scrambles were solved.</param>
    public string ToText(int scrambles)
    {
        StringBuilder text = new();
        text.AppendLine($"rubik2x2 benchmark — {scrambles} scrambles plus the antipodal position");
        text.AppendLine($"{Coordinates.StateCount:N0} reachable states, maximum distance 14 quarter turns");
        text.AppendLine($"{Language} {Version} ({Build})");
        text.AppendLine();
        text.AppendLine($"{"algorithm",-11}{"setup ms",11}{"avg ms",10}{"worst ms",11}{"peak MB",10}{"optimal",9}");
        text.AppendLine(new string('-', 62));
        foreach (AlgorithmResult result in Algorithms)
        {
            text.AppendLine(
                $"{result.Name,-11}{result.SetupMs,11:F1}{result.AvgMs,10:F3}" +
                $"{result.WorstMs,11:F3}{result.PeakMb,10:F1}{(result.Optimal ? "yes" : "NO"),9}");
        }

        int[] workloads = [1, 10, 100, 1_000, 10_000];
        text.AppendLine();
        text.AppendLine("total seconds for a workload (setup + solves)");
        text.Append($"{"algorithm",-11}");
        foreach (int count in workloads)
        {
            text.Append($"{count,12:N0}");
        }

        text.AppendLine();
        text.AppendLine(new string('-', 11 + (12 * workloads.Length)));
        foreach (AlgorithmResult result in Algorithms)
        {
            text.Append($"{result.Name,-11}");
            foreach (int count in workloads)
            {
                text.Append($"{result.TotalMs(count) / 1000,12:F2}");
            }

            text.AppendLine();
        }

        return text.ToString().TrimEnd();
    }
}
