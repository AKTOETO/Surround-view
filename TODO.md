# Оставшиеся работы

Сверено с кодом и проверочными отчётами 09.10.2026. Выполненные пункты удалены из этого списка; их реализация и результаты описаны в документах по ссылкам. Состояние Linux-прототипа и ограничения: [[prototype/STATUS]]. Подробная последовательность этапов: [[planning/ROADMAP]].

## Приоритет 1 — доказать качество решения для диплома

1. Расширить depth-truth validation за пределы аналитических плоскостей: независимые sphere/cube и discontinuous silhouettes на Blender EXR rasterization; добавить object-ID/semantic visibility, ground/raised markers, несколько сцен и train/holdout. Уже выполнена аналитическая plane-проверка и depth-based visibility screening одного кадра: [[validation/DEPTH_VISIBILITY_TRUTH]], [[validation/STITCH_VISIBILITY]].
2. Провести подтверждающую серию E-STITCH-01: реализовать seam-distance feather, graph-cut seam и multi-band baseline; сравнить с hard/edge/angular modes на одинаковых warps/input masks, а затем plane, bowl, dome+floor, cylinder, cube и параметризованной `burger-like` формой при равных mesh/memory budgets. Считать seam ΔE/градиент, object-ID ghost contours, depth consistency, timing и memory; включить held-out scenes/poses. Исследовательский протокол и screen results: [[research/PROJECTION_AND_STITCHING]], [[validation/STITCH_VISIBILITY]].
3. Добавить независимый image-quality oracle и корректную маску кузова/собственной видимости; сверить GPU readback с Blender scene truth. Измерить параллакс/двоение на ближних и вертикальных объектах, пропуски покрытия и движение seam на последовательности кадров.
4. Расширить калибровочное исследование за пределы известных intrinsics и четырёх PnP-вариантов: сравнить семейства моделей дисторсии, intrinsics/extrinsics и joint calibration, планарные/непланарные и кодированные шаблоны, разнообразие поз, occlusion, blur/glare и survey error. Настройку и validation разделять. Текущие результаты синтетических опытов: [[research/CALIBRATION]], [[research/IMAGE_CALIBRATION]], [[research/MOUNT_CALIBRATION]].
5. На доступных реальных снимках и физически измеренных позах повторить detector/calibration/diagnostic испытания; определить ложные тревоги, пропуски и физически обоснованные quality-gate пороги. Текущие 3 px RMSE / 8 px max предварительны.

## Приоритет 2 — превратить прототип в управляемую систему

6. Реализовать typed ConfigService в сервере и `sv-client-lib`: чтение, schema/capability discovery, validate, update, status и revision-aware apply. Конфиг изменяет только сервер; учесть atomic persistence, frame-boundary apply, pending restart, повтор operation ID, потерю ACK и disk errors. Реализованный wire-контракт и gaps: [[engineering/PROTOCOL_IMPLEMENTED]], целевые workflows: [[architecture/CLIENT_SERVER_MODEL]].
7. Перейти от односессионного accept/render path к нескольким клиентам; сделать view и subscriptions per-session, а source/calibration/fusion — server-owned. Проверить изоляцию медленного клиента и reconnect без потери чужой сессии.
8. Добавить subscriptions для final/intermediate products: stitched canvas, выбранные camera frames, validity/coverage/weights и metadata. Отключать final render/readback, когда у сессий нет подписки на final frame; ограничивать память, частоту и сетевой бюджет.
9. Завершить trace этапов pipeline: receive/decode/queue/sync/projection/fusion/render/readback/send/receive/present, корректно разделяя CPU/GPU и clock domains. Проверить slow subscribers, no-final path, dropped frames и saturation.
10. Довести `sv-simulator` до общего инструмента: добавить безопасное управление реальными server settings после появления ConfigService; объединить предусмотренные synthetic/photographic/Blender, replay/producer, calibration, experiment и report workflows через общие application services.
11. Связать driving preview с producer: синхронно рендерить четыре камеры с виртуальных монтажных поз движущегося автомобиля, отправлять кадры серверу, сохранять независимые poses/timestamps/depth/visibility. Добавить pause/reset/replayable trajectory, редактор камеры/ошибок монтажа и observations для запуска calibration jobs. Сейчас World tab — только визуальная управляемая сцена.
12. Расширить автомобильный Qt UI настройками, которые сервер действительно предоставляет, после реализации typed API; затем интегрировать Aurora lifecycle и touch UI. `QtQuick.Controls` не поддерживается Aurora согласно её руководству; использовать разрешённые Aurora Controls/Silica и проверить на SDK/устройстве: [[engineering/AURORA]].

## Приоритет 3 — проверить отказы, производительность и перенос

13. Проверить V4L2 на реальных камерах: negotiated format/resolution, sensor timestamps, disconnect/reconnect и ограниченное завершение при зависшем `VideoCapture::read`.
14. Выполнить two-host испытания `sv-client-lib` и producer: разделённые control/data, RGBA markers, clock mapping/skew, reconnect, пропуски/перестановки, длительный overload, RSS и thermal. TCP loopback и localhost не заменяют эту проверку.
15. Решить необходимость UDP отдельным transport design (packetization, reassembly, loss/reorder/deadlines); сейчас сервер намеренно принимает только настроенные Unix/TCP listeners.
16. Подготовить отдельные Aurora target packages GLM/OpenCV требуемых ABI/версий, проверить SDK BuildRequires и `mb2 installdeps`, собрать GPU/CPU RPM без FetchContent и пройти rpm-validator, установку и запуск на устройстве. Linux RPM и developer CMake fallback уже описаны в [[engineering/AURORA]]; target SDK/устройства в текущей проверке нет.
17. Сравнить одинаковую native test suite на Aurora с сохранёнными PC baselines, доступными API/extensions, задержкой, памятью и тепловыми условиями. Проверить `.desktop` и icon payload на целевом validator.

## Действия автора и финальная приёмка

- Согласовать с руководителем постановку, вклад и критерии диплома; подтвердить календарь защиты и доступ к целевому стенду.
- Заполнить аппаратный паспорт Aurora и предоставить физические камеры/калибровочную мишень для пунктов, которые нельзя закрыть синтетикой.
- После завершения опытов обновить выводы диплома, провести единую редактуру, повторить сборку и воспроизведение демонстрации. Текущие главы 1–5 — рабочая редакция: [[diploma/README]].

## Проверки, которые закрывают связанные пункты

Не объявлять пункты завершёнными по одному unit test. Сохранять исходные данные, конфигурацию, revision, версии, checksums, отчёт и отрицательные результаты. Общие условия приёмки: [[validation/ACCEPTANCE]]; аудит и подтверждения текущего состояния: [[planning/AUDIT]].
