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

## Расширение: координаты исходных камер

Следующий программный шаг добавляет native opt-in `source_projections`; формат и коды: [[engineering/RENDERING#Opt-in source projections]]. Это инженерная проверка диагностического API, не новая серия качества и не продолжение исторического baseline `carrier_positions_v1.json` с прежними hashes.

`tests/render_source_inspection_tests.cpp` создаёт четыре камеры над плоскостью. Первая имеет ненулевую fisheye distortion; остальные задают отрицательную глубину, узкую угловую область и заведомый выход за изображение. Координаты каждого допустимого output pixel проверяются независимой double CPU функцией `sv::project` с допуском 0.01 source pixel. Допуск является fixture-specific assertion, а не измеренным максимумом для всех камер. Проверяются все семь статусов, NaN, запрос без destination, отсутствие кадров после предыдущего валидного набора, очистка capture и равенство обычного RGBA до/после inspection.

Результат 11.10.2026: GTest **7/7** на Mesa llvmpipe и NVIDIA RTX 5070 Ti, включая прежние пять carrier fixtures и triangle oracle. `render_fusion_parity` проверяет четыре камеры в 18 комбинациях fusion/diagnostic/boundary на каждом из пяти carriers: source validity совпадает с реальными hybrid samples вне background/ego. Проверка full composition, сохранности RGBA/resources и plane wire parity проходит. Обе целевые CTest-группы прошли (2/2).

```sh
cmake --build build --target sv-carrier-inspection-tests sv-render-fusion-probe sv-server -j4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json ctest --test-dir build -R '^(carrier_inspection|render_fusion_parity)$' --output-on-failure
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json EGL_PLATFORM=surfaceless build/sv-carrier-inspection-tests
```

Требуются доступ к GPU для NVIDIA и разрешённый Unix socket для integration. Первые запуски в sandbox остановились соответственно на EGL initialization и socket bind; после запуска с необходимым доступом проверки прошли.

Native fusion probe дополнительно экспортирует `cameraN-source-pixels.f32` (H×W×2, float32 в native byte order тестовой машины) и `cameraN-source-status.u8` (H×W). Эти тестовые файлы не являются переносимым wire-контрактом. Передача через server/client-lib, triangle ID, scene-surface truth, contributing support, измерение overhead и boundary-focused optical tests остаются открытыми. Исторические 40-condition archived RGBA comparisons заново не запускались; текущая проверка равенства относится к включению/выключению capture в новой сборке.

## Расширение: вклад камер после смешивания

`FusionInspection` получает реальные post-smoothing blend weights, normalized per-level weights и четыре reconstructed linear-RGB contributions; определения: [[engineering/RENDERING#Opt-in fusion contributions]]. Существующие diagnostic weights view и `FusionResult::weights` сохраняют прежний смысл до smoothing. Включение capture не заменяет основную арифметику другим renderer.

В `tests/fusion_inspection_tests.cpp`:

- Для четырёх modes × двух boundary policies × двух seam solvers (16 комбинаций) проверены bitwise-equivalent color/weights при включении capture, reconstruction sum с допуском 2e-6, нормировка/неотрицательность level coefficients и фактическое число уровней.
- Known-answer full-support constant fixture: входные веса 1:2:3:4 дают коэффициенты 0.1:0.2:0.3:0.4 и итоговый RGB 0.75; четыре вклада равны 0.025, 0.1, 0.225, 0.4.
- Повторное использование destination после многоуровневого capture не сохраняет старые уровни; all-invalid fixture даёт нулевые weights/contributions/output.
- Две неперекрывающиеся половины raster демонстрируют ненулевой multiband camera0 contribution справа от её validity при нулевом локальном blend weight. Это known-behavior control, а не доказательство ошибки конкретной Blender-сцены.

Native render fusion probe запрашивает capture для каждой из 18 fusion/diagnostic/boundary combinations на пяти carriers и экспортирует `blend-weights.f32`, `levelN-weights.f32`, `cameraN-contribution.f32`. Layout: H×W×channels, origin top-left, native float32 byte order из index.json; уровень N использует собственные reduced dimensions из case.fusion_levels в index.json. Формат предназначен для тестовых артефактов, не wire ABI. Проверка unsupported native GPU mode добавлена в source inspection GTest.

```sh
cmake --build build --target sv-fusion-boundary-tests sv-fusion-probe sv-render-fusion-probe sv-carrier-inspection-tests sv-server -j4
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json ctest --test-dir build -R '^(fusion_boundary|carrier_inspection|native_fusion_parity|render_fusion_parity)$' --output-on-failure
```

Пределы проверки: небольшие synthetic fixtures, не реальная scene correspondence; GPU-only contributions, bounded wire capture и оценка overhead остаются открытыми. Исторические baselines/числа качества не пересчитаны этим инженерным шагом.

Результат 11.10.2026: CPU GTest **7/7** (четыре прежние boundary проверки и три contribution проверки); целевые CTest группы native_fusion_parity/render_fusion_parity/carrier_inspection/fusion_boundary **4/4** на Mesa с разрешённым IPC. Это regression validation текущей реализации, а не новая quality серия.

Расширение12.10.2026: `FusionInspection::level_contributions` сохраняет signed weighted RGB bands каждого уровня/камеры. Native probe поддерживает selected manifest frame и color-only mode, записывает carrier XYZ и file hashes. Matched60-condition проверка и per-level/SV01 regressions: [[validation/HYBRID_CORRESPONDENCE]].
