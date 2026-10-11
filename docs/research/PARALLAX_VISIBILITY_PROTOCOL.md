# Условная оценка видимой поверхности coded target

E-STITCH-parallax-visibility-01, 11.10.2026. Follow-up [[validation/PARALLAX_HEIGHT]] на тех же240 native outputs: новых запусков, независимых seeds или performance samples нет. Historical full-mask baseline сохраняется; перед вычислением новых критериев воспроизводится прежний audit. Из equality исключаются ровно два code digests: расширенные analyzer и object_metrics.py. Старые и новые digests сохраняются; все прежние числовые результаты, inputs/native hashes, остальные code hashes и protocol digests должны совпасть без tolerance.

## Определение до вычисления результатов

P — RGB chroma prediction, T — independent direct object-ID mask. Обе маски проходят прежний component filter min8 **до** ограничения visibility. V=T∩A, где A — independent any-camera ray visibility. U=T\A игнорируется: prediction внутри U не является ни успехом, ни ложным positive. Маленькие visible fragments не удаляются повторным component filter.

- TP=|P∩V|; FN=|V\P|; FP=|P\T|.
- Conditional IoU=TP/(TP+FP+FN), recall=TP/|V|, precision=TP/(TP+FP).
- Если V пусто, все conditional scores undefined/null независимо от FP; пустое наблюдение не считается успехом. Counts публикуются всегда. Precision undefined при отсутствии оцениваемых predictions.
- Off-target predictions во всём кадре сохраняют FP penalty: ограничение evaluation только положительными visible pixels скрывало бы ложные копии.
- Сохраняются full-mask IoU, visible fraction, ignored prediction count и признак контакта T с границей кадра. Последний выявляет риск clipping, но не гарантирует полноту объекта при false.

A получается из исходного paired truth, не из carrier/output. Она означает видимость direct surface point в хотя бы одной source camera по геометрии экспортера, а не совпадение sampled pixels/цвета или достаточное разрешение. Невозможность восстановить силуэт может включать sampling и projection; критерий не локализует причину до конкретного алгоритма. Natural-object correspondence/ghost recognition не заявляются.

Публикуются все6 cases×2 poses×5 carriers×4 profiles без отбора. Отдельно проверяются known-answer perfect-visible, ignored-only, false-positive, no-visible, fragment-filter и clipping controls. Визуализация показывает все cases на первом pose при dome/multi_band/zero, выбранном до анализа, и полную матрицу метрик по двум poses. Нельзя использовать conditional score как чистый причинный эффект расстояния: fixed view и clipping остаются прежними.
