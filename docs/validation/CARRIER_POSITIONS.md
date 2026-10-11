# Native carrier-position pilot, 11.10.2026

## Что реализовано

`RenderInspectionRequest::carrier_positions` включает получение фактических perspective-interpolated координат носителя в системе автомобиля. Данные поступают из того же GLSL varying `world`, с теми же vertex/index buffers, MVP и depth test, что используются при отрисовке. Матрица `RenderInspection::carrier_positions` имеет тип CV_32FC3, единицы — метры, начало растра — top-left. Background и ego model представлены NaN; footprint остаётся отдельным carrier region3 и имеет координату, хотя изображение камер туда обычно не проецируется.

Три дополнительных прохода упаковывают IEEE754 float bits в RGBA8, по одному проходу на ось. Dithering отключён, integer arithmetic highp; кодирование не квантует координату до8бит и не требует float-color framebuffer extension. Ego geometry участвует в depth test, но её пиксели отбрасываются по region ID. Существующий inspection geometry layer включает ego model также при weight/coverage diagnostic views; он не тождествен финальному diagnostic RGB/depth policy. Пилот accuracy использует color view. Обычный capture regions остаётся доступным без выделения positions. Данные владеют памятью через OpenCV; вызов выполняется в render thread.

`sv-carrier-probe CONFIG OUTPUT --positions` экспортирует `carrier-positions.f32`: little-endian float32, H×W×3, X/Y/Z. В report записаны SHA256, layout, scope, payload size, actual device, raster_subpixel_bits и проверка неизменности обычного RGBA. Без флага positions пусты и три дополнительных прохода не выполняются. Подробности: [[engineering/USAGE#Фактические координаты носителя]].

Это первый этап [[planning/ROADMAP#Ближайший исследовательский этап: source correspondence и triangle-hit]]. Пока получена точка фактического carrier mesh; triangle ID, source-camera UV/contributing support, independent scene-surface correspondence и typed wire channel ещё требуют реализации. Render-thread API и локальный исследовательский export не закрывают весь клиент-серверный diagnostic workflow.

## Независимые проверки

Шесть GTest cases: один known-answer oracle со shared edge, parallel/behind/no-hit и nearest-depth checks; пять carrier fixtures —plane, bowl, dome-floor, cylinder-floor, cube-floor. Независимый double-precision Möller–Trumbore intersection выбирает ближайший hit по фактическим треугольникам. Построение mesh разделяется с renderer: это независимость алгоритма пересечения, а не независимая реализация генератора сетки.

Для каждого backend проверены1343 выбранных rays суммарно по пяти coarse meshes160×100. Для всех поддерживаемых пикселей проверена обратная проекция в их top-left центры. Background/ego должны иметь три NaN, carrier support — три finite coordinates. Проверены отрицательные координаты, очистка positions после region-only запроса, обновление поверхности и отсутствие изменений обычного RGBA. Coarse dome имеет точки внутри идеальной сферы: oracle сравнивает треугольники, а не ошибочно подменяет их analytic dome.

| Носитель | Rays/backend | NVIDIA max axis error, mm | Mesa max axis error, mm | NVIDIA max reprojection, px | Mesa max reprojection, px |
|---|---:|---:|---:|---:|---:|
| Plane |189 |2.268140 |0.006261 |0.00190219 |0.00004888 |
| Bowl |200 |1.315981 |0.006536 |0.00192299 |0.00004827 |
| Dome-floor |318 |1.738945 |0.011885 |0.00193208 |0.00014332 |
| Cylinder-floor |318 |1.738945 |0.004749 |0.00193208 |0.00004139 |
| Cube-floor |318 |2.268140 |0.006261 |0.00190219 |0.00004888 |

Actual devices: NVIDIA GeForce RTX5070Ti/PCIe/SSE2 и Mesa llvmpipe (LLVM22.1.8,256bits). Оба сообщают GL_SUBPIXEL_BITS=8. Одна эта capability не объясняет различие численных остатков; конкретный вклад raster/interpolation implementation здесь не изолирован. Меньший остаток Mesa не означает превосходство по качеству surround-rendering или производительности.

Первоначальный pilot assertion1mm оказался слишком строгим для NVIDIA: максимум2.268140mm. После этого выбран fixture-specific bound5mm по оси и0.002px для обратной проекции. Эта корректировка явно записана в `configs/research/carrier-positions-plan.json`; это инженерный пилот, а не заранее зарегистрированная подтверждающая quality hypothesis. Пределы относятся только к выбранным coarse fixtures и views; их нельзя переносить на большие расстояния или medium640×360 экспорт как измеренную гарантию.

## Native export и совместимость обычного pipeline

Пять medium640×360 configs прежнего rig101-near-z080 использованы для нового native export. Они имеют прежние SHA256. World-point data занимают2764800bytes на конфигурацию, дополнительно к region mask и transient readbacks. Independent scene truth в этом проходе не снимается, `source_frames=0`: рисунки описывают геометрию носителя, а не происходящее в мире.

| Носитель | Finite carrier pixels | NaN background/ego pixels |
|---|---:|---:|
| Plane |138362 |92038 |
| Bowl |146275 |84125 |
| Dome-floor |228024 |2376 |
| Cylinder-floor |228024 |2376 |
| Cube-floor |228024 |2376 |

Для замкнутых носителей оставшиеся2376pixels принадлежат ego model, а не пространственной дыре в носителе. Это один view; texture validity и полное покрытие всех возможных views этим результатом не доказываются.

Отдельный native replay на прежних800px inputs/640×360 output:5 fresh servers,360 recorded samples, включая280 stored measurement RGBA и40 условий (5carriers×4fusion/boundary profiles×2poses). Все360 recorded hashes и280 actual RGBA files совпали с reference. Checked conditions, configs, scenario и device/catalog прежние; различаются source revision/fingerprint. Новый renderer fingerprint `df708cec09fb3602e5bfdb5064d0202dd31f93e3d491b0a3cee5ad1c274df1f1`, compiled source revision `d3751e7c563fb5377f793148b7272385dd10cdf3`. Compatibility screen ограничен этим case и не заменяет повтор всей480-condition study после будущих изменений продукта.

Дополнительно native fusion probe теперь запрашивает positions при capture синтетических PNG input frames во всех18mode/diagnostic/boundary cases на каждом из пяти carriers. Existing full-composition/plane-wire parity проверяет сохранность hybrid samples, RGBA и resource reuse. После расширения probe CTest render_fusion_parity прошёл за1.77s. Первоначальная попытка внутри sandbox не могла bind Unix IPC; вне sandbox проверка прошла. Предшествующий комплект render_modes/blender_fixture/object_metrics/render_fusion_parity:4/4,5.70s; GTest6/6 на каждом из двух явно выбранных backends.

## Provenance, воспроизведение и границы

`baselines/carrier_positions_v1.json` содержит geometry layouts/counters/hashes, configs, исходные GTest properties, XML/code/binary hashes и обе native report hashes каждой parity пары. Generated root `artifacts/carrier-inspection-v1` сохраняет positions, raw native reports/RGBA, XML, executable archives и test-provenance. Reference raw `parallax-sensor-*` нужен для проверки прежних reports/files. Plan committed476ec4f до native parity trials; первый допуск1mm не скрыт. Исходная native implementation зафиксирована d3751e7, audit/plot generator755254e, hybrid test extensiona2aaea7.

Повторный read-only audit совпал с первым побитно. `plot_carrier_positions.py` только проверяет готовые native outputs и рисует графики; rendering/intersection oracle выполняются C++. Raw generated artifacts не добавляются в Git/LFS; tracked baseline и подписанные рисунки — воспроизводимые свидетельства. Команды: [[engineering/USAGE#Фактические координаты носителя]], диплом §4.59/рисунки4.81–4.83.

Открыты correspondence с source/scene surfaces, contributing weights/UV и triangle identity/coverage, экспорт через sv-server/sv-client-lib, отдельный учёт diagnostic overhead, exhaustive boundary/near/far cases, независимые scenes/mounts и Аврора. Пилот не объясняет пока причину нулевого raised-object IoU и не ранжирует носители/алгоритмы.
