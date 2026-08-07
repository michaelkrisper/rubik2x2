using Rubik2x2.Solvers;

namespace Rubik2x2.Tests;

/// <summary>All three algorithms, on the same cubes, held to the same standard.</summary>
public sealed class SolverTests
{
    public static TheoryData<Algorithm> Algorithms => [Algorithm.Bfs, Algorithm.Ida, Algorithm.Table];

    [Theory]
    [MemberData(nameof(Algorithms))]
    public void SolutionsAreOptimalAndActuallySolve(Algorithm algorithm)
    {
        Func<CubeState, string> solve = Solver.Create(algorithm, TestCubes.Table);
        foreach (CubeState scramble in TestCubes.Scrambles(25))
        {
            string solution = solve(scramble);
            Assert.Equal(TestCubes.Table.Distance(scramble), solution.Length);
            Assert.True(scramble.Apply(solution).IsSolved, $"'{solution}' does not solve {scramble}");
        }
    }

    [Fact]
    public void TheThreeAlgorithmsAgreeOnLength()
    {
        foreach (CubeState scramble in TestCubes.Scrambles(25, firstSeed: 100))
        {
            int bfs = BidirectionalSearch.Solve(scramble).Length;
            int ida = IdaStarSolver.Solve(scramble).Length;
            int table = TestCubes.Table.Solve(scramble).Length;
            Assert.Equal(bfs, ida);
            Assert.Equal(bfs, table);
        }
    }

    [Theory]
    [MemberData(nameof(Algorithms))]
    public void TheAntipodalPositionNeedsExactlyFourteenMoves(Algorithm algorithm)
    {
        CubeState hardest = CubeState.Parse(TestCubes.Antipodal);
        string solution = Solver.Create(algorithm, TestCubes.Table)(hardest);
        Assert.Equal(14, solution.Length);
        Assert.True(hardest.Apply(solution).IsSolved);
    }

    [Theory]
    [MemberData(nameof(Algorithms))]
    public void ASolvedCubeNeedsNoMoves(Algorithm algorithm)
    {
        Assert.Empty(Solver.Create(algorithm, TestCubes.Table)(CubeState.Solved));
    }

    [Theory]
    [MemberData(nameof(Algorithms))]
    public void CubesHeldAnyWhichWayStillSolve(Algorithm algorithm)
    {
        Func<CubeState, string> solve = Solver.Create(algorithm, TestCubes.Table);
        CubeState scramble = CubeState.Solved.Scramble(14, new Random(31));
        int expected = TestCubes.Table.Distance(scramble);
        foreach (CubeState held in CubeSymmetry.Orientations(scramble))
        {
            string solution = solve(held);
            Assert.Equal(expected, solution.Length);
            Assert.True(held.Apply(solution).IsSolved);
        }
    }

    [Fact]
    public void PatternDatabaseBoundsAreAdmissibleButLoose()
    {
        PatternDatabases databases = PatternDatabases.Shared;
        (int permutation, int orientation) = databases.Reach;
        Assert.True(permutation <= 8 && orientation <= 8, "the databases should be too shallow to prune well");

        Random random = new(19);
        for (int trial = 0; trial < 20_000; trial++)
        {
            int index = random.Next(Coordinates.StateCount);
            Assert.True(databases.Bound(index) <= TestCubes.Table.Distance(index), $"bound overestimates at {index}");
        }
    }

    [Fact]
    public void AlgorithmNamesRoundTrip()
    {
        foreach (Algorithm algorithm in Enum.GetValues<Algorithm>())
        {
            Assert.True(Solver.TryParse(Solver.NameOf(algorithm), out Algorithm parsed));
            Assert.Equal(algorithm, parsed);
        }

        Assert.False(Solver.TryParse("nope", out _));
        Assert.Throws<ArgumentNullException>(() => Solver.Create(Algorithm.Table));
    }
}
