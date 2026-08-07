using Rubik2x2.Solvers;

namespace Rubik2x2.Tests;

/// <summary>
/// The distance table, checked against the published distance distribution.
/// </summary>
public sealed class DistanceTableTests
{
    /// <summary>OEIS A079762: states of the pocket cube by quarter-turn distance.</summary>
    private static readonly int[] OeisA079762 =
    [
        1, 6, 27, 120, 534, 2256, 8969, 33058, 114149, 360508,
        930588, 1350852, 782536, 90280, 276,
    ];

    [Fact]
    public void HistogramMatchesOeisA079762()
    {
        Assert.Equal(OeisA079762, TestCubes.Table.Histogram());
        Assert.Equal(Coordinates.StateCount, OeisA079762.Sum());
        Assert.Equal(DistanceTable.MaxDistance, OeisA079762.Length - 1);
    }

    [Fact]
    public void ParallelBuildProducesTheSameTable()
    {
        DistanceTable sequential = TestCubes.Table;
        DistanceTable parallel = TestCubes.ParallelTable;

        Assert.True(parallel.Workers > 0);
        Assert.Equal(sequential.Histogram(), parallel.Histogram());
        for (int index = 0; index < Coordinates.StateCount; index++)
        {
            if (sequential.Distance(index) != parallel.Distance(index))
            {
                Assert.Fail($"state {index} differs: {sequential.Distance(index)} vs {parallel.Distance(index)}");
            }
        }
    }

    [Fact]
    public void EveryStateHasADownhillNeighbour()
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        Random random = new(7);
        for (int trial = 0; trial < 5000; trial++)
        {
            int index = random.Next(Coordinates.StateCount);
            int distance = TestCubes.Table.Distance(index);
            if (distance == 0)
            {
                continue;
            }

            bool downhill = false;
            for (int move = 0; move < Moves.SolveCount && !downhill; move++)
            {
                downhill = TestCubes.Table.Distance(tables.Apply(index, move)) == distance - 1;
            }

            Assert.True(downhill, $"state {index} at distance {distance} has no neighbour one step closer");
        }
    }

    [Fact]
    public void TheAntipodalPositionIsFourteenMovesOut()
    {
        CubeState hardest = CubeState.Parse(TestCubes.Antipodal);
        Assert.Equal(14, TestCubes.Table.Distance(hardest));
        Assert.Equal(14, TestCubes.Table.Solve(hardest).Length);
    }
}
