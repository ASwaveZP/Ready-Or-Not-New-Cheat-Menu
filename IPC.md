# Menu ↔ native DLL protocol

Transport: Windows named pipe `\\.\pipe\RONFentry.Menu.v1`.

Encoding: UTF-8, one JSON object per line. The client requests the current setting snapshot with:

```json
{"type":"get"}
```

The DLL returns a `state` object containing supported booleans, numeric settings, and the player-name field. Updates use a `set` message with a boolean, number, or string scalar:

```json
{"type":"set","key":"cfg_EnableGodmode","value":false}
```

The native side acknowledges accepted updates with an `ack` object. Unknown keys or value types receive `ok:false`. Numeric values are clamped to the native control's allowed range; accepted updates are applied on the game render thread and saved through the existing configuration writer. The pipe is local to the current Windows session; it does not expose a network listener.

The WPF UI is embedded in the native DLL and starts when the injected DLL hosts the .NET Framework CLR. Its status indicator shows whether the native pipe is available. A setting is only reported as applied after the native side acknowledges it.
