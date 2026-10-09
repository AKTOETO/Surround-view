# Image-Quality Oracle, Vehicle Body Mask, and GPU Readback Validation

Date: 10.10.2026. Status: A simplified analytic Scene Truth Oracle and vehicle-body mask exist; GPU readback has a CPU comparison. The old synthetic temporal test has been replaced with matched Blender captures and known-answer controls; see [[PAIRED_STITCH_TEMPORAL]]. One short clip validates the measurement workflow, not temporal quality across scenes. The legacy E-STITCH seam/ghost metrics were incorrect; v2 now computes output-based proxies, but they still lack independent object/edge truth and do not close image-quality validation. Results: [[E_STITCH_01_V2]], audit and remaining acceptance work: [[planning/AUDIT]], [[../TODO]].

## 1. Аналитический oracle прототипа и ограничение Scene Truth

Инструмент `tools/image_quality_oracle.py` выполняет прямое аналитическое трассирование лучей из виртуальной камеры наблюдателя $V$ на геометрию упрощённой сцены, заданной в коде:
- Дорожное полотно $Z=0$ с осевой/боковой разметкой и шахматными калибровочными мишенями;
- Приподнятые 3D-препятствия (цилиндрические болларды $h=1.0\text{ м}$, припаркованные автомобили);
- 3D-модель кузова эго-автомобиля (шасси, кабина, боковые зеркала).

### Исходная попытка сравнения с Blender street capture

**Эти PSNR/SSIM/MAE числа не являются валидной Scene Truth оценкой.** `render_scene_oracle()` задаёт упрощённую аналитическую дорогу и собственную разметку (например, центр и боковые линии), тогда как RGB кадры взяты из Blender street scene с иной геометрией и расположением разметки. Поэтому oracle RGB и физически снятые для этого fixture RGB не описывают идентичную сцену/материал. Повторить оценку можно только после общей параметризации/экспорта exact geometry, pose, object IDs и materials; иначе оставить этот результат как smoke работы метрик и не трактовать его как качество восстановления.

### Сравнение сшитого кругового изображения с текущим analytic fixture

```sh
python3 tools/validate_gpu_readback.py \
  --config artifacts/blender-depth-truth-dataset-fixed/config.json \
  --dataset artifacts/blender-depth-truth-dataset-fixed \
  --binary build/sv-bench \
  --output artifacts/image-quality-oracle-v1
```

| Область | PSNR, dB | SSIM | MAE | Пиксели ROI | Интерпретация |
|---|---:|---:|---:|---:|---|
| **Overall (без кузова)** | 15.00 | 0.8473 | 0.1163 | 483 946 | Общая согласованность круговой панорамы |
| **Дорожное полотно (Ground)** | 15.05 | 0.8497 | 0.1154 | 481 004 | Сравнение ground ROI в analytic fixture; RGB ground truth не согласован с Blender улицей |
| **Препятствия (Vertical)** | 10.08 | 0.4576 | 0.2652 | 2 942 | Аналитическая модель препятствий; не физическая оценка параллакса |

**Ограниченный результат:** В этой упрощённой аналитической сцене ground ROI даёт SSIM $\approx 0.85$, а ROI вертикальных препятствий — около $0.46$. Это полезная проверка конкретного fixture, но не универсальная мера качества на реальной сцене: цвета/геометрия oracle синтетические, а test matrix ограничена.

---

## 2. 3D-маска кузова и самозатенение (`tools/body_mask.py`)

1. **Геометрическая модель:** Кузов автомобиля декомпозирован на 4 ориентированных 3D-бокса: нижнее шасси ($4.6 \times 1.8 \times 0.7\text{ м}$), остекленная кабина ($2.0 \times 1.53 \times 0.65\text{ м}$) и левое/правое боковые зеркала ($0.20 \times 0.16 \times 0.12\text{ м}$).
2. **Самозатенение камер:** Функция `compute_camera_self_occlusion` проверяет луч от оптического центра камеры до 3D-точки носителя. Если луч пересекает кузов эго-автомобиля до достижения поверхности, точка помечается как затененная кузовом ($M_{body}=0$).
3. **Footprint маска:** Область основания автомобиля исключается из статистики ошибки и заменяется 3D-моделью кузова.

---

## 3. Валидация GPU Readback против CPU Reference

Сравнение кадра, отрендеренного на GPU (`sv-bench` / EGL Surfaceless GLES 3.0), с эталонным CPU-рендером (`tools/reference.py`):
- **PSNR (GPU vs CPU):** $23.49\text{ dB}$
- **SSIM (GPU vs CPU):** $0.9664$
- **Среднее расхождение:** $3.72\text{ LSB}$ (по 8-битным цветовым каналам)
- **Согласованность маски:** 100% совпадение границ видимости.

Небольшое расхождение в пределах нескольких единиц младшего разряда обусловлено аппаратной билинейной интерполяцией текстурных координат и 32-битной float-арифметикой GPU fragment shader.

---

## 4. Парная временная проверка

Старый sine-shift тест и его нулевые centroid shifts удалены: рассчитанная поза автомобиля в нём не применялась. Текущий инструмент принимает фактические последовательности и matched direct Blender RGB/object-ID truth. Команды, новые числа и ограничения: [[PAIRED_STITCH_TEMPORAL]]. Аналитический `render_scene_oracle()` выше сохранён для отдельных synthetic tests; он по-прежнему не является эталоном Blender street.

```sh
python3 tools/temporal_seam_stability.py \
  --dataset tests/data/paired_street_v1 \
  --capture tests/data/paired_street_v1 \
  --output artifacts/paired-temporal-repeat
```

В трёхкадровой серии машина действительно перемещается на 0.4 м за переход. Graph-cut boundary symmetric distance — 0.09161 / 0.04814 px. Feather weights остаются неподвижными в vehicle-fixed view, но RGB меняется. Измерение residual change вычитает совпадающий прямой вид сцены; без optical flow оно включает изменение геометрической/окклюзионной ошибки и не считается чистым flicker. Independent RGB extra edges не являются object-level ghost rate. Нужны holdout clips, matched visibility, динамические объекты и фотометрические возмущения.

## 5. Source visibility и контроль разрешения

Продолжение: [[STITCH_RESOLUTION_VISIBILITY]]. Paired schema 2 исключает hide-render helper meshes из ray casts, экспортирует camera-visibility bits для прямых scene points и позволяет any-camera ROI. В первой paired schema 1 эти helpers могли влиять на object-ID карты; её результаты сохраняются как исторический smoke. Числа текущей серии с исправленными картами нельзя напрямую сравнивать со старым ROI.
