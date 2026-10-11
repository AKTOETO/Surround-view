# Парный контроль дискретизации fisheye, 11.10.2026

## Постановка и происхождение данных

Frozen protocol [[research/PARALLAX_SENSOR_PROTOCOL]]/commit `f5efb17` задаёт сравнение fisheye400×400→800×800 при cubemap512 и output640×360. Recipe `assets/scenarios/parallax-sensor-v1.json` сохраняет12 прежних cases: два монтажа101/104, near/far, высоты центра box0.2/0.8/1.4m, две позы. Пять medium носителей и четыре fusion/boundary profiles прежние. Runtime plan закрепляет reference root/inputs/full-summary SHA256 серии [[validation/PARALLAX_OUTPUT]].

Новый Blender driver `tools/blender/parallax_sensor.py` применяет `camera_sampling.py` к actual/nominal intrinsics до capture. Удваиваются focal lengths; principal point преобразуется как `2*(c+0.5)-0.5`. Extrinsics, distortion, theta_max1.48rad, optical heights, view, движение и мир сохраняются. Угловые круги строго внутри обоих sensor rasters:189.44px radius/10.06px margin против378.88px/20.62px. Это передискретизация тех же наблюдений, не новый физический сенсор.

До native matrix принят полный контроль всех12 captures: decoded source cube RGB/static PNG metadata и независимые direct RGB/object-ID/raw visibility совпали. Файлы каждой серии проверены по собственным manifest hashes. Только переменные PNG поля Date/Camera/Scene/RenderTime исключены из межсерийного metadata comparison; terminal numeric Blender name suffixes нормализуются с сохранением numeric IDs/semantic base names и original tables. Эти правила заранее включены в protocol, новых приёмочных deviations нет. Генератор восстановил исходную активную Scene.

Аудитор `tools/research/sensor_sampling.py` проверяет actual source image dimensions400/800, intrinsics-only config equality, все faces и truth frames, original binary hashes, actual device/fingerprint, optics tolerance и строгое совпадение контрольных факторов. Instrumentation и known-answer tests зафиксированы до native quality analysis в `2044d74`. Модуль имеет отдельное имя, чтобы его импорт не пересекался с Blender driver.

## Принятые результаты всех480 пар

60 fresh native processes,4320 samples,3360 measurement RGBA и480 quality conditions. Проверены960 фактически экспортированных source cube faces обеих серий; independent direct RGB/ID/visibility совпадают побитно. Actual optics error нового capture максимум0.000047768px при frozen tolerance0.001px. Binaries и fingerprint `601a3878ba99e6bc953756bbc0c40e969fdb0c89c5d7487349332b7960ac87f4` прежние; CPU fusion + actual GLES backend NVIDIA Corporation / NVIDIA GeForce RTX5070Ti/PCIe/SSE2, а не Mesa. Все480 пар имеют разные RGBA hashes.

| Метрика | Fisheye400 | Fisheye800 |
|---|---:|---:|
| Raised full IoU0 |320/320 |320/320 |
| Raised conditional IoU0 |320/320 |320/320 |
| Conditional undefined |0/480 |0/480 |
| Centroid undefined |240/480 |240/480 |
| Ground full IoU, диапазон |0.051701…0.327966 |0.049245…0.310287 |
| Near-z080 centroid distance / output height |0.284821…0.331941 |0.286687…0.330917 |

| Разность800−400 |Уменьшение |Равенство |Увеличение |Undefined |Диапазон defined |
|---|---:|---:|---:|---:|---|
| Full IoU |130 |320 |30 |0 |−0.0176794…+0.0103852 |
| Conditional IoU |102 |320 |58 |0 |−0.0191068…+0.0092946 |
| Predicted pixels |200 |241 |39 |0 |−59…+94 |
| Centroid distance, px |88 |0 |152 |240 |−0.372481…+1.019286 |
| Linear RGB MAE |55 |0 |425 |0 |−0.000058709…+0.000874567 |

Снижение IoU — ухудшение, снижение distance/MAE — улучшение. Predicted area сама по себе не задаёт направление качества. Full/conditional ties320 относятся ко всем raised conditions; ground changes имеют смешанное направление. Отсутствующий centroid не заменяется0. Undefined pattern совпадает в обеих сериях.

Суммарный coded chroma count четырёх source cameras для каждой позы положителен в обеих сериях; отношение high/low3.94051…4.07524, близкое к росту площади растра в4 раза. Это число цифровых source pixels, не новое наблюдение и не доказательство correspondence с output pixels. Видимость independent target surfaces неизменна, поскольку direct RGB/ID/raw visibility приняты побитно. В отличие от output-resolution опыта, ROI/output pixel grid здесь прежние; source-edge параметры в pixels имеют иной угловой support. RGB MAE является парной оценкой всего заданного pipeline, но не изолирует конкретный фильтр или геометрическую причину.

## Воспроизведение и ограничения

Повторный полный read-only paired audit совпал побитно с первым и tracked baseline; SHA256 `8727e527ab294f45348121f74208e7ec90fe868b0b9b7da60dd8586f74b70dac`. Предыдущая серия также переаудирована: численные результаты, inputs/native hashes и controls сохранены; допускается только заранее оговорённое изменение analyzer code digests.

Baseline `baselines/parallax_sensor_v1.json` содержит все480 keyed pairs, hashes обеих RGBA каждой пары,60+60 native report hashes,12 controls с actual face counts/cube digests/truth digests, optics checks, original ID tables и source chroma counts. Raw high root `artifacts/parallax-sensor-v1`, inputs `artifacts/parallax-sensor-inputs-v1`; reference `parallax-output-*` не подменяется. Проверки не допускают изменений view, mesh budget, output, cubemap, extrinsics или binaries. Нет новых protocol deviations.

CTest object_metrics/blender_fixture/render_fusion_parity/render_modes:4/4,5.97s; object_metrics39 known-answer tests, включая pixel-center scaling/angular support, plan confounders, cube pixel/metadata/hash tampering. Исходная активная Blender Scene подтверждена через MCP после опытов.

Увеличение только fisheye raster400→800 недостаточно для устранения raised-object failure. Совместно с предыдущими cubemap/output controls это сужает гипотезу о недостаточной дискретизации, но **не доказывает сходимость и не изолирует геометрию**. Один flat world, два монтажа и две зависимые позы не дают universal carrier/fusion ranking. Далее нужны native source-surface correspondence/triangle-hit, optical-height/baseline sweep и независимые natural scenes. Target Aurora/FPS/thermal и реальные сенсоры не проверены.

Команды: [[engineering/BLENDER#Fisheye800 при прежнем поле зрения]], [[engineering/USAGE#Чувствительность к разрешению fisheye]]. Диплом §4.58/рисунки4.78–4.80. Generated raw artifacts игнорируются Git; recipe, baseline и иллюстрации сохраняются обычным Git без LFS.
