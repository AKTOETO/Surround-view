# Source-surface correspondence: высота и второй кадр

Дата: 11.10.2026. Exploratory follow-up к [[validation/SOURCE_CORRESPONDENCE]]: два прежних case одного seed101, два коррелированных кадра, пять carriers, true calibration, edge_feather/color. **20 native captures**, не 20 независимых повторений. Замороженный план: `configs/research/source-correspondence-controls-plan.json`; baseline: `docs/validation/baselines/source_correspondence_controls_v1.json`; raw root: `artifacts/source-correspondence-controls-v1`.

## Условия и измерение

Сопоставлены `rig101-near-z020` и `rig101-near-z080`: box высотой0.4m с центром соответственно0.2 и0.8m. Первый касается дороги, но не является плоской мишенью. Frames0/1 имеют timestamps0/200000000ns; автомобиль смещается по X на0.4m без поворота. Сохранены прежние fisheye800×800, output640×360, configs и RGB. Повторного Blender RGB capture нет: независимые scene rays проверяют прежние входы и truth.

План и ray helper зафиксированы в `13c2a5f` до измерений. Probe поддерживает необязательный `FRAME_INDEX` (default0), выбирает соответствующие пути и timestamp из manifest; индекс проверяется до EGL и создания output. Код этого расширения находится в `37377d1`. Сборка содержит configure-time source_revision `ad95005f84e7bf1b1fa74c2a1b5c061430be803f` и source_fingerprint `02b340af9f83642629e65bd7271bbfcbe689bbfce707485284e421c397a680fb`: revision **не описывает все изменения probe** на момент сборки. Точный сохранённый binary SHA256 `d9f3349d5a03dfbace41615ac07b609d37bfacd2a4d94def0e79b008bf59f115`, probe source hash и invocation log входят в baseline. Collector/plot/coordinate test зафиксированы `7d6a305` перед финальным read-only audit.

Независимый Blender nearest-hit oracle создаёт отдельную сцену и восстанавливает ранее активную. Direct mask совпал побитно:166/161 pixels для z020 и178/172 для z080. Visibility bits совпали на всех target pixels. Все20 ordinary RGBA hashes совпали с **соответствующим frame_baseline** прежнего серверного отчёта. Probe также проверяет неизменность RGBA до/во время/после inspection.

Direct points и source hits Blender заданы в world coordinates; native carrier points и calibration — в vehicle coordinates. Для optical projection используется обратная ego pose, для surface distances — прямая. Тест с известными rotation90° и translation проверяет оба направления. Совпадение surface определяется как target identity и расстояние nearest hit до direct point≤0.02m; просто одинаковая object identity недостаточна.

## Результат

В таблице ROI — output pixels, видимые хотя бы одной исходной камерой. Source pairs = pixels×4; valid camera не обязательно та, которая видит правильную target surface. UV gap ниже рассчитан только для пар с native validity **и** direct visibility этой камеры. Invalid pairs не включаются в distance/match выводы.

| Case / frame | Carriers | Pixels / pairs | Invalid | Valid → road | Same target, wrong point | Matched surface | RGB proxy в ROI |
|---|---|---:|---:|---:|---:|---:|---:|
| z020 / 0 | plane, dome, cylinder, cube | 76 / 304 | 210 | 78 | 16 | 0 | 11 |
| z020 / 0 | bowl | 76 / 304 | 210 | 76 | 18 | 0 | 14 |
| z020 / 1 | plane, dome, cylinder, cube | 73 / 292 | 219 | 66 | 7 | 0 | 8 |
| z020 / 1 | bowl | 73 / 292 | 219 | 65 | 8 | 0 | 9 |
| z080 / 0 | все пять | 76 / 304 | 228 | 76 | 0 | 0 | 0 |
| z080 / 1 | все пять | 69 / 276 | 207 | 69 | 0 | 0 | 0 |

RGB proxy — raw magenta threshold без component filtering, не новая quality IoU. Поднятый объект по-прежнему отсутствует в expected ROI: camera3 выбирает road и на втором кадре. У ground-contact box некоторые лучи попадают в сам target, но в другую surface point; цветовое совпадение в11/14 либо8/9 pixels не устанавливает геометрического соответствия. Zero matched surface сохраняется также на visible interior после двух cross erosions:46/43 pixels для z020,48/43 для z080.

| Case / frame | Plane median UV gap, px | Bowl median UV gap, px |
|---|---:|---:|
| z020 / 0 | 66.937 | 66.515 |
| z020 / 1 | 76.249 | 75.442 |
| z080 / 0 | 203.997 | 203.819 |
| z080 / 1 | 246.327 | 246.141 |

Независимая double projection actual carrier points отличается от native UV менее0.00009 source pixel во всех20 условиях. При этом ground-contact UV gap остаётся значительно больше; контроль подтверждает влияние высоты в этой постановке, но не задаёт универсальную зависимость ошибки от высоты. Формы enclosure не устраняют неверное floor correspondence здесь. Нет holdout, поворотов, других seeds, planar target либо сравнения fusion algorithms. Нулевые match counts не доказывают невозможности работы planar warp на действительно плоской поверхности.

Графики и реальные RGB фрагменты: диплом §4.63, `source_controls_metrics.png`, `source_controls_views.png`; скрипт `docs/diploma/plot_source_controls.py`. Blender после опыта снова имеет исходную активную Scene (Cube, Camera, Light), что проверено через MCP.

## Воспроизведение

Требуются сохранённые `parallax-sensor-*` inputs/reports и generator hashes из frozen plan, EGL/GLES, Blender MCP либо Python console, NumPy/SciPy/Matplotlib/Pillow. Только read-only audit текущего raw archive:

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_source_controls.py \
  --output-report artifacts/source-correspondence-controls-v1/reaudit.json
cmp docs/validation/baselines/source_correspondence_controls_v1.json \
  artifacts/source-correspondence-controls-v1/reaudit.json
```

Четыре known-answer tests и CTest1/1 прошли; два read-only audit совпали побитно. Tracked baseline содержит hashes и измерения, но не заменяет raw archive. Новый build может дать иные binary/source hashes. Не перезаписывайте исходный план/baseline: скопируйте план, измените output_root и используйте новый каталог. Native capture из корня проекта после `cmake --build build --target sv-correspondence-probe -j4` (путь плана в примере — **новая копия**):

```python
from pathlib import Path
import hashlib, json, os, shutil, subprocess
root = Path.cwd()
plan_path = root/'artifacts/source-controls-repeat-plan.json'
plan = json.loads(plan_path.read_text())
output = root/plan['output_root']
output.mkdir(exist_ok=False)
binary = root/'build/sv-correspondence-probe'
shutil.copy2(binary, output/binary.name)
record = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
          'plan_sha256': hashlib.sha256(plan_path.read_bytes()).hexdigest(), 'cases': []}
for case in plan['cases']:
    for frame in plan['frames']:
        for carrier in plan['carriers']:
            argv = [str(binary), str(root/case['native_reference_root']/carrier/'config.json'),
                    str(root/case['input_root']/'seed101-inputs/manifest.json'),
                    str(output/case['id']/f'frame{frame}'/'native'/carrier), str(frame)]
            result = subprocess.run(argv, check=True, timeout=60, env=os.environ)
            record['cases'].append({'case': case['id'], 'frame': frame, 'carrier': carrier,
                                    'argv': argv, 'returncode': result.returncode})
            (output/'invocations.json').write_text(json.dumps(record, indent=2)+'\n')
```

Выберите рабочий EGL backend через environment перед capture; опубликованная серия использовала NVIDIA surfaceless. Затем в Blender (абсолютный root заменить на свою копию):

```python
import sys
sys.path.insert(0, '/home/bogdan/prog/MAI/surround-view/tools/blender')
import source_surface_controls
source_surface_controls.capture(
    '/home/bogdan/prog/MAI/surround-view',
    '/home/bogdan/prog/MAI/surround-view/artifacts/source-controls-repeat-plan.json')
```

Audit новой серии: `python3 docs/diploma/plot_source_controls.py --plan artifacts/source-controls-repeat-plan.json --output-report artifacts/source-controls-repeat/summary.json --figures artifacts/source-controls-repeat/figures`. Это research orchestration, не Python реализация продуктового renderer. Native diagnostic delivery через сервер/library пока отсутствует.

## Следующий проверяемый шаг

Сначала добавить действительно planar target known-answer control, затем attribution для displaced RGB pixels и hybrid fusion на тех же rays, включая bilinear footprint и level contributions. После локализации проверить mitigation при неизменных входах; расширять optical-height/mount/scene families отдельно. Triangle identity/coverage, bounded typed diagnostics, физические камеры и Аврора остаются открытыми.
