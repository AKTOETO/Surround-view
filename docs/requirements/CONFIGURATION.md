# Требования к конфигурации и калибровке

## Назначение и статус

Рабочая редакция от 06.10.2026; основа от 19.09.2026. Конфигурация задаёт автомобиль, четыре камеры, проекцию, поверхность, источники, метрики и ограничения ресурсов без перекомпиляции. Это проектная схема; пример ниже синтаксически полный, его данные синтетические, а файлы набора должны быть подготовлены на M2. Формулы и смысл матриц — [MATHEMATICS.md](../architecture/MATHEMATICS.md), решения — [[architecture/DECISIONS|ADR]], приёмка — [ACCEPTANCE.md](../validation/ACCEPTANCE.md). Модель камеры соответствует OpenCV [[references/VISION#S04|S04]].

## Требования

| ID | Приоритет | Условие и наблюдаемое поведение | Приёмка | Задача |
|---|---|---|---|---|
| CFG-F-001 | MUST | schema_version явна; неизвестная версия отклоняется. | T-CFG-001: версия в effective config и причина отказа. | R2 |
| CFG-F-002 | MUST | Размеры автомобиля, начало V и маска заданы в метрах. | T-CFG-002: три конфигурации, маска внутри плоской зоны. | R2, R4 |
| CFG-F-003 | MUST | Ровно четыре уникальные камеры задают resolution/format и T_camera_from_vehicle. | T-CFG-003: RᵀR/det, ID, units, stride и преобразование известных точек. | R2 |
| CFG-F-004 | MUST | Модель opencv_fisheye имеет fx/fy/cx/cy, k1…k4, alpha=0, разрешение и область валидности; происхождение калибровки сохранено. | T-CFG-004: коэффициенты, угловая область, resize/crop и независимые наблюдения. | R2 |
| CFG-F-005 | MUST | Выбранный реализованный тип поверхности (`rectangular_bowl_v1` либо `dome_floor_v1`), параметры сетки, лимиты и область виртуальных ракурсов заданы данными и строго проверяются. Plane доступен в benchmark-варианте. Cylinder/cube/Burger и новые правила слияния пока не являются типами текущей config schema. Для купола виртуальная камера обязана находиться внутри сферы. | T-CFG-005: изменение bowl/купол-параметров, topology и E-MESH-01; неизвестный метод и camera distance ≥ радиуса отклонены. Кандидаты будущего расширения оцениваются в E-STITCH-01 до включения в schema. | R4 |
| CFG-F-006 | MUST | Timeouts, окно skew, max age, политика устаревания и очереди имеют явные пределы. | T-CFG-006: политика наблюдаема в E-ROBUST-01 и effective config. | R5 |
| CFG-F-007 | MUST | Метры/радианы/наносекунды и порядок матриц определены схемой; NaN/Inf не допускаются. | T-CFG-007: неизвестные units, отрицательные размеры и вырожденные преобразования отклонены. | R2 |
| CFG-F-008 | MUST | Полная валидация выполняется до pipeline с путём поля и причиной. | T-CFG-008: неверные параметры поверхности/камер, отсутствующие данные, несовместимые профили. | R2, R5 |
| CFG-F-009 | MUST | Конфигурация содержит только описательные поля; нет исполняемого кода/shader source. | T-CFG-009: неизвестные поля отклонены, код не выполняется. | R5 |
| CFG-F-010 | MUST | Эффективная конфигурация после defaults сохраняется с хэшем в каждом опыте. | T-CFG-010: печать/экспорт, повтор с тем же effective config. | R6 |
| CFG-F-011 | MUST | Калибровка связывает параметры с наблюдениями и отчётом; диагностика задаёт версию метода, пороги и область пригодности. | T-CFG-011: missing report, неверная версия и непригодные условия; схема расширяется до реализации CAL-F-004/005. | R2, R4 |

## Полный численный пример

Четыре синтетические камеры смотрят наружу и вниз на 30°. Позиции центров: front=(2,3;0;1,2), right=(0;−0,9;1,2), rear=(−2,3;0;1,2), left=(0;0,9;1,2) м. Матрицы ниже сразу переводят V → C; параметры не являются калибровкой реального автомобиля. `mask: full_image` допустима только для этого идеализированного набора. Сам пример не подтверждает достижение 99% покрытия ROI или performance-целей.

```yaml
schema_version: 1
profile_id: local-reference-draft
units: {length: m, angle: rad, time: ns}
vehicle:
  dimensions_m: {length: 4.6, width: 1.8, height: 1.5}
  origin: footprint_center_on_ground
  axes: {x: forward, y: left, z: up}
  mask_margin_m: 0.1
cameras:
  - id: 0
    name: front
    calibration_id: synthetic-front-v1
    calibration_origin: analytic_fixture
    calibration_residual_px: null
    resolution: {width: 1280, height: 720}
    calibration_resolution: {width: 1280, height: 720}
    image_transform: {resize: [1.0, 1.0], crop_xy: [0, 0]}
    image: {pixel_format: RGB8, stride_bytes: 3840, row_origin: top_left, color_space: sRGB, transfer: sRGB, mask: full_image}
    projection: {model: opencv_fisheye, fx: 400.0, fy: 400.0, cx: 639.5, cy: 359.5, alpha: 0.0, k: [0.0, 0.0, 0.0, 0.0], theta_max_rad: 1.45, z_epsilon_m: 0.000001}
    T_camera_from_vehicle:
      - [0.0, -1.0, 0.0, 0.0]
      - [-0.5, 0.0, -0.866025403784, 2.189230484541]
      - [0.866025403784, 0.0, -0.5, -1.391858428704]
      - [0.0, 0.0, 0.0, 1.0]
  - id: 1
    name: right
    calibration_id: synthetic-right-v1
    calibration_origin: analytic_fixture
    calibration_residual_px: null
    resolution: {width: 1280, height: 720}
    calibration_resolution: {width: 1280, height: 720}
    image_transform: {resize: [1.0, 1.0], crop_xy: [0, 0]}
    image: {pixel_format: RGB8, stride_bytes: 3840, row_origin: top_left, color_space: sRGB, transfer: sRGB, mask: full_image}
    projection: {model: opencv_fisheye, fx: 400.0, fy: 400.0, cx: 639.5, cy: 359.5, alpha: 0.0, k: [0.0, 0.0, 0.0, 0.0], theta_max_rad: 1.45, z_epsilon_m: 0.000001}
    T_camera_from_vehicle:
      - [-1.0, 0.0, 0.0, 0.0]
      - [0.0, 0.5, -0.866025403784, 1.489230484541]
      - [0.0, -0.866025403784, -0.5, -0.179422863406]
      - [0.0, 0.0, 0.0, 1.0]
  - id: 2
    name: rear
    calibration_id: synthetic-rear-v1
    calibration_origin: analytic_fixture
    calibration_residual_px: null
    resolution: {width: 1280, height: 720}
    calibration_resolution: {width: 1280, height: 720}
    image_transform: {resize: [1.0, 1.0], crop_xy: [0, 0]}
    image: {pixel_format: RGB8, stride_bytes: 3840, row_origin: top_left, color_space: sRGB, transfer: sRGB, mask: full_image}
    projection: {model: opencv_fisheye, fx: 400.0, fy: 400.0, cx: 639.5, cy: 359.5, alpha: 0.0, k: [0.0, 0.0, 0.0, 0.0], theta_max_rad: 1.45, z_epsilon_m: 0.000001}
    T_camera_from_vehicle:
      - [0.0, 1.0, 0.0, 0.0]
      - [0.5, 0.0, -0.866025403784, 2.189230484541]
      - [-0.866025403784, 0.0, -0.5, -1.391858428704]
      - [0.0, 0.0, 0.0, 1.0]
  - id: 3
    name: left
    calibration_id: synthetic-left-v1
    calibration_origin: analytic_fixture
    calibration_residual_px: null
    resolution: {width: 1280, height: 720}
    calibration_resolution: {width: 1280, height: 720}
    image_transform: {resize: [1.0, 1.0], crop_xy: [0, 0]}
    image: {pixel_format: RGB8, stride_bytes: 3840, row_origin: top_left, color_space: sRGB, transfer: sRGB, mask: full_image}
    projection: {model: opencv_fisheye, fx: 400.0, fy: 400.0, cx: 639.5, cy: 359.5, alpha: 0.0, k: [0.0, 0.0, 0.0, 0.0], theta_max_rad: 1.45, z_epsilon_m: 0.000001}
    T_camera_from_vehicle:
      - [1.0, 0.0, 0.0, 0.0]
      - [0.0, -0.5, -0.866025403784, 1.489230484541]
      - [0.0, 0.866025403784, -0.5, -0.179422863406]
      - [0.0, 0.0, 0.0, 1.0]
surface:
  type: rectangular_bowl_v1
  flat_half_length_m: 2.6
  flat_half_width_m: 1.2
  outer_half_length_m: 6.0
  outer_half_width_m: 4.5
  corner_height_m: 1.5
  mesh:
    method: uniform
    uniform_cells: [128, 128]
    adaptive: {initial_cells: [16, 16], uv_tolerance_px: 0.5, screen_tolerance_px: 0.5, max_depth: 8, max_triangles: 131072, view_samples: [front, rear, top]}
  roi: {x_m: [-4.0, 4.0], y_m: [-3.0, 3.0], exclude_vehicle_mask: true}
blending: {method: feather, epsilon_weight: 0.000001, linear_rgb: true}
virtual_camera:
  target_m: [0.0, 0.0, 0.0]
  azimuth_rad: [-3.141592653589793, 3.141592653589793]
  elevation_rad: [0.35, 1.5707963267948966]
  distance_m: [6.0, 18.0]
  clearance_m: 0.1
  fov_y_rad: 1.0
  clip_m: [0.1, 50.0]
  pan_enabled: false
  initial_preset: front
  presets:
    front: {azimuth_rad: 0.0, elevation_rad: 0.7, distance_m: 10.0}
    rear: {azimuth_rad: 3.141592653589793, elevation_rad: 0.7, distance_m: 10.0}
    top: {azimuth_rad: 0.0, elevation_rad: 1.5707963267948966, distance_m: 10.0}
source: {mode: replay, manifest: data/control/manifest.json, speed: 1.0}
output: {width: 1280, height: 720, pixel_format: RGBA8, stride_bytes: 5120, row_origin: top_left, color_space: sRGB, transfer: sRGB}
transport: {protocol_version: 1, kind: unix_stream, ipc_dir: /tmp/sv-local, max_header_bytes: 65536, max_payload_bytes: 67108864, handshake_timeout_ms: 2000, message_timeout_ms: 2000, release_timeout_ms: 250}
runtime:
  input_fps: 30
  render_target_fps: 30
  startup_timeout_ms: 2000
  skew_window_ms: 10
  max_input_age_ms: 100
  hold_last_ms: 0
  ui_stale_ms: 100
  shutdown_timeout_ms: 2000
  input_queue_per_camera: 3
  output_slots: 3
  control_queue: 64
  trace_queue: 4096
  stale_policy: exclude
```

## Диапазоны и проверка

Параметры `flat_half_length_m/flat_half_width_m/outer_half_length_m/outer_half_width_m/corner_height_m` соответствуют a,b,A,B,H математической спецификации. Проверять `A>a>length/2+mask_margin`, `B>b>width/2+mask_margin`, H≥0, положительные размеры автомобиля; ROI лежит в рабочем прямоугольнике, исключённая площадь явно учитывается. Начальные линии сетки дополняются линиями плоской зоны; при adaptive subdivision учитывается весь бюджет согласования соседей.

Матрицы имеют нижнюю строку [0,0,0,1], ортонормальное R и det=1 с допуском 10⁻⁸ для данных double. Fx/fy положительны, alpha=0, k конечны, `0<theta_max<π/2`, z_epsilon>0, модель монотонна на используемом интервале. Четыре ID уникальны; resolution/stride/payload согласуются с PRO-F-004. Калибровка реального набора содержит путь к проверочным наблюдениям и остаточную ошибку; `null` допустим для аналитического fixture с явным происхождением.

Смена разрешения требует документированного resize/crop, обновлённых intrinsics и масок по формулам COORDINATE_SYSTEMS. Коэффициенты k не масштабируются. Хэш калибровки рассчитывается по каноническому представлению её эффективных параметров и включается в manifest/кадры; правила канонизации фиксируются при реализации схемы. Действительные хэши и пути файлов нельзя заменять фиктивными значениями в приёмочном наборе.

Orbit position = target + distance·(cos(elevation)cos(azimuth), cos(elevation)sin(azimuth), sin(elevation)). Состояние ограничивается указанными диапазонами и проверяется на попадание внутрь объёма автомобиля. Для исходной области дополнительно требуется z ≥ H+clearance: камера остаётся выше всей поверхности, в том числе при выходе её XY за рабочий прямоугольник. Пресеты проходят те же проверки, top использует устойчивый выбор up. Pan по умолчанию выключен.

Размеры изображений, очередей и IPC — в пределах PRO-F-009. Для исходного профиля hold_last_ms=0; расширение с hold-last должно задавать конечную границу и явную маркировку. Неизвестные поля отклоняются. Структурную JSON Schema или эквивалентную машинную схему и parser нужно добавить вместе с реализацией на M2; приведённый пример не является доказательством работы ещё не реализованного parser.


## Связанные источники

[[references/README|Единый каталог литературы и документации]].

## Расширение для калибровки

Численный пример выше — baseline рендера, а не полный ввод `sv-configurator`. В машинную схему добавить ссылки/хэши обучающих и проверочных наблюдений, геометрию шаблона, отчёт, diagnostic method/version и зафиксированные пороги. Синтетическая остаточная ошибка `null` не заменяет независимую проверку. Эти поля пока не являются реализованным parser.
