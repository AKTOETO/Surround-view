# Проверка FrameSource и виртуальных камер 0.6.0

Дата: 06.10.2026. Linux host, GCC 16.2/OpenCV 5/Boost 1.92; интеграция сервера — EGL surfaceless. GPU regression groups дополнительно повторены с явным Mesa EGL vendor; Blender/socket smoke — NVIDIA RTX 5070 Ti. Реализация и границы: [[engineering/SOURCES]]. Исторические platform baselines и RPM 0.5.0 не изменены.

## Результаты

| Проверка | Результат |
|---|---|
| Release CTest | 12/12 групп |
| CPU Debug ASan/UBSan | 8/8 групп |
| GPU regression с явным Mesa vendor | 4/4 групп: render_modes, camera_sources, replay_ipc, client_transports |
| Injected blocked decode | GL-facing poll/request не ждут loader; pause completion после декодирования |
| Replay lifecycle | Pause barrier, один step, cache, bounded mailbox, decode failure и stopped request rejection |
| Source config | Replay/socket приняты; unknown type, duplicate camera, relative Unix path и invalid timeout отвергнуты |
| Socket saturation | Q последних frames каждой из четырёх камер; независимые quotas, точный drop count, cleanup/shutdown |
| Настоящий сервер Unix и TCP | Четыре RGB8 входа, READY, provenance, pause/orbit/resume, source step rejection |
| Camera faults | Duplicate закрывает один вход; остальные дают DEGRADED; новая session/sequence=0 восстанавливает READY |
| Metadata/deadline | Неверная calibration и partial handshake закрывают проблемную камеру |
| Host producer | Самостоятельный процесс передаёт verified analytic recording через Unix/TCP; malformed digest/path/offset отвергнуты до подключения |

Native fixture: `tests/source_tests.cpp` / `sv-source-tests`; host integration — три cases в `tests/test_sources.py`. CTest sources timeout 10 s, camera_sources — 45 s. API responsiveness проверена при намеренно заблокированном loader, а не по среднему времени быстрого чтения. Насыщение mailbox проверено без poll: при Q=3 отправлено 40 кадров, сохранено 12, вытеснено 28, каждая камера сохранила sequences 7…9. Socket shutdown проверен с верхней границей 1 s в native fixture. Эти тестовые границы не объявляются realtime guarantees на любой платформе.

## Повторение

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake --build build-cpu -j 4
ctest --test-dir build-cpu --output-on-failure
```

В обоих RPM предусмотрен `sv-source-tests` (без Python/GPU), запуск из любого рабочего каталога:

```sh
sv-source-tests /usr/share/surround-view/configs/synthetic.json
```

Тест создаёт временные manifest и Unix sockets в `/tmp`, удаляет их при штатном завершении. Platform report остаётся отдельным инструментом [[engineering/PLATFORM_TEST]] с 25 критериями/10 нагрузками в GPU-профиле: source tests не включены в этот счёт. Оба Linux RPM 0.6.0 собраны и проверены ниже; SDK/Аврора не проверены.

## Открытые условия

Не выполнены физическое двухмашинное испытание, аппаратный capture/cancellation и exposure timestamps, clock mapping, длительный overload/RSS/thermal, realtime Blender rendering/вождение. Интеграция producer проверена на analytic recording и сохранённой Blender-записи; оба source режима validator пройдены. Предыдущая проверка мира — [[validation/BLENDER_SMOKE]]. Source decoder ограничивает сообщение, а mailbox — число owning frames; это не замер пикового RSS.

## Linux RPM 0.6.0

Source0 обоих пакетов экспортирован из `674102a0b1e6baa4c3222f8f988104b64d658443`; SHA-256 `62a4777653caaec25e83ac9b4571758ce00e7211a820957e4381360fe533a0b0`. Implementation fingerprint `d7708c598d1845dbdcc017b7381f13c686100480a0c8946829a269b3b4220b42` совпадает с новыми PC baselines. Последующий commit `6e2707e` расширяет host Blender validator/рисунок, не C++ payload.

| Payload | Installed qualification | SHA-256 RPM |
|---|---|---|
| `artifacts/rpm-v06-gpu/RPMS/x86_64/surround-view-0.6.0-1.x86_64.rpm` | 25 pass, 0 fail, 0 skip; renderer RTX 5070 Ti | `5c9ed2e2773477c74408b7335e5c10424b58ab89d4510e8ce6ca5fb60eee462e` |
| `artifacts/rpm-v06-cpu/RPMS/x86_64/surround-view-cpu-0.6.0-1.x86_64.rpm` | 17 pass, 0 fail, 1 ожидаемый GPU skip | `c80383320eb393585ff2c5e0e92256abb0fd74184ab4c728a9f5af0561ebbfb6` |

В обоих payload запущены core (4684 checks) и native source fixture, из installed headers/static libraries/CMake exports собран внешний consumer без Qt/OpenCV/EGL/GLES. Файлы `INSTALLED_REPORT.md`, `SOURCE_TESTS.md`, `CLIENT_CONSUMER.md`, `files.txt`, `requires.txt`, `build.log` находятся в соответствующем RPM work directory. Короткая package qualification использует 8 samples/2 warmup/1 context и не заменяет PC performance baseline. `--nodeps` применяется только host verifier с `tests/rpm_host.macros`; это не приёмка Aurora BuildRequires/ABI.

## Blender → producer → socket source → GPU

`tools/blender/validate.py --source socket` проверил сохранённый metric-street dataset: 4 момента, 16 hashed PPM, все камеры меняются, четыре distinct READY output IDs, RGBA alpha=255, source clock provenance и front preset revision. Report: `artifacts/blender-virtual-v06/smoke.json`; manifest SHA-256 `d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc`. Actual renderer — NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2. Сохранённый `server-view.png` SHA-256 `29392ee9c5665f830a9035d53b9dd0592b639682eaa5102b198e370425add7be` опубликован как рисунок 3.14. Default replay validator также повторён: `artifacts/blender-replay-v06/smoke.json`, PASS. Команды — [[engineering/BLENDER]].

## Новые PC baselines

[[validation/baselines/PC_RTX_V06]] и [[validation/baselines/PC_MESA_V06]]: Release revision `6e2707e4b52a786b2c8580a6cf08e11043eb5454`, 60 samples/10 warmup/3 contexts, OpenCV threads=1, по 25 pass, 10 workloads. Strict comparison прошёл: [[validation/PC_COMPARISON_V06]]. Серии запущены последовательно; это короткие render/readback измерения, не full server/thermal/display latency. Старые 0.5 отчёты не менялись.

Первый запуск с label Mesa фактически использовал NVIDIA: он оставлен только в локальном `artifacts/platform-mesa-v06` и **не опубликован** как Mesa baseline. Для правильного запуска выбран `/usr/share/glvnd/egl_vendor.d/50_mesa.json`, actual GL_RENDERER — `llvmpipe (LLVM 22.1.8, 256 bits)`, raw report в `artifacts/platform-mesa-v06-forced`. `LIBGL_ALWAYS_SOFTWARE=1` отдельно недостаточен при GLVND. Инструкция — [[engineering/PLATFORM_TEST]].

Последующее host-расширение producer добавляет автоматический reconnect, late-drop и JSON/Markdown отчёты: [[validation/PRODUCER_RECOVERY]]. Первоначальная host acceptance содержала три cases; текущая camera_sources включает 13 cases. C++ payload и первичные native baselines выше не изменены.
