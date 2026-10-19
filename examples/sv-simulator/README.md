# sv-simulator

The **Исследования** page provides a typed single-scenario editor, background C++ execution,
progress/cancel and timing rows. It shares FileExperiment with svctl; no Python process or
manual JSON is needed. Local reference selection and saved-scenario execution are supported; masked MAE/RMSE/PSNR and
criteria status are distinct from workflow success. Saved batch plans also run through the shared C++ run_batch service, with per-member progress,
cancellation and workflow/quality/restore summaries. The **Составить серию** mode adds, reorders and removes up to eight saved scenarios using controls.
The compiled service saves inputs/plan.json for reruns; referenced data is not bundled.
The **Импортировать план…** button loads an existing plan into the editable list, preserving
IDs/order and resolving paths from the selected batch file directory. Failed import leaves the
current draft intact; scenario/reference preflight still runs at execution.
The **Сохранить черновик как…** button writes a new schema1 plan without a connection or
scenario/reference IO. It refuses overwrite and tracks edits after saving. Saved absolute paths
remain valid when the plan file is moved, provided the source files stay in place.
The form reads the current session's ConfigService fusion catalog. Known methods,
pyramid boundaries, level ranges and seam solvers are checked in C++ before launching.
Unsupported selections remain explicit; unavailable or malformed catalogs block form starts.
Saved scenario/batch workflows retain their existing runner/server validation.
The **Матрица параметров** group sweeps a pyramid-level range and/or both seam solvers.
The C++ adapter applies axes only to relevant methods and shares the common matrix expander;
C++ preview enforces the aggregate 32-variant/10000-trial limits. Expanded ordinary scenarios
remain reusable with svctl. Carrier profiles and applicable edge-width/angle-power/smoothness lists are also supported.
The **Критерии качества опыта** group optionally replaces the reference manifest's full
threshold set with explicitly selected MAE/RMSE/PSNR limits. A reference and nonempty set
are required; the original manifest is unchanged and both policies are recorded in the report.
Default behavior still inherits the manifest. See docs/engineering/RESEARCH_QUALITY.md.
The **Сравнение** tab reads two individual native reports offline in a C++ worker, matches
full settings and gates B−A timing/quality deltas on reported input/workload/reference identities.
It shows both platform passports and exports an exclusive new audit file with raw SHA256.
This checks report claims, not data attestation or scientific acceptance; see
[report comparison](../../docs/engineering/RESEARCH_REPORTS.md).
See docs/engineering/RESEARCH_BATCH.md, section «Исследование из sv-simulator».

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
- Configuration forms: use the header Configuration shortcut, then Fusion / Carrier / Operation / Telemetry tabs. Read active/persisted, edit controls, validate, apply temporarily or explicitly save the entire active snapshot. Replies and reconnect recovery use typed sv-client-lib apply-once/status; no JSON entry or direct server file access. Unknown outcomes lock new edits; changed server instances require reconciliation. See `docs/engineering/CONFIG_SERVICE.md`.
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
sv-client-lib. Configure collection in the Telemetry configuration tab, then enable the separate 500ms reader
in the telemetry panel. CPU/RSS/render, Pipeline and I/O tabs keep at most120 samples and show loss/gaps,
fresh-only render quantiles and the latest config revision. Stopping the reader does not disable
server collection; epoch/disconnect resets the local window. Use Save window to export up to120 raw samples, then disconnect and Open offline report
to view the same charts. This is a static bounded snapshot; streaming/timed replay and other
new subsystem instrumentation remains pending. Existing span selectors include fresh-only quantiles;
I/O shows backend-specific queues/counters, delivery flags and the age of the last render record.
These are not dedicated capture/decode/send timers. Small Y values autoscale and metric switches repaint. See `docs/engineering/TELEMETRY_ARCHIVE.md`. Contract: `docs/engineering/TELEMETRY.md`.

Per-camera source work appears in I/O (read/load/prepare choices) when telemetry collection is enabled.
These are successful operations of selected render inputs; rerender can repeat the same source_sequence,
so capture quantiles are not displayed. Null means unavailable/not applicable, and replay cache hit
means skipped load (0ms). See `docs/engineering/SOURCE_TIMING.md` for clock/epoch guards and limits.


I/O also exposes six health counters/flags per camera (24 additional choices, 42 total): open attempts, read failures, OpenCV exceptions, reconnects, release failures and worker stopped. They follow source lifetime, not telemetry epoch, and remain available without a fresh frame. Unsupported backend values are missing; exact counters remain in details/archive when too large for graph doubles. See [[engineering/SOURCES#Диагностика состояния каждой аппаратной камеры]].


I/O details now show the checked backend mode and open-attempt identity without extra metric choices. Unrequested properties remain null in checked fields; separate best-effort Automatic readback is available when supported; closed/rejected capture clears the mode. Archive views show historical snapshots, not current camera availability. See [[engineering/SOURCES#Проверенный режим камеры через SDK]].


I/O details separate current checked camera mode from selected-frame capture provenance. A retained frame may belong to an older opening after reconnect; its attempt/mode is preserved in telemetry archives. No metric choices were added. [[engineering/SOURCES#Происхождение выбранного кадра при reconnect]].


I/O details include optional per-camera recovery snapshots: exact failure streak, full current retry interval (not time remaining), and terminal reason. Unsupported/legacy data stays unavailable. No additional metric choices; archive values are historical. See [[engineering/SOURCES#Снимок восстановления камеры]].


Current and Selected I/O details distinguish checked profile fields from optional Automatic readback (FPS/FOURCC/buffers). Invalid/unsupported readback is unknown and does not imply capture failure. The selected Frame retains its opening snapshot across reconnect. [[engineering/SOURCES#Наблюдаемые свойства автоматического профиля камеры]].


В форме исследования можно собирать набор полных active-профилей носителя и пересекать
его с fusion matrix. Сначала примените геометрию через конфигуратор и обновите active,
затем добавьте снимок; черновик не используется. Включите набор перед запуском.
Сценарий сохраняет точные surface в каждом variant, а карточки различаются индексом
и типом. Подробности: [набор носителей](../../docs/engineering/RESEARCH_BATCH.md#набор-носителей-в-форме).


В группе матрицы доступны раскрываемые числовые списки edge width/angle power/smoothness:
SpinBox, добавление и удаление отдельных значений. Оси учитываются только применимыми
методами; catalog проверяет и явные значения, и отправляемые defaults сценария.
[Применимость и пределы](../../docs/engineering/RESEARCH_BATCH.md#числовые-списки-параметров-fusion).


Сравнение отчётов использует policy v2 с observation gate: capture/evaluator/progress flag
и стабильные telemetry settings. Паспорта показывают эту нагрузку; legacy reports остаются
читаемыми, но не разрешают timing delta. [Контракт и пределы](../../docs/engineering/RESEARCH_REPORTS.md#нагрузка-наблюдения-и-policy-v2).


The **Переносимый набор** plan mode selects a bundle directory, verifies it offline and
runs it through the same C++ ScenarioBundle service as svctl. Expand **Экспортировать
сохранённый сценарий** to select a saved scenario, parent folder and new folder name.
File reads/copying/hashing/reference decoding happen in the research worker; operations
share busy/cancel/close handling. A previous verification receipt never bypasses run-time
verification. Server replay/configuration must be prepared separately; the bundle carries
client scenario/reference only. Unsaved form export and batch bundles remain future work.
[Workflow and limits](../../docs/engineering/RESEARCH_BATCH.md#переносимый-набор-в-sv-simulator).
