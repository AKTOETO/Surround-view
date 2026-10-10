# sv-simulator

`sv-simulator` is a Qt 6 desktop client for connecting to `sv-server` through `sv-client-lib`. The current GUI provides connection settings, local Unix IPC discovery, remote TCP connection, the server commands currently supported by the protocol, and read-only frame/source timing diagnostics. When Qt Quick 3D is installed, the GUI also provides a procedural street preview with a drivable vehicle and four visible camera markers. This first driving scene is a visualization only: it does not yet render synchronized camera images or send them to `sv-server`, and it does not generate calibration observations. The world editor, camera-error controls and general ConfigService remain future work. Runtime fusion/surface editing is available through the client library.

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

For a local Qt Quick 3D import/render smoke, run `QT_QPA_PLATFORM=offscreen build/examples/sv-simulator/sv-simulator --world-smoke`. It loads the driving scene and exits after three seconds; a real display/GPU run is still required to qualify interactive performance.

The connection panel allows editing the Unix directory or TCP host/ports. Timeout, reconnect interval, and retry count configure the client library. These are client transport options; they do not rewrite the server config. Endpoint values are saved with Qt `QSettings`.

## Available server controls

- View presets, orbit, and zoom.
- Runtime fusion and carrier editing: load a server snapshot, edit its JSON, and apply against the captured config revision. The panel displays server catalogs, all six fusion fields and the full active surface object. Stale revisions and invalid settings are rejected by the server. ACK updates the displayed actual state; drafts are not overwritten by incoming frames or acknowledgements. Reconnect clears drafts. Updates are temporary, with no server config file access from the client.
- Replay pause, resume, and step. `step` is rejected by live camera/socket sources.
- Frame metadata: source type, fusion/view identifiers, config revision, and which camera inputs were selected.
- Coarse pipeline timing fields supplied by the server. GPU draw time is shown only when a supported timer query produced a value; the reported intervals overlap and must not be summed.
- Calibration status, cancellation, and application for a job ID. The GUI does not fabricate observations, so starting a calibration job is disabled until it has a real observation workflow. Running native OpenCV solver cancellation is cooperative and takes effect after the solver returns.
- Optional Qt Quick 3D street preview: drive with `W/S` or arrow keys and steer with `A/D` or arrow keys. The scene has an elevated chase view, a road, generated building blocks and four camera markers on the car. Press `Esc` to leave driving mode. The preview is not a camera stream producer.

The current server protocol does not expose arbitrary config read/update, source switching, output subscriptions, or multi-client session management. Those controls should be added when the server implements their typed APIs, rather than writing the server's config file directly from this GUI. The implemented framing, command fields, ownership, calibration lifecycle and known gaps are documented in [[engineering/PROTOCOL_IMPLEMENTED]].

## Verification limits

The GUI and visual driving preview have been compiled/smoke-tested on Linux. Remote two-host operation, Aurora, physical cameras, and using the driving preview as a live camera producer have not been accepted. Read [[engineering/CLIENT_LIBRARY]], [[engineering/USAGE]], and [[architecture/CLIENT_SERVER_MODEL]] for the current protocol boundary.

Runtime command errors reported locally by the client library also end the pending operation. They are displayed as an unconfirmed request, not as a server rejection; mutations are never retried automatically. A lost session can have an unknown mutation outcome. Frame queue limits do not discard command acknowledgements or connection lifecycle events. The separate C++ regression is available as `ctest --test-dir build -R simulator_runtime_errors --output-on-failure`.

Explicit command-line endpoints also populate the connection panel. Invalid timeout/endpoint input is checked before stopping the current connection or saving replacement settings. Unix discovery skips directories longer than the transport's 80 UTF-8 byte limit and continues to later candidates; the last successful Unix directory is saved after the ready handshake. TCP preferences are stored when a valid connection attempt starts.

On session loss, discovery/replacement, or explicit disconnect, cached images, frame metrics, runtime snapshots, calibration command tracking and the implicit last calibration job ID are cleared. A job from an old session must be selected explicitly and remains subject to server ownership checks. Client transport failures for tracked calibration commands are shown as unconfirmed outcomes.

Frame image URLs include both session ID and frame ID, preventing cache/load identity collisions when a new session reuses frame numbers. The touchscreen client also ignores late image-ready notifications for a URL that is no longer current.

Ошибки обработки прикладных полей сообщения перехватываются на GUI-потоке: соединение закрывается, частичный runtime snapshot, calibration job и изображение очищаются, прежние queued callbacks инвалидируются. Причина отображается в status; для продолжения подключитесь заново через панель подключения. Команды изменения не повторяются автоматически. Углы, расстояние, ошибки калибровки и timings принимают целые и дробные JSON числа. Регрессии: [[validation/RESEARCH_RUNTIME]].
