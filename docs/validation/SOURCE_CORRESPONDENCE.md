# Native source-to-scene correspondence: первый локализующий опыт

Дата: 11.10.2026. Scope: один ранее изученный case `rig101-near-z080`, первый кадр (0 ns), true calibration, 800×800 source fisheye, 640×360 output, пять носителей и обычный `edge_feather/color`. Это exploratory localization, не holdout и не рейтинг алгоритмов.

План `configs/research/source-correspondence-plan.json` фиксирует inputs/config hashes, frame, carriers, ROI и tolerance 0.02 m. C++ probe и план зафиксированы `d654301`, начальная независимая ray procedure — `ad95005` до измерений. Дополнительные проверки JSON roundtrip, visibility и hash binding добавлены при аудите; они не выдаются за предварительно зарегистрированную quality hypothesis.

## Измерительная цепочка

1. `sv-correspondence-probe` использует настоящий C++ `Renderer`, четыре сохранённых входных кадра и прежние server configs. Экспортирует ordinary RGBA, depth-tested carrier points/regions и shader source pixel/status. Проверяет одинаковый RGBA до, во время и после inspection.
2. Blender MCP создаёт отдельную сцену по сохранённому recipe с прежним seed, ошибками монтажа, target box и flat-road override. Исходная открытая сцена восстанавливается. Historical generator hashes и camera extrinsics проверяются.
3. Независимые virtual-camera rays восстанавливают весь direct target mask: он побитно совпадает с прежним (178 pixels). Camera visibility bits повторно проверены и совпадают на всех 178 pixels. Истинные точки — nearest opaque scene hits, не box center и не analytical carrier intersection.
4. Через сохранённый subpixel source UV каждого допустимого native sample строится независимый source ray Blender. Записываются nearest scene hit и тип поверхности. Source inverse намеренно ограничен equidistant zero-distortion/zero-skew optics этого fixture.
5. Read-only collector проверяет binary/input/native-product/scene-truth hashes, порядок пикселей, manifests и ordinary RGBA hashes. Последние совпали с archived server baseline hashes для всех пяти носителей. Это сравнение с сохранёнными baseline hashes, не повторная проверка наличия старых baseline RGBA files.

## Результаты

Каждая строка ниже справедлива отдельно для каждого из пяти носителей. Source pairs — число output pixels × четыре камеры; это коррелированные наблюдения одного кадра.

| ROI | Output pixels | Source pairs | Invalid projection | Valid ray → road | Valid ray → target | Target RGB pixels в ROI |
|---|---:|---:|---:|---:|---:|---:|
| Весь independent target | 178 | 712 | 534 | 178 | 0 | 0 |
| Видимый хотя бы одной source camera | 76 | 304 | 228 | 76 | 0 | 0 |
| Interior после двух default cross erosions | 102 | 408 | 306 | 102 | 0 | 0 |
| Visible interior | 48 | 192 | 144 | 48 | 0 | 0 |

Во всех 178 pixels допустима только камера3. Камера0 имеет151 behind-camera и27 outside-angle отказов; камеры1/2 — по178 behind-camera. Её edge weight равен1 на всех178 точках. Таким образом, для этого `edge_feather` ROI нет локального конкурирующего веса, который мог бы восстановить потерянную surface identity. Геометрическое соответствие уже нарушено до смешивания.

| Carrier | Carrier Z в ROI, m | Carrier → direct scene distance, m (min / median / max) | True-visible UV → native UV, px (min / median / max) | Max native / independent projection difference, px |
|---|---|---|---|---:|
| Plane | 0 | 0.8336 / 1.3481 / 1.4939 | 171.980 / 203.997 / 240.195 | 0.0000808 |
| Bowl | 0.000132–0.003856 | 0.8283 / 1.3451 / 1.4935 | 171.597 / 203.819 / 240.134 | 0.0000842 |
| Dome-floor | 0 | 0.8336 / 1.3481 / 1.4938 | 171.971 / 203.991 / 240.179 | 0.0000700 |
| Cylinder-floor | 0 | 0.8336 / 1.3481 / 1.4937 | 171.964 / 203.977 / 240.168 | 0.0000898 |
| Cube-floor | 0 | 0.8335 / 1.3480 / 1.4938 | 171.966 / 203.982 / 240.187 | 0.0000814 |

UV displacement рассчитан по76 pixels с direct source visibility и допустимой native projection. Native/double projection residual относится к178 допустимым samples каждого carrier, не всему изображению и не универсальной точности драйвера.

Target box расположен на Z=0.6–1.0 m. Virtual ray встречает его раньше носителя; carrier выбирает floor либо низкий участок bowl, который геометрически находится за объектом на этом луче. Source projection этого другого 3D point корректно попадает в дорогу. Переход от стенки dome к cylinder/cube не изменяет выбранный floor в данном ROI.

Объект присутствует в других местах server output: raw magenta proxy даёт4843 pixels для plane/dome/cylinder/cube и6261 для bowl; proxy намеренно без component filtering, поэтому не равен прежней quality metric. Иллюстрация показывает сильное растяжение и смещение. Анализ соответствия всех этих displaced pixels ещё не выполнен. Это объяснение отсутствия объекта в expected ROI одного edge-feather кадра, а не доказательство причин всех исторических ошибок multiband или natural-object ghosting.

## Проверки и воспроизведение

Baseline: `docs/validation/baselines/source_correspondence_v1.json`. Raw root: `artifacts/source-correspondence-v1`; содержит сохранённый binary, invocation log, пять native captures, Blender hit arrays, summary и повторный read-only audit. Повторный audit на зафиксированной версии исходников побитно совпал с baseline. Позднейшее расширение probe меняет его source hash: для побитного исторического audit нужна сохранённая версия исходников; новый controls baseline оформлен отдельно. Compiled binary metadata: source_revision `684083db0b86a9c17c77de0103d11eab371a4478`, source_fingerprint `fdad9903f4bb27ca0fc9ece04394a95e107a75adb9c37e0ca43932ff02bab61f`. Probe был скомпилирован до коммита d654301; точный binary hash и probe-source hash сохранены в baseline. Git revision сам по себе не описывает незакоммиченный на момент сборки probe; новая сборка может иметь другие metadata/hashes.

Новая CTest группа `source_correspondence` содержит3 known-answer controls: equidistant projection по оси/углу, различение invalid/no-hit/wrong-object/wrong-point/match, проверка little-endian payload/checksum/size. Три проверки и CTest1/1 прошли.

Первый preflight остановился на строгом сравнении JSON matrices: сериализация C++ отличалась от исходных doubles максимум на1.110223e−16. После проверки неизменности исходного config hash разрешён только matrix roundtrip tolerance1e−12; прочие camera fields сравниваются точно. Это не допуск геометрической ошибки калибровки. Visibility validation и report hash binding добавлены затем; повторные Blender casts сохраняли прежние hit/mask arrays побитно. Оптическая модель и данные не менялись.

Исторические `plot_source_correspondence.py`/`cmp` ниже требуют версии исходников первого опыта (probe `d654301`, collector `8f5c2d4`): текущий probe имеет другой source hash. Для текущей версии используйте отдельный controls audit по ссылке выше.

```sh
cmake --build build --target sv-correspondence-probe -j4
EGL_PLATFORM=surfaceless build/sv-correspondence-probe \
  artifacts/carrier-inspection-v1/native/seed101/plane/config.json \
  artifacts/parallax-sensor-inputs-v1/rig101-near-z080/seed101-inputs/manifest.json \
  /tmp/sv-correspondence-plane
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_correspondence.py \
  --output-report artifacts/source-correspondence-v1/reaudit.json
cmp docs/validation/baselines/source_correspondence_v1.json artifacts/source-correspondence-v1/reaudit.json
```

Для повторения полного capture создайте копию плана с новым output_root и сохраните binary/hash/invocations. Пример тестовой orchestration (выполняется из корня проекта; новый каталог должен отсутствовать):

```python
from pathlib import Path
import hashlib, json, shutil, subprocess
root = Path.cwd()
plan = json.loads((root/'configs/research/source-correspondence-plan.json').read_text())
plan['output_root'] = 'artifacts/source-correspondence-repeat'
plan_path = root/'artifacts/source-correspondence-repeat-plan.json'
plan_path.write_text(json.dumps(plan, indent=2)+'\n')
output = root/plan['output_root']
output.mkdir(exist_ok=False)
binary = root/'build/sv-correspondence-probe'
shutil.copy2(binary, output/binary.name)
invocations = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'cases': []}
for carrier in plan['carriers']:
    argv = [str(binary), str(root/plan['native_reference_root']/carrier/'config.json'),
            str(root/plan['input_root']/'seed101-inputs/manifest.json'),
            str(output/'native'/carrier)]
    subprocess.run(argv, check=True, timeout=60)
    invocations['cases'].append({'carrier': carrier, 'argv': argv, 'returncode': 0})
(output/'invocations.json').write_text(json.dumps(invocations, indent=2)+'\n')
```

Затем в Blender Python console/MCP передайте путь к этой копии плана; read-only audit запускается с `--plan artifacts/source-correspondence-repeat-plan.json` и новым output report. Ниже показан вызов для исходного архивного плана; при повторении замените последний аргумент:

```python
import sys
sys.path.insert(0, '/home/bogdan/prog/MAI/surround-view/tools/blender')
import source_surface_truth
source_surface_truth.capture(
    '/home/bogdan/prog/MAI/surround-view',
    '/home/bogdan/prog/MAI/surround-view/configs/research/source-correspondence-plan.json')
```

Команда probe пишет scratch export в /tmp; для повторения с новой сборкой используйте отдельную копию плана с новым output_root и новый каталог, сохранив исторический baseline. Его hashes ожидаемо изменятся при другом binary. Предпосылки — прежние generated Blender inputs, доступ к GLES, NumPy/SciPy/Matplotlib/Pillow для аудита/графиков. Python выполняет независимые ray truth/метрики/рисунки; продуктовая проекция и рендеринг остаются в C++.

## Открыто

- Height/second-frame controls выполнены отдельно: [[validation/SOURCE_CORRESPONDENCE_CONTROLS]]. Открыты настоящий planar target, повороты, другие views/mounts и независимые сцены; пока нет generalization.
- Attribution для displaced target pixels, pixel filtering и source surface boundaries; ray identity не равна bilinear RGB identity.
- Hybrid multiband/seam contributions на тех же rays; текущий опыт только edge_feather.
- Triangle ID/coverage, bounded server/library diagnostic delivery и отдельный capture overhead.
- Проверяемая стратегия уменьшения height/parallax failure; смена enclosure сама по себе не решает floor correspondence.
- Physical data/Aurora.
