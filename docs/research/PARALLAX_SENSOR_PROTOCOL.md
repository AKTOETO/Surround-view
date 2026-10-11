# E-STITCH-parallax-sensor-01: sampling fisheye400→800

Фиксируется до захвата и quality outputs, 11.10.2026. После [[validation/PARALLAX_SAMPLING]] и [[validation/PARALLAX_OUTPUT]] проверить влияние fisheye raster400×400→800×800 при cubemap512/output640×360, прежнем мире/монтаже/view/алгоритмах.

## Дизайн и физический смысл

Сохраняются12 height/range cases, mounts101/104,2 poses,5 medium carriers,4 fusion/boundary profiles,2 warmups/7 repeats, binaries и actual GLES device.60 новых servers/480 conditions; никакой параллельной нагрузки Blender/build/tests/plots во время native trials. Источник является синтетическим resampling прежней cubemap512, а не новым физическим сенсором или дополнительными наблюдениями мира. Больший source raster не увеличивает исходную детализацию cubemap; вывод о sampling convergence по одному удвоению запрещён.

Новый driver `tools/blender/parallax_sensor.py` сохраняет controlled flat-world построение прежнего driver, но до capture преобразует actual и nominal intrinsics через versioned `camera_sampling.py`. Legacy drivers/helpers не меняются. Recipe `assets/scenarios/parallax-sensor-v1.json` добавляет только одинаковый `fisheye_resolution=[800,800]` ко всем cases прежнего output recipe. Runtime plan `configs/research/parallax-sensor-plan.json` меняет experiment/inputs_plan/protocol и frozen reference на640×360 full summary предыдущей серии с exact SHA256; все алгоритмические параметры и output остаются прежними.

Для scale2: $f'_x=2f_x$, $f'_y=2f_y$, $c'_x=2(c_x+0.5)-0.5$, аналогично y. Исходные128/199.5 становятся256/399.5. Extrinsics, distortion k=0, alpha=0, theta_max=1.48, z_epsilon сохраняются. Это pixel-center scaling, не изменение camera pose/FOV. Для данной centered equidistant optics angular disk имеет radius128×1.48=189.44px при principal199.5 и целиком лежит внутри400 raster;800 disk также целиком внутри. Поэтому rectangle pixel-center bounds не обрезают angular support в обеих сетках. Проверить эту предпосылку явно, не распространять доказательство на произвольные distortion/alpha/crop configurations.

## Приёмка до парного вывода

Проверить оба полных native audits, pinned reference summary с разрешёнными historical analyzer/metric digest отличиями; сохранить numeric/input/native/protocol поля. Между сериями high config должен быть **точно** результатом resample_config(low config), включая неизменные output/vehicle/view/T/прочие поля; nominal аналогично. Проверить actual input dimensions, frames/stamps/poses, mount offsets, target/world inventories и generator/converter hashes. Actual optics checks каждой серии должны оставаться в своём pixel-unit tolerance; их численные ошибки не обязаны совпадать после смены units.

Все реальные cube faces512 должны иметь одинаковые decoded RGB/static PNG metadata. Raw PNG SHA256 проверяются внутри каждой серии, между сериями могут различаться только служебные Date/Camera/Scene/RenderTime; эта уже известная особенность учитывается заранее. Считать фактические faces, исключая overview. Native binaries, source fingerprint и catalog backend/vendor/renderer обязаны совпасть; software environment flag не является паспортом Mesa.

Заново экспортировать независимые direct640×360 RGB/object-ID/raw visibility camera bits в Blender и требовать их побитного совпадения с reference. Literal Blender numeric name suffix может меняться; сравнить numeric ID→semantic base-name, target/helper/ego IDs и все прочие truth definitions. Ни изменение видимой поверхности, ни новый truth viewpoint не должны маскироваться под sampling. Несовпадение любого контроля блокирует парный вывод; не ослаблять criterion по quality results.

## Анализ

Все480 rows сопоставлять по `(case,carrier,truth_index,mode,boundary)`. Первичный результат —full IoU320 raised pairs, ground160 отдельно. Сохранить обе RGBA hashes, full/conditional IoU, predicted pixels, centroid distance и visible-interior linear RGB MAE, signed high−low deltas. Unknown scores остаются null с отдельным учётом transitions; poses/carriers/profiles не считаются независимыми репликами. Component cutoff8px и chroma .15 одинаковы на фиксированном output; дополнительная output-area scaling не требуется.

Source-параметры в pixel units, включая edge_width_px, остаются прежними: это sensitivity всей серверной цепочки к input raster, не изоляция единственного фильтра. Pyramid/output grid фиксированы. Даже устойчивый negative result не исключает совместное cubemap/fisheye/output sampling влияние, source correspondence или geometry mismatch и не выбирает universal carrier/fusion. Следующие этапы: native triangle-hit/source-surface correspondence, optical-height/baseline и независимые natural scenes. Timings не интерпретировать как target Aurora benchmark.
