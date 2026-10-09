# Image-Quality Oracle, Vehicle Body Mask, and GPU Readback Validation

Date: 09.10.2026. Status: A simplified analytic Scene Truth Oracle and vehicle-body mask exist; GPU readback has a CPU comparison. The temporal tool is only a synthetic texture-shift smoke test: its computed vehicle pose is unused and seam masks come from static geometry weights, so its reported zero displacement is not evidence of dynamic seam stability. E-STITCH seam/ghost metrics also need correction; audit details and remaining acceptance work: [[planning/AUDIT]], [[../TODO]].

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

## 4. Временная стабильность шва и мерцание (`tools/temporal_seam_stability.py`)

```sh
python3 tools/temporal_seam_stability.py \
  --config artifacts/blender-depth-truth-dataset-fixed/config.json \
  --output artifacts/temporal-stability-v1
```

Скрипт выдаёт следующую таблицу на искусственно сдвигаемой текстуре (10 кадров, заданные 2.0 м/с и 30 fps):

| Стратегия Fusion | Смещение шва (mean px) | Смещение шва (p95 px) | Temporal Flicker (Var) |
|---|---:|---:|---:|
| `hard_best_angle` | 0.000 | 0.000 | 0.008336 |
| `edge_feather` | 0.000 | 0.000 | 0.006913 |
| `angular_feather` | 0.000 | 0.000 | 0.007617 |
| `graph_cut_seam` | 0.000 | 0.000 | 0.008337 |
| `multi_band` | 0.000 | 0.000 | 0.006962 |

- Эти значения не оценивают движение камеры или сцены: `T_veh` в коде вычисляется, но не используется, а seam mask получается из статических validity/weight maps. Нулевой сдвиг обусловлен конструкцией теста и не подтверждает временную стабильность fusion.
- Flicker отражает только изменение искусственной синусоидальной текстуры. Сравнение методов по этому числу не подтверждено; нужны реальные последовательные рендеры rig/scene и seam truth: [[../TODO]].
