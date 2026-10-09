# Cube-face resolution / source-visibility fixture

Авторский процедурный мир `SV Research Street`, захвачен в Blender 5.2.2 LTS / EEVEE
10.10.2026. Два условия: исходные кубические грани 64×64 и 256×256, итоговые fisheye
в обоих случаях 400×400. Три позы: x=0/0.4/0.8 м; время 0/0.2/0.4 с. Прямой вид
виртуальной камеры — 320×180. Это один короткий синтетический клип, не независимые
сцены или повторы. Без внешних художественных ассетов, sensor noise и динамических объектов.

Каталоги `64/` и `256/` имеют одинаковую структуру:

- `camera*_*.png`, `manifest.json`: lossless camera inputs, calibration IDs, timestamps,
  SHA-256; создаются `tools/blender/convert.py --image-format png`.
- `config.json`, `ground_truth.json`: конфигурация, позы, происхождение capture/converter.
- `capture.json`: индекс оригинального raw cube capture и хэши. Raw cubes/snapshot
  не включены; generated captures находятся в `artifacts/paired-resolution-{64,256}-v2`.
- `virtual_*.png`, `objects_*.npy`: direct RGB и uint16 nearest-rendered-mesh IDs.
- `visibility_*.npy`: uint8 bit i означает видимость точки прямого вида из камеры i.
  Требуются попадание в fisheye FOV/image и совпадение первого opaque ray hit с точкой
  в пределах 0.02 м. Hidden-render helper meshes исключены из ray casts.
- `paired_truth.json` schema 2: хэши truth, source-visibility encoding/tolerance, список
  исключённых helpers, проверка проекции, хэши capture scripts.

Размер двух условий вместе ~3.45 MB. Decoded direct RGB pixels и object/visibility arrays
в двух условиях побитно совпадают. PNG metadata/encoded hashes могут отличаться.
Проверка `tools/research/stitch_resolution.py` отвергает неподходящие пары.
Object IDs и visibility не учитывают прозрачность/сглаживание. Это видимость **точки
сцены**, а не поверхности carrier; она не гарантирует, что fusion семплирует ту же точку.
Параметры мира/исходный `.blend` находятся в `assets/scenes/metric-street`.
Численный опыт повторяется без Blender. Протокол/команды:
`docs/validation/STITCH_RESOLUTION_VISIBILITY.md`.

## Convergence extension 512

Каталог `512/` добавлен 10.10.2026 как контроль 256→512. Та же структура и decoded direct RGB/object/visibility truth, дополнительный размер ~1.9 MB. Команда: `python3 tools/research/stitch_resolution.py --sizes 256 512 --output artifacts/convergence-repeat`. Результат — `docs/validation/STITCH_CONVERGENCE_ROBUSTNESS.md`.
