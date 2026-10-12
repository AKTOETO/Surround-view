# Допуски visibility и погрешность supplied geometry

При точной supplied geometry найден nominal profile, сохраняющий весь source-visible subset при outside1/0. Однако translation uncertainty выявляет failures и рост ошибочно принятых samples; устойчивого универсального профиля не получено.

## Native runtime contract

Сервер принимает optional `source_occlusion_box.support_tolerance_m` только для `bilinear_box_surface_v1`: диапазон[0,0.05]m, default0.00001m. Он меняет полосу принадлежности actual carrier point конечной грани supplied box. Это отдельный параметр от endpoint bias `tolerance_m`; соседний inset10µm не меняется. `center` остаётся default, omission нового поля воспроизводит прежний box-surface output.

$$S_\epsilon(P)=[\min_k\inf_{Q\in F_k}\|P-Q\|_\infty\le\epsilon],\qquad O=O_c(b)\lor(O_r(b)\land\neg S_\epsilon(P)).$$

Support band $\epsilon$ не отменяет central blocking $O_c$ с bias $b$. Увеличение полосы может сохранить неверную поверхность; это явно заданная гипотеза о геометрической погрешности, не её автоматическая оценка. Поле проходит server validation и revision-aware full-surface `Client::configure_surface`; catalog публикует `default_support_tolerance_m` и `support_tolerance_range_m` вместо прежнего fixed-band metadata key. Для other modes поле отвергается как неприменимое. GUI-форма остаётся задачей.

## План и независимые критерии

Frozen matrix:76 native conditions на NVIDIA RTX5070Ti, native revision `7043c6157644b807c085371b9b46bc8a034aacf5`, plan commit2375bc9 до capture. План `configs/research/visibility-sensitivity-plan.json`: два прежних correlated frames; nominal bias0/2/20mm × support10µm/1mm/10mm и matched centers; шесть translations±5mm по vehicle X/Y/Z при bias20mm × три bands и matched centers; plane/local off controls. Это **не полный совместный factorial**: shifted geometry не пересекалась с bias0/2mm. RGB, cameras, actual carrier, independent Blender truth и размеры box не менялись. Perturbation относится к предоставленной серверу геометрии, не к новому физическому capture.

Полосы выбраны после прежнего boundary failure: исследование developmental, не independent holdout. Внутри frozen серии параметры не подгонялись. Восемь historical off/center/default-band RGBA hashes совпали. Полный JSON с76 rows, configs, hashes, oracle counts и gates: [[validation/baselines/visibility_sensitivity_v1.json]].

Hits — chroma proxy на independent source-visible target subset76/69; outside — proxy вне полной target mask178/172. Threshold `min(R,B)-G>0.15` применяется к RGB8/255, без component filtering. Road MAE в linear RGB считается на прежнем фиксированном ROI169447/169513, включая fallback. Coverage138052/138058 неизменна. Proxy не является segmentation natural objects.

Пять заранее заданных gates проверяются раздельно: сохранить **все**76/69 hits, не иметь visible target без source, outside ниже nominal center, road MAE не выше nominal center, не увеличить nominal-blocked retained camera-point pairs. Последний diagnostic использует nominal box на actual carrier positions и fixed reference bias2mm независимо от configured box/bias. Это known-box predicate, не полная scene-depth truth. Accepted pairs не равны положительно взвешенным fusion contributions. Отдельно записаны accepted target-camera pairs без independent truth visibility и supported road pairs; они показывают риск неверного surface association, а не доказывают итоговый вклад в RGB.

## Nominal tolerance screen

| Frame | Bias, mm | Center hits / outside | Band10µm hits / outside | Band1mm hits / outside | Band10mm hits / outside |
|---|---:|---:|---:|---:|---:|
| 0 | 0 | 49 / 197 | 49 / 0 | 49 / 0 | 49 / 1 |
| 0 | 2 | 75 / 197 | 75 / 0 | 75 / 0 | 75 / 1 |
| 0 | 20 | 76 / 198 | 75 / 1 | 76 / 1 | 76 / 2 |
| 1 | 0 | 33 / 56 | 33 / 0 | 33 / 0 | 33 / 0 |
| 1 | 2 | 69 / 56 | 69 / 0 | 69 / 0 | 69 / 0 |
| 1 | 20 | 69 / 56 | 69 / 0 | 69 / 0 | 69 / 0 |

Таблица — Hits/outside для каждой пары допусков; denominators76/69. Одинаковое число outside не означает одинаковый RGB/полный target: подробные IoU/MAE/unsupported counts есть в JSON.

![Nominal tolerance matrix](../diploma/figures/experiments/visibility_sensitivity_nominal.png)

Рисунок — Native nominal sensitivity: hits и outside показаны отдельно для каждого frame/bias/band.

## Translation uncertainty

| Frame | Shift | Center hits / outside | Band10µm hits / outside | Band1mm hits / outside | Band10mm hits / outside | Nominal-blocked retained, band10mm |
|---|---|---:|---:|---:|---:|---:|
| 0 | nom | 76 / 198 | 75 / 1 | 76 / 1 | 76 / 2 | 3 |
| 0 | xp | 76 / 214 | 75 / 1 | 76 / 1 | 76 / 2 | 3 |
| 0 | xm | 76 / 190 | 73 / 1 | 76 / 1 | 76 / 1 | 3 |
| 0 | yp | 76 / 255 | 74 / 1 | 75 / 1 | 76 / 2 | 3 |
| 0 | ym | 76 / 166 | 75 / 1 | 75 / 1 | 76 / 1 | 1 |
| 0 | zp | 5 / 477 | 2 / 2 | 2 / 2 | 5 / 3 | 2 |
| 0 | zm | 76 / 34 | 2 / 1 | 2 / 1 | 76 / 2 | 7 |
| 1 | nom | 69 / 56 | 69 / 0 | 69 / 0 | 69 / 0 | 0 |
| 1 | xp | 69 / 83 | 69 / 0 | 69 / 0 | 69 / 0 | 0 |
| 1 | xm | 69 / 61 | 68 / 0 | 68 / 0 | 69 / 0 | 0 |
| 1 | yp | 69 / 92 | 69 / 0 | 69 / 0 | 69 / 0 | 0 |
| 1 | ym | 69 / 53 | 68 / 0 | 69 / 0 | 69 / 0 | 0 |
| 1 | zp | 4 / 104 | 2 / 0 | 2 / 0 | 4 / 0 | 0 |
| 1 | zm | 69 / 44 | 3 / 0 | 3 / 0 | 69 / 0 | 1 |

Таблица — Bias20mm, box-only shifts; xp/xm =±5mm по X, yp/ym по Y, zp/zm по Z в vehicle frame. Reference-blocked counts сравниваются с nominal center3/0, не со смещённым center.

![Supplied-box translation sensitivity](../diploma/figures/experiments/visibility_sensitivity_geometry.png)

Рисунок — Поддержка видимого объекта и false-color proxy при неизменных inputs/truth и сдвинутом supplied box.

![Native target detail](../diploma/figures/experiments/visibility_sensitivity_rgb.png)

Рисунок — Actual native RGB крупным планом, nominal bias20mm. Cyan — full target contour; отсутствующие грани не дорисованы. Сохранение76/69 visible pixels не равнозначно заполнению178/172 target pixels.

## Выводы

При nominal box, bias20mm и support1mm восстановлены76/76 и69/69 visible hits, outside1/0, full IoU0.424581/0.401163. Прежний boundary pixel возвращается; road MAE0.020771376/0.020801917 против center0.020982273/0.020858322. Но unsupported полный target89/103 остаётся: сохранение видимой части не означает восстановления невидимых граней.

Support10mm не улучшает visible hits относительно1mm, а в frame0 увеличивает outside1→2 и reference-blocked retained pairs2→3; supported road pairs0→4 (frame1 0→1). Меньшая road MAE при этом не отменяет лишнего marker. При bias0 visible hits даже center падают до49/76 и33/69; при bias2mm —75/76 и69/69. Повышение support band не может восстановить sample, уже отклонённый central predicate. Independent oracle согласован с этим поведением; float/shader bug из данного результата не следует.

Geometry shift Z+5mm снижает center hits до5/76 и4/69; box-surface support10mm сохраняет те же5/76 и4/69. Увеличение support band не исправляет ошибочный central blocking. Z−5mm с support10mm возвращает76/76 и69/69, но reference-blocked retained pairs7/1 превышают nominal center3/0; одновременно accepted target-camera pairs без independent visibility17/1 против13/0 у nominal1mm. Такой вариант не проходит gates. Для некоторых горизонтальных смещений широкая полоса помогает, но это не общая устойчивость.

Все пять gates проходят23/54 surface outputs; одновременно для обоих кадров —7/27 пар условий. Ни одна проверенная полоса не проходит оба направления Z uncertainty. Default не меняется. Это проверенный nominal candidate profile, **не подтверждённая robust/general mitigation**.

## Проверки и повторение

Native geometry и retained source UV exact для всех local условий; independent six-face/membership oracle0 mismatches на15985665 prior-valid camera-point pairs, включая bias0. Все mismatch counts сохранены, заранее разрешённого исключения stress cases нет. Это численная проверка predicate, не15.99млн независимых quality observations. Full build и CTest7/7 успешны, carrier GTest10/10 на Mesa/NVIDIA, independent22/22; runtime band recovery, hidden-bottom rejection, no extra source uploads, hybrid/inspection parity и restoration проверены. Второй audit побитно совпал.

```sh
python3 tests/test_source_correspondence.py
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_visibility_sensitivity.py \
  --output-report artifacts/visibility-sensitivity-v1/reaudit.json \
  --figures artifacts/visibility-sensitivity-v1/reaudit-figures
cmp artifacts/visibility-sensitivity-v1/reaudit.json docs/validation/baselines/visibility_sensitivity_v1.json
```

Raw configs: `artifacts/visibility-sensitivity-configs-v1`; archived binary,76 native argv, products и invocation status: `artifacts/visibility-sensitivity-v1`. Frozen source/config/input hashes находятся в плане; report содержит output hashes и native fingerprint. Raw artifacts не входят в Git, audit требует указанных inputs. Native CLI: `sv-correspondence-probe CONFIG MANIFEST OUTPUT FRAME`; Python — independent analysis/figures. Новая revision требует нового плана/output root.

## Что остаётся

Дальше разделить sources of failure: центральное перекрытие при неверных bounds, surface membership discrepancy, отсутствие наблюдаемого source для части target, filter contamination. Нужны явная uncertainty/unsupported policy, separate oracle upper bound и проверка, какие surfaces реально можно восстановить из доступных данных. Полосу1mm нельзя объявить выбранной для продукта по двум уже исследованным кадрам; необходимы независимые geometry/mount/scene seeds, rotations/scales/height-field errors, full interaction design, natural objects и source-surface truth, равные бюджеты/overhead. Physical/Aurora acceptance остаётся открытой. Завершён sensitivity screen, general mitigation и приоритет1 не закрыты.

§4.78: post-hoc exhaustive audit74 local outputs,54 surface outputs имеют600 central/303 remaining-footprint loss memberships на145 unique frame/pixels;18 center controls200 central/0 footprint. Hidden target102/103: off marker hits102/103 не доказывают восстановления скрытых поверхностей. Nominal1mm hits76/69 при IoU0.424581/0.401163 близки к условному visible-only ceiling0.426966/0.401163 (не universal bound). Independent23/23, CTest1/1, reaudit identical; native code не менялся. Проект unsupported/support diagnostics ещё требует native реализации: [[architecture/UNSUPPORTED_SURFACES]], [[validation/VISIBILITY_FAILURE]].
