using System.Diagnostics;
using System.Runtime.InteropServices;
using Rubik2x2.Solvers;

namespace Rubik2x2.Benchmarking;

/// <summary>
/// Measures the three algorithms on the same workload, setup cost included.
/// </summary>
/// <remarks>
/// <para>
/// Per-solve time alone flatters the lookup table, because it hides the exhaustive
/// search that produced it. What decides which algorithm to reach for is the total
/// time for the workload you have, so setup, per-solve and worst case are all
/// reported and <see cref="BenchmarkReport.ToText"/> adds the crossover.
/// </para>
/// <para>
/// The move tables are shared by all three algorithms and are built before the clock
/// starts, exactly as the other implementations do, so <c>setup_ms</c> compares
/// like with like.
/// </para>
/// </remarks>
public static class Benchmark
{
    /// <summary>An antipodal position: 14 quarter turns from solved, the hardest there is.</summary>
    public const string Antipodal = "wppryygrygrwgprbbbwpbwyg";

    /// <summary>Roughly how long each timed measurement should run.</summary>
    private const double TargetMs = 250;

    /// <summary>How many solutions are checked against the distance table.</summary>
    private const int OptimalitySample = 25;

    /// <summary>Runs the full comparison.</summary>
    /// <param name="scrambles">The workload; every algorithm solves all of them.</param>
    /// <param name="workers">Threads for the parallel table build; 0 means one per processor.</param>
    /// <exception cref="ArgumentException">No scrambles were given.</exception>
    public static BenchmarkReport Run(IReadOnlyList<CubeState> scrambles, int workers = 0)
    {
        ArgumentNullException.ThrowIfNull(scrambles);
        if (scrambles.Count == 0)
        {
            throw new ArgumentException("at least one scramble is needed", nameof(scrambles));
        }

        workers = workers > 0 ? workers : Environment.ProcessorCount;
        CubeState hardest = CubeState.Parse(Antipodal);

        _ = CoordinateMoveTables.Shared; // Shared by all three; not part of any setup figure.
        DistanceTable reference = DistanceTable.Build();

        AlgorithmResult bfs = Measure(
            "bfs",
            setup: null,
            solverFor: static () => Solver.Create(Algorithm.Bfs),
            // Evaluated after the warm-up solves, once the lazy mark array exists.
            retainedBytes: static () => BidirectionalSearch.RetainedBytes,
            scrambles,
            hardest,
            reference);

        AlgorithmResult ida = Measure(
            "ida",
            setup: static () => PatternDatabases.Build(),
            solverFor: static () => Solver.Create(Algorithm.Ida),
            retainedBytes: static () => PatternDatabases.Shared.ByteCount,
            scrambles,
            hardest,
            reference);

        AlgorithmResult table = Measure(
            "table",
            setup: static () => DistanceTable.Build(),
            solverFor: () => reference.Solve,
            retainedBytes: () => reference.ByteCount,
            scrambles,
            hardest,
            reference);

        // Only the build is parallelised, so the per-solve figures are the table's by
        // construction; measuring them twice would only add noise.
        Settle();
        double parallelSetup = MeasureMs(() => DistanceTable.BuildParallel(workers), repeats: 1);
        AlgorithmResult parallel = table with
        {
            Name = "table-par",
            SetupMs = Round(parallelSetup),
            Workers = workers,
        };

        return new BenchmarkReport
        {
            Language = "C#",
            Version = Environment.Version.ToString(),
            Build = $"{RuntimeInformation.FrameworkDescription}, Release, TieredPGO, " +
                    $"{RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant()}",
            Algorithms = [bfs, ida, table, parallel],
        };
    }

    private static AlgorithmResult Measure(
        string name,
        Action? setup,
        Func<Func<CubeState, string>> solverFor,
        Func<long> retainedBytes,
        IReadOnlyList<CubeState> scrambles,
        CubeState hardest,
        DistanceTable reference)
    {
        Settle();
        double setupMs = setup is null ? 0 : MeasureMs(setup, repeats: 1);
        Func<CubeState, string> solve = solverFor();

        // Let tiered compilation settle before anything is timed.
        for (int i = 0; i < Math.Min(scrambles.Count, 8); i++)
        {
            solve(scrambles[i]);
        }

        solve(hardest);

        Settle();
        double singleAverage = MeasureMs(() => SolveAll(solve, scrambles), repeats: 1) / scrambles.Count;
        int workloadRepeats = RepeatsFor(singleAverage * scrambles.Count);
        double averageMs = MeasureMs(() => SolveAll(solve, scrambles), workloadRepeats) / scrambles.Count;

        Settle();
        double singleWorst = MeasureMs(() => solve(hardest), repeats: 1);
        double worstMs = MeasureMs(() => solve(hardest), RepeatsFor(singleWorst));

        long before = GC.GetAllocatedBytesForCurrentThread();
        solve(hardest);
        long allocated = GC.GetAllocatedBytesForCurrentThread() - before;

        // Every solution of the timed passes above was thrown away; check a sample of
        // them properly, against the table's distance and against the cube itself.
        bool optimal = true;
        for (int i = 0; i < Math.Min(scrambles.Count, OptimalitySample) && optimal; i++)
        {
            CubeState state = scrambles[i];
            string solution = solve(state);
            optimal = solution.Length == reference.Distance(state) && state.Apply(solution).IsSolved;
        }

        return new AlgorithmResult
        {
            Name = name,
            SetupMs = Round(setupMs),
            AvgMs = Round(averageMs),
            WorstMs = Round(worstMs),
            PeakMb = Round((retainedBytes() + allocated) / 1e6),
            Optimal = optimal,
        };
    }

    private static void SolveAll(Func<CubeState, string> solve, IReadOnlyList<CubeState> scrambles)
    {
        foreach (CubeState state in scrambles)
        {
            solve(state);
        }
    }

    private static int RepeatsFor(double milliseconds) =>
        milliseconds <= 0 ? 100 : Math.Clamp((int)(TargetMs / milliseconds), 1, 10_000);

    /// <summary>
    /// Collects what the previous measurement left behind, so a build is not timed
    /// while paying for someone else's garbage.
    /// </summary>
    private static void Settle()
    {
        GC.Collect(2, GCCollectionMode.Aggressive, blocking: true);
        GC.WaitForPendingFinalizers();
        GC.Collect(2, GCCollectionMode.Aggressive, blocking: true);
    }

    private static double MeasureMs(Action action, int repeats)
    {
        long started = Stopwatch.GetTimestamp();
        for (int i = 0; i < repeats; i++)
        {
            action();
        }

        return Stopwatch.GetElapsedTime(started).TotalMilliseconds / repeats;
    }

    /// <summary>
    /// Rounds for reporting: three decimals above a millisecond, three significant
    /// digits below it, so a table lookup reads as 0.00041 rather than 0.
    /// </summary>
    private static double Round(double value)
    {
        if (value <= 0)
        {
            return 0;
        }

        int digits = value >= 1 ? 3 : (int)Math.Ceiling(-Math.Log10(value)) + 2;
        return Math.Round(value, Math.Min(digits, 15), MidpointRounding.AwayFromZero);
    }
}
