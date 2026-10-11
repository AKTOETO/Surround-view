# Покрытие кадра геометрией носителя

E-STITCH-carrier-coverage-01, 11.10.2026. Frozen plan: `configs/research/carrier-coverage-plan.json`. Цель — проверить, какую часть носителя действительно исследовали прежние RGB-опыты. Геометрическая разметка не зависит от изображения камер и не является scene truth.

## Native измерение

`Renderer::render(..., RenderInspection*)` выполняет дополнительный проход теми же VAO/индексами, MVP, viewport, depth buffer format и GL_LESS. Carrier и модель автомобиля рисуются заново с очищенной глубиной. Fragment shader выдаёт ID, dithering отключён, RGBA8 readback переворачивается в top-left CV_8UC1. Обычный output уже вычислен и не изменяется. Проход существует только при явном inspection; default server render его не выполняет. Это локальный render-thread API, **не** SV01 subscription.

| ID | Имя | Определение видимого фрагмента |
|---:|---|---|
| 0 | background | Геометрия не закрыла пиксель |
| 1 | floor | Floor mesh либо rectangular carrier с интерполированным world.z ≤ 1e−6 m |
| 2 | shell | Shell mesh купола, цилиндра или куба, включая потолок |
| 3 | vehicle_footprint | Carrier-фрагмент внутри server vehicle mask с margin |
| 4 | raised_bowl | Rectangular carrier с world.z > 1e−6 m вне footprint |
| 5 | vehicle_model | Видимая модель автомобиля, прошедшая depth test |

ID4 означает растеризованную высоту **носителя**, не высоту реального объекта. Граница плоского участка определяется фактической триангуляцией и интерполяцией z, поэтому её нельзя подменять аналитическим прямоугольником flat_half_length/width. ID2 не разделяет стены/потолок и не показывает число отдельных треугольников. Vehicle model включена даже при diagnostic=coverage/weights: это геометрическая проверка одного и того же кадра, а не разметка соответствующего diagnostic output.

## Матрица

Пять неизменённых medium meshes из [[validation/CARRIER_BUDGET]], исходные config.json seed101 закреплены SHA256. Размер320×180, distance8.5m, azimuth0.8rad, clip0.1…100m. Три ракурса: historical elevation1/fov1rad; lateral elevation0.35/fov1.6rad; overhead elevationπ/2/fov1rad. Боковой elevation выбран на нижней допустимой границе production config; положение проходит штатную проверку safe_view.

`sv-carrier-probe` — скомпилированный исследовательский probe над production Renderer. Камерных кадров нет: измеряется геометрия, а не доступность текстур. Для15 masks проверяются полный partition raster, checksums, единый source fingerprint и побитовое совпадение обычного RGBA до/после inspection. Для historical masks дополнительно вычисляются30 пересечений с прежними any-camera interior ROIs шести кадров seed101–103. `load_sequence` проверяет input/truth provenance; vehicle/view/output обязаны совпадать. Сохраняются hashes ROI, paired_truth и raw geometry masks. Повторное использование геометрии между seeds допустимо, поскольку mounting и scene objects не определяют carrier/view. Новые RGB/Blender truth для lateral/overhead здесь не создаются.

## Проверки и ограничения

C++ known-answer controls: плоскость имеет floor/background и не имеет shell/raised; все три enclosure имеют shell/floor и не имеют background; ego видим; после plane→bowl появляются raised pixels; inspection не меняет обычный RGBA. Existing fusion probe сохраняет carrier mask и проверяет RGBA parity на всех его hybrid mode/boundary/diagnostic условиях.

Доля пикселей зависит от ракурса, разрешения и растеризации. Это не площадь поверхности, triangle-hit coverage, source-camera coverage, физическая глубина или качество реконструкции. Нет независимого аналитического oracle для каждого контура. Тайминги штатного Renderer исключают дополнительный inspection pass, но wall-clock вызова с inspection включает его; такие вызовы нельзя использовать как обычные performance trials. Итоги: [[validation/CARRIER_COVERAGE]].
