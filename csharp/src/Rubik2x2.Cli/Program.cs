using System.Diagnostics;
using Rubik2x2;
using Rubik2x2.Benchmarking;
using Rubik2x2.Solvers;

namespace Rubik2x2.Cli;

/// <summary>The <c>rubik2x2</c> command line. Arguments are parsed by hand.</summary>
internal static class Program
{
    private const string Usage = """
        rubik2x2 — optimal solver for the 2x2x2 Rubik's Cube (Pocket Cube)

        Usage:
          rubik2x2 solve <state> [--algorithm bfs|ida|table]
          rubik2x2 scramble [-n N] [--seed S]
          rubik2x2 table
          rubik2x2 bench [--json] [--scrambles FILE] [-n N]
          rubik2x2 show <state> [--color | --no-color]

        A state is 24 colour letters (rwbypg) in side order UP FRONT DOWN BACK LEFT
        RIGHT, each side read row-major; "-" reads the state from stdin. The solved
        cube is yyyyrrrrwwwwppppbbbbgggg.

        solve prints nothing but the move sequence, so it pipes. Moves are l/L, t/T
        and f/F: lowercase and uppercase are inverse quarter turns.
        """;

    private static int Main(string[] args)
    {
        try
        {
            return Dispatch(args);
        }
        catch (UnsolvableCubeException error)
        {
            Console.Error.WriteLine($"No solution: {error.Message}");
            return 1;
        }
        catch (FormatException error)
        {
            Console.Error.WriteLine($"Invalid cube: {error.Message}");
            return 2;
        }
        catch (ArgumentException error)
        {
            Console.Error.WriteLine(error.Message);
            return 2;
        }
    }

    private static int Dispatch(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            Console.WriteLine(Usage);
            return args.Length == 0 ? 2 : 0;
        }

        string[] rest = args[1..];
        return args[0] switch
        {
            "solve" => Solve(rest),
            "scramble" => Scramble(rest),
            "table" => Table(),
            "bench" => Bench(rest),
            "show" => Show(rest),
            _ => Unknown(args[0]),
        };
    }

    private static int Unknown(string command)
    {
        Console.Error.WriteLine($"Unknown command '{command}'.");
        Console.Error.WriteLine(Usage);
        return 2;
    }

    private static int Solve(string[] args)
    {
        Arguments arguments = Arguments.Parse(args, valued: ["--algorithm"], flags: []);
        CubeState state = ReadState(arguments.Positional(0, "solve needs a state"));

        Algorithm algorithm = Algorithm.Bfs;
        if (arguments.Value("--algorithm") is { } name && !Solver.TryParse(name, out algorithm))
        {
            throw new ArgumentException($"unknown algorithm '{name}' (expected {string.Join(", ", Solver.Names)})");
        }

        DistanceTable? table = algorithm is Algorithm.Table ? DistanceTable.Build() : null;
        string solution = Solver.Create(algorithm, table)(state);
        if (solution.Length > 0)
        {
            Console.WriteLine(solution);
        }
        else
        {
            Console.Error.WriteLine("Already solved.");
        }

        return 0;
    }

    private static int Scramble(string[] args)
    {
        Arguments arguments = Arguments.Parse(args, valued: ["-n", "--moves", "--seed"], flags: []);
        int moves = arguments.Integer("-n") ?? arguments.Integer("--moves") ?? 8;
        int? seed = arguments.Integer("--seed");
        Random random = seed is { } value ? new Random(value) : Random.Shared;
        Console.WriteLine(CubeState.Solved.Scramble(moves, random));
        return 0;
    }

    private static int Table()
    {
        long started = Stopwatch.GetTimestamp();
        DistanceTable table = DistanceTable.Build();
        TimeSpan elapsed = Stopwatch.GetElapsedTime(started);
        int[] histogram = table.Histogram();

        Console.WriteLine(string.Join(", ", histogram));
        Console.WriteLine(
            $"{histogram.Sum():N0} states, maximum distance {histogram.Length - 1}, " +
            $"built in {elapsed.TotalMilliseconds:F0} ms ({table.ByteCount / 1e6:F1} MB)");
        return 0;
    }

    private static int Show(string[] args)
    {
        Arguments arguments = Arguments.Parse(args, valued: [], flags: ["--color", "--no-color"]);
        CubeState state = ReadState(arguments.Positional(0, "show needs a state"));
        ColorMode mode = arguments.Flag("--no-color") ? ColorMode.None
            : arguments.Flag("--color") ? CubeRenderer.Best()
            : CubeRenderer.Detect(Console.IsOutputRedirected);
        Console.WriteLine(CubeRenderer.Render(state, mode));
        return 0;
    }

    private static int Bench(string[] args)
    {
        Arguments arguments = Arguments.Parse(args, valued: ["--scrambles", "-n", "--samples"], flags: ["--json"]);
        int count = arguments.Integer("-n") ?? arguments.Integer("--samples") ?? 200;
        string? path = arguments.Value("--scrambles");

        List<CubeState> scrambles = path is null ? Generate(count) : ReadScrambles(path, count);
        BenchmarkReport report = Benchmark.Run(scrambles);
        Console.WriteLine(arguments.Flag("--json") ? report.ToJson() : report.ToText(scrambles.Count));
        return 0;
    }

    private static List<CubeState> Generate(int count)
    {
        List<CubeState> scrambles = new(count);
        for (int seed = 0; seed < count; seed++)
        {
            scrambles.Add(CubeState.Solved.Scramble(DistanceTable.MaxDistance, new Random(seed)));
        }

        return scrambles;
    }

    private static List<CubeState> ReadScrambles(string path, int count)
    {
        List<CubeState> scrambles = [];
        foreach (string line in File.ReadLines(path))
        {
            if (line.AsSpan().Trim().IsEmpty)
            {
                continue;
            }

            scrambles.Add(CubeState.Parse(line));
            if (scrambles.Count == count)
            {
                break;
            }
        }

        if (scrambles.Count == 0)
        {
            throw new ArgumentException($"no scrambles in '{path}'");
        }

        return scrambles;
    }

    private static CubeState ReadState(string text) =>
        CubeState.Parse(text == "-" ? Console.In.ReadToEnd() : text);

    /// <summary>A hand-rolled parser: options with values, boolean flags, positionals.</summary>
    private sealed class Arguments
    {
        private readonly Dictionary<string, string> _values = [];
        private readonly HashSet<string> _flags = [];
        private readonly List<string> _positional = [];

        public static Arguments Parse(string[] args, string[] valued, string[] flags)
        {
            Arguments parsed = new();
            for (int i = 0; i < args.Length; i++)
            {
                string argument = args[i];
                if (Array.IndexOf(flags, argument) >= 0)
                {
                    parsed._flags.Add(argument);
                }
                else if (Array.IndexOf(valued, argument) >= 0)
                {
                    if (i + 1 >= args.Length)
                    {
                        throw new ArgumentException($"{argument} needs a value");
                    }

                    parsed._values[argument] = args[++i];
                }
                else if (argument.StartsWith("--", StringComparison.Ordinal) && argument.Contains('='))
                {
                    string[] pair = argument.Split('=', 2);
                    if (Array.IndexOf(valued, pair[0]) < 0)
                    {
                        throw new ArgumentException($"unknown option '{pair[0]}'");
                    }

                    parsed._values[pair[0]] = pair[1];
                }
                else if (argument.Length > 1 && argument[0] == '-' && argument != "-")
                {
                    throw new ArgumentException($"unknown option '{argument}'");
                }
                else
                {
                    parsed._positional.Add(argument);
                }
            }

            return parsed;
        }

        public string? Value(string option) => _values.GetValueOrDefault(option);

        public bool Flag(string option) => _flags.Contains(option);

        public int? Integer(string option)
        {
            if (Value(option) is not { } text)
            {
                return null;
            }

            return int.TryParse(text, out int value)
                ? value
                : throw new ArgumentException($"{option} expects a number, got '{text}'");
        }

        public string Positional(int index, string message) =>
            index < _positional.Count ? _positional[index] : throw new ArgumentException(message);
    }
}
