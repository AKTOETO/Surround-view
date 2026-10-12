# Radial footprint visibility: отрицательный preservation result

Кандидат реализован и проверен, но **не прошёл критерий сохранения видимого объекта**. На local carrier guard0 уменьшил outside198/56 до30/6, сохранив лишь12/76 и10/69 source-visible pixels. Guard1 дал outside1/0, но сохранил только2/76 и2/69. Default остаётся `center`; general mitigation и приоритет1 не закрыты.

## Серверный контракт

В сервер добавлен экспериментальный `surface.source_occlusion_box.mode="bilinear_radial_v1"` и `guard_texels=0/1`. Default `center` сохраняет прежнюю семантику. Настройки доступны через существующий `configure_surface`, каталог и sv-client-lib; отдельный wire command не нужен. При отключённом box фильтрация не выполняется. Для нового режима поддерживается только equidistant optics с нулевыми коэффициентами distortion; неподдерживаемая конфигурация отклоняется сервером.

Пусть $P$ — actual carrier position, $C_i$ — центр камеры, $R_i=\|P-C_i\|$. Для каждого соседнего source texel $j$ строится обратный equidistant ray $q_{i,j}$ и endpoint:

$$P_{i,j}=C_i+R_i\,R_{camera\to vehicle}\,q_{i,j}.$$

Расстояние берётся от центрального carrier point; это **radial approximation**, а не измеренная глубина соседней поверхности. При guard0 проверяются четыре bilinear texel с строго положительными коэффициентами. Guard1 проверяет 4×4 neighborhood. UV ограничиваются границами source raster. Sample отклоняется, если central ray или любой проверяемый сосед блокируется supplied opaque AABB до endpoint с прежним metric bias0.02m. Для соседей box сжат на10µm; центральный closed-box predicate не меняется. Обработка общей validity используется ordinary GPU, hybrid samples и inspection status7; retained UV и positions не меняются.

Inset установлен до frozen capture: исходный тест обнаружил float/double расхождения на точных касаниях ребра. После уточнения interior-контракта независимый double six-face oracle и shader slab test согласованы. Это численная конвенция, не доказательство физической точности10µm. Геометрия box — известный вход из Blender, не результат восстановления по RGB.

## Frozen experiment

План: `configs/research/filter-visibility-plan.json`; native revision `8274fdbf145e3cea7b303941c1911bf95d01b1f5`. Два прежних correlated frames × восемь вариантов =16 native captures на NVIDIA RTX5070Ti. Plane/local off, center, guard0 и guard1 сравниваются при прежних source RGB, independent Blender truth, cameras и geometry. Восемь off/center output hashes побитно совпали с историческими controls. Политики и bias объявлены до capture, после просмотра RGB параметры не подбирались.

Tracked отчёт: [[validation/baselines/filter_visibility_v1.json]]. Raw configs: `artifacts/filter-visibility-configs-v1`; products, archived binary, invocation log и отчёты: `artifacts/filter-visibility-v1`. В плане и отчёте сохранены hashes бинарника, исходников, inputs, configs и products. Raw artifacts не входят в Git; tracked JSON и рисунки сохраняют результаты, полное воспроизведение требует указанных входов.

| Frame | Носитель / policy | Outside proxy | Full IoU | Visible hits | Target без source | Road MAE |
|---|---|---:|---:|---:|---:|---:|
| 0 | `plane` | 4843 | 0.000000 | 0/76 | 0 | 0.029184 |
| 0 | `plane_center` | 192 | 0.000000 | 0/76 | 0 | 0.020990 |
| 0 | `plane_fp0` | 28 | 0.000000 | 0/76 | 0 | 0.020846 |
| 0 | `plane_fp1` | 0 | 0.000000 | 0/76 | 0 | 0.020779 |
| 0 | `local_100` | 4922 | 0.034902 | 76/76 | 0 | 0.029305 |
| 0 | `local_center` | 198 | 0.215426 | 76/76 | 78 | 0.020982 |
| 0 | `local_fp0` | 30 | 0.072115 | 12/76 | 144 | 0.020838 |
| 0 | `local_fp1` | 1 | 0.011173 | 2/76 | 163 | 0.020771 |
| 1 | `plane` | 5373 | 0.000000 | 0/69 | 0 | 0.030401 |
| 1 | `plane_center` | 51 | 0.000000 | 0/69 | 0 | 0.020843 |
| 1 | `plane_fp0` | 2 | 0.000000 | 0/69 | 0 | 0.020799 |
| 1 | `plane_fp1` | 0 | 0.000000 | 0/69 | 0 | 0.020784 |
| 1 | `local_100` | 5449 | 0.030600 | 69/69 | 0 | 0.030615 |
| 1 | `local_center` | 56 | 0.302632 | 69/69 | 103 | 0.020858 |
| 1 | `local_fp0` | 6 | 0.056180 | 10/69 | 162 | 0.020813 |
| 1 | `local_fp1` | 0 | 0.011628 | 2/69 | 170 | 0.020802 |

Outside — число chroma proxy pixels за независимой target mask; threshold `min(R,B)-G>0.15` на RGB8/255, без component filtering. Это coded-color proxy, не semantic ghost segmentation. Full IoU учитывает весь кадр, visible subset определён независимо от результата. Road MAE считается в linear RGB на неизменном road ROI, включая отсутствие source/fallback; ROI169447/169513 pixels, carrier coverage138052/138058 остаётся прежним.

![Actual native RGB](../diploma/figures/experiments/filter_visibility_rgb.png)

Рисунок — Independent Blender truth и actual local-carrier outputs; cyan contour показывает target mask. Почти полное исчезновение marker в guard1 нельзя трактовать как восстановление объекта.

![Joint preservation metrics](../diploma/figures/experiments/filter_visibility_metrics.png)

Рисунок — Outside, full IoU, visible hits и road MAE: уменьшение остатков сопровождается потерей полезного target.

## Проверки и воспроизведение

На2591739 prior-valid camera-point pairs в12 включённых условиях independent six-face oracle имеет0 status mismatches; carrier geometry и retained UV побитно прежние. Это проверка реализации predicate, не2591739 независимых наблюдений качества. Full build успешен; Mesa CTest7/7; carrier GTest10/10 на Mesa/NVIDIA; independent Python20/20. Повторный audit дал побитно одинаковый JSON.

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R 'core|carrier_inspection|replay_ipc|render_fusion_parity|render_modes|source_correspondence|research_scenarios' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_filter_visibility.py \
  --output-report artifacts/filter-visibility-v1/reaudit.json \
  --figures artifacts/filter-visibility-v1/reaudit-figures
cmp artifacts/filter-visibility-v1/reaudit.json docs/validation/baselines/filter_visibility_v1.json
```

Для нового capture использовать native `sv-correspondence-probe`; точные CLI arguments каждого из16 запусков сохранены в `artifacts/filter-visibility-v1/invocations.json`. Переиспользовать frozen configs и manifest из плана, сверить binary/input hashes до запуска. Не перезаписывать frozen products результатами иной сборки: для новой revision нужен отдельный output root и новый план. Полный порядок probe описан в [[engineering/USAGE]].

## Вывод и следующий опыт

Правило «любой сосед закрыт → удалить весь sample» с постоянным radial range слишком консервативно для выбранной сцены. Full IoU local снижается0.215426→0.072115→0.011173 и0.302632→0.056180→0.011628. Меньшие outside и road MAE сами по себе не означают улучшения изображения. Исчезновение target при guard1 делает критерий outside тривиальным без preservation controls.

Следующий кандидат должен различать соседей, принадлежащих той же поддерживаемой поверхности, и реальный occluder. Surface-consistent endpoint/identity — гипотеза для отдельного paired опыта, не установленная данным pilot причина и не реализованное исправление. Сохранить center control, independent visible subset, unsupported counts, road ROI; затем проверить tolerance/geometry uncertainty, другие scene families, natural-object holdout и равный бюджет overhead. Один known box и два correlated frames не дают универсального рейтинга; Aurora/physical evaluation остаются открытыми.
