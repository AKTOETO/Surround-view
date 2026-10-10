# E-STITCH-server-boundary-01: протокол парного серверного опыта

Зафиксирован 11.10.2026 до просмотра результатов новой серии. Это exploratory follow-up на уже просмотренных Blender fixtures, не независимый holdout. Родительские опыты: [[validation/PYRAMID_BOUNDARY]], [[validation/OBJECT_STITCH]].

## Вопросы и неизменяемые условия

1. Уменьшает ли normalized support ошибку относительно direct-view Blender RGB на текстурированной сцене, либо его преимущество ограничено постоянным полем?
2. Меняется ли форма coded target при одинаковых источниках и поверхности?
3. Как меняется стоимость гибридного production renderer внутри той же пары mode/carrier/frame?

Первичная matrix: tracked `tests/data/object_stitch_v1`, обе позы, пять native носителей plane/bowl/dome/cylinder/cube из текущего CARRIERS baseline, два режима multi_band/graph_cut_multi_band × zero/normalized. Итог 40 условий, два warmup и семь measurement blocks на каждом кадре, случайный полный порядок внутри block, seed 20261011. Levels=4, smoothness=0.1, edge width=24, angle power=2, diagnostic=color. Размер вывода 320×180. Оба boundary варианта внутри пары используют одинаковую сетку/texture budget; носители между собой **не** объявляются равнобюджетными.

Дополнительный screen: сохранённый девятикадровый `artifacts/object-stitch-motion-inputs` с truth из `artifacts/object-stitch-motion-capture`, только dome baseline и те же четыре fusion profiles. 36 условий. Это существующая одна траектория, не новый длинный клип и не независимая сцена. Если доступны не все проверяемые hashes, дополнительную серию не выполнять.

Клиент — нативный `svctl research`, transport Unix, сервер — production sv-server. Все final RGBA сохраняются нативным capture consumer библиотеки; Python используется только для независимой проверки truth/hashes, метрик и иллюстраций. Реализация fusion/проекции/последовательного запуска не переносится в Python. Native Linux Mesa device/driver и production source fingerprint записываются в каждом raw report.

## Входы, сопоставление и качество

- Проверить capture/config/manifest/paired-truth hashes существующим `load_sequence`. Для каждого sample проверить hash сохранённого RGBA, размер/alpha, READY, frame_set_id и полное совпадение inputs с baseline соответствующего кадра.
- Каждая группа из четырёх inputs должна иметь один scenario source_timestamp_ns; по нему сопоставить direct RGB/object truth. Frame index runner не является индексом fixture. Все ожидаемые timestamps должны встретиться ровно в одном frame baseline; при loop wrap не считать переход последнего кадра к первому непрерывным движением.
- Primary RGB ROI — независимая any-camera visibility и non-ego object interiors из load_sequence: удалить background/ego и полосу в два пикселя вокруг границ object IDs. Не менять ROI по полученному изображению или методу.
- Primary quality — MAE в linear RGB и MAE в sRGB по этому ROI. Дополнительно фиксировать linear-RGB p95 absolute channel error. Эти показатели объединяют геометрию/параллакс/photometry, не являются чистым seam score.
- Coded-target mask извлечь неизменным chroma classifier/threshold/minimum area из fixture. Сопоставить с direct object ID: IoU, precision/recall, false positives/missing pixels, components/row runs. Не называть это natural-object correspondence или полным ghost detector.
- Проверить повторяемость RGBA hashes внутри каждого frame/variant. Повторные рендеры не считаются независимыми сценами. Отдельно сохранить метрики каждого кадра/носителя и разность normalized−zero; не выбирать лучший параметр по результатам.

## Timing и восстановление

Stage durations берутся из metadata production кадра: fusion_cpu и render_wall. Для каждой frame/mode/carrier пары сравниваются семь measurement samples boundary вариантов; warmup исключается. Представить медиану и p95 nearest rank по каждому варианту, разность/отношение descriptive. CPU/GPU spans не суммировать с вложенными render_wall. Не объявлять эти короткие серии sustained FPS, sensor-to-display latency или итоговым ranking методов.

Серверы запускать последовательно; не совмещать timed trials с CTest/другими research runs. Зафиксировать GL device, источник, код и protocol hash. ОС/фоновые процессы не контролируются полностью; thermal и целевая Аврора остаются отдельными испытаниями.

Runner требует capability experiment_step_v1 для нескольких кадров; один lease защищает fusion/surface/pause и step. При stale baseline допустим один step для получения READY. Между кадрами возвращается исходный fusion/surface, выполняется step, затем все варианты получают один paused frame set. Проверяется последний исходный RGBA до восстановления pause. `restored=true` означает только fusion/surface/pause: cursor/history не восстановлены, что явно записывается в report. Failure/cancel/capture error должны запускать тот же cleanup.

## Критерии и ограничения вывода

Серия недействительна при несовпадении hashes/provenance, неполной matrix, отсутствии READY или успешного восстановления. Синтетические controls проверяют нулевую ошибку RGB, ошибку известного цветового сдвига, совпадение/дублирование target masks и запрет интерпретации source timestamp wrap как непрерывного клипа.

Допустимый вывод — описать, на каких проверенных условиях normalized улучшает/ухудшает независимую RGB/target метрику и сколько стоит. Нельзя распространять constant-field результат на все сцены, объявлять оптимальный носитель при неравных сетках или выбирать универсально лучший алгоритм. Для завершения пунктов 1–7 остаются новые сцены/mount seeds, natural-object truth, long clips, radiometric compensation, robust calibration families и равные бюджеты. Blender MCP при подготовке серии недоступен; новый capture не заявляется.
