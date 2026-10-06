# Проверка FrameSource и виртуальных камер 0.6.0

Дата: 06.10.2026. Linux host, GCC 16.2/OpenCV 5/Boost 1.92; интеграция сервера — EGL surfaceless/Mesa llvmpipe. Реализация и границы: [[engineering/SOURCES]]. Исторические platform baselines и RPM 0.5.0 не изменены.

## Результаты

| Проверка | Результат |
|---|---|
| Release CTest | 12/12 групп |
| CPU Debug ASan/UBSan | 8/8 групп |
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

Тест создаёт временные manifest и Unix sockets в `/tmp`, удаляет их при штатном завершении. Platform report остаётся отдельным инструментом [[engineering/PLATFORM_TEST]] с 25 критериями/10 нагрузками в GPU-профиле: source tests не включены в этот счёт. Проверка payload/version нового RPM описывается отдельно после сборки; SDK/Аврора не проверены.

## Открытые условия

Не выполнены физическое двухмашинное испытание, аппаратный capture/cancellation и exposure timestamps, clock mapping, длительный overload/RSS/thermal, автоматический producer reconnect, realtime Blender rendering/вождение. Интеграция нового producer проверена на analytic recording; сохранённая Blender-запись ранее проверена replay-путём ([[validation/BLENDER_SMOKE]]). Source decoder ограничивает сообщение, а mailbox — число owning frames; это не замер пикового RSS.
