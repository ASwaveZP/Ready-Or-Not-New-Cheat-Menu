using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Web.Script.Serialization;

namespace RonMenu;

internal sealed class MenuPipeClient
{
    private const string PipeName = "RONFentry.Menu.v1";

    private readonly JavaScriptSerializer _json = new();
    private readonly object _setLock = new();
    private Task _setTail = Task.CompletedTask;

    public Task<Dictionary<string, object>?> GetStateAsync() => Task.Run<Dictionary<string, object>?>(() =>
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            pipe.Connect(500);
            using var writer = new StreamWriter(pipe, new UTF8Encoding(false), 1024, true) { AutoFlush = true };
            using var reader = new StreamReader(pipe, Encoding.UTF8, false, 1024, true);
            writer.WriteLine("{\"type\":\"get\"}");
            var response = _json.Deserialize<Dictionary<string, object>>(reader.ReadLine() ?? "{}");
            if (response == null || !response.TryGetValue("settings", out var settings)) return new Dictionary<string, object>();
            var state = settings as Dictionary<string, object> ?? new Dictionary<string, object>();
            if (response.TryGetValue("status", out var rawStatus) && rawStatus is Dictionary<string, object> status && status.TryGetValue("map", out var map))
                state["__status_map"] = map;
            return state;
        }
        catch { return null; }
    });

    public Task<bool> SetAsync(string key, object value)
    {
        lock (_setLock)
        {
            var request = _setTail.ContinueWith(
                _ => SendSetAsync(key, value),
                CancellationToken.None,
                TaskContinuationOptions.None,
                TaskScheduler.Default).Unwrap();
            _setTail = request;
            return request;
        }
    }

    private Task<bool> SendSetAsync(string key, object value) => Task.Run(() =>
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            pipe.Connect(700);
            using var writer = new StreamWriter(pipe, new UTF8Encoding(false), 1024, true) { AutoFlush = true };
            using var reader = new StreamReader(pipe, Encoding.UTF8, false, 1024, true);
            writer.WriteLine(_json.Serialize(new Dictionary<string, object> { ["type"] = "set", ["key"] = key, ["value"] = value }));
            var ack = _json.Deserialize<Dictionary<string, object>>(reader.ReadLine() ?? "{}");
            return ack != null && ack.TryGetValue("ok", out var ok) && Convert.ToBoolean(ok);
        }
        catch { return false; }
    });
}
