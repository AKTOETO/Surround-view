# Paired street v1

Три синхронных кадра четырёх камер, прямые RGB виды виртуальной камеры и uint16
object-ID карты одной сцены `SV Research Street`, Blender 5.2.2 LTS / EEVEE.
Захвачено через MCP 10.10.2026 скриптом `tools/blender/paired_truth.py`.
Положение автомобиля: x=0, 0.4, 0.8 м; время: 0, 200000000, 400000000 нс.
Это движение со скоростью 2 м/с, редкое семплирование исходной 30-Hz шкалы (5 Hz).

- `camera*_*.png`: lossless PNG-переупаковка PPM после cubemap→fisheye conversion;
  разрешение 400×400, исходные perspective faces только 64×64. Увеличение разрешения
  не добавляет детализацию; это fixture методики, а не качественная фото/видеосерия.
- `virtual_*.png`: прямой рендер 320×180 из той же сцены и позы.
- `objects_*.npy`: ближайший mesh ID по `Scene.ray_cast` в центре каждого пикселя;
  ноль — фон. Карты не учитывают прозрачность и сглаживание. Оценка исключает
  кузов, фон и полосу вокруг границ объектов.
- `manifest.json`: актуальные пути/хэши PNG входов, camera calibration IDs и timestamps.
- `ground_truth.json`: poses и provenance конвертера оригинального захвата.
- `capture.json`: оригинальный индекс raw cubemap capture; перечисленные cube PNG и
  `street.blend` сюда не включены. Его хэш связывает inputs и paired truth.
- `paired_truth.json`: хэши direct RGB/object IDs, конфигурация, ID кузова и проверка
  проекции (максимальная ошибка 0.000114 px).

Исходники мира находятся в `assets/scenes/metric-street`; raw capture и snapshot
данного прогона — в игнорируемом `artifacts/paired-street-v3`. Этот компактный fixture
(~1.4 MB) позволяет повторить численные результаты без Blender; его нельзя выдавать
за независимые scenes/seeds или реальные камеры. Авторская процедурная сцена;
внешние изображения в этом fixture не использованы. Протокол, результаты и команды:
`docs/validation/PAIRED_STITCH_TEMPORAL.md`.
