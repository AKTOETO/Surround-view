# sv-simulator

`sv-simulator` is a Qt 6 desktop client for connecting to `sv-server` through `sv-client-lib`. The current GUI provides connection settings, local Unix IPC discovery, remote TCP connection, the server commands currently supported by the protocol, and read-only frame/source timing diagnostics. The larger world editor and runtime server configuration API remain future work; this application does not yet simulate vehicle motion or generate calibration observations.

## Connection setup

Build with the project's normal CMake configuration:

```sh
cmake -S . -B build -DSV_CLIENT=ON
cmake --build build --target sv-simulator
```

Launch without arguments to search local Unix socket directories. Discovery checks, in order, `SV_IPC_DIR`, the last-used directory, `$XDG_RUNTIME_DIR/sv-prototype`, `/tmp/sv-prototype`, `/tmp/sv-street`, and `/tmp/sv-blender`. It tries only directories containing both `control.sock` and `data.sock`, with one protocol connection attempt per candidate; stale sockets are skipped after failure. The search is intentionally bounded and does not scan all of `/tmp` or the network.

```sh
build/examples/sv-simulator/sv-simulator
```

You can also start from a specific endpoint:

```sh
build/examples/sv-simulator/sv-simulator --unix /tmp/sv-v4l2
build/examples/sv-simulator/sv-simulator --tcp 192.168.1.20 53101 53102
```

For an automated connection/frame smoke run, add `--smoke`; the process exits successfully after receiving a frame and exits with status 1 if no frame arrives within ten seconds.

The connection panel allows editing the Unix directory or TCP host/ports. Timeout, reconnect interval, and retry count configure the client library. These are client transport options; they do not rewrite the server config. Endpoint values are saved with Qt `QSettings`.

## Available server controls

- View presets, orbit, and zoom.
- Replay pause, resume, and step. `step` is rejected by live camera/socket sources.
- Frame metadata: source type, fusion/view identifiers, config revision, and which camera inputs were selected.
- Coarse pipeline timing fields supplied by the server. GPU draw time is shown only when a supported timer query produced a value; the reported intervals overlap and must not be summed.
- Calibration status/application for a job ID created by another client, such as `svctl`. The GUI does not fabricate observations, so starting a calibration job is disabled until it has a real observation workflow.

The current server protocol does not expose arbitrary config read/update, source switching, fusion selection, output subscriptions, or multi-client session management. Those controls should be added when the server implements their typed APIs, rather than writing the server's config file directly from this GUI.

## Verification limits

The GUI has been compiled on Linux. Remote two-host operation, Aurora, physical cameras, and interactive Blender-world simulation have not been accepted. Read [[engineering/CLIENT_LIBRARY]], [[engineering/USAGE]], and [[architecture/CLIENT_SERVER_MODEL]] for the current protocol boundary.
