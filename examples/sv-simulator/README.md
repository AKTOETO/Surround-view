# sv-simulator

`sv-simulator` is a Qt 6 desktop client for connecting to `sv-server` through `sv-client-lib`. The current GUI provides connection settings, local Unix IPC discovery, remote TCP connection, the server commands currently supported by the protocol, and read-only frame/source timing diagnostics. When Qt Quick 3D is installed, the GUI also provides a procedural street preview with a drivable vehicle and four visible camera markers. This first driving scene is a visualization only: it does not yet render synchronized camera images or send them to `sv-server`, and it does not generate calibration observations. The world editor, camera-error controls and remaining ConfigService root groups remain future work. Typed ConfigService forms for fusion/surface, explicit runtime/persistence and process-scoped operation recovery are available through the client library.

## Connection setup

Build with the project's normal CMake configuration:

```sh
cmake -S . -B build -DSV_CLIENT=ON -DSV_SIMULATOR=ON
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
- Configuration forms: use the header Configuration shortcut, then Fusion / Carrier / Operation tabs. Read active/persisted, edit controls, validate, apply temporarily or explicitly save the entire active snapshot. Replies and reconnect recovery use typed sv-client-lib apply-once/status; no JSON entry or direct server file access. Unknown outcomes lock new edits; changed server instances require reconciliation. See `docs/engineering/CONFIG_SERVICE.md`.
- Replay pause, resume, and step. `step` is rejected by live camera/socket sources.
- Frame metadata: source type, fusion/view identifiers, config revision, and which camera inputs were selected.
- Coarse pipeline timing fields supplied by the server. GPU draw time is shown only when a supported timer query produced a value; the reported intervals overlap and must not be summed.
- Calibration status, cancellation, and application for a job ID. The GUI does not fabricate observations, so starting a calibration job is disabled until it has a real observation workflow. Running native OpenCV solver cancellation is cooperative and takes effect after the solver returns.
- Optional Qt Quick 3D street preview: drive with `W/S` or arrow keys and steer with `A/D` or arrow keys. The scene has an elevated chase view, a road, generated building blocks and four camera markers on the car. Press `Esc` to leave driving mode. The preview is not a camera stream producer.

The protocol exposes a scoped ConfigStore read and fusion/surface/telemetry updates; remaining group updates, source switching, parallel output subscriptions and multi-client session management are not implemented. Those controls should be added when the server implements their typed APIs, rather than writing the server's config file directly from this GUI. The implemented framing, command fields, ownership, calibration lifecycle and known gaps are documented in [[engineering/PROTOCOL_IMPLEMENTED]].

## Verification limits

The GUI and visual driving preview have been compiled/smoke-tested on Linux. Remote two-host operation, Aurora, physical cameras, and using the driving preview as a live camera producer have not been accepted. Read [[engineering/CLIENT_LIBRARY]], [[engineering/USAGE]], and [[architecture/CLIENT_SERVER_MODEL]] for the current protocol boundary.

Runtime command errors reported locally by the client library also end the pending operation. They are displayed as an unconfirmed request, not as a server rejection; mutations are never retried automatically. A lost session can have an unknown mutation outcome. Frame queue limits do not discard command acknowledgements or connection lifecycle events. The separate C++ regression is available as `ctest --test-dir build -R simulator_runtime_errors --output-on-failure`.

Explicit command-line endpoints also populate the connection panel. Invalid timeout/endpoint input is checked before stopping the current connection or saving replacement settings. Unix discovery skips directories longer than the transport's 80 UTF-8 byte limit and continues to later candidates; the last successful Unix directory is saved after the ready handshake. TCP preferences are stored when a valid connection attempt starts.

On session loss, discovery/replacement, or explicit disconnect, cached images, frame metrics, runtime snapshots, calibration command tracking and the implicit last calibration job ID are cleared. A job from an old session must be selected explicitly and remains subject to server ownership checks. Client transport failures for tracked calibration commands are shown as unconfirmed outcomes.

Frame image URLs include both session ID and frame ID, preventing cache/load identity collisions when a new session reuses frame numbers. The touchscreen client also ignores late image-ready notifications for a URL that is no longer current.

Ошибки обработки прикладных полей сообщения перехватываются на GUI-потоке: соединение закрывается, частичный runtime snapshot, calibration job и изображение очищаются, прежние queued callbacks инвалидируются. Причина отображается в status; для продолжения подключитесь заново через панель подключения. Команды изменения не повторяются автоматически. Углы, расстояние, ошибки калибровки и timings принимают целые и дробные JSON числа. Регрессии: [[validation/RESEARCH_RUNTIME]].

В fusion snapshot сервера добавлен `pyramid_boundary` (`zero`/`normalized`). Редактор принимает шесть базовых полей и optional седьмое; normalized переключает экспериментальную поддержку пирамиды в сервере. Default zero сохраняет прежние исследования. Контракт и ограничения: [[validation/PYRAMID_BOUNDARY]].

Catalog v4 additionally advertises optional `seam_solver=binary_pairs|alpha_expansion` for both graph-cut modes. The adapter accepts this field alongside pyramid_boundary and passes it through typed sv-client-lib. Loading an alpha snapshot preserves the field; omitting it on full apply resets the default binary_pairs. Unknown fields are rejected, and solver validation remains server-owned. The current form provides a solver selector; validation remains server-owned. Actual frame metadata includes energy/convergence diagnostics, with no local solver execution.

Catalog v5 adds optional `fusion.fallback_display={"mode":"legacy"|"hatch"}`. The configuration form has a fallback hatch checkbox; apply it through the normal validate/apply workflow. Existing server revision/rejection handling applies; legacy is omitted from normalized snapshots. Scenario variants also preserve the nested field. This marks the existing fallback branch only, not wrong-point sampling or scene confidence: [[validation/FALLBACK_DISPLAY]]. Fusion and required geometry settings now use tabbed controls. Optional support/occlusion forms are available under Carrier → Support/Visibility; other root groups remain planned.


## Configuration forms and build dependencies

Build with `-DSV_CLIENT=ON -DSV_SIMULATOR=ON`; Qt Quick Controls2 development/runtime QML modules
and Qt Quick Layouts must be installed. `-DSV_SIMULATOR=OFF` keeps the touchscreen client without
the laptop GUI; Aurora RPM profiles explicitly use this mode. No new dependency is downloaded.

The editor retains operation ID, instance, base, groups and persist before enqueueing. Reconnect
reads the server instance and queries status; explicit retry keeps the original intent. The journal
is limited to128 operations per server process; GUI intent is in-memory only. Restart needs manual
active/persisted reconciliation. Saving writes the entire active snapshot, including earlier runtime
changes. Optional geometry fields are preserved until switching type, which resets them with a warning.

Tests: `ctest --test-dir build -R 'simulator_runtime_errors|simulator_session_state|simulator_configuration_qml|client_transports|output_products' --output-on-failure`.
The last two require Unix/TCP bind permissions and EGL; select Mesa as described in project usage.
The QML test uses offscreen software rendering. Optional screenshot:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software SV_QML_SCREENSHOT=/tmp/sv-config-editor.png build/sv-simulator-configuration-qml-tests
```


## Optional surfaces and QML fixes (14.10.2026)

Carrier now has Geometry / Support / Visibility tabs. Toggle reference_height_m, local_support_patch,
source_occlusion_box or its uncertainty; disabling omits the field. AABB bounds, tolerance, three
visibility modes, guard texels, box-surface support tolerance/facing, uncertainty radius and ambiguous
policy use forms and server catalog. Mode changes remove incompatible fields; all semantic and
optics validation stays in sv-server. Tolerances display6 decimal places. These are supplied geometric
assumptions, not estimated scene depth/confidence. See `docs/engineering/CONFIG_SERVICE.md`.

Signal handlers now declare value/mouse/event explicitly. DriveWorld no longer overrides FINAL
left/right properties. Preview uses meter-sized builtins and radians internally, degrees for Euler;
Esc returns to frame view and focus loss clears movement keys. Click the 3D area to resume keyboard
control after using the sidebar. This remains a visual placeholder, not an image producer.

Qt Quick3D≥6.3 is required for preview. --world-smoke now fails if the lazy Loader fails, rather than
accepting only a successful main-window load. Native QML regression captures framework warnings,
loads DriveWorld and checks input/motion/units; a host Mesa OpenGL smoke also passed. Existing GUI
processes must be restarted to load rebuilt resources.

Server capability `telemetry_v1` adds opt-in process CPU/RSS and existing frame-span polling through
sv-client-lib. This simulator still displays its earlier frame diagnostics; dedicated polling graphs
and telemetry settings forms are pending. Contract: `docs/engineering/TELEMETRY.md`.
