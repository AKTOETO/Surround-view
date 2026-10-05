# Проверка Blender → fisheye replay → сводный обзор

Дата: 06.10.2026. Короткий интеграционный опыт; **не** серия измерения FPS, качества швов или целевой приёмки Авроры. Команды и повторение — [[engineering/BLENDER]].

## Условия

| Параметр | Значение |
|---|---|
| Blender / MCP | 5.2.2 LTS / add-on 1.8, protocol 13 |
| Source renderer | EEVEE, Standard view transform, exposure 0, gamma 1 |
| Сцена | Procedural metric street, authored in `tools/blender/scene.py` |
| Движение | x=2t м, 30 scenario FPS; четыре момента t=0…0.1 s |
| Вход | 4 × 400 × 400 RGB8, 5 perspective faces × 256 × 256 на каждый центр |
| Source frames | 4 синхронных набора, 16 PPM, 80 source faces |
| Surface / output | `dome_floor_v1`, R=12 м, 960 × 540 RGBA8 |
| Bench/server backend | NVIDIA GeForce RTX 5070 Ti / surfaceless EGL |
| Дополнительный нижний ракурс | Mesa llvmpipe / surfaceless; иллюстрация геометрии, не сравнение времени |

Кадры внутри набора создаются последовательно при неподвижной сцене. Сценарные timestamps описывают траекторию; runtime-время offline capture им не равно. Поворот/траектория виртуального наблюдателя не меняет исходные physical-camera poses.

## Результаты

| Проверка | Наблюдение | Граница вывода |
|---|---|---|
| Independent Blender projection | 72 точки, max error **0.000035004 px**, threshold 0.001 px | Проверяет face-camera matrices, FOV и pixel-center convention |
| Direction-field conversion | 6 host-тестов проходят; max допускаемый linear-RGB error 0.008 | Проверяет ориентацию face и конверсию, не фотометрическую реалистичность |
| Manifest integrity | Все 16 PPM hashes совпали | Подтверждает конкретную запись |
| Motion | Каждый из четырёх потоков содержит разные checksums | Подтверждает изменение RGB, не модель автомобильной динамики |
| Renderer | 0 validity mismatches, max GPU projection error ≈0.000043764 px | Проверка калиброванной проекции в точках; не ошибка изображения полного мира |
| Server IPC | 4 READY outputs, разные frame IDs, весь alpha=255 | Подтверждает replay и отсутствие прозрачных отверстий |
| Control | Команда `front` принята и её revision появляется в кадре | Подтверждает локальное управление |
| Основной CTest | 7/7 групп проходят | Математика, calibration, vision tools, reports, Blender fixture, IPC |

`roi_coverage≈0.99548` в bench относится к заданным ground ROI samples. Это не доля всех пикселей виртуального изображения, не оценка покрытия неба и не оценка отсутствия ghosting. Два measured render/readback samples после одного warmup служат smoke; из них нельзя делать вывод о sustained FPS или отношениях платформ.

Manifest SHA-256:

```text
d91af117739c24ec52f173369692ad9793804ed1837fecd65f6751f5b7eb96bc
```

Первичные local artifacts: `artifacts/blender-street-capture/capture.json`, `artifacts/blender-street/ground_truth.json`, `artifacts/blender-validation-final/smoke.json`, `bench/metrics.json` и `server.jsonl`. Скрипты и небольшой набор иллюстраций сохраняются обычным Git; авторский `.blend` дополнительно сохранён в `assets/scenes/metric-street/` с provenance ([[engineering/ASSETS]]). Полные capture/replay-серии остаются локальными. Git LFS отменён по решению пользователя.

## Обнаруженные ограничения и действия

Боковые камеры сначала попали внутрь mirror housing; оптический центр перенесён наружу до |y|=1.10 м. Проверка mounts теперь требует clearance от корпуса зеркала. Так устраняется ошибка сцены до исследования fusion.

Даже с правильными центрами вертикальные столбики растягиваются по полу, а дальние здания и припаркованные автомобили двоятся на перекрытиях. Источник имеет настоящие разнесённые centers, поэтому параллакс стал наблюдаемым. Текущий фиксированный edge-feather и оболочка предполагают геометрию поверхности-носителя; они не реконструируют глубину исходного мира.

Следующий опыт должен добавить depth/visibility truth, измеримые ground/raised markers и варианты hard/distance/graph-cut/multi-band по [[research/PROJECTION_AND_STITCHING]]. Четыре кадра не образуют достаточный temporal dataset. Независимые сцены validation, реальные камеры, realtime producers, удалённый transport и интерактивное вождение остаются открытыми.
