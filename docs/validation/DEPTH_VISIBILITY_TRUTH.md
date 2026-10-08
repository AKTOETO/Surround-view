# Blender depth truth v1: smoke validation

Дата: 09.10.2026. Статус: успешно проверен экспорт и преобразование одного синтетического кадра; геометрическая точность depth-проекции на независимых примитивах ещё не установлена.

## Условия и воспроизведение

- Blender 5.2.2 LTS, EEVEE.
- Процедурная метрическая улица `blender_metric_street_v1`; 4 разнесённых камеры, один кадр, 30 FPS.
- 32×32 пикселя на cube face (диагностический smoke-размер, не production-quality).
- Capture экспортировал 20 EXR: 4 камеры × 5 направлений (`nz` не нужен при FOV < 180°). Каждый файл включён в `capture.json` SHA-256 manifest.
- Конвертер создал 4 radial-range карты float32, включённые в `ground_truth.json` с checksums.

```sh
blender --background --python tools/blender/scene.py -- \
  --output artifacts/depth-capture --frames 1 --face-size 32 --depth-truth
python3 tools/blender/convert.py \
  --capture artifacts/depth-capture --output artifacts/depth-dataset
python3 tests/test_blender_fixture.py
```

Фактически проверенный источник: `artifacts/blender-depth-truth-probe`; воспроизводимая команда выше позволяет создать новый capture, так как диагностические артефакты не входят в Git.

## Измерения на полученных картах

| Камера | Валидные лучи | Медиана, м | p95, м | Максимум, м |
|---:|---:|---:|---:|---:|
| 0 | 52.44% | 2.242 | 16.303 | 30.500 |
| 1 | 53.22% | 3.012 | 17.288 | 36.733 |
| 2 | 52.24% | 2.229 | 16.233 | 34.865 |
| 3 | 53.21% | 3.005 | 17.355 | 37.050 |

Покрытие ограничено FOV и геометрией Blender-сцены; NaN означает луч без поверхности, а не ошибку конвертера. Проверка также обнаружила и устранила граничный дефект: при нулевом bilinear-весе валидного texel `argmax(weights * valid)` мог выбрать invalid sentinel. Отдельный regression test воспроизводит эту ситуацию. Содержимое карт проверено после повторной конвертации: нет значений выше 300 м и нет sentinel около `1e10`.

## Вывод и ограничения

Экспортная цепочка EXR → radial range работает на тестовой сцене, хранит происхождение и проверяемые hashes; это делает доступной независимую геометрическую глубину для последующего seam/ghosting анализа. Smoke-тест **не подтверждает точность**: нужны контрольные плоскость/сфера/куб на известных расстояниях, проекция rays на аналитические решения, convergence по face size, отдельный scene-visibility/object-ID pass, а затем сравнение carrier/fusion. Цветовой replay по-прежнему не использует depth truth для изменения рендера.

Связано: [[engineering/BLENDER]], [[research/PROJECTION_AND_STITCHING]], [[validation/ANALYTIC_REFERENCE]].
