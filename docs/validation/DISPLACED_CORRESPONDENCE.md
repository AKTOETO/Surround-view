# Displaced target: source-surface attribution

Дата:12.10.2026. Exploratory follow-up [[validation/SOURCE_CORRESPONDENCE_CONTROLS]], диплом §4.66. Case `rig101-near-z080`, frames0/1, true calibration, пять carriers,800×800 inputs/640×360 output, unchanged edge_feather/color. План `configs/research/displaced-correspondence-plan.json`, helper и начальный collector зафиксированы `193155a` до Blender casts. Baseline `docs/validation/baselines/displaced_correspondence_v1.json`; raw root `artifacts/displaced-correspondence-v1`.

## Выборка и независимость

Исследуется raw magenta proxy `min(R,B)-G>0.15` на обычном native output, без component filtering. Это **output-conditioned выборка**, поэтому она не оценивает recall, не является independent ghost segmentation и не заменяет прежнюю quality IoU. Displaced означает отсутствие в соответствующей independent direct target mask. Из исходных10 native captures повторно использованы actual UV/status, carrier XYZ, ordinary RGBA и inputs; нового native rendering/RGB capture не было. Binary hash сохранён и проверен: `d9f3349d5a03dfbace41615ac07b609d37bfacd2a4d94def0e79b008bf59f115`.

Blender реконструирует ту же scene recipe/target/road/rig, проверяет generator/input/report hashes, ego pose, target position и calibration. Для каждого выбранного output pixel кастуются virtual nearest hit и фактические native source rays. World/vehicle coordinates преобразуются явно. Active scene восстанавливается. Ray identities — target/road/ego/other/no-hit — не вычисляются из RGB proxy.

Всего52720 выбранных pixels,105440 valid source pairs. Pixels/cameras/frames/carriers коррелированы; это не52720 независимых испытаний. Во всех10 условиях **весь** proxy displaced и virtual nearest surface — road. Иначе говоря, в ожидаемой target mask нет ни одного proxy pixel, но на дороге есть растянутое цветовое изображение объекта.

## Измеренный механизм

| Frame | Carriers | Proxy / displaced pixels | Target point-ray pixels | RGB proxy без target ray | Target pairs: camera0 / camera3 |
|---|---|---:|---:|---:|---:|
| 0 | plane/dome/cylinder/cube, каждый | 4843 / 4843 | 4651 | 192 | 619 / 4032 |
| 0 | bowl | 6261 / 6261 | 6130 | 131 | 1778 / 4352 |
| 1 | plane/dome/cylinder/cube, каждый | 5373 / 5373 | 5322 | 51 | 0 / 5322 |
| 1 | bowl | 5595 / 5595 | 5534 | 61 | 0 / 5534 |

Всего51556 target source-ray hits; каждый находится **перед native carrier point**, то есть foreground object перекрывает видимость точки, которую сервер использует для sampling. Gap вдоль camera-to-carrier ray≥2.017m; max поперечный residual source hit относительно этого луча≤1.005e−6m. Это проверка по фактическим coordinates, не схематический луч через box center.

В frame0 target samples приходят от camera0 и camera3; во frame1 — отcamera3. На каждом pixel два native samples допустимы. При target ray его normalized edge weight равен0.5; второй sample видит road. Допустимость source projection проверяет angle/image support, но не scene visibility carrier point. Поэтому смешивание может перенести foreground texture на floor carrier, даже если его virtual ray видит дорогу.

Для plane/dome/cylinder/cube matched-to-virtual-surface weight mass имеет median0.5, а у bowl0: другой road sample тоже не совпадает с direct virtual road point в пределах2cm из-за carrier shape. Same object ID road сам по себе не гарантирует совпадения surface point. Этот ROI результат не является универсальным ranking bowl versus enclosures.

Ранее §4.62 показал отсутствие target в ожидаемом положении: virtual ray должен встретить target, но carrier projection выбирает road. Новый опыт показывает обратную сторону: virtual ray должен видеть road, а native source ray блокируется target перед carrier. Эти два механизма совместно объясняют большое смещение в исследуемом edge-feather fixture; seam reweighting не восстанавливает автоматически правильную 3D surface.

## Фильтрация, RGB и несовпадения

1164 выбранных pixels суммарно имеют magenta RGB, но ни один subpixel source ray не пересекает target (192/131/51/61 на carrier/frame groups в таблице). Они **не отброшены** и не объявлены однозначно false positives. Point rays не равны bilinear/antialiased samples. Причина этих mismatch pixels требует проверки source four-texel footprints и прежнего cubemap→fisheye/antialias pipeline; данный опыт не приписывает все mismatch только bilinear filtering.

Отдельно сохранённые исходные RGB samples реконструированы по actual native UV: clamped bilinear interpolation **до sRGB linearization**, затем normalized edge weights и sRGB encode. На proxy ROI max absolute channel residual против ordinary native output0.783889 RGB8 LSB во всех10 условиях; median0.156–0.177 LSB. Это photometric consistency, не exact bit parity и не доказательство surface identity. Source frames уже содержат результаты synthetic camera resampling/AA.

Native configs этого fixture не имеют явного `fusion` блока: проверен default edge_feather/color, edge_width24px из `include/sv/config.hpp`; hash определения записан в baseline. Collector исправлен после capture для разрешения этого native default; это не изменение input/config/shader или предварительно заявленного chroma threshold. Occlusion-gap decomposition и RGB reconstruction уточнены при аудите (`174736d`). Не выдавать их за preregistered hypothesis.

## Воспроизведение

Требуются прежние `parallax-sensor-*` и `source-correspondence-controls-v1` raw inputs/native outputs, генераторы Blender с frozen hashes, NumPy/SciPy/Pillow/Matplotlib. Read-only audit:

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_displaced_correspondence.py \
  --output-report artifacts/displaced-correspondence-v1/reaudit.json
cmp docs/validation/baselines/displaced_correspondence_v1.json \
  artifacts/displaced-correspondence-v1/reaudit.json
```

Восемь known-answer controls и CTest1/1 прошли; новый test проверяет integer/fractional/border bilinear samples, magenta threshold и nonfinite input rejection. Два read-only audits совпали побитно. Графики создаёт `docs/diploma/plot_displaced_correspondence.py` рядом с главой; actual ray illustration выбирает median index среди sorted proxy pixels с target source hit, pixel(297,332), camera3, frame0 plane. Это illustration, не независимая выборка для статистики.

Новый Blender capture: скопируйте план, измените output_root и передайте путь новой копии (абсолютный root заменить на свою копию). Старый raw/baseline не перезаписывать:

```python
import sys
sys.path.insert(0, '/home/bogdan/prog/MAI/surround-view/tools/blender')
import displaced_correspondence
print(displaced_correspondence.capture(
    '/home/bogdan/prog/MAI/surround-view',
    '/home/bogdan/prog/MAI/surround-view/artifacts/displaced-repeat-plan.json'))
```

Audit новой серии: `python3 docs/diploma/plot_displaced_correspondence.py --plan artifacts/displaced-repeat-plan.json --output-report artifacts/displaced-repeat/summary.json --figures artifacts/displaced-repeat/figures`. Raw содержит50 NPY arrays и scene-truth с report/file/helper hashes. Необходимость Python ограничена независимым oracle/контрольными метриками/рисунками; product Renderer остаётся C++.

## Следующий этап

- Matched hybrid capture **тех же**640×360/800×800 inputs/config/frames, zero/normalized multiband и seam variants; проследить per-level signed contributions. Старые `native-fusion-raster-real-v2` относятся к320×180,400×400 nominal cameras и другой сцене/pose — их нельзя соединять с текущими rays как парный опыт.
- Проверить four-texel surface footprint и source resampling для1164 point-ray/RGB mismatch pixels; расширить выборку вне output-conditioned proxy и на natural objects.
- Проверить mitigation height/occlusion failure на неизменных inputs и independent truth. Подавление occluded samples само по себе не восстанавливает target в правильном положении.
- Triangle precision/coverage, bounded server/library diagnostic delivery, независимые сцены и физический/Aurora стенд остаются открытыми.

Matched hybrid640×360/800×800 follow-up выполнен: [[validation/HYBRID_CORRESPONDENCE]]. Он переиспользует ray identities только после exact source/carrier parity на прежних domains; новые hybrid proxy pixels и four-texel mismatch truth ещё требуют отдельного capture.

Four-texel follow-up выполнен: [[validation/SOURCE_FOOTPRINT]], §4.68. Все1164 mismatch pixels сохранены:1024 имеют положительный neighboring target texel,140 остаются без target-center support; actual upstream resampling attribution ещё открыт.
