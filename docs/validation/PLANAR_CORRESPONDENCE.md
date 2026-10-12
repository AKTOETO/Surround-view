# Изолированный planar correspondence control

Дата:11.10.2026. Геометрический known-answer follow-up к [[validation/SOURCE_CORRESPONDENCE_CONTROLS]]. План `configs/research/planar-correspondence-plan.json`, baseline `docs/validation/baselines/planar_correspondence_v1.json`, raw root `artifacts/planar-correspondence-v1`. Код/план/analytic test зафиксированы `f7ba3f2` **до scene rays**; сохранение отрицательных исходов и разложение UV residual — `cb347fa` после первого измерения. Пороги не изменялись.

## Что проверено

В отдельной Blender scene создан единственный mesh: квадрат Z=0 размером200×200m, без ego и occluders. Не box нулевой толщины и не плоскость поверх дороги: в сцене только одна поверхность. Из прежних native captures используются carrier XYZ, source UV/status и camera config. Рендерер заново не запускался; источники RGB прежней сцены с box **не являются planar RGB truth**. Это geometry-only control, не оценка RGB, fusion или ghosting.

Два frames выбирают166/161 output pixels по заранее сохранённой z020 target mask. Маска служит только выборкой rays; мишень-объект в новой сцене отсутствует. Virtual rays через эти pixel centers дают nearest plane hits Blender. По каждому допустимому native UV строится отдельный source ray и plane hit. Всего10 carrier/frame условий;249 valid pairs/carrier в frame0 и161 в frame1,2050 pairs суммарно. Это не независимые испытания сцены. Native plane/dome/cylinder/cube ROI имеет region1 (floor); bowl — region4.

Независимая аналитическая проверка использует \(t=-o_z/d_z\), \(P=o+td\) и собственную look-at конструкцию collector. Она не импортирует Blender virtual_pose. Parallel/behind/nonfinite rays отклоняются; known-answer test проверяет oblique/vertical rays и инвариантность к масштабу direction. Direct Blender/analytic residual≤0.000001444m, source residual≤0.000020301m. World/vehicle transforms применяются явно.

## Пороги и отрицательные результаты

До измерений заданы max physical distance0.0001m и max source UV gap0.01px. Оба обязательны для четырёх плоских floor carriers. Bowl изогнут, поэтому приведён отдельно без требования совпасть с плоскостью. Collector возвращает measured failures и aggregate `exact_planar_controls_passed=false`; данные не скрыты исключением после первого непрошедшего carrier.

| Frame | Carrier | Valid pairs | Max source distance, mm | Max UV gap, px | Strict control |
|---|---|---:|---:|---:|---|
| 0 | plane | 249 | 0.080437 | 0.007420 | pass |
| 0 | bowl | 249 | 29.191345 | 1.488423 | curved comparison |
| 0 | dome_floor | 249 | 0.084195 | 0.010064 | FAIL |
| 0 | cylinder_floor | 249 | 0.088415 | 0.011027 | FAIL |
| 0 | cube_floor | 249 | 0.101889 | 0.010465 | FAIL |
| 1 | plane | 161 | 0.067389 | 0.007453 | pass |
| 1 | bowl | 161 | 23.540358 | 1.322348 | curved comparison |
| 1 | dome_floor | 161 | 0.068936 | 0.007514 | pass |
| 1 | cylinder_floor | 161 | 0.085263 | 0.009351 | pass |
| 1 | cube_floor | 161 | 0.080697 | 0.007001 | pass |

**5/8 exact-floor условий прошли,3/8 не прошли**: dome/cylinder/cube в frame0. Max dome UV0.010064px, cylinder0.011027px; cube превышает и UV0.010465px, и surface0.101889mm. Пороги не ослаблялись после наблюдения. Plane прошёл оба frames; frame1 прошёл у всех четырёх exact-floor carriers.

Разложение показывает: native optical projection actual carrier point отличается от независимой double projection максимум на0.000081413px. Остальной UV gap уже проявляется при проекции native carrier position вместо independent virtual-plane point. Это локализует residual в цепочке carrier-hit/raster-position и согласованности virtual ray, но **не доказывает**, какая конкретно операция/precision вызывает его. Direct/source Blender numerical residual также ненулевой и отдельно сохранён. Нужен bounded precision/backend control, а не объявление всех проверок успешными.

При прежнем loose surface criterion0.02m все1640 valid source pairs exact-floor carriers соответствуют direct plane surface. Bowl даёт340/410 matches; max surface gap29.191/23.540mm, max UV gap1.488/1.322px для frames0/1. Это descriptive curvature effect в данном ROI, не рейтинг carrier качества. Общий planar floor механизм в выбранном ROI существенно точнее прежнего объёмного box correspondence; это не устраняет высотные искажения, окклюзии или stale frames.

## Воспроизведение и аудит

Требуются сохранённые raw captures [[validation/SOURCE_CORRESPONDENCE_CONTROLS]] и frozen files плана. Scene helper `tools/blender/planar_correspondence.py` выполняется в Blender MCP/console; collector `tools/research/planar_correspondence.py` только читает native exports. Python здесь — независимый исследовательский oracle/графики, основной C++ renderer не заменён.

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_planar_correspondence.py \
  --output-report artifacts/planar-correspondence-v1/reaudit.json
cmp docs/validation/baselines/planar_correspondence_v1.json \
  artifacts/planar-correspondence-v1/reaudit.json
```

Для нового ray capture скопируйте план, задайте новый output_root и вызовите внутри Blender (замените абсолютный root и путь копии плана):

```python
import sys
sys.path.insert(0, '/home/bogdan/prog/MAI/surround-view/tools/blender')
import planar_correspondence
planar_correspondence.capture(
    '/home/bogdan/prog/MAI/surround-view',
    '/home/bogdan/prog/MAI/surround-view/artifacts/planar-repeat-plan.json')
```

Audit новой серии: `python3 docs/diploma/plot_planar_correspondence.py --plan artifacts/planar-repeat-plan.json --output-report artifacts/planar-repeat/summary.json --figures artifacts/planar-repeat/figures`. Новый plan/helper hash меняет baseline, исходный не перезаписывать. Для другой native сборки нужна отдельная новая provenance/план, не только output_root.

Пять known-answer tests и CTest1/1 прошли. Повторный read-only audit побитно совпал с baseline. После добавления защиты существующего output (`422af28`) ray capture повторён: все14 NPY arrays совпали побитно с первым capture, сохранённым в `artifacts/planar-correspondence-v1-first-capture`; итоговый scene-truth содержит обновлённый helper hash. Отказ повторной записи проверен в Blender: active scene, число scenes/meshes/objects не изменились. Helper удаляет созданные scene/mesh/object и возвращает исходную активную сцену.

## Открыто

- Объяснить3 строгих failure отдельно: bounds finite-raster position precision, pixel-center/virtual-matrix согласованность, повторение на другом backend и независимых points. Не смешивать строгие пороги с прежним surface tolerance2cm.
- RGB planar fixture с соответствующими camera inputs/direct truth, bilinear footprint и boundary control; данный опыт этого не проверяет.
- Displaced target RGB attribution и hybrid level contributions; mitigation высотного failure.
- Triangle identity/coverage, bounded server/library diagnostics, независимые сцены и физический/Aurora стенд.

Первый paired precision/backend follow-up выполнен отдельно: [[validation/PLANAR_PRECISION]]. Он сохраняет исходные3 strict failures, проверяет analytical plane и partial uniform-rounding model; конкретный triangle/raster механизм пока не установлен.
