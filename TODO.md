# Оставшиеся работы

Сверено с кодом и проверочными отчётами 09.10.2026. Выполненные пункты удалены из этого списка; их реализация и результаты описаны в документах по ссылкам. Состояние Linux-прототипа и ограничения: [[prototype/STATUS]]. Подробная последовательность этапов: [[planning/ROADMAP]].

## Приоритет 1 — доказать качество решения для диплома

Все исследовательские пункты Приоритета 1 успешно выполнены, верифицированы и интегрированы в дипломные главы:
- Аналитическая верификация Depth Truth на 3D-примитивах, силуэтах и семантике: [[validation/DEPTH_VISIBILITY_TRUTH]];
- Подтверждающая серия E-STITCH-01 (84 случая: 6 носителей × 7 стратегий слияния): [[research/PROJECTION_AND_STITCHING]], [[validation/STITCH_VISIBILITY]];
- Независимый оракул качества Scene Truth Oracle, маска кузова и GPU Readback: [[validation/IMAGE_QUALITY_ORACLE]];
- Расширенное калибровочное исследование (семейства дисторсии, Joint Bundle Adjustment): [[research/CALIBRATION]];
- Испытания детекторов в оптических условиях стресса и физическое обоснование порогов Quality Gate: [[diploma/04_EXPERIMENTAL_STUDY]].

## Приоритет 2 — превратить прототип в управляемую систему

1. Реализовать typed ConfigService в сервере и `sv-client-lib`: чтение, schema/capability discovery, validate, update, status и revision-aware apply. Конфиг изменяет только сервер; учесть atomic persistence, frame-boundary apply, pending restart, повтор operation ID, потерю ACK и disk errors. Реализованный wire-контракт и gaps: [[engineering/PROTOCOL_IMPLEMENTED]], целевые workflows: [[architecture/CLIENT_SERVER_MODEL]].
2. Перейти от односессионного accept/render path к нескольким клиентам; сделать view и subscriptions per-session, а source/calibration/fusion — server-owned. Проверить изоляцию медленного клиента и reconnect без потери чужой сессии.
3. Добавить subscriptions для final/intermediate products: stitched canvas, выбранные camera frames, validity/coverage/weights и metadata. Отключать final render/readback, когда у сессий нет подписки на final frame; ограничивать память, частоту и сетевой бюджет.
4. Завершить trace этапов pipeline: receive/decode/queue/sync/projection/fusion/render/readback/send/receive/present, корректно разделяя CPU/GPU и clock domains. Проверить slow subscribers, no-final path, dropped frames и saturation.
5. Довести `sv-simulator` до общего инструмента: добавить безопасное управление реальными server settings после появления ConfigService; объединить предусмотренные synthetic/photographic/Blender, replay/producer, calibration, experiment и report workflows через общие application services. Интегрировать в симулятор все режимы работы с сервером (включая запуск стандартизованных тестовых сценариев S0–S5) для сбора ключевых метрик на финальном стенде, чтобы обеспечить прямое сравнение финального стенда с самим собой и с конфигурацией ПК.
6. Связать driving preview с producer: синхронно рендерить четыре камеры с виртуальных монтажных поз движущегося автомобиля, отправлять кадры серверу, сохранять независимые poses/timestamps/depth/visibility. Добавить pause/reset/replayable trajectory, редактор камеры/ошибок монтажа и observations для запуска calibration jobs. Сейчас World tab — только визуальная управляемая сцена.
7. Расширить автомобильный Qt UI настройками, которые сервер действительно предоставляет, после реализации typed API; затем интегрировать Aurora lifecycle и touch UI. `QtQuick.Controls` не поддерживается Aurora согласно её руководству; использовать разрешённые Aurora Controls/Silica и проверить на SDK/устройстве: [[engineering/AURORA]].

## Приоритет 3 — проверить отказы, производительность и перенос

8. Проверить V4L2 на реальных камерах: negotiated format/resolution, sensor timestamps, disconnect/reconnect и ограниченное завершение при зависшем `VideoCapture::read`.
9. Выполнить two-host испытания `sv-client-lib` и producer: разделённые control/data, RGBA markers, clock mapping/skew, reconnect, пропуски/перестановки, длительный overload, RSS и thermal. TCP loopback и localhost не заменяют эту проверку.
10. Решить необходимость UDP отдельным transport design (packetization, reassembly, loss/reorder/deadlines); сейчас сервер намеренно принимает только настроенные Unix/TCP listeners.
11. Подготовить отдельные Aurora target packages GLM/OpenCV требуемых ABI/версий, проверить SDK BuildRequires и `mb2 installdeps`, собрать GPU/CPU RPM без FetchContent и пройти rpm-validator, установку и запуск на устройстве. Linux RPM и developer CMake fallback уже описаны в [[engineering/AURORA]]; target SDK/устройства в текущей проверке нет.
12. Сравнить одинаковую native test suite на Aurora с сохранёнными PC baselines, доступными API/extensions, задержкой, памятью и тепловыми условиями. Обеспечить сквозной сбор метрик на финальном стенде по сценариям S0–S5 через симулятор для сопоставления целевого стенда с базовой конфигурацией ПК. Проверить `.desktop` и icon payload на целевом validator.

## Действия автора и финальная приёмка

- Согласовать с руководителем постановку, вклад и критерии диплома; подтвердить календарь защиты и доступ к целевому стенду.
- Заполнить аппаратный паспорт Aurora и предоставить физические камеры/калибровочную мишень для пунктов, которые нельзя закрыть синтетикой.
- После завершения опытов обновить выводы диплома, провести единую редактуру, повторить сборку и воспроизведение демонстрации. Текущие главы 1–5 — рабочая редакция: [[diploma/README]].

## Проверки, которые закрывают связанные пункты

Не объявлять пункты завершёнными по одному unit test. Сохранять исходные данные, конфигурацию, revision, версии, checksums, отчёт и отрицательные результаты. Общие условия приёмки: [[validation/ACCEPTANCE]]; аудит и подтверждения текущего состояния: [[planning/AUDIT]].
