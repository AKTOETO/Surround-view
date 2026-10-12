# Cubemap→fisheye: RGB и геометрическая поддержка остаточных samples

Дата:12.10.2026. Продолжение [[validation/SOURCE_FOOTPRINT]], диплом §4.69. План `configs/research/cube-footprint-plan.json`, baseline `docs/validation/baselines/cube_footprint_v1.json`. Raw root `artifacts/cube-footprint-v1`, frozen requests `artifacts/cube-footprint-requests-v1.json`. Native renderer и исходные кадры не менялись.

## Выборка и provenance

Проверяются **все140** pixels, оставшиеся без target-hit среди положительно взвешенных fisheye texel centers в §4.68. Включены все положительно взвешенные texel каждой допустимой source camera этих pixels, не только цветные. Повторяющиеся requests дедуплицированы по frame/camera/source x/y:256 уникальных fisheye texel и592 уникальных frame/camera/face/cube texel centers. Group mapping сохраняет исходные140 output pixels и их carriers. Frames0/1 одного rig/seed коррелированы; сумма по пяти carriers не является пятью независимыми сценами.

План/код/tests зафиксированы `3c4f9f1` до Blender capture. Frozen files включают предыдущие plans/baselines/scene truth, новый helper/collector, requests, capture.json, ground_truth.json, converter и coordinate contract. Requests связывают actual cube/input PNG SHA256, source RGB, выбранную face, UV, четыре cube texel, веса и RGB каждого texel. Повторная генерация requests дала идентичную структуру. Исторический capture — BLENDER_EEVEE, face512, существующие800×800 fisheye inputs. Новый geometric oracle — Blender5.2.2LTS. Настройки samples/pixel filter старый capture.json полностью не архивирует; текущую настройку Blender нельзя выдавать за историческую.

## Независимый RGB контроль

Для zero-distortion equidistant source pixel $(u,v)$ вычисляется optical ray с $a=(u-c_x)/f_x$, $b=(v-c_y)/f_y$, $\theta=\sqrt{a^2+b^2}$:

\[
\mathbf r=(a\sin\theta/\theta,\; b\sin\theta/\theta,\;\cos\theta).
\]

При $\theta=0$ используется непрерывный предел. Dominant-axis face выбирается с X/Y/Z tie order; для face basis $B$ координаты cube pixel centers равны:

\[
\mathbf q=B\mathbf r,\qquad
\mathbf p=\frac{N}{2}\left(\frac{\mathbf q_{xy}}{q_z}+1\right)-\frac12.
\]

Независимый audit задаёт шесть bases явно и проверяет known-axis/off-axis answers и inverse roundtrip. Он не вызывает production/offline converter для расчёта pixels. Четыре cube texel учитывают clamp-to-edge; mixing выполняется **в linear light**, затем encode, round-to-nearest-even и RGB8, как в зафиксированном converter contract:

\[
I_{\rm fisheye}=Q_8\left(E\left(\sum_{j=0}^{3}w_jD(I_{{\rm cube},j})\right)\right).
\]

$D/E$ — sRGB decode/encode. Это отличается от последующего server source sample, который интерполирует encoded source texture перед fusion linearization (§4.66). Для **всех256** выбранных fisheye texel actual RGB восстановлен **точно,0LSB**. Исходные кубические PNG не рендерились заново.

## Независимая геометрическая проверка

В Blender восстановлены прежние road/target/ego, transforms и frame poses, проверены generator hashes/rig/target positions. Луч через центр каждого выбранного fisheye texel снова выбирает road (kind2). Для четырёх cube texel вычислены opaque central-ray identities. Дополнительно внутри каждого cube pixel проводится фиксированная стратифицированная сетка8×8, offsets $(i+0.5)/8-0.5$, $i=0..7$, по обеим осям. Лучи проходят через площадь пикселя; grid target-hit count сохраняется отдельно от center kind.

Grid — **геометрический диагностический контроль**, а не sample locations Eevee и не reconstruction его antialias/shading kernel. Отсутствие grid hits вообще не доказывало бы отсутствие target в непрерывной площади. Наличие hits доказывает лишь найденную геометрическую поддержку.

## Результат

| Кадр | Носитель | Остаток после fisheye centers | Cube center support | Только area-grid support | Без найденного support |
|---:|---|---:|---:|---:|---:|
| 0 | plane | 28 | 27 | 1 | 0 |
| 0 | bowl | 14 | 13 | 1 | 0 |
| 0 | dome_floor | 29 | 28 | 1 | 0 |
| 0 | cylinder_floor | 28 | 27 | 1 | 0 |
| 0 | cube_floor | 28 | 27 | 1 | 0 |
| 1 | plane | 2 | 2 | 0 | 0 |
| 1 | bowl | 5 | 5 | 0 | 0 |
| 1 | dome_floor | 2 | 2 | 0 | 0 |
| 1 | cylinder_floor | 2 | 2 | 0 | 0 |
| 1 | cube_floor | 2 | 2 | 0 | 0 |

У135/140 output pixels существует положительно взвешенный cube texel, central ray которого выбирает target. У5/140 такой center support отсутствует, но внутри хотя бы одного положительно взвешенного cube texel сетка обнаруживает target. Все140 имеют положительно взвешенный chroma cube texel. На этой выбранной выборке pixels без найденного center/area support не осталось.

Последние5 — не пять независимых failures: они принадлежат frame0 пяти carriers и ссылаются на один и тот же source-camera0 footprint: fisheye texels(42,431),(42,432),(43,431),(43,432). В запросах всего47/256 имеют cube center support,69/256 — area-grid support,69/256 — cube chroma support,52/256 сами проходят исходный chroma threshold. Эти числа относятся к texel requests, а не к140 output pixels; смешивать знаменатели нельзя. Равенство69 не устанавливает причинное равенство RGB и geometry.

Результат объясняет, почему texel-center identity недостаточна для filtered RGB: исходный fisheye texel получает соседний cube RGB, а cube RGB имеет ненулевой pixel footprint. Это не доказывает точные causal color fractions или исторические Eevee sample weights, не исправляет объект в пространстве и не отменяет height/parallax failure §§4.62–4.67.

## Воспроизведение и проверки

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_cube_footprint.py \
  --output-report artifacts/cube-footprint-v1/reaudit.json
cmp docs/validation/baselines/cube_footprint_v1.json artifacts/cube-footprint-v1/reaudit.json
```

Прошли13 known-answer tests и CTest1/1, repeated read-only audit побитно совпал. Добавлены все шесть face centers/inverse, off-axis orientation known answers, tie rule, invalid ray, equidistant axis/quarter-turn controls. Требуются прежние raw artifacts и system NumPy/SciPy/Pillow/Matplotlib; Blender helper использует bpy/mathutils/NumPy, без Pillow/SciPy imports. Native code этим опытом не меняется, его performance не переизмеряется.

Requests можно независимо повторно получить из прежней выборки:

```python
import sys, json
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'tools/research'))
from cube_footprint_audit import prepare
expected=json.loads((root/'artifacts/cube-footprint-requests-v1.json').read_text())
assert prepare(root,root/'configs/research/source-footprint-plan.json')==expected
```

Для нового geometric capture скопировать план, задать новый несуществующий output_root и запустить через Blender MCP:

```python
import sys, importlib
from pathlib import Path
root=Path('/home/bogdan/prog/MAI/surround-view') # локальный путь
sys.path.insert(0,str(root/'tools/research'))
sys.path.insert(0,str(root/'tools/blender'))
import cube_footprint
importlib.reload(cube_footprint)
print(cube_footprint.capture(root,root/'artifacts/cube-footprint-repeat-plan.json'))
```

Затем передать копию плана plot/audit через `--plan`, `--output-report` и `--figures` с новыми путями. Исторический baseline не заменять новым capture автоматически.

## Дальше

Bounded support attribution прежних1164 edge mismatches завершён на заявленных уровнях:1024 neighboring fisheye-center support (§4.68),135 cube-center support и5 cube-area-grid-only support. Полная причинная реконструкция Eevee filter не выполнена и не нужна для признания ограничения central-ray labels. Следующий приоритет — независимая scene truth на новых hybrid proxy coordinates (§4.67), затем проверяемая mitigation height/occlusion failure на unchanged inputs. Новые сцены/optics, convergence area grid при отрицательных результатах, triangle coverage, GPU-only attribution и physical/Aurora испытания остаются отдельными задачами.
