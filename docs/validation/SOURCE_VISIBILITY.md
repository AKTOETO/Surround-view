---
tags: [validation, rendering, visibility, parallax]
status: controlled-pilot
updated: 2026-10-12
---

# Серверный supplied-box visibility control

После [[validation/LOCAL_SUPPORT]] проведён отдельный контроль перекрытия исходных камер. Native server исключает источник, если отрезок camera→carrier пересекает **явно предоставленный непрозрачный AABB** раньше конечной точки более чем на заданный допуск. Это один известный blocker, не полная scene/depth model и не RGB depth estimation. Он не рисуется и не изменяет carrier XYZ. Исполнение C++/GLES, доступ — существующий `sv-client-lib::Client::configure_surface`.

## Вход, контракт и provenance

`surface.source_occlusion_box` разрешён для rectangular carrier. Required fields: `min_m`, `max_m` — XYZ arrays из трёх finite numbers с упорядоченными bounds в[-100,100]m; `tolerance_m` ∈[0,0.1]m. Frame — vehicle. Отключение — отсутствие объекта. Catalog: `surface_catalog.rectangular_source_occlusion`. Config revisions, invalid/stale rejection и temporary restore используют прежний серверный путь; persistent config file не изменяется. Через этот путь visibility-only update пока также перестраивает mesh.

Native slab intersection использует origin из inverse camera extrinsics и finite segment до vehicle-frame carrier point. При distance≤tolerance rejection не выполняется. Иначе reject, если entry≤exit на отрезке[0,1] и `entry < 1-tolerance/distance`. Endpoint bias в метрах сохраняет точку на видимой грани. Проверка общая для ordinary GPU modes, hybrid validity samples и source inspection. Status7 `supplied_occluder`, UV NaN; прежние причины rejection имеют прежний приоритет. Выход за box ничего не говорит о других blockers сцены.

- Frozen plan: `configs/research/source-visibility-plan.json`, committed до captures. Native revision `4aa653e402e2e2df3228e6d3fb1e55dd41a5acbd`; binary/source SHA в плане и baseline.
- Native binary/raw/logs/invocations: `artifacts/source-visibility-v1`; configs: `artifacts/source-visibility-configs-v1`; raw не в Git/LFS.
- Tracked report: `docs/validation/baselines/source_visibility_v1.json`; независимый collector `tools/research/source_visibility.py` использует общий geometry audit `local_support.py`; рисунки — `docs/diploma/plot_source_visibility.py`.
- Прежние rig101-near-z080 RGB и independent Blender truth, true calibration, input800×800/output640×360, edge_feather; два correlated frames. Ни входные кадры, ни ground truth не регенерировались.
- Frames0/1 × plane,plane_occluded,local_100,local_100_occluded — восемь условий. Local plateau1.0m/half extents0.2m/transition0.2m прежние. World box min(2.2,2.1,0.6),max(2.6,2.5,1.0), известный nominal cube. Vehicle x bounds второго кадра сдвинуты на−0.4m. Tolerance0.02m выбран до captures, без подстройки.

Source/config/manifest/input/product hashes, frame index, binary binding, revision/fingerprint и unchanged ordinary output проверены. Plane и local_100 без visibility побитно совпали с предыдущими archived RGBA. В четырёх on/off парах carrier XYZ/regions и retained UV **побитно одинаковы**. Independent six-face oracle проверил863913 ранее допустимых camera-point pairs:20385 rejected,0 status mismatches. Это повторяющиеся samples четырёх correlated paired cases, не863913 независимых observations. Reaudit побитно совпал.

## Результаты на фиксированных independent ROI

Proxy `min(R,B)-G>0.15` на RGB8/255, без component filtering; IoU включает все outside pixels. Это диагностический цветовой marker, не semantic segmentation. Road linear RGB MAE — на неизменных independent road ROI169447/169513, включая uncovered/fallback pixels.

| Frame | Вариант | Hits / expected | Outside proxy | Raw IoU | Road MAE | Target без источника | Rejected camera pairs |
|---:|---|---:|---:|---:|---:|---:|---:|
| 0 | plane | 0/178 | 4843 | 0.000000 | 0.029184315 | 0 | 0 |
| 0 | plane + visibility | 0/178 | 192 | 0.000000 | 0.020989785 | 0 | 4655 |
| 0 | local1.0 | 178/178 | 4922 | 0.034902 | 0.029304553 | 0 | 0 |
| 0 | local1.0 + visibility | 81/178 | 198 | 0.215426 | 0.020982273 | 78 | 4912 |
| 1 | plane | 0/172 | 5373 | 0.000000 | 0.030400689 | 0 | 0 |
| 1 | plane + visibility | 0/172 | 51 | 0.000000 | 0.020843058 | 0 | 5322 |
| 1 | local1.0 | 172/172 | 5449 | 0.030600 | 0.030615295 | 0 | 0 |
| 1 | local1.0 + visibility | 69/172 | 56 | 0.302632 | 0.020858322 | 103 | 5496 |

Local outside count снижается на95.98%/98.97%. Source-visible target proxy hits остаются76/76 и69/69; visible source-projection pairs77/69 и их errors побитно прежние. Но full expected proxy hits снижаются до81/178 и69/172. After filtering target без источника78/103, road без источника524/572 вместо501/501; carrier coverage unchanged138052/138058.

Это разделяет два ранее смешанных эффекта: local geometry улучшает положение известной поверхности; source visibility убирает многие samples, на которых текстура объекта ошибочно переносилась на другую поверхность. Прежние100% expected proxy hits не доказывали наблюдаемость всего виртуального объекта. Source-visible subset составляет76/178 и69/172; полного независимого восстановления скрытых граней эта постановка не обеспечивает. «Без источника» — runtime validity count для текущего приближённого carrier, а не новая независимая semantic truth.

![Native RGB с проверкой перекрытия](../diploma/figures/experiments/source_visibility_rgb.png)

Рисунок — Independent Blender и actual native plane/local с visibility off/on. Крупные смещённые области маркера исчезают, остаются тонкие цветовые остатки; часть target теряет source support.

![Парные метрики visibility control](../diploma/figures/experiments/source_visibility_metrics.png)

Рисунок — Полная raw IoU, outside count, road MAE и unchanged geometry distance. Улучшение IoU не заменяет анализ покрытия и условной видимости.

## Проверки и воспроизведение

Full CMake build прошёл. Mesa CTest7/7:core,render_modes,source_correspondence,render_fusion_parity,replay_ipc,carrier_inspection,research_scenarios; carrier GTest10/10 дополнительно на NVIDIA. Новая GTest проверка сравнивает shader с independent six-face oracle, сохраняет front-facing endpoint, сверяет rejected hybrid samples с source status, отсутствие дополнительных uploads и exact restore. Core покрывает schema, размеры arrays и ordered bounds; IPC — catalog/apply/revisions, bad bounds/tolerance, counters и exact frame/config restore. Independent controls18/18, включая parallel/miss/endpoint/interior/translated rays. Frozen audit —0 mismatches, repeat identical.

```sh
cmake -S . -B build
cmake --build build -j4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R '^(core|carrier_inspection|replay_ipc|render_fusion_parity|render_modes|source_correspondence|research_scenarios)$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_visibility.py \
  --output-report artifacts/source-visibility-v1/reaudit.json \
  --figures artifacts/source-visibility-v1/reaudit-figures
cmp docs/validation/baselines/source_visibility_v1.json artifacts/source-visibility-v1/reaudit.json
```

Пример одного native capture; при повторе использовать новый output root/plan и сохранять все argv/return codes с binary/plan SHA:

```sh
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json EGL_PLATFORM=surfaceless \
  artifacts/source-visibility-v1/sv-correspondence-probe \
  artifacts/source-visibility-configs-v1/frame1-local_100_occluded.json \
  artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-inputs/manifest.json \
  artifacts/source-visibility-repeat/frame1-local_100_occluded 1
```

Configs — прежние local-support frameN-plane/local_100 плюс box для on cases, из plan с vehicle shift. Historical audits требуют archived raw и соответствующий source checkout; не отключать hash checks при последующей разработке. Две PNG создаются только независимым audit/plot, сам visibility algorithm выполняется сервером.

## Ограничения и следующий этап

Остаются198/56 outside proxy pixels, полный target частично unsupported. Проверка central ray не учитывает bilinear/cubemap/Eevee footprint; **причина каждого нового остатка этим опытом не установлена**. Следующий bounded diagnostic: per-pixel correspondence/footprint остатка, отдельно источник, mesh approximation и unsupported target; затем при необходимости filter-aware policy с preservation controls. Нужны допуск/geometry uncertainty sensitivity, новые сцены/натуральные объекты/poses, реалистичная оценка depth из доступных sensors/RGB и политика unsupported surfaces. No equal-budget timing/overhead, no physical/Aurora verification. Один known AABB — полезный механизм исследовательского сервера, не универсальное решение всех occlusions.
