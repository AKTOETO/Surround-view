# Supplied-box surface visibility: сохранение объекта и граничный отказ

Guard1 на local уменьшает outside198→1 и56→0, повышает full IoU0.215426→0.418994 и0.302632→0.401163, сохраняет75/76 и69/69 source-visible pixels. Road MAE уменьшается, но первый кадр **не проходит заранее заданный строгий preservation gate**: один visible pixel становится unsupported. Guard0 имеет тот же пропуск. Из четырёх local candidate/frame comparisons все четыре критерия одновременно проходят только два (frame1 guard0/1). Это улучшение относительно radial-кандидата, не завершённая general mitigation и не основание заменить default.

## Реализованный контракт

Новый native mode `surface.source_occlusion_box.mode="bilinear_box_surface_v1"` сохраняет центральный sample на видимой поверхности supplied opaque box. Сначала выполняется прежняя central-ray occlusion: скрытая грань никогда не получает exemption. Если central ray не blocked, принадлежность actual carrier point конечной грани box проверяется в фиксированной полосе10µm по L∞ расстоянию. Внутренние точки и продолжения граней вне этой численной полосы exemption не получают. Вне полосы применяется неизменный radial footprint guard0/1. `center` остаётся default.

Для box $B=[b_{min},b_{max}]$ и шести конечных граней $F_k$:

$$S(P)=\left[\min_{k=1}^{6}\inf_{Q\in F_k}\|P-Q\|_\infty\le10^{-5}\,\mathrm m\right].$$

Пусть $O_c$ — прежний central blocking, $O_r$ — radial policy (§4.75). Тогда:

$$O_{box}=O_c\lor\bigl(O_r\land\neg S(P)\bigr).$$

Это bounded known-object support exemption, а не восстановление принадлежности каждого соседнего луча произвольной поверхности. Numerical band10µm независим от configurable endpoint bias20mm. Прежний neighbor inset10µm, zero-distortion equidistant optics, source status7 и ограничения guard0/1 сохранены. Каталог публикует mode и read-only `box_surface_membership_epsilon_m`; переключение через existing full-surface `Client::configure_surface` с revision, сервер владеет config. Отдельных GUI controls для нового режима ещё нет.

## Замороженная матрица

План: `configs/research/box-surface-visibility-plan.json`, native revision `2ca07ca5b4c78d5bdc912d6693ffd601013fc1ac`, plan commit40ddcaf до capture. Два correlated frames × два carrier × шесть policies =24 native outputs на NVIDIA RTX5070Ti. Все16 прежних off/center/radial outputs совпали по RGBA SHA; восемь новых — box-surface guard0/1. Input RGB, independent Blender truth, supplied box и height field прежние. Параметры не подбирались после просмотра результатов.

Для каждого local candidate/frame заранее заданы четыре отдельных gates: сохранить center visible proxy hits; не увеличить visible target без source; уменьшить outside proxy; не увеличить road MAE. Полная таблица и boolean gates: [[validation/baselines/box_surface_visibility_v1.json]].

| Frame | Вариант | Outside | Full IoU | Visible hits | Visible без source | Full target без source | Road MAE |
|---|---|---:|---:|---:|---:|---:|---:|
| 0 | `plane` | 4843 | 0.000000 | 0/76 | 0 | 0 | 0.029184 |
| 0 | `plane_center` | 192 | 0.000000 | 0/76 | 0 | 0 | 0.020990 |
| 0 | `plane_fp0` | 28 | 0.000000 | 0/76 | 0 | 0 | 0.020846 |
| 0 | `plane_fp1` | 0 | 0.000000 | 0/76 | 0 | 0 | 0.020779 |
| 0 | `plane_sf0` | 28 | 0.000000 | 0/76 | 0 | 0 | 0.020846 |
| 0 | `plane_sf1` | 0 | 0.000000 | 0/76 | 0 | 0 | 0.020779 |
| 0 | `local_100` | 4922 | 0.034902 | 76/76 | 0 | 0 | 0.029305 |
| 0 | `local_center` | 198 | 0.215426 | 76/76 | 0 | 78 | 0.020982 |
| 0 | `local_fp0` | 30 | 0.072115 | 12/76 | 64 | 144 | 0.020838 |
| 0 | `local_fp1` | 1 | 0.011173 | 2/76 | 74 | 163 | 0.020771 |
| 0 | `local_sf0` | 30 | 0.375000 | 75/76 | 1 | 81 | 0.020838 |
| 0 | `local_sf1` | 1 | 0.418994 | 75/76 | 1 | 90 | 0.020771 |
| 1 | `plane` | 5373 | 0.000000 | 0/69 | 0 | 0 | 0.030401 |
| 1 | `plane_center` | 51 | 0.000000 | 0/69 | 0 | 0 | 0.020843 |
| 1 | `plane_fp0` | 2 | 0.000000 | 0/69 | 0 | 0 | 0.020799 |
| 1 | `plane_fp1` | 0 | 0.000000 | 0/69 | 0 | 0 | 0.020784 |
| 1 | `plane_sf0` | 2 | 0.000000 | 0/69 | 0 | 0 | 0.020799 |
| 1 | `plane_sf1` | 0 | 0.000000 | 0/69 | 0 | 0 | 0.020784 |
| 1 | `local_100` | 5449 | 0.030600 | 69/69 | 0 | 0 | 0.030615 |
| 1 | `local_center` | 56 | 0.302632 | 69/69 | 0 | 103 | 0.020858 |
| 1 | `local_fp0` | 6 | 0.056180 | 10/69 | 59 | 162 | 0.020813 |
| 1 | `local_fp1` | 0 | 0.011628 | 2/69 | 67 | 170 | 0.020802 |
| 1 | `local_sf0` | 6 | 0.387640 | 69/69 | 0 | 103 | 0.020813 |
| 1 | `local_sf1` | 0 | 0.401163 | 69/69 | 0 | 103 | 0.020802 |

`sf0/sf1` — box-surface guard0/1, `fp0/fp1` — прежний radial. Chroma proxy `min(R,B)-G>0.15` на RGB8/255, без component filtering. Full IoU включает всю target mask178/172 и все outside pixels; visible subset76/69 задан independent truth. Road ROI169447/169513 включает fallback и остаётся фиксированным; covered road138052/138058 неизменен. Это coded-color screen, не segmentation natural objects. Plane не восстанавливает target ни при одном policy: visibility не исправляет неверную carrier surface.

![Native surface preservation RGB](../diploma/figures/experiments/box_surface_visibility_rgb.png)

Рисунок — Actual native RGB: независимый Blender, center, radial guard1 и box-surface guard1. Cyan — независимая target mask. Полезный marker возвращается, скрытые грани не реконструируются.

![Joint surface preservation metrics](../diploma/figures/experiments/box_surface_visibility_metrics.png)

Рисунок — Раздельная оценка outside, full IoU, visible hits и road MAE; два frame и два carrier. Для local наблюдается улучшение, strict preservation первого кадра не выполнен.

## Отдельный post-hoc разбор одного отказа

После primary report выбран единственный visible target pixel, который есть в local_center и отсутствует в local_sf1: frame0, output(y,x)=(211,318), source camera3. Native carrier vehicle/world point=(2.506034374,2.500159502,0.999201894)m; independent scene point=(2.505959511,2.499999762,0.999071896)m. Разность positions0.219137mm; расстояние до ближайшей конечной box face по L∞ —0.159502mm, больше численной полосы0.01mm. Central status[4,3,3,1] становится[4,3,3,7], independent visibility bit8.

Точка не проходит surface membership по объявленному контракту; её прежняя допустимая камера отклоняется radial footprint rule. Это локализованный boundary failure при фактическом расхождении carrier/scene positions; он сам по себе не разделяет mesh/raster/calibration причины. Нельзя назвать его автоматически floating-point error. После обнаружения epsilon не менялся; нового capture с подогнанной полосой нет. Tracked post-hoc данные: [[validation/baselines/box_surface_boundary_v1.json]], отдельно от frozen primary report.

## Проверки и повторение

Independent finite-face membership и six-face segment oracle:0 mismatches на4319565 prior-valid camera-point pairs в20 включённых условиях. Native geometry и retained UV exact;16 historical RGBA matches. Это проверка реализации, не4319565 независимых quality samples. Full build и CTest7/7 прошли, carrier GTest10/10 на Mesa/NVIDIA, independent21/21. Дополнительно проверяется GPU/hybrid validity и обычный/inspection RGBA parity нового policy, visible top preservation, hidden bottom rejection и отсутствие расширения membership до20mm endpoint bias. Повторный audit побитно совпал.

```sh
python3 tests/test_source_correspondence.py
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_box_surface_visibility.py \
  --output-report artifacts/box-surface-visibility-v1/reaudit.json \
  --figures artifacts/box-surface-visibility-v1/reaudit-figures
cmp artifacts/box-surface-visibility-v1/reaudit.json docs/validation/baselines/box_surface_visibility_v1.json
```

Raw products, archived native binary и точные24 argv: `artifacts/box-surface-visibility-v1`; frozen configs: `artifacts/box-surface-visibility-configs-v1`. План и JSON фиксируют binary/source/config/input/product hashes и native source fingerprint. Raw artifacts не входят в Git. Audit требует указанных входов; новые revision запускать в отдельном output root. Native capture исполняется `sv-correspondence-probe CONFIG MANIFEST OUTPUT FRAME` (подробности [[engineering/USAGE]]); Python только анализирует результат.

## Следующий этап

Выполнить отдельный заранее заданный paired sweep endpoint bias и geometry uncertainty с preservation/unsupported/road controls; отделить численную полосу от физической погрешности supplied bounds/height field. Не повышать epsilon для единственного выбранного pixel без проверки ложных exemptions на road, interior и hidden faces. Далее поддержка произвольных поверхностей/реалистичного depth, independent scene families и natural-object holdout, равный mesh/memory budget и overhead, физический/Aurora стенд. Верхняя face известного box не заменяет полную source-surface correspondence. Приоритет1 остаётся открытым.
