---
tags: [validation, rendering, geometry, parallax]
status: controlled-pilot
updated: 2026-10-12
---

# Локальная заданная высота: объект и дорога

Следующий контроль после [[validation/REFERENCE_HEIGHT]] отделяет локальный подъём от глобального сдвига всей дороги. В native server реализован `surface.local_support_patch`: явный height field с плато и переходом к основной поверхности, применение через существующий `sv-client-lib::Client::configure_surface`. За пределами footprint геометрия остаётся прежней. **Положение, размеры, pose и высота взяты из известной синтетической сцены. Они не оценены из RGB.** Это ограниченный oracle-informed geometry control, не готовый depth/occlusion алгоритм.

## Зафиксированная постановка и происхождение данных

- Plan до capture: `configs/research/local-support-plan.json`; native revision `37f26348327c83adb40d7c18eaacac203240b865`, archived executable SHA `66425a424ac336621100db4a8b835510e2befe5108eba04266bc2dca1973182d`.
- Collector: `tools/research/local_support.py`; tracked report: `docs/validation/baselines/local_support_v1.json`; рисунки и код: `docs/diploma/plot_local_support.py`.
- Raw outputs/binary/logs/invocations: `artifacts/local-support-v1`; configs: `artifacts/local-support-configs-v1`. Raw assets не добавляются в Git/LFS.
- Прежние rig101-near-z080 RGB/истинная калибровка, input800×800/output640×360, edge_feather и virtual camera. Два correlated frames, перевод rig вдоль x на0.4m. Independent Blender RGB/object-ID/source-visibility/world-points из прежнего capture; файлы, manifest, configs, audit dependencies и native sources связаны SHA.
- Четыре заранее выбранных варианта на каждом кадре: plane z0, global z0.8, local plateau h0.8, local plateau h1.0. Local half extents0.2×0.2m, transition0.2m. Высоты соответствуют центру и верхней грани куба размером0.4m, с центром z0.8m. Это не поиск оптимума по результатам local captures.
- Центр world(2.4,2.3,0.8), преобразованный сохранённой pose: XY(2.4,2.3) и(2.0,2.3) в vehicle frame. Known-answer test проверяет также rotation, а не только translation.

Обе plane RGBA побитно совпали с историческими controls, обе global0.8 — с [[validation/REFERENCE_HEIGHT]]. Во всех восьми captures ordinary output unchanged относительно capture без inspection. Collector проверяет frozen files, порядок/return codes, binary/plan binding, source revision/fingerprint, inputs и products. Повторный audit побитно совпал с report.

## Метрики и результат

Raw chroma proxy: `min(R,B)-G > 0.15` на RGB8/255, без component filtering. IoU учитывает **все** outside pixels; proxy не является semantic segmentation. Геометрические distance/source-projection diagnostics считаются отдельно. Road linear RGB MAE оценивается на неизменных independent road ROI169447/169513 pixels, включая непокрытые пиксели. Указываются carrier coverage и число изменённых road pixels относительно plane.

| Frame | Вариант | Hits / expected | Raw IoU | Outside proxy | Road MAE | Road changed vs plane |
|---:|---|---:|---:|---:|---:|---:|
| 0 | plane | 0/178 | 0.000000 | 4843 | 0.029184315 | 0 |
| 0 | global0.8 | 175/178 | 0.017556 | 9790 | 0.121100048 | 144483 |
| 0 | local0.8 | 125/178 | 0.024601 | 4903 | 0.029279155 | 396 |
| 0 | local1.0 | 178/178 | 0.034902 | 4922 | 0.029304553 | 411 |
| 1 | plane | 0/172 | 0.000000 | 5373 | 0.030400689 | 0 |
| 1 | global0.8 | 168/172 | 0.034554 | 4690 | 0.115435489 | 144489 |
| 1 | local0.8 | 118/172 | 0.021053 | 5433 | 0.030572080 | 357 |
| 1 | local1.0 | 172/172 | 0.030600 | 5449 | 0.030615295 | 373 |

Local0.8 и global0.8 образуют сравнение при одинаковой заданной высоте: local сохраняет road error близко к plane, но source-visible proxy hits лишь31/76 и25/69; visible projection median201.419/244.293px. Height-center недостаточна для точной геометрии объёмного куба.

Local1.0 даёт все expected proxy hits, включая source-visible76/76 и69/69. Visible source-projection median0.004639/0.003396px, max0.019034/0.010720px на77/69 camera-point pairs. Это conditional subset, не весь объект: carrier/direct-target distance median0.015736/0.020126m и max0.130182/0.128747m остаются ненулевыми. Height field не воспроизводит вертикальные боковины и нижнюю поверхность висящего куба. Counts target coverage178/172 сохранены.

Road MAE local1.0 относительно plane растёт на0.41%/0.71%; road carrier coverage остаётся138052/138058. Участок не сдвигает всю дорогу, но закрывает часть её pixels, а изменение topology сетки может менять raster rounding вне footprint. Нельзя утверждать побитную неизменность RGB дороги по неизменности её геометрии.

Крупные ложные копии остаются: outside4922/5449 и raw IoU0.034902/0.030600. Для frame1 local1.0 raw IoU даже ниже global0.8, несмотря на лучшее target alignment. Позитивный вывод ограничен позиционированием при известной геометрии и меньшим road tradeoff. Устранение ghosts и general mitigation **не продемонстрированы**.

![Независимый RGB и native outputs](../diploma/figures/experiments/local_support_rgb.png)

Рисунок — Слева independent Blender, далее plane/global/local варианты; cyan — независимый expected target. Локальный объект появляется в ожидаемом месте, ложные копии остаются внизу.

![Совместные метрики объекта и дороги](../diploma/figures/experiments/local_support_metrics.png)

Рисунок — Полная raw IoU, outside count, fixed-road MAE и median distance; два correlated frames не заменяют holdout.

## Проверки и воспроизведение

Полная CMake сборка прошла. Mesa CTest7/7:core,render_modes,source_correspondence,render_fusion_parity,replay_ipc,carrier_inspection,research_scenarios. После усиления проверки local rays carrier GTest9/9 дополнительно прошёл на Mesa и NVIDIA. Core проверяет plateau/ramp/unchanged ground, наличие plateau vertices на грубой сетке, schema/bounds и view clearance. Integration проверяет catalog, runtime patch apply, revision/mesh counter, unchanged inputs/uploads, invalid transition/containment и exact restore original frame/config bytes. Independent Python controls17/17, включая vehicle-frame conversion.

```sh
cmake -S . -B build
cmake --build build -j4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R '^(core|carrier_inspection|replay_ipc|render_fusion_parity|render_modes|source_correspondence|research_scenarios)$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_local_support.py \
  --output-report artifacts/local-support-v1/reaudit.json \
  --figures artifacts/local-support-v1/reaudit-figures
cmp docs/validation/baselines/local_support_v1.json artifacts/local-support-v1/reaudit.json
```

Пример одного capture из восьми (для повтора использовать новый output root и новый plan, не перезаписывать frozen raw data):

```sh
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json EGL_PLATFORM=surfaceless \
  artifacts/local-support-v1/sv-correspondence-probe \
  artifacts/local-support-configs-v1/frame1-local_100.json \
  artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-inputs/manifest.json \
  artifacts/local-support-repeat/frame1-local_100 1
```

Configs образованы из прежнего frame0-h0.0.json: surface без patch для plane, reference_height_m0.8 для global, local fields из plan для local; XY через world-to-vehicle, h1.0=0.8+0.4/2. Все остальные поля прежние, hashes обязаны совпадать. Archive invocation содержит полный argv каждого случая. Reaudit требует archived raw assets; один Git checkout без них не воспроизводит числовые результаты. После изменения native sources historical audit запускать на соответствующем checkout, сохраняя hash checks.

## Что остаётся

Следующий контроль: server-side source visibility/occlusion с явно предоставленным depth/scene input, отдельно от geometry patch; проверить сохранение ground и удаление displaced copies при unchanged RGB. Потом — реалистичный способ получения дополнительной геометрии из доступных sensors/RGB, natural objects, новые poses/mounts/scenes, overhead и physical/Aurora. Один supplied patch не является универсальной scene model; sparse patch boundaries и corner triangulation требуют convergence controls. Эти пункты остаются в [[planning/ROADMAP]] и корневом TODO.
