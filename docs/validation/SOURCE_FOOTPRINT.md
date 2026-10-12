# Четыре texel билинейной выборки и остаточные расхождения

Дата: 12.10.2026. Follow-up [[validation/DISPLACED_CORRESPONDENCE]] (§4.66), после [[validation/HYBRID_CORRESPONDENCE]] (§4.67). План: `configs/research/source-footprint-plan.json`. Независимый Blender helper: `tools/blender/source_footprint.py`; read-only audit: `tools/research/source_footprint_audit.py`. Baseline: `docs/validation/baselines/source_footprint_v1.json`, raw: `artifacts/source-footprint-v1`.

## Постановка и выборка

Проверяются **все 1164** прежних output-conditioned magenta pixels, для которых ни один допустимый точечный source ray не попадал в target. Дополнительно берутся первые32 row-major pixels с target-ray hit из каждого из10 carrier/frame groups:320 контрольных pixels. Выборка зафиксирована до capture, не подбирается по новому результату. Два прежних коррелированных frames, пять carriers, edge_feather, true calibration,800×800 inputs/640×360 output. Это не независимые1164 наблюдения сцены и не оценка recall.

Native executable и выходы не пересчитываются. Frozen plan связывает старые native reports, исходные кадры, Blender recipe, scene truth, baseline и новые helper/audit hashes. После фиксации плана выявлены отсутствующие Pillow/SciPy в Python Blender; imports для независимого RGB audit перенесены внутрь collect. Геометрический capture использует NumPy/mathutils/bpy; окончательная версия плана зафиксирована `e22352c` до успешного capture. Пакеты в Blender не устанавливались. Пользовательская исходная сцена восстановлена через finally; архивные assets не перезаписывались.

## Независимая процедура

1. В Blender реконструируется прежняя сцена с target/road/ego и теми же camera extrinsics, poses и target positions. Исторические generator hashes проверяются.
2. На всей выбранной выборке повторяется центральный source ray. Его kind должен точно совпасть с архивом, world hit — в пределах1e−5m. Это контроль воспроизведения, не новый допуск правильности carrier geometry.
3. Для каждого допустимого native source UV рассчитываются четыре texel centers и билинейные веса. Учитываются clamp-to-edge, повторяющиеся граничные texel и нулевые веса.
4. Через каждый центр независимо проводится opaque scene ray. Kind target/road/ego/other записывается отдельно от actual RGB texel и его веса. Внутри case одинаковые texel rays кешируются; это не усреднение surface labels.
5. Read-only audit снова вычисляет footprint из native UV, проверяет файлы по SHA256, повторную идентичность центральных hits, выборку и RGB reconstruction. Взвешенная сумма actual texel RGB совпадает с прежним независимым bilinear sampler при допуске2e−15 normalized RGB.

Для $u=i+a$, $v=j+b$, где $i=\lfloor u\rfloor$, $j=\lfloor v\rfloor$, веса TL/TR/BL/BR равны:

\[
w_{00}=(1-a)(1-b),\quad w_{10}=a(1-b),\quad
w_{01}=(1-a)b,\quad w_{11}=ab.
\]

Вывод о наличии target support требует **строго положительного** веса texel и допустимой source camera. Color threshold остаётся прежним: min(R,B)−G>0.15 после деления RGB8 на255. Это proxy цвета, не surface identity. Веса здесь относятся к source bilinear interpolation, не к alpha multiband; предыдущая проверка fusion/RGBA остаётся отдельной.

## Результаты

| Кадр | Носитель | Расхождения | Есть target texel с весом > 0 | Нет target среди texel centers |
|---:|---|---:|---:|---:|
| 0 | plane | 192 | 164 | 28 |
| 0 | bowl | 131 | 117 | 14 |
| 0 | dome_floor | 192 | 163 | 29 |
| 0 | cylinder_floor | 192 | 164 | 28 |
| 0 | cube_floor | 192 | 164 | 28 |
| 1 | plane | 51 | 49 | 2 |
| 1 | bowl | 61 | 56 | 5 |
| 1 | dome_floor | 51 | 49 | 2 |
| 1 | cylinder_floor | 51 | 49 | 2 |
| 1 | cube_floor | 51 | 49 | 2 |

**1024/1164 (87.97%)** mismatch pixels имеют target-center support хотя бы в одном положительно взвешенном соседнем texel допустимой камеры. Это объясняет наличие соседнего геометрического support, но не доказывает, что весь RGB вклад сформирован именно target или что правильное положение объекта восстановлено.

**140/1164 (12.03%)** не имеют target hit ни на одном положительно взвешенном texel center допустимых cameras. При этом все140 имеют положительно взвешенный magenta texel. Следовательно, mismatch уже присутствует между RGB исходного texel и его центральным scene ray. Нельзя объявить140 false positives или приписать весь остаток server bilinear filtering.

Все320 target-ray controls имеют положительный target texel и положительный chroma texel. Это sanity control прежней выборки, не общая чувствительность детектора. Все1484 выбранных pixels имеют положительный chroma texel; центральные ray kinds/hits воспроизведены. Минимальная дистанция всех допустимых выбранных samples до края sensor проверена отдельно; нулевого edge_feather веса среди них нет.

## Что установлено и что осталось

Четыре source texel расширяют геометрическую поддержку относительно одного центрального ray. Но source texel сам сформирован filtered cubemap→fisheye conversion в linear light и Blender rasterization/antialiasing. Центральный opaque ray не описывает площадь такого фильтра. В140 остаточных случаях следующий опыт должен проследить **actual cubemap texels, interpolation weights и RGB**, проверить их central/area rays и исходный pixel filter. Приписывание остатка конкретно antialiasing пока не доказано.

Этот опыт не меняет product rendering и не устраняет height/parallax failure. Он не включает новые hybrid proxy coordinates (§4.67), все source pixels, GPU-only path, physical/Aurora trials или natural-object generalization. Эти задачи остаются в TODO/ROADMAP.

## Воспроизведение

Read-only audit использует system NumPy/SciPy/Pillow/Matplotlib и сохранённые raw artifacts:

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_footprint.py \
  --output-report artifacts/source-footprint-v1/reaudit.json
cmp docs/validation/baselines/source_footprint_v1.json artifacts/source-footprint-v1/reaudit.json
```

Результат:11/11 known-answer controls (включая fractional/zero/clamped footprint и selection без потери mismatches), CTest1/1; audit побитно воспроизводится. Рисунки и генератор расположены рядом с главой4. Product binary не менялся, поэтому native performance tests этим опытом не переоцениваются.

Для нового capture создайте копию плана с новым `output_root`, который ещё не существует. В Blender MCP выполнить следующий код с новым путём плана (original output повторно создавать нельзя):

```python
import sys, importlib
from pathlib import Path
root=Path('/home/bogdan/prog/MAI/surround-view')  # заменить на локальный корень
sys.path.insert(0,str(root/'tools/research'))
sys.path.insert(0,str(root/'tools/blender'))
import source_footprint
importlib.reload(source_footprint)
print(source_footprint.capture(root,root/'artifacts/source-footprint-repeat-plan.json'))
```

Audit копии: `python3 docs/diploma/plot_source_footprint.py --plan artifacts/source-footprint-repeat-plan.json --output-report artifacts/source-footprint-repeat/summary.json --figures artifacts/source-footprint-repeat/figures`. Plan hash меняется ожидаемо; не заменять исторический baseline новым capture автоматически.
