# Финальная оптимизация и защита: перенос рекомендаций РБПО

Материалы прочитаны из `/home/bogdan/prog/MAI/RBPO/lab1/work/requirements.md`,
`work/roadmap.md`, `report/tables/threats_top5.md` и `report/tables/cwe_map.md`.
Это модель угроз/учебные требования R-01…R-12, а не подтверждение эксплуатации всех42 угроз
в текущем prototype. RBPO описывает расширенный вариант с тремя processing channels/арбитром;
текущий проект имеет другую границу реализации. Сам RBPO не изменялся.

Меры выполнять на финальном этапе hardening/оптимизации, после продуктового ConfigService,
mixed camera sources и GUI; не добавлять новые исследовательские ветви. Перед исправлениями
повторно сверить source IDs и код: требования/приоритеты учебного отчёта не заменяют тесты.

|Источник РБПО|Что проверить/доработать в Surround View|Критерий приёмки|
|---|---|---|
|R-03/R-12, T-03/T-10, CWE-287/266|Аутентификация network control/data, роли view/calibration/admin; локальные Unix permissions/peer identity отдельно|Неавторизованные подключения и critical commands отклоняются; no plaintext fallback; Unix-only не открывает IP listeners|
|R-05, T-01/T-07/T-32, CWE-345|Producer identity, integrity и anti-replay для socket cameras; binding camera_id/sequence/calibration/clock domain|Подмена camera_id/payload, stale/replayed frames не принимаются; authenticated reconnect восстанавливает корректный producer|
|R-06, CWE-306/345|Происхождение calibration observations/config, права persistent apply, audit и rollback|Подмена calibration manifest или недостаточная роль отклоняются до publication/persistence|
|R-08, T-08/T-09/T-12/T-13, CWE-190/400/770/755|Fuzz SV01/calibration/source decoders; caps/timeouts/rate limits до allocation; GPU/CPU budgets и bounded shutdown|Oversize/truncated/flood/reorder inputs не вызывают crash/unbounded RSS; медленный клиент не блокирует capture; отказ GPU/I/O оставляет согласованное состояние|
|R-10, T-09|Индикация stale/NO_INPUT/DEGRADED, frame age и причины в GUI|Потеря/зависание камеры видимы, устаревший кадр не выдаётся как свежий; сроки задаются по целевому стенду|
|R-11, CWE-778/117/284|Ограничение размера/rotation trace, audit persistent changes, neutralization полей и права файлов|Flood/log-injection не заполняют диск и не подделывают audit events; изменение/утрата записей обнаруживается выбранной policy|
|R-07, T-27/T-40, CWE-494|Подпись RPM/repository, проверка provenance/версий, offline dependency policy для Aurora|Неподписанный/подменённый пакет отклоняется целевым installer; SDK не скачивает зависимости во время RPM build|
|R-09/R-12, CWE-250/922|Least privilege process, device ACL/udev, service sandbox и разделение сетей по фактическому deployment|Сервер не требует root для штатной работы; разрешены только нужные devices/files/endpoints; параметры привилегий проверены на Aurora|

Транспортную защиту/аутентификацию согласовать отдельным protocol design. Нельзя просто включить
неописанный security flag в SV01v1 (сейчас flags0) или считать CRC/hash аутентификацией.
HMAC/mTLS/сертификаты и управление ключами выбирать вместе с угрозами/ресурсными бюджетами;
физическая локальная V4L2 camera не обязана иметь network certificate. Защита UDP нужна только
после отдельного решения реализовать UDP, а не потому, что она есть в учебном варианте.

R-01/R-02 (TMR/арбитр2из3), DTLS telemetry, VLAN/802.1X, NTS/DNSSEC и WORM90 суток —
отдельные deployment/architecture решения, не обязательный рефакторинг дипломного ядра.
Для TMR заранее определить time alignment и допустимые различия результатов: строгий hash
разных GPU/backend нельзя считать универсальным quality oracle (см. [[validation/ENDPOINT_PRECISION]]).
Hash chain без внешнего доверенного anchor сама по себе не доказывает отсутствие удаления tail.

Оптимизацию выполнять по PC/target reports с одинаковыми scenarios: CPU/GPU stage timings,
capture/queue/skew, RSS/VRAM, bandwidth, thermal и slow subscribers. После исправлений повторять
quality/rejection regressions; уменьшение latency не оправдывает скрытое ухудшение calibration,
seams или freshness. Источники угроз связывать с конкретным тестом и commit, а не закрывать
пункт по одному наличию библиотеки TLS или compiler flag.

Связанные планы: [[../TODO]], [[planning/ROADMAP]], [[engineering/SOURCES]],
[[engineering/CONFIG_SERVICE]], [[engineering/PROTOCOL_IMPLEMENTED]].
