# Planar precision: парный NVIDIA / Mesa control

Дата:12.10.2026. Follow-up трёх strict failures [[validation/PLANAR_CORRESPONDENCE]], диплом §4.65. План `configs/research/planar-precision-plan.json` зафиксирован `51f6375` до новых captures; baseline `docs/validation/baselines/planar_precision_v1.json`; raw root `artifacts/planar-precision-v1`. Это geometry/precision experiment, не новый рейтинг сшивки или performance benchmark.

## Что менялось

10 прежних NVIDIA carrier/frame captures сопоставлены с10 новыми native Mesa llvmpipe captures: два frames, пять carriers. Запускался **тот же архивный binary** SHA256 `d9f3349d5a03dfbace41615ac07b609d37bfacd2a4d94def0e79b008bf59f115`; source/config/manifest/input hashes зафиксированы. Shader files также проверяются, поскольку Renderer читает их во время исполнения. Меняется backend: NVIDIA RTX5070Ti versus llvmpipe (LLVM22.1.8,256bits). Входные изображения всё ещё принадлежат прежней box scene; нового planar RGB truth нет. Actual device names записаны в каждой строке baseline.

Использованы те же166/161 pixel centers; source validity совпала во всех10 парах. Normal RGBA неизменен до/во время/после inspection в каждом capture, но **RGBA hashes NVIDIA и Mesa различаются во всех10 парах**. Межbackend побитная RGB equivalence не заявляется, quality этого различия здесь не измерялась. Нет рандомизированного чередования GPU/CPU запусков, timing/thermal или confidence interval.

## Разложение остатка

Collector независимо строит double look-at/perspective MVP по проверенным формулам `View::mvp`, а не читает captured uniforms. Plane intersections unprojected double MVP совпадают с отдельным virtual-ray plane oracle с допуском1e−10m. Затем выполняются две разные проверки:

- Native carrier position повторно проецируется в output raster; отклонение от выбранного pixel center измеряется в **output pixels**.
- Double MVP округляется до float32, как при upload matrix, и снова используется в double inverse plane oracle. Это изолированная модель **только uniform rounding**; она не моделирует vertex operations, clipping, fixed subpixel precision, interpolation или driver optimizations.

Оптическая projection actual carrier point проверяется отдельно от геометрического point correspondence. Analytic plane здесь double; в §4.64 использованы Blender hits. Значения новых distance/UV потому слегка отличаются от прежних; исходные failures и baseline не заменяются.

## Результат

| Frame / carrier | NVIDIA max UV, px | Mesa max UV, px | NVIDIA / Mesa strict pass |
|---|---:|---:|---|
| 0 / plane | 0.007407 | 0.000588 | yes / yes |
| 0 / dome_floor | 0.010159 | 0.000505 | NO / yes |
| 0 / cylinder_floor | 0.011143 | 0.000560 | NO / yes |
| 0 / cube_floor | 0.010461 | 0.000512 | NO / yes |
| 1 / plane | 0.007459 | 0.000750 | yes / yes |
| 1 / dome_floor | 0.007494 | 0.000572 | yes / yes |
| 1 / cylinder_floor | 0.009321 | 0.000611 | yes / yes |
| 1 / cube_floor | 0.006958 | 0.000679 | yes / yes |

Оба заранее заданных порога сохранены:0.1mm source-to-plane distance и0.01px source UV gap. Exact-floor controls: NVIDIA5/8, Mesa8/8. Три NVIDIA failures относятся к тем же dome/cylinder/cube frame0. Bowl остаётся curved descriptive comparison и не включён в8 strict conditions.

| Максимум по восьми exact-floor условиям | NVIDIA | Mesa |
|---|---:|---:|
| Source hit → ideal plane point, mm | 0.100072 | 0.006372 |
| Correct plane → native source UV, px | 0.011143 | 0.000750 |
| Native point → output pixel center, output px | 0.00187936 | 0.00004708 |
| Optical residual actual carrier point, source px | 0.00008141 | 0.00057327 |

Модель uniform rounding даёт max plane shift0.0000000582m и max source UV shift0.000005063px, одинаковые для всех carriers одного frame. Округление только MVP не воспроизводит ни carrier dependence, ни observed NVIDIA residual. На Mesa residual carrier reprojection гораздо меньше, при этом optical residual несколько больше NVIDIA. Следовательно, оптическая точность и точность выбора геометрической точки — разные измерения.

Наблюдаемая зависимость от backend при одинаковом binary/config/inputs локализует малый planar residual в графической вычислительной цепочке и согласованности rasterized carrier position с идеальным лучом. Это **не доказательство конкретного механизма subpixel quantization/interpolation**: для него нужны actual vertex/clip/triangle data и контролируемая raster ablation. Не следует добавлять CPU коррекцию carrier points лишь ради прохождения порога: это изменит shader sampling/обычный RGBA и требует отдельного product design/quality parity.

Большой объёмный height failure предыдущих опытов этой численной погрешностью не объясняется: здесь речь о долях миллиметра и тысячных source pixels, тогда как raised fixture имел hundreds-of-pixels UV gap. Это сопоставление масштабов отдельных fixtures, а не универсальный error bound.

## Воспроизведение

Требуются старые native/Blender inputs из [[validation/SOURCE_CORRESPONDENCE_CONTROLS]], тот же архивный executable и frozen files плана. Read-only audit:

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  ctest --test-dir build -R '^carrier_inspection$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_planar_precision.py \
  --output-report artifacts/planar-precision-v1/reaudit.json
cmp docs/validation/baselines/planar_precision_v1.json artifacts/planar-precision-v1/reaudit.json
```

Семь known-answer controls, CTest source_correspondence1/1 и существующая C++ GTest группа carrier_inspection на Mesa прошли. Два read-only audit совпали побитно. В audit обнаружена несовместимость `np.row_stack` с установленной NumPy; заменена на `np.vstack`, добавлен тест look-at/perspective target center (`dac4f4a`). Это исправление исследовательского collector, не изменение Renderer или исходных native captures.

Для новых backend captures сначала скопируйте план, задайте новый output_root. Проверяйте все frozen_files hashes; копируйте binary и сохраняйте его SHA256. Для каждого frame/carrier выполняйте (выражения в угловых скобках заменить на значения копии плана):

```sh
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  artifacts/source-correspondence-controls-v1/sv-correspondence-probe \
  <config_root>/<carrier>/config.json <manifest> \
  <output_root>/frame<frame>/native/<carrier> <frame>
```

Создайте `<output_root>/invocations.json` с `binary_sha256`, `plan_sha256`, `cases` (10 записей: `frame`, `carrier`, `argv`, `returncode`), сохраните executable как `<output_root>/sv-correspondence-probe`. Полная orchestration сохранена как argv/logs в raw archive; схема соответствует [[validation/SOURCE_CORRESPONDENCE_CONTROLS#Воспроизведение]], с новым frame/carrier path и Mesa environment. Audit новой серии:

```sh
python3 docs/diploma/plot_planar_precision.py \
  --plan artifacts/planar-precision-repeat-plan.json \
  --output-report artifacts/planar-precision-repeat/summary.json \
  --figures artifacts/planar-precision-repeat/figures
```

Текущий paired collector намеренно требует llvmpipe и прежний NVIDIA reference. Для другого backend/binary/scene это отдельная версия опыта, не переименование device label. Бинарные и shader hashes новых сборок ожидаемо меняются.

## Что остаётся

Малый precision residual теперь измерен отдельно и не требует повторения прежних resolution sweeps. Actual triangle/vertex/clip precision attribution остаётся инженерным follow-up; планарные RGB/bilinear tests — отдельным quality control. Следующий основной исследовательский шаг — соответствие **смещённых target pixels** реальным source surfaces и hybrid level contributions, затем проверяемое уменьшение height/parallax failure. Независимые сцены и физический/Aurora стенд нужны для полного заключения.

Следующий displaced edge-feather source-surface этап выполнен: [[validation/DISPLACED_CORRESPONDENCE]]. Он объясняет foreground occlusion передcarrier и сохраняет ray/RGB mismatches; hybrid matched capture остаётся следующим отдельным опытом.
