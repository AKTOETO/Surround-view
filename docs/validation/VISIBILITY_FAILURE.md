# Разделение visibility failures и ненаблюдаемых поверхностей

Это post-hoc exploratory audit всех74 local outputs прежнего76-case native опыта. Он завершает разбор stage-level потерь и обосновывает требования к unsupported policy; саму новую политику отображения/сетевой diagnostic channel не реализует.

## План и классификатор

План `configs/research/visibility-failure-plan.json` зафиксирован commit47dff5a до расчёта категорий, но **после** получения primary aggregate results. Primary JSON/config/product hashes проверяются; code `tools/research/visibility_failure.py`, plot script рядом с главой. Описывать этот audit как подтверждающую независимую preregistered проверку нельзя.

Для каждого independent source-visible target pixel без chroma proxy классификатор проверяет native source statuses относительно local-off geometry eligibility и independent central segment predicate. Четыре непересекающихся класса: (1) baseline samples недоступны; (2) все baseline-eligible sources отклонены central blocking; (3) после central test источники ещё допустимы, но после footprint filtering не остаётся samples; (4) хотя бы один native sample допустим, но output proxy отсутствует. В третьем классе возможна смесь central/footprint rejection между камерами: он не означает, что central blocking отсутствует совсем.

Source eligibility здесь не равна положительному blending weight или соответствию той же scene surface. Классификация локализует stage-level отказ по всем eligible cameras, а не восстанавливает все физические причины. Geometry/retained UV parity и0 oracle mismatches проверены в primary [[validation/VISIBILITY_SENSITIVITY]]. Все74 local outputs относятся к edge_feather (default при omission fusion); выводы о стадиях не переносить автоматически на signed nonlocal multiband/graph-cut paths. Новых native captures и изменений алгоритмов нет.

## Результаты

В54 surface outputs обнаружено903 visible-loss memberships:600 all-central и303 remaining-footprint; baseline-unavailable0 и source-present/proxy-missing0. Они относятся к145 unique(frame,y,x) coordinates на двух correlated frames, не к903 независимым pixels. В18 center controls —200 central losses,0 footprint, как требует контракт. Два off controls потерь не имеют. Полный74-row отчёт хранит coordinates/category каждого visible loss, а не только выбранные примеры.

Для Z+5mm/support10mm остаются71/65 central losses; расширение support band не может их снять. При nominal bias0 все27/36 потерь также central. Следовательно, одинаковое исчезновение marker требует разных исправлений: membership/filter guard не исправляет central occluder misplacement. Причины geometry discrepancy внутри central stage (mesh, pose, bounds, projection/raster) этим audit не разделены.

Independent full target178/172 разделён на visible76/69 и hidden102/103. Off output окрашивает все102/103 hidden pixels; center frame0 —5; nominal1mm profile —0/0. Это color proxy без наблюдения соответствующей target surface; uniform marker color не позволяет по одному RGB совпадению доказать point/surface correspondence. В nominal1mm profile13/0 hidden pixels имеют хоть какой-то geometrically eligible sample,89/103 не имеют ни одного. Наличие sample для hidden target не делает его наблюдаемым: он может проецироваться на другую поверхность.

| Frame | Вариант | Visible hits | Central loss | Footprint loss | Hidden pixels | Hidden proxy hits | Hidden source absent |
|---|---|---:|---:|---:|---:|---:|---:|
| 0 | `local_100` | 76/76 | 0 | 0 | 102 | 102 | 0 |
| 0 | `c_nom_b020` | 76/76 | 0 | 0 | 102 | 5 | 78 |
| 0 | `s_nom_b020_e1mm` | 76/76 | 0 | 0 | 102 | 0 | 89 |
| 0 | `s_nom_b000_e1mm` | 49/76 | 27 | 0 | 102 | 0 | 89 |
| 0 | `s_zp_b020_e10mm` | 5/76 | 71 | 0 | 102 | 0 | 89 |
| 0 | `s_zm_b020_e10mm` | 76/76 | 0 | 0 | 102 | 2 | 87 |
| 1 | `local_100` | 69/69 | 0 | 0 | 103 | 103 | 0 |
| 1 | `c_nom_b020` | 69/69 | 0 | 0 | 103 | 0 | 103 |
| 1 | `s_nom_b020_e1mm` | 69/69 | 0 | 0 | 103 | 0 | 103 |
| 1 | `s_nom_b000_e1mm` | 33/69 | 36 | 0 | 103 | 0 | 103 |
| 1 | `s_zp_b020_e10mm` | 4/69 | 65 | 0 | 103 | 0 | 103 |
| 1 | `s_zm_b020_e10mm` | 69/69 | 0 | 0 | 103 | 1 | 102 |

Таблица — Выбранные controls из полного74-row отчёта. Hidden source absent — отсутствие любого допустимого native sample; hidden label — независимая видимость scene point. Это разные признаки.

![Stage-level loss partition](../diploma/figures/experiments/visibility_failure_causes.png)

Рисунок — Видимые потери разделены на central и remaining-footprint; показаны bias grid и geometry grid при support1mm. Другие варианты включены в полный отчёт.

![Independent visible and hidden target](../diploma/figures/experiments/visibility_failure_observability.png)

Рисунок — Full target разложен на visible/hidden и proxy hit/miss. Окрашенный hidden subset в off режиме не является доказательством восстановления скрытой поверхности.

## Условная граница full proxy IoU

При **явно ограниченной модели** $\widehat T\subseteq T\cap V$ (предсказывать marker только на independent source-visible target), отсутствие outside даёт условный proxy ceiling:

$$IoU_{visible\ only}\le\frac{|T\cap V|}{|T|}=\frac{76}{178}=0.426966\quad\text{или}\quad\frac{69}{172}=0.401163.$$

Если все visible pixels сохранены и outside $g$, фактический score равен $|T\cap V|/(|T|+g)$. Nominal1mm/bias20mm даёт76/179=0.424581 и69/172=0.401163. Это **не универсальная upper bound** для reconstruction, priors, temporal models или повторения одинакового цвета на скрытой грани. Она условна относительно запрета hidden predictions и известной truth visibility. Малая full IoU не всегда является потерей видимой части, а высокая hidden recall не доказывает физически правильной реконструкции. Natural appearance и correspondence должны проверяться отдельно.

## Требования к обработке отсутствующих данных

Проект политики: [[architecture/UNSUPPORTED_SURFACES]]. Текущий final RGBA содержит opaque fallback; diagnostic coverage показывает количество допустимых camera samples, а не scene observability/confidence. Independent target visibility mask доступна только в опыте, не как готовая production оценка.

Следующий native шаг — отдельные validity/positive-weight diagnostic products с явной unsupported/fallback семантикой через server/sv-client-lib; не объявлять неизвестные пиксели достоверно восстановленными. Geometry uncertainty требует отдельного решения central occlusion; заливка неизвестной области не исправляет correspondence. Temporal fill/prior reconstruction требуют отдельной маркировки, temporal/occlusion controls и validation. Full IoU сохранять вместе с visible recall, hidden proxy count, outside и road error.

## Проверки и повторение

Independent23/23, CTest source_correspondence1/1 успешны; категории exhaustive/disjoint, invalid shape и создание новых eligible samples отвергаются. Для каждой строки сумма categories равна primary visible losses; center никогда не получает footprint loss. Native source/report/product hashes проверены; second audit JSON побитно совпал. Дополнительный native build не нужен: product code не менялся в этом audit.

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R source_correspondence --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_visibility_failure.py \
  --output-report artifacts/visibility-failure-v1/reaudit.json \
  --figures artifacts/visibility-failure-v1/reaudit-figures
cmp artifacts/visibility-failure-v1/reaudit.json docs/validation/baselines/visibility_failure_v1.json
```

Tracked JSON: [[validation/baselines/visibility_failure_v1.json]]. Raw primary products остаются в `artifacts/visibility-sensitivity-v1`; post-hoc reports — `artifacts/visibility-failure-v1`. Raw не входят в Git; повторение требует этих inputs. Один known target/два кадра, constant coded color и отсутствие holdout остаются ограничениями. General reconstruction/uncertainty mitigation и аппаратная приёмка не закрыты.

Продолжение: [[validation/VISIBILITY_UNCERTAINTY]] проверяет bounded central uncertainty отдельно на18 center controls; native local-support mask и wire delivery реализованы последующими этапами [[validation/RENDER_SUPPORT]], [[validation/OUTPUT_PRODUCTS]]. Исторические результаты этого audit не пересчитаны.
