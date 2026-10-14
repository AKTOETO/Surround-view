# Независимые текстурированные сцены: первый screen

План: `configs/research/textured-source-plan.json`. Фиксируется до Blender capture.
Это следующий опыт после [[validation/MESA_SOURCE_TRUTH]], не финальное сравнение алгоритмов сшивки.

Две новые сцены: seeds211/223, разные poses/размеры/повороты препятствия и виртуальной камеры.
Дорога и препятствие получают разные воспроизводимые неоднородные RGB textures128²,
world-space box projection, linear texture interpolation и emission shader. Освещение материалов
не является фактором этого опыта. Физический монтаж камер случайный, сервер получает true calibration;
это не проверка estimated calibration. Cycles CPU8 samples, denoising off; cube256, fisheye400,
output320×180, один frame. Эти размеры — feasibility budget, не доказательство сходимости фильтра.

Сохраняются четыре camera inputs, independent direct virtual RGB, полные object-ID/visibility,
evaluated world triangles и заранее заданные Blender ray anchors offset8/step32.
Пользовательская активная сцена восстанавливается, существующие output directories отклоняются.
Все helpers и native binaries имеют SHA в plan; экспорт содержит hashes всех payloads и Blender version.

Native C++ renderer запускается на Mesa с двумя profiles: flat plane и supplied local support patch
до верхней грани препятствия. Patch имеет axis-aligned bounding footprint повёрнутого box и transition0.2m;
он не моделирует стены box и не объявляется полноценной реконструкцией. Source occlusion отключена.
Одинаковые camera inputs, fusion settings и view внутри paired comparison. Материалы/позы не меняются
после просмотра результатов. Перед analysis проверяются parity ordinary RGBA, calibration, inputs,
фактический backend, совместимость fingerprint двух probes и hashes export.

Centre truth: все centres lattice offset4/step8 в native domain, independent inverse physical
camera ray по фактическим retained UV и intersection evaluated triangles. Matching требует finite hits,
одинакового kind road/target/ego и расстояния≤2/20mm. Проверить anchors≤0.2mm и semantic virtual kind
против полного Blender object-ID. RGB range проверить независимым encoded-RGB bilinear sampler≤1byte;
undefined при менее2 eligible cameras. Thresholds8/16/32/64/128, строго D>T, не оптимизировать.
Отдельно посчитать paired all-matching, mixed и no-matching, без объединения зависимых pixels в trials.

Final RGB: independent virtual RGB используется только на road/target ROI с eroded1px object-ID
границей и отдельно на subset, где visibility содержит≥2 physical cameras. Публикуются denominator
и MAE encodedRGB bytes; visibility не доказывает правильность carrier projection. Semantic target
mask не превращать в chroma segmentation: объект теперь неоднородный. Этот шаг не вводит validated
ghost/seam quality score и не ранжирует fusion algorithms по одной агрегированной MAE.

После baseline: balanced controls, exposure/filter/noise matrix, отдельная атрибуция filtered RGB,
ghost/seam metric и дополнительные scene families. Новые seeds сами по себе не делают эти procedural
boxes natural-object holdout. Не подменять centre geometry метрикой perceptual quality.

## Отметки после захвата

Первоначальный протокол сохранён в `09e4397`. Retry plan после float32 compatibility fix —
`02a462b`, до успешных captures. Collector допускает лишь camera matrix serialization roundtrip≤1e−12,
как предыдущий matched audit; physical inverse rays используют original calibration.
После просмотра outputs добавлены отдельные **posthoc common-domain** ROI, без замены
предварительно заданных interior/two-camera-visible interior. Результаты и обнаруженный пустой
target overlap: [[validation/TEXTURED_SOURCE]].
