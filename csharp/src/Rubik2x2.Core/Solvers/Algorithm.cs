namespace Rubik2x2.Solvers;

/// <summary>The three ways of finding a shortest solution.</summary>
/// <remarks>
/// All three return optimal solutions and differ only in what they spend to get
/// there: <see cref="Bfs"/> has no setup and moderate cost per solve,
/// <see cref="Ida"/> has cheap setup and the slowest solves, <see cref="Table"/> has
/// an expensive setup after which solving is effectively free.
/// </remarks>
public enum Algorithm
{
    /// <summary>Bidirectional breadth-first search; no precomputation.</summary>
    Bfs,

    /// <summary>Iterative-deepening A* with pattern databases.</summary>
    Ida,

    /// <summary>Lookup in the full distance table.</summary>
    Table,
}

/// <summary>Turns an <see cref="Algorithm"/> into something that solves cubes.</summary>
public static class Solver
{
    /// <summary>Every algorithm's command-line name, in declaration order.</summary>
    public static IReadOnlyList<string> Names { get; } = ["bfs", "ida", "table"];

    /// <summary>The command-line name of an algorithm.</summary>
    /// <param name="algorithm">The algorithm to name.</param>
    public static string NameOf(Algorithm algorithm) => Names[(int)algorithm];

    /// <summary>Parses a command-line algorithm name.</summary>
    /// <param name="name">One of <see cref="Names"/>, case-insensitively.</param>
    /// <param name="algorithm">The parsed algorithm.</param>
    /// <returns>True when the name is known.</returns>
    public static bool TryParse(string name, out Algorithm algorithm)
    {
        for (int i = 0; i < Names.Count; i++)
        {
            if (string.Equals(Names[i], name, StringComparison.OrdinalIgnoreCase))
            {
                algorithm = (Algorithm)i;
                return true;
            }
        }

        algorithm = default;
        return false;
    }

    /// <summary>Builds a solver.</summary>
    /// <param name="algorithm">Which algorithm to use.</param>
    /// <param name="table">
    /// The distance table, required by <see cref="Algorithm.Table"/> and unused otherwise.
    /// </param>
    /// <exception cref="ArgumentNullException">
    /// <see cref="Algorithm.Table"/> was asked for without a table.
    /// </exception>
    public static Func<CubeState, string> Create(Algorithm algorithm, DistanceTable? table = null) =>
        algorithm switch
        {
            Algorithm.Bfs => state => BidirectionalSearch.Solve(state),
            Algorithm.Ida => state => IdaStarSolver.Solve(state),
            Algorithm.Table => (table ?? throw new ArgumentNullException(nameof(table))).Solve,
            _ => throw new ArgumentOutOfRangeException(nameof(algorithm)),
        };
}
