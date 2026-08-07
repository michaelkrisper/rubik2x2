namespace Rubik2x2.Solvers;

/// <summary>
/// God's algorithm: the exact distance from every state to solved, in one byte each.
/// </summary>
/// <remarks>
/// <para>
/// The pocket cube has 3,674,160 positions — few enough to hold every one's exact
/// distance in a flat 3.7 MB array. Then there is nothing left to search, only a
/// table to read: at a state <em>d</em> moves from solved exactly one neighbour is
/// <em>d-1</em> away, so a solve costs <em>d</em> lookups, constant in the
/// difficulty of the scramble and optimal by construction.
/// </para>
/// <para>
/// Distances are stored as <c>distance + 1</c> so that zero can mean "not yet
/// reached" during the breadth-first build. The build is the whole trade: one
/// exhaustive sweep over 22 million edges, paid up front, which only wins above
/// its break-even — see the benchmark.
/// </para>
/// </remarks>
public sealed class DistanceTable
{
    /// <summary>God's number for the pocket cube in the quarter-turn metric.</summary>
    public const int MaxDistance = 14;

    private readonly byte[] _distances;

    private DistanceTable(byte[] distances, int workers)
    {
        _distances = distances;
        Workers = workers;
    }

    /// <summary>How many threads built this table; 1 for the sequential build.</summary>
    public int Workers { get; }

    /// <summary>Bytes the table occupies.</summary>
    public int ByteCount => _distances.Length;

    /// <summary>Reports one completed breadth-first level as (depth, states found).</summary>
    /// <param name="depth">The level just completed.</param>
    /// <param name="states">How many states sit at that distance.</param>
    public delegate void Progress(int depth, int states);

    /// <summary>Searches the whole state space on the calling thread.</summary>
    /// <param name="progress">Called once per completed level, if given.</param>
    public static DistanceTable Build(Progress? progress = null)
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[] p0 = tables.WidePermutation(0), p1 = tables.WidePermutation(1), p2 = tables.WidePermutation(2);
        int[] p3 = tables.WidePermutation(3), p4 = tables.WidePermutation(4), p5 = tables.WidePermutation(5);
        int[] o0 = tables.Orientation(0), o1 = tables.Orientation(1), o2 = tables.Orientation(2);
        int[] o3 = tables.Orientation(3), o4 = tables.Orientation(4), o5 = tables.Orientation(5);

        byte[] distances = new byte[Coordinates.StateCount];
        distances[Coordinates.SolvedIndex] = 1;

        // Both buffers start wide enough that the growing levels cost a handful of
        // reallocations rather than a dozen; they are swapped, never reallocated apart.
        int[] level = new int[1 << 16];
        int[] next = new int[1 << 16];
        level[0] = Coordinates.SolvedIndex;
        int levelCount = 1;
        int depth = 0;

        while (levelCount > 0)
        {
            progress?.Invoke(depth, levelCount);
            depth++;
            byte marker = (byte)(depth + 1);
            int found = 0;

            // Six children per node, unrolled: this loop runs 22 million times, and at
            // that point a table indirection per move is a measurable share of the build.
            for (int i = 0; i < levelCount; i++)
            {
                int permutation = Math.DivRem(level[i], Coordinates.OrientationCount, out int orientation);
                if (found + Moves.SolveCount > next.Length)
                {
                    Array.Resize(ref next, next.Length * 2);
                }

                int child = p0[permutation] + o0[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }

                child = p1[permutation] + o1[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }

                child = p2[permutation] + o2[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }

                child = p3[permutation] + o3[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }

                child = p4[permutation] + o4[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }

                child = p5[permutation] + o5[orientation];
                if (distances[child] == 0)
                {
                    distances[child] = marker;
                    next[found++] = child;
                }
            }

            (level, next) = (next, level);
            levelCount = found;
        }

        return new DistanceTable(distances, 1);
    }

    /// <summary>Searches the whole state space with several threads.</summary>
    /// <param name="workers">Threads to use; defaults to <see cref="Environment.ProcessorCount"/>.</param>
    /// <param name="progress">Called once per completed level, if given.</param>
    /// <remarks>
    /// The search is level-synchronous, so a level's frontier can be split across
    /// workers and merged at the barrier. Two workers can still reach the same state
    /// at once, which is settled by a compare-and-exchange claim: see
    /// <see cref="ParallelFrontier"/>. The resulting table is identical to
    /// <see cref="Build"/>'s, byte for byte.
    /// </remarks>
    public static DistanceTable BuildParallel(int workers = 0, Progress? progress = null)
    {
        workers = workers > 0 ? workers : Environment.ProcessorCount;
        byte[] distances = ParallelFrontier.Search(workers, progress);
        return new DistanceTable(distances, workers);
    }

    /// <summary>Quarter turns from an encoded state to solved.</summary>
    /// <param name="index">An index below <see cref="Coordinates.StateCount"/>.</param>
    public int Distance(int index) => _distances[index] - 1;

    /// <summary>Quarter turns from a cube to solved.</summary>
    /// <param name="state">The cube to measure.</param>
    public int Distance(CubeState state) => Distance(Coordinates.Encode(state));

    /// <summary>States per distance, index 0 being the solved state.</summary>
    /// <remarks>
    /// This must equal OEIS A079762. No incorrect move table or encoding can
    /// reproduce it, which makes it the strongest check the implementation has.
    /// </remarks>
    public int[] Histogram()
    {
        int[] histogram = new int[MaxDistance + 2];
        foreach (byte value in _distances)
        {
            histogram[value]++;
        }

        int highest = Array.FindLastIndex(histogram, count => count > 0);
        return histogram[1..(highest + 1)];
    }

    /// <summary>Walks downhill to solved: one table read per candidate move.</summary>
    /// <param name="state">The cube to solve.</param>
    /// <returns>A shortest move sequence; empty when the cube is already solved.</returns>
    /// <exception cref="UnsolvableCubeException">The cube is not reachable.</exception>
    public string Solve(CubeState state)
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int index = Coordinates.Encode(state);
        int remaining = _distances[index] - 1;
        Span<char> path = stackalloc char[MaxDistance];
        int length = 0;

        while (remaining > 0)
        {
            int permutation = Math.DivRem(index, Coordinates.OrientationCount, out int orientation);
            int stepped = -1;
            for (int move = 0; move < Moves.SolveCount; move++)
            {
                int child = tables.WidePermutation(move)[permutation] + tables.Orientation(move)[orientation];
                if (_distances[child] == remaining)
                {
                    // == (remaining - 1) + 1: one step closer.
                    path[length++] = Moves.Solve[move];
                    index = child;
                    remaining--;
                    stepped = move;
                    break;
                }
            }

            if (stepped < 0)
            {
                throw new UnsolvableCubeException("the distance table is inconsistent");
            }
        }

        return new string(path[..length]);
    }
}
