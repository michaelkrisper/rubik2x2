namespace Rubik2x2.Solvers;

/// <summary>
/// The distance-table build, one breadth-first level at a time, across threads.
/// </summary>
/// <remarks>
/// <para>
/// Breadth-first search is level-synchronous: every state at depth <c>d</c> is known
/// before any state at depth <c>d+1</c> is expanded. That barrier is the whole
/// opportunity — within a level the frontier is just a list of independent nodes, so
/// it can be sliced across workers and merged at the barrier.
/// </para>
/// <para>
/// The hazard is the shared array: two workers can reach the same unvisited state at
/// the same moment, and both would append it. .NET has no atomic operation on a
/// single <see cref="byte"/>, so the claim is made on a side bitset of 32-bit words
/// with <see cref="Interlocked.CompareExchange(ref int, int, int)"/>: whoever sets
/// the bit owns the state and is the only one to write its distance byte and append
/// it. The bitset costs 460 KB, one twelfth of a second byte array, and the common
/// case — a state already visited — is a plain read with no bus traffic. Each worker
/// fills its own buffer, and the buffers are concatenated once per level, so nothing
/// else is shared.
/// </para>
/// </remarks>
internal static class ParallelFrontier
{
    /// <summary>Below this many nodes a level is not worth handing to the scheduler.</summary>
    private const int ParallelThreshold = 4096;

    internal static byte[] Search(int workers, DistanceTable.Progress? progress)
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[] p0 = tables.WidePermutation(0), p1 = tables.WidePermutation(1), p2 = tables.WidePermutation(2);
        int[] p3 = tables.WidePermutation(3), p4 = tables.WidePermutation(4), p5 = tables.WidePermutation(5);
        int[] o0 = tables.Orientation(0), o1 = tables.Orientation(1), o2 = tables.Orientation(2);
        int[] o3 = tables.Orientation(3), o4 = tables.Orientation(4), o5 = tables.Orientation(5);

        byte[] distances = new byte[Coordinates.StateCount];
        uint[] claimed = new uint[(Coordinates.StateCount + 31) / 32];

        Claim(claimed, Coordinates.SolvedIndex);
        distances[Coordinates.SolvedIndex] = 1;

        int chunkCount = workers * 4; // Slices per level: enough to even out stragglers.
        int[][] buffers = new int[chunkCount][];
        int[] counts = new int[chunkCount];
        for (int chunk = 0; chunk < chunkCount; chunk++)
        {
            buffers[chunk] = new int[1 << 16];
        }

        int[] level = new int[1 << 16];
        int[] next = new int[1 << 16];
        level[0] = Coordinates.SolvedIndex;
        int levelCount = 1;
        int depth = 0;
        ParallelOptions options = new() { MaxDegreeOfParallelism = workers };

        while (levelCount > 0)
        {
            progress?.Invoke(depth, levelCount);
            depth++;
            byte marker = (byte)(depth + 1);

            int slices = levelCount < ParallelThreshold ? 1 : chunkCount;
            int slice = ((levelCount - 1) / slices) + 1;
            int[] frontier = level;
            int nodes = levelCount;

            Parallel.For(0, slices, options, chunk =>
            {
                int start = chunk * slice;
                int end = Math.Min(start + slice, nodes);
                int[] buffer = buffers[chunk];
                int found = 0;

                for (int i = start; i < end; i++)
                {
                    int permutation = Math.DivRem(frontier[i], Coordinates.OrientationCount, out int orientation);
                    if (found + Moves.SolveCount > buffer.Length)
                    {
                        Array.Resize(ref buffer, buffer.Length * 2);
                    }

                    // Unrolled exactly as in the sequential build, so the two measure the
                    // same work and their setup times can be compared. The distance read
                    // is a hint only — the claim is what decides ownership.
                    int child = p0[permutation] + o0[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }

                    child = p1[permutation] + o1[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }

                    child = p2[permutation] + o2[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }

                    child = p3[permutation] + o3[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }

                    child = p4[permutation] + o4[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }

                    child = p5[permutation] + o5[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = marker;
                        buffer[found++] = child;
                    }
                }

                buffers[chunk] = buffer;
                counts[chunk] = found;
            });

            int total = 0;
            for (int chunk = 0; chunk < slices; chunk++)
            {
                total += counts[chunk];
            }

            if (total > next.Length)
            {
                next = new int[Math.Max(total, next.Length * 2)];
            }

            int offset = 0;
            for (int chunk = 0; chunk < slices; chunk++)
            {
                Array.Copy(buffers[chunk], 0, next, offset, counts[chunk]);
                offset += counts[chunk];
                counts[chunk] = 0;
            }

            (level, next) = (next, level);
            levelCount = total;
        }

        return distances;
    }

    /// <summary>Takes ownership of a state, or reports that another worker got there first.</summary>
    private static bool Claim(uint[] claimed, int index)
    {
        int word = index >> 5;
        uint bit = 1u << (index & 31);
        uint current = Volatile.Read(ref claimed[word]);
        while ((current & bit) == 0)
        {
            uint witnessed = Interlocked.CompareExchange(ref claimed[word], current | bit, current);
            if (witnessed == current)
            {
                return true;
            }

            current = witnessed;
        }

        return false;
    }
}
