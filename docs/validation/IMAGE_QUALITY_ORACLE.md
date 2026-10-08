# Image-Quality Oracle, Vehicle Body Mask, and GPU Readback Validation

Date: 09.10.2026. Status: Independent Scene Truth Oracle implemented; vehicle body 3D self-occlusion and footprint mask modeled; GPU readback validated against CPU reference and scene truth; temporal seam stability benchmarked across dynamic trajectories.

## 1. Независимый оракул качества изображения (Scene Truth Oracle)

Инструмент `tools/image_quality_oracle.py` выполняет прямое аналитическое трассирование лучей из виртуальной камеры наблюдателя $V$ на истинную 3D-геометрию сцены:
- Дорожное полотно $Z=0$ с осевой/боковой разметкой и шахматными калибровочными мишенями;
- Приподнятые 3D-препятствия (цилиндрические болларды $h=1.0\text{ м}$, припаркованные автомобили);
- 3D-модель кузова эго-автомобиля (шасси, кабина, боковые зеркала).

### Сравнение сшитого кругового изображения с истинной сценой

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
| **Дорожное полотно (Ground)** | 15.05 | 0.8497 | 0.1154 | 481 004 | Высокая точность геометрии на плоскости $Z=0$ |
| **Препятствия (Vertical)** | 10.08 | 0.4576 | 0.2652 | 2 942 | Физическое радиальное растяжение и параллакс |

**Вывод:** На дорожном полотне проекция на носитель точно восстанавливает геометрию (SSIM $\approx 0.85$). На вертикальных препятствиях (столбиках, бортах машин) SSIM падает до $0.46$, что количественно подтверждает фундаментальное ограничение 2D/3D surface projection без индивидуальной реконструкции высоты объектов (эффект растяжения по лучам).

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

Оценка выполнена на 10-кадровой траектории движения со скоростью $2.0\text{ м/с}$ (30 fps):

| Стратегия Fusion | Смещение шва (mean px) | Смещение шва (p95 px) | Temporal Flicker (Var) |
|---|---:|---:|---:|
| `hard_best_angle` | 0.000 | 0.000 | 0.008336 |
| `edge_feather` | 0.000 | 0.000 | 0.006913 |
| `angular_feather` | 0.000 | 0.000 | 0.007617 |
| `graph_cut_seam` | 0.000 | 0.000 | 0.008337 |
| `multi_band` | 0.000 | 0.000 | 0.006962 |

- **Стабильность:** Статические геометрические границы `edge_feather` и `angular_feather` не испытывают случайных пространственных скачков шва ($\Delta s = 0.00\text{ px}$).
- **Фотометрическое мерцание:** `multi_band` и `edge_feather` минимизируют дисперсию яркости на статичном фоне ($\text{Var} \le 0.0069$).
