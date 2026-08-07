using Rubik2x2.Solvers;

namespace Rubik2x2.Tests;

/// <summary>
/// Shared fixtures. The distance table takes a few hundred milliseconds to build, so
/// the whole suite shares one.
/// </summary>
internal static class TestCubes
{
    /// <summary>An antipodal position: 14 quarter turns from solved.</summary>
    public const string Antipodal = "wppryygrygrwgprbbbwpbwyg";

    private static readonly Lazy<DistanceTable> LazyTable = new(() => DistanceTable.Build(), isThreadSafe: true);

    private static readonly Lazy<DistanceTable> LazyParallelTable =
        new(() => DistanceTable.BuildParallel(), isThreadSafe: true);

    public static DistanceTable Table => LazyTable.Value;

    public static DistanceTable ParallelTable => LazyParallelTable.Value;

    /// <summary>Reproducible scrambles, each a full 14 random turns from solved.</summary>
    public static IEnumerable<CubeState> Scrambles(int count, int firstSeed = 0)
    {
        for (int seed = firstSeed; seed < firstSeed + count; seed++)
        {
            yield return CubeState.Solved.Scramble(14, new Random(seed));
        }
    }
}
