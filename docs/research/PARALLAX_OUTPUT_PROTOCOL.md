# E-STITCH-parallax-output-01: выходной растр при прежних входах

Зафиксировано до нового capture и quality outputs, 11.10.2026. Предыдущий контроль [[validation/PARALLAX_SAMPLING]] изменял cubemap; теперь проверяется выход320×180→640×360 при cubemap512 и четырёх fisheye400×400.

## Дизайн

Все12 cases/two mounts101/104, высоты .2/.8/1.4м, near/far, два прежних poses, пять medium carriers, четыре fusion/boundary profiles,2 warmups/7 repeats сохраняются. Используются прежние бинарники и неизменённый `tools/blender/parallax_study.py`. High output230400 pixels находится в native CPU research budget262144. Новые60 fresh servers/480 conditions. Выходной root новый; Blender и trials идут последовательно, сборка/тесты/figures во время trials запрещены.

Recipe `assets/scenarios/parallax-output-v1.json` отличается от512px sampling recipe только capture width/height. Runtime `configs/research/parallax-output-plan.json` отличается output/experiment/inputs_plan/protocol и frozen reference metadata. Reference указывает прежний full summary с точным SHA256; его численные/native/input/protocol поля должны воспроизводиться (кроме разрешённых digests расширенного analyzer/metric module). Reference hashes не менять после просмотра новых результатов.

## Контроли

Проверить полное равенство конфигураций после удаления только output dimensions, stamps/poses, cube files по SHA256 и decoded fisheye inputs, capture metadata после удаления output dimensions и одинаковых raw-file hashes. Объект/материал/монтаж/view/mesh/binary/device неизменны. Проверить все actual output/camera dimensions и analytical full-box bounds: high bounds =2×(low+.5)−.5. Новый full box должен целиком помещаться в кадре; direct IDs/geometry projection/visibility checks остаются активны.

Direct truth **не** должен совпадать побитно: новые pixel centers и независимые ray casts при640×360. Проверить общие cfg/poses, scene inventory/target positions/ID table/source-visibility definition и helper hashes; записать hashes новых RGB/ID/visibility, per-pose target/visible counts. Простой resize старого truth запрещён. Фактический expected renderer —NVIDIA RTX5070Ti/GLES, CPU fusion, не Mesa по environment flag.

## Метрики и ограничения до анализа

Сопоставлять все480 rows по `(case,carrier,truth_index,mode,boundary)`. Первичный full IoU контроль —320 raised pairs, дополнительно160 ground. Сохранить full/conditional IoU, predicted area fraction, centroid distance / output height, linear RGB MAE и обе RGBA hashes. `null` и defined/undefined transitions сохранять явно; зависимые profiles/carriers/poses не являются независимыми репликами.

Основной high score использует прежний fixed8px component threshold. Дополнительная **заранее заданная** sensitivity использует32px при640×360, эквивалентные8px по доле площади исходного кадра. Пересчитать full/conditional masks и centroid для всех480 outputs с32px; не выбирать лучший порог по результату. Source chroma threshold .15 неизменен. Report включает оба high варианта и low8px; смена tiny component policy может влиять на missing object.

Pyramid levels и другие серверные параметры в pixel units остаются прежними: при большем output их угловой/пространственный support меняется. RGB ROI boundary suppression также фиксирована в pixels; ROI не является строго общим непрерывным support. Поэтому это sensitivity всей цепочки к output resolution, а не чистая rasterizer ablation, сходимость или доказательство отсутствия sampling error. Даже устойчивый negative result не заменяет fisheye-resolution, source correspondence/triangle-hit, optical-height/baseline и independent natural-scene tests. Timing не использовать как сопоставимую performance benchmark. Полные новые reports и отрицательные результаты сохранить; протокол после результатов не редактировать.
