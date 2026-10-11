# E-STITCH-parallax-sampling-01: парный контроль cubemap resolution

Протокол фиксируется до захвата и просмотра новых quality outputs, 11.10.2026.

Вопрос: сохраняются ли ошибки положения coded target из [[validation/PARALLAX_FRAMED]] при увеличении разрешения исходных cubemap? Меняется **только** `capture.face_size`: 256 → 512. Fisheye inputs остаются 400×400, выход 320×180. Это контроль одного этапа sampling, а не доказательство сходимости всей системы или отдельное измерение геометрической ошибки.

## Дизайн и принятие данных

- Низкое разрешение: `configs/research/parallax-framed-plan.json`, `assets/scenarios/parallax-framed-v1.json`, `artifacts/parallax-framed-inputs-v2`, `artifacts/parallax-framed-v2`, baseline `docs/validation/baselines/parallax_framed_v1.json`.
- Новое разрешение: `configs/research/parallax-sampling-plan.json`, `assets/scenarios/parallax-sampling-v1.json`, новые каталоги `artifacts/parallax-sampling-inputs-v1` и `artifacts/parallax-sampling-v1`.
- Все 12 прежних case IDs: два mount seeds 101/104 × near/far × высоты центра .2/.8/1.4 м. Две прежние позы, пять medium carriers и четыре fusion/boundary profiles; 60 свежих серверов, 480 quality conditions. Выбор удачных отдельных строк запрещён.
- Сохраняются сцена, материал и размер box, poses, actual extrinsics/intrinsics, виртуальная камера, mesh budgets, sequence/warmup/repeats и бинарники. Используется прежний неизменённый Blender driver `tools/blender/parallax_study.py`.
- Перед сравнением проверить hashes и полные audit обеих серий; baseline должен воспроизводиться, кроме явно разрешённых analyzer/metric code digests. Сравнить asset plans после удаления только `capture.face_size`, runtime plans после удаления experiment/inputs_plan/protocol. Полные replay configs, stamps, poses, decoded direct RGB, object-ID и source visibility должны совпасть побитно. Проверить реальные размеры всех cube faces и неизменность camera input размеров. Несовпадение контроля блокирует парное заключение.
- Требовать одинаковые product source fingerprint и фактические backend/vendor/renderer из native catalogs; переменная `LIBGL_ALWAYS_SOFTWARE=1` не считается доказательством Mesa. Ожидаемый desktop device: NVIDIA RTX 5070 Ti, CPU fusion + GLES raster. Это не Аврора и не embedded performance comparison.
- Новый захват и native trials запускать последовательно; во время native trials не запускать Blender, сборку, тесты или figures.

## Анализ, включая отрицательный результат

Ключ строки: `(case, carrier, truth_index, mode, boundary)`. Вывести все 480 пар: full IoU, conditional source-visible IoU, predicted target pixels, centroid distance и linear RGB MAE; показать signed deltas. Undefined conditional scores и отсутствующие centroids сохранять как `null`, отдельно считать смену defined/undefined. Не считать это независимыми 480 репликами: poses, carriers и алгоритмы зависимы внутри одной сцены.

Первичный показатель: full IoU поднятых targets (320 пар), отдельно ground controls (160). Дополнительно доля нулевых IoU, conditional undefined и centroid displacement, графики по height/range/rig с указанием pooling. Подтверждение: остаётся ли полный нулевой overlap во всех поднятых условиях? Даже при подтверждении вывод ограничен cubemap 256→512 на фиксированных fisheye/output resolutions. Следующими отдельными опытами остаются sensor/output resolution, source correspondence/triangle-hit, optical-height/baseline sweep и независимые натуральные объекты. Вывод о предпочтительном carrier или universal stitching algorithm из этой серии не допускается.

## Воспроизведение

В Blender вызвать `capture_study` из `tools/blender/parallax_study.py` с новым asset plan и новым inputs root. Затем:

```sh
python3 tools/research/parallax_height.py --plan configs/research/parallax-sampling-plan.json --root artifacts/parallax-sampling-v1 --inputs artifacts/parallax-sampling-inputs-v1 --run-binaries build --output artifacts/parallax-sampling-v1/summary.json
```

Парный audit должен дополнительно проверить прежний baseline и входы; отдельный high-resolution summary сам по себе не является сравнением.
