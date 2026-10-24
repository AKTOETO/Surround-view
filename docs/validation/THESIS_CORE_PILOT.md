# Сквозной пилот сокращённого дипломного исследования

25.10.2026. Протокол: [[research/THESIS_CORE_PROTOCOL]]. Это workflow pilot на старой
Blender street recording, не новая holdout-серия и не выбор лучшего алгоритма.
Product code не менялся; использованы existing server, sv-client-lib и C++ ExperimentRunner.

## Результат

success=true, restored=true;54 trials (12 warmup +42 measured),6 variants и42 сохранённых
RGBA. Каждый variant дал один и тот же RGB hash во всех9 повторениях. Все samples
используют один frame_set, одинаковые4 image/model identities и view; source timestamp0.
Все четыре packed RGB8 identities независимо пересчитаны из PPM с domain/size prefix.
Active mesh:3196/3200 triangles, position/index buffers58512/58392 bytes.
Original fusion/surface/pause восстановлены; baseline/restored RGB hashes совпали.
Replay cursor не восстанавливался. Сервер после опыта штатно завершён SIGINT.

![Шесть результатов настоящего сервера](../diploma/figures/experiments/thesis_core_pilot.png)

*Рисунок П.1 — один и тот же Blender street frame,3 fusion ×2 carriers, output320×180.
Панели выбираются как первый measured trial по заранее заданному порядку variants,
без выбора красивого повторения. Верхний ракурс в основном показывает пол: он не
подтверждает качество стенки купола; низкий ракурс предусмотрен финальным протоколом.*

| Carrier | Метод | Active triangles | Active buffers, bytes | Render wall p50, ms | p95, ms |
|---|---|---:|---:|---:|---:|
| plane | hard_best_angle | 3196 | 58512 | 2.133 | 2.348 |
| plane | edge_feather | 3196 | 58512 | 2.024 | 2.183 |
| plane | multi_band | 3196 | 58512 | 26.671 | 31.314 |
| dome_floor | hard_best_angle | 3200 | 58392 | 1.885 | 2.327 |
| dome_floor | edge_feather | 3200 | 58392 | 2.095 | 2.368 |
| dome_floor | multi_band | 3200 | 58392 | 28.799 | 28.959 |

Время — описательный render/readback wall на7 повторах одного frozen frame, не FPS,
не capture-to-display и не приёмка быстродействия. Capture/readback observation включено,
telemetry sampling выключен. Показатели нельзя переносить на RTX/Aurora или считать
стоимостью всех стадий без пересечений.

Actual GL_RENDERER: AMD Ryzen 9 9950X 16-Core Processor (radeonsi, raphael_mendocino, ACO, DRM 3.64, 7.2.6-arch2-1).
Mesa vendor selection не означает software llvmpipe. Configured build revision в отчёте
b9f9ca3: бинарник собран с тогда незакоммиченным исправлением, позже сохранённым вe866550.
Source fingerprint/полные client/server passports включены в compact audit; код после
проверенной сборки не менялся. Нельзя выдавать revision descriptor за чистую сборку b58812a.

Quality reference/criteria не подключались: quality_status=not_requested. Нет independent
RGB truth и проверки новых калибраторов; E1–E3 не объявляются завершёнными. Resident mesh
cache включает исходный high-density dome и другие carriers: активный лимит60500 bytes
не является resident/RSS bound. Для финальной серии начальный carrier задан протоколом.

## Первичные данные и воспроизведение

Raw config/report/42 RGBA/trace находятся в artifacts/thesis-core-pilot-20261025.
Compact audit с hashes/identities/resources/timings/passports:
[thesis_core_pilot_v1.json](baselines/thesis_core_pilot_v1.json).
Исторический manifest: artifacts/blender-street/manifest.json,
SHA256 d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc.
Raw report SHA256 8b000dbb032c79989a41105e9679dc2af499f16c25ca6cf47af06f277c8b3de1.

Подготовка config и команды native запуска: [[engineering/USAGE#Компактный дипломный пилот]].
При повторении report/capture directory должны быть новыми; первый прогон не перезаписывать.
Сохранённая Blender recording должна существовать либо быть воссоздана по
[[engineering/BLENDER]]. Она остаётся historical/exploratory, не становится holdout после повторения.

График и независимая проверка сохранённых captures (Python только post-processing):

```sh
MPLCONFIGDIR=/tmp/sv-thesis-pilot-mpl python3 docs/diploma/plot_thesis_pilot.py \
  --report artifacts/thesis-core-pilot-20261025/report.json \
  --config artifacts/thesis-core-pilot-20261025/config.json \
  --manifest artifacts/blender-street/manifest.json \
  --scenario configs/research/thesis-core.json \
  --figure docs/diploma/figures/experiments/thesis_core_pilot.png \
  --audit docs/validation/baselines/thesis_core_pilot_v1.json
```

Audit успешен; negative check с width640 вместо320 отклоняет несовместимый config
до экспорта figure/audit. py_compile script и git diff --check успешны. Полный CTest в этом срезе
не повторяется: product binaries не менялись; предыдущая проверенная сборка69/69 описана
в [[validation/CLIENT_SMOKE#Защита клиентских deadline]].
