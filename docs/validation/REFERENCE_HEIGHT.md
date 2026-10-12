# Заданная высота носителя: native control и цена глобальной коррекции

Дата:12.10.2026. Продолжение [[validation/HYBRID_DOMAIN]], диплом §4.71. **Это supplied-height sensitivity control, а не depth estimation и не завершённая mitigation.** Native change `9ede8af`, frozen plan/collector `9f6a91a`. План `configs/research/reference-height-plan.json`, baseline `docs/validation/baselines/reference_height_v1.json`, raw `artifacts/reference-height-v1`, исходные конфигурации `artifacts/reference-height-configs-v1`.

## Что реализовано в продукте

Для `rectangular_bowl_v1` добавлено optional `surface.reference_height_m`, default0, finite range[0,5]m. Геометрия $z=h+H(d_x^2+d_y^2)/2$: при corner_height_m=0 получается плоскость на заданной высоте. Ненулевой H поднимает весь bowl, не меняя curvature. Другие types поле не принимают. Это предположение о поверхности, а не вычисленная глубина.

Сервер публикует field/default/unit/range/meaning в `surface_catalog.rectangular_reference_height`. Существующий `sv-client-lib::Client::configure_surface` передаёт full replacement surface с base_config_revision и optional experiment lease. Сервер валидирует bounds и view clearance, подготавливает mesh buffers и публикует между render calls. Source images не перезагружаются. Runtime revision/mesh counter меняются; temporary surface update не записывает исходный config на диск. Persistent ConfigService остаётся отдельной задачей.

CPU mesh, GPU projection и inspection используют одну поднятую геометрию. Region inspection сравнивает world.z с reference height, поэтому raised flat support не обозначается curved только из-за абсолютного z. Safe view требует eye.z>reference_height+corner_height+0.1m. Default0 сохраняет прежнюю геометрию и ordinary RGBA.

## Постановка и фиксация

Восемь native captures: frames0/1 × заранее выбранные heights0,0.4,0.8,1.2m, plane/edge_feather, прежние true calibration и RGB800×800/output640×360. **Высота target center0.8m уже известна из исследованного fixture**; это не blind estimate или holdout. Новый executable собран после native commit9ede8af, source revision в reports полный `9ede8af70911d07ddc89048c1b54ef72b433ce4f`, NVIDIA GeForce RTX5070Ti. Exact binary SHA256, source fingerprint, source/config/manifest/input/product hashes и invocations сохранены.

При h=0 оба ordinary output RGBA побитно совпали с прежними archived edge outputs. Capture inspection не меняет обычный кадр. Independent expected target mask, road object-ID ROI, virtual RGB/points и source visibility взяты из прежнего Blender direct truth, не из carrier. New Blender rendering не выполнялось. Native probe использует тот же renderer, что сервер; живое переключение отдельно проверено IPC integration test.

## Метрики и границы

Raw color proxy прежний: min(R,B)−G>0.15 в RGB8/255, без component filtering. Полная IoU сохраняет все outside pixels в union; hits/expected — лишь proxy recall и не semantic object recall. Source-visible expected ROI фиксирован:76/69 pixels. Carrier distance сравнивает native carrier points с independent virtual target points в world frame. Это не проверка surface identity source ray.

На фиксированном independent road ROI169447/169513 pixels считается end-to-end linear RGB MAE **включая непокрытые pixels**. Native carrier coverage на road изменяется: frame0 138052→147861, frame1 138058→147867 при h0→1.2; coverage counts сохранены отдельно. MAE не является isolated texture error на common covered pixels и не подменяется меняющимся удобным ROI.

| Кадр | h, м | Proxy hits / expected | Raw IoU | Proxy вне expected | Carrier/target median, м | Road linear RGB MAE |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0.0 | 0/178 | 0.000000 | 4843 | 1.348128 | 0.029184 |
| 0 | 0.4 | 0/178 | 0.000000 | 9127 | 0.779794 | 0.037895 |
| 0 | 0.8 | 175/178 | 0.017556 | 9790 | 0.237932 | 0.121100 |
| 0 | 1.2 | 0/178 | 0.000000 | 2273 | 0.356025 | 0.249403 |
| 1 | 0.0 | 0/172 | 0.000000 | 5373 | 1.371721 | 0.030401 |
| 1 | 0.4 | 0/172 | 0.000000 | 7106 | 0.783439 | 0.034518 |
| 1 | 0.8 | 168/172 | 0.034554 | 4690 | 0.243733 | 0.115435 |
| 1 | 1.2 | 0/172 | 0.000000 | 0 | 0.387058 | 0.247508 |

При h0.8 expected proxy hits выросли до175/178 и168/172; source-visible hits73/76 и67/69. Carrier/target median distance снизилась с1.348/1.372m до0.238/0.244m. Visible-point source projection median error снизилась с203.997/246.327px до30.636/34.672px; ошибка остаётся большой и не доказывает correct surface selection.

Цена глобального сдвига: outside proxy9790/4690, raw IoU лишь0.017556/0.034554, road MAE0.121100/0.115435 против0.029184/0.030401 при h0. При h1.2 raw hits снова0. Даже меньшая visible projection median25.981/28.330px при h1.2 не возвращает target proxy: этот scalar diagnostic не равен image quality. Установка известной высоты улучшает часть target alignment, но разрушает ground model и не устраняет ghosts/visibility.

## Проверки

Полная CMake сборка прошла. Итоговый Mesa CTest7/7:core,render_modes,source_correspondence,render_fusion_parity,replay_ipc,carrier_inspection,research_scenarios. Carrier GTest8/8 дополнительно прошёл на NVIDIA; raised-plane points сверены с independent actual-triangle oracle, default plane check сохранён. Nonzero constant interpolation имеет rounding до нескольких float32 ULP, поэтому поднятый plane-z проверяется при1e−6m, z0 — при1e−7m; это численный fixture tolerance, не физическая точность.

Integration test проверяет catalog, configure_surface .8m, mesh counter+1, unchanged inputs/uploads, rejection negative/>5 heights, stale/invalid updates и exact restoration старого кадра/config bytes. Core test проверяет optional default/schema, bounds, offset bowl/plane и clearance. Independent audit tests16/16, включая raw union/outside и undefined empty IoU. Reaudit побитно совпал.

## Воспроизведение

```sh
cmake -S . -B build
cmake --build build -j4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R '^(core|carrier_inspection|replay_ipc|render_fusion_parity|render_modes|source_correspondence|research_scenarios)$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_reference_height.py \
  --output-report artifacts/reference-height-v1/reaudit.json
cmp docs/validation/baselines/reference_height_v1.json artifacts/reference-height-v1/reaudit.json
```

Для нового capture скопировать frozen plan, задать новые config/output paths и hashes. Использовать archived executable либо оформить новую binary/source provenance. Пример **одного** из восьми native условий (OUTPUT должен быть новым; для всей серии frames×heights указаны в плане):

```sh
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json EGL_PLATFORM=surfaceless \
  artifacts/reference-height-v1/sv-correspondence-probe \
  artifacts/reference-height-configs-v1/frame0-h0.8.json \
  artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-inputs/manifest.json \
  artifacts/reference-height-repeat/frame0-h0.8 0
```

Сохранять invocations.json с binary/plan SHA и всеми8 return codes как в историческом raw root; новый audit запускать с `--plan`/`--output-report`/`--figures`, не заменять baseline автоматически. Plans связывают source hashes: historical audits после изменения renderer запускать на соответствующей версии исходников в отдельном checkout, с доступом к archived artifacts, а не отключать hash checks.

## Следующий этап

Глобальная reference height оставлена как runtime ablation knob. Успешная general mitigation **не продемонстрирована**. Требуется пространственно меняющаяся геометрия/visibility: сначала явный depth/height-informed control с отдельным источником дополнительной информации и independent truth, затем реалистичный вариант из доступных sensors/RGB. Ground и raised target должны оцениваться совместно. Не маскировать oracle scene depth под решение из четырёх RGB камер. Natural-object/holdout, calibration perturbations и physical/Aurora остаются открытыми.
