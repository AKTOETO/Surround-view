---
tags: [validation, rendering, visibility, sampling]
status: diagnostic-control
updated: 2026-10-12
---

# Остатки после source visibility: центральный луч и footprint

После [[validation/SOURCE_VISIBILITY]] выполнен независимый разбор всех остаточных цветовых pixels. **Native renderer и его параметры не изменялись.** Повторно использованы прежние ordinary RGBA/source UV/status/carrier XYZ; Blender восстановил frozen сцену и выполнил opaque scene ray casts. Исследован источник остатка, устранение его новым методом не заявляется.

## План и evidence

- Frozen plan `configs/research/visibility-residual-plan.json`,94 pinned files, committed до ray capture. Four cases: frames0/1 × plane_occluded/local_100_occluded. Selection: все raw chroma pixels вне independent target плюс **все** independent source-visible target pixels как controls, без лимита или выбора удачных examples.
- Blender helper `tools/blender/visibility_residual.py`; primary independent collector `tools/research/visibility_residual.py`; plot/descriptive post-hoc drill-down `docs/diploma/plot_visibility_residual.py`. Последний связывает собственный SHA с report, явно помечает post-hoc scope и не меняет primary plan/collector.
- Raw arrays/scene-truth/report: `artifacts/visibility-residual-v1`. Tracked baseline: `docs/validation/baselines/visibility_residual_v1.json`. Binary/source/native products связаны с историческим [[validation/SOURCE_VISIBILITY]], новых server captures нет.
- Blender5.2.2LTS; generator hashes, rig matrices и target pose проверены, flat-road override прежний. Исходная открытая Scene с Cube/Camera/Light восстановлена после capture. Изображения с камер не регенерировались.

Для каждого selected output pixel сохранены nearest virtual kind, source center kind/hit, четыре source texel center kinds, coordinates и bilinear weights. Поддержка texel учитывается только при положительном texture coefficient и camera edge weight. Цвета берутся из **реальных frozen input RGB**, а не из object-ID. Fusion edge_feather независимо реконструирован из четырёх bilinear samples в linear RGB и обратном sRGB encode.

## Результат

| Frame / carrier | Residual | Target at source center | Neighbor target texel only | Colored texel without target-center texel | Visible controls |
|---|---:|---:|---:|---:|---:|
| 0 / plane | 192 | 0 | 164 | 28 | 76 |
| 0 / local | 198 | 2 | 167 | 29 | 76 |
| 1 / plane | 51 | 0 | 49 | 2 | 69 |
| 1 / local | 56 | 0 | 50 | 6 | 69 |
| Всего | 497 | 2 | 430 | 65 | 290 |

Все497 residual virtual rays попадают в road,0 — в target. У всех есть положительно взвешенный chroma texel; no-weighted-source residual0. Для432 pixels есть положительный target texel-center support, включая2 center-target cases. Для оставшихся65 source RGB уже содержит цвет маркера, хотя ни один положительно взвешенный texel-center ray не попал в target. Это показывает различие между geometric center identity и отфильтрованным RGB. **Точные cubemap/Eevee filter weights для этих65 этим опытом не восстановлены**; прежние footprint controls не заменяют трассировку новых rays.

Независимая реконструкция outputRGB отличается менее чем на1LSB: max0.580158/0.580158/0.646510/0.773483LSB соответственно четырём cases, при заранее заданном пределе2LSB. Нет новых source RGB и подгонки output colors. Это численный контроль выборки и слияния, не полная causal reconstruction предыдущего Eevee rendering.

Controls: все76/69 direct pixels на каждом носителе независимо распознаны как target. На local все76/69 имеют center-target и target-texel support. На plane —0/0: один visibility predicate не создаёт отсутствующую высоту. Все controls имеют weighted source, но для plane он выбирает другую поверхность. Число290 включает повторные controls для двух носителей; независимых scene/poses не прибавилось.

![Классы остаточных пикселей](../diploma/figures/experiments/visibility_residual_support.png)

Рисунок — Раздельные center-target, neighboring-target texel и source-colored/no-target-center texel классы всех497 residual pixels.

## Post-hoc geometry drill-down

После primary audit отдельно исследованы два center-target pixels frame0/local, camera3:

| Output(y,x) | Carrier минус source-hit range, м | Blocked при tolerance0 | Blocked при tolerance0.02 |
|---|---:|---|---|
| (207,309) | 0.001996149 | да | нет |
| (208,309) | 0.019753227 | да | нет |

Независимый six-face predicate на actual carrier position подтверждает: оба source rays сохраняются при настроенном metric endpoint bias. Это проверка predicate, **не новый native rendering при нулевом допуске**. Automatic вывод «установить tolerance0» не следует: bias также защищает видимые грани от self-occlusion/raster rounding, нужны preservation controls.

У plane source-road hits совпадают с carrier с max19.182/16.499µm. У local есть4/5 residual output pixels хотя бы с одной source-road hit distance>2cm, max9.243/3.394m. Это отдельный признак приблизительной ramp geometry: ray может пересечь road далеко от поднятой точки. Эти4/5 — пересекающийся дополнительный diagnostic, их нельзя складывать с категориями таблицы. Порог2cm использует прежний tolerance, chosen post-hoc для описания, не оптимизации. Median остаётся порядка2–3µm и скрывал бы эти редкие большие отклонения.

![Положение остаточных пикселей](../diploma/figures/experiments/visibility_residual_pixels.png)

Рисунок — Actual frozen output: yellow residual, cyan independent target, red два center-target pixels у границы local ramp.

## Воспроизведение и проверки

```sh
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_visibility_residual.py \
  --output-report artifacts/visibility-residual-v1/reaudit.json \
  --figures artifacts/visibility-residual-v1/reaudit-figures
cmp docs/validation/baselines/visibility_residual_v1.json artifacts/visibility-residual-v1/reaudit.json
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
```

Independent tests19/19, включая complete residual/visible-control selection и empty selection; CTest source_correspondence прошёл. Reaudit побитно совпал. Product code не менялся, повторная native matrix/full build для этого read-only audit не требовалась.

Для нового Blender capture нужны archived inputs и новый output_root/plan. Код для `execute_blender_code` (root — путь репозитория на машине Blender):

```python
import sys, importlib.util
from pathlib import Path
root = Path('/home/bogdan/prog/MAI/surround-view')
sys.path[:0] = [str(root/'tools/research'), str(root/'tools/blender')]
# Scene dependencies могут изменить sys.path. Явно pre-load research module:
# одноимённый Blender helper не должен подменить его.
spec = importlib.util.spec_from_file_location('visibility_residual', root/'tools/research/visibility_residual.py')
audit = importlib.util.module_from_spec(spec)
sys.modules['visibility_residual'] = audit
spec.loader.exec_module(audit)
spec = importlib.util.spec_from_file_location('sv_visibility_residual_blender', root/'tools/blender/visibility_residual.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
print(helper.capture(root, root/'configs/research/visibility-residual-plan.json'))
```

В исходном запуске без явного preload возник import collision до создания capture output; приведённая команда устранила его. Frozen plan/helper не изменялись после успешного capture. Для воспроизведения historical audit нужны pinned sources и raw; не отключать hashes при последующих изменениях.

## Следующий этап

Разработать **server-side filter-aware visibility policy**, отдельно от geometry/sensor depth: учитывать source footprint и проверять сохранение всех local source-visible controls. Провести paired tolerance/geometry uncertainty sensitivity, отслеживать остатки, border erosion, новые unsupported pixels и road error. Не увеличивать объём/erosion ради одной метрики без preservation check. Затем реалистичный depth/scene input, natural-object holdout, overhead и physical/Aurora. Данные497 selected pixels — conditional diagnostic одной ранее исследованной сцены, не общий рейтинг алгоритмов.
