namespace Rubik2x2.Solvers;

/// <summary>
/// The distance-table build, one breadth-first level at a time, across threads.
/// </summary>
/// <remarks>
/// <para>
/// Breadth-first search is level-synchronous: every state at depth <c>d</c> is known
/// before any state at depth <c>d+1</c> is expanded. That barrier is the whole
/// opportunity — within a level the frontier is just a list of independent nodes, so
/// it can be sliced across workers and merged where they meet.
/// </para>
/// <para>
/// The hazard is the shared array: two workers can reach the same unvisited state at
/// the same moment, and both would append it. .NET has no atomic operation on a
/// single <see cref="byte"/>, so the claim is made on a side bitset of 32-bit words
/// with <see cref="Interlocked.CompareExchange(ref uint, uint, uint)"/>: whoever sets
/// the bit owns the state and is the only one to write its distance byte and append
/// it. The bitset costs 460 KB — one eighth of a second byte array — and the common
/// case, a state already visited, stays a plain read with no bus traffic. Each worker
/// fills a buffer of its own; the buffers are concatenated once per level, so nothing
/// else is shared.
/// </para>
/// <para>
/// The workers are dedicated threads around a <see cref="Barrier"/> rather than
/// <see cref="Parallel.For(int, int, Action{int})"/>. Whole levels take a few
/// milliseconds each and the build is over in a few hundred, which is the same
/// timescale as the thread pool's ramp-up: measured with <c>Parallel.For</c> the
/// build time swung between one and two times the sequential build depending on how
/// warm the pool happened to be. Threads that live for the whole search make the
/// figure repeatable, and the barrier's post-phase action is a natural place to merge
/// the level.
/// </para>
/// </remarks>
internal static class ParallelFrontier
{
    internal static byte[] Search(int workers, DistanceTable.Progress? progress)
    {
        CoordinateMoveTables tables = CoordinateMoveTables.Shared;
        int[] p0 = tables.WidePermutation(0), p1 = tables.WidePermutation(1), p2 = tables.WidePermutation(2);
        int[] p3 = tables.WidePermutation(3), p4 = tables.WidePermutation(4), p5 = tables.WidePermutation(5);
        int[] o0 = tables.Orientation(0), o1 = tables.Orientation(1), o2 = tables.Orientation(2);
        int[] o3 = tables.Orientation(3), o4 = tables.Orientation(4), o5 = tables.Orientation(5);

        byte[] distances = new byte[Coordinates.StateCount];
        uint[] claimed = new uint[((Coordinates.StateCount + 31) / 32) + 1];
        Claim(claimed, Coordinates.SolvedIndex);
        distances[Coordinates.SolvedIndex] = 1;

        int[][] buffers = new int[workers][];
        int[] counts = new int[workers];
        for (int worker = 0; worker < workers; worker++)
        {
            buffers[worker] = new int[1 << 16];
        }

        int[] level = new int[1 << 16];
        int[] next = new int[1 << 16];
        level[0] = Coordinates.SolvedIndex;
        int levelCount = 1;
        int depth = 0;
        byte marker = 2; // Children of the solved state are one move out: 1, stored as 2.

        using Barrier barrier = new(workers, _ =>
        {
            progress?.Invoke(depth, levelCount);
            depth++;
            marker = (byte)(depth + 2);

            int total = 0;
            for (int worker = 0; worker < workers; worker++)
            {
                total += counts[worker];
            }

            if (total > next.Length)
            {
                next = new int[Math.Max(total, next.Length * 2)];
            }

            int offset = 0;
            for (int worker = 0; worker < workers; worker++)
            {
                Array.Copy(buffers[worker], 0, next, offset, counts[worker]);
                offset += counts[worker];
                counts[worker] = 0;
            }

            (level, next) = (next, level);
            levelCount = total;
        });

        void Work(int id)
        {
            while (true)
            {
                int nodes = levelCount;
                if (nodes == 0)
                {
                    return;
                }

                int slice = ((nodes - 1) / workers) + 1;
                int start = Math.Min(id * slice, nodes);
                int end = Math.Min(start + slice, nodes);
                int[] frontier = level;
                int[] buffer = buffers[id];
                byte value = marker;
                int found = 0;

                for (int i = start; i < end; i++)
                {
                    int permutation = Math.DivRem(frontier[i], Coordinates.OrientationCount, out int orientation);
                    if (found + Moves.SolveCount > buffer.Length)
                    {
                        Array.Resize(ref buffer, buffer.Length * 2);
                    }

                    // Unrolled exactly as in the sequential build, so both measure the
                    // same work. The distance read is a hint; the claim decides ownership.
                    int child = p0[permutation] + o0[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }

                    child = p1[permutation] + o1[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }

                    child = p2[permutation] + o2[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }

                    child = p3[permutation] + o3[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }

                    child = p4[permutation] + o4[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }

                    child = p5[permutation] + o5[orientation];
                    if (distances[child] == 0 && Claim(claimed, child))
                    {
                        distances[child] = value;
                        buffer[found++] = child;
                    }
                }

                buffers[id] = buffer;
                counts[id] = found;
                barrier.SignalAndWait();
            }
        }

        Thread[] threads = new Thread[workers - 1];
        for (int worker = 0; worker < threads.Length; worker++)
        {
            int id = worker + 1;
            threads[worker] = new Thread(() => Work(id))
            {
                IsBackground = true,
                Name = $"rubik2x2-table-{id}",
            };
            threads[worker].Start();
        }

        Work(0); // The caller is a worker too; one fewer thread to create and schedule.
        foreach (Thread thread in threads)
        {
            thread.Join();
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
