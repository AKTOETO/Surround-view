# E3: подготовка входов и контракт E1→Blender

25.10.2026. Статус: **offline preflight выполнен; изображения и quality results E3
ещё не получены**. MCP Blender не подключается; по указанию автора работа продолжается
без рендера. Результат — подготовленные планы и исправление передачи поз, не новая
RGB-выборка. Основной протокол: [[research/THESIS_CORE_PROTOCOL]].

## Найденная несовместимость

Одинаковый mount seed не воспроизводит одинаковую камеру в двух прежних генераторах:
E1 задаёт Rx(pitch)R_nominalRz(−yaw) и сдвиг по vehicle X для всех камер; legacy Blender
задаёт Rx(−pitch)R_nominalRz(−yaw) и сдвиг front/rear поY, sides поX. Это два разных
контракта. Исторические Blender recipes не объявляются ошибочными и не переписываются;
ошибкой E3 было бы считать их совпадающими с E1 по одному seed.

При seed1001 и тех же bounds3°/3°/0.05м legacy Blender относительно истинных поз E1:

| Камера | Ошибка центра,mm | Различие поворота,deg |
|---|---:|---:|
| Front |44.167|5.811594|
| Right |0|5.085616|
| Rear |49.186|4.817254|
| Left |0|4.416859|

Такое различие смешало бы качество калибратора с другой фактической установкой камер.
Новый camera_rig передаёт **сами матрицы** из E1, без повторного sampler. Legacy default
perturb сохраняется. Для explicit rig запрещены дополнительные mount bounds/overrides;
проверяются ordered IDs, finite rigid right-handed4×4 и неизменность остальных полей
между nominal/actual. Scene builder использует actual для capture/locators и сохраняет
nominal/provenance в capture.json. Converter переносит camera_rig_provenance в
ground_truth.json; legacy field отсутствует, если его не было в capture. Runtime этого нового Blender adapter ещё не проверен.

## Подготовленный набор

Frozen mapping: configs/research/thesis-scene-inputs.json —street-a260101,
street-b260102, cargo yard-a260103; mount seeds1001/1002/1003. Два view high/low,
static frame0, cube face256, output320×180. Primary mask до просмотра новых RGB
задана как non-ego/any-camera-visible по independent object/visibility truth с erosion2px;
маска ещё не построена. Она общая для nominal/true/estimated и не зависит от их errors. Выбор estimated profile=Gaussian RANSAC+LM
сделан после численного E1, до новых scene RGB: обычные observations без injected outliers,
а не выбор лучшего отдельного fit. В каждом mount используются все4 выбранных candidates.

Exporter подготовил18 startup profiles (2 views ×3 mounts ×nominal/true/estimated),
два capture plans по9 cases и registry SHA256. Capture использует только true profiles;
estimated и nominal предназначены серверным runs на **тех же** source RGB. Candidate
calibration IDs взяты из server E1 results, source physical truth остаётся неизменной.

Найдена и устранена ошибка подготовки config: FRAME/reference view содержит target_m,
но startup virtual_camera не принимает это поле. В плане остаются5 поддержанных config
полей; Blender и сервер используют default target[0,0,0]. Explicit FRAME-only field теперь
отклоняется exporter до output. Нативный loader проверяет итоговые JSON, поэтому Python
структурная проверка не выдаётся за полную C++ config validation.

## Проверки без Blender

- 25/25 offline tests, включая6 новых методов проверки rig/bridge, прошли; CTest
  blender_fixture1/1 за5,32с (финальный прогон с converter provenance assertion). Legacy scene/mount, optics и converter checks сохранены.
- Все18 startup configs приняты настоящим sv-project;3456 validation-point projections
  корректны. Для6 estimated profiles24 camera RMSE совпали с E1 с допуском1e−7px.
- 72 camera matrices в2×9 capture cases точно равны source E1 truth; nominal/estimated
  profile matrices также сверены с исходными records. Пересэмплирования нет.
- Повреждённый файл/источник, отсутствующий fit, reflected transform, новые intrinsics,
  повторная perturbation, неизвестный target_m и повторный output отвергаются тестами.
- Py_compile и git diff --check успешны. Native binaries не менялись; новый полный
  CTest/build не запускались. Actual Blender scene/capture, PNG truth и final RGBA не проверены.

Raw prepared set: artifacts/thesis-e3-ready-20261025. Предыдущая preparation attempt
с target_m в artifacts/thesis-e3-prepared-20261025 не прошла native validation и не
используется. Compact audit:
[thesis_e3_preflight_v1.json](baselines/thesis_e3_preflight_v1.json).
В нём сохранены hashes20 prepared files и camera models nominal/true/estimated для3
mounts, чтобы позы не зависели только от временных output directories. Бинарный мир
не создавался; будущие scenes восстанавливаются procedural recipes и этими моделями.

## Воспроизведение

Нужны raw truth/report завершённого E1 ([[validation/THESIS_E1]]). Exporter не вычисляет
калибровку и не правит конфиг работающего сервера — он подготавливает scientific fixtures.

```sh
python3 docs/diploma/plot_thesis_calibration.py prepare-e3 \
  configs/research/thesis-scene-inputs.json \
  artifacts/thesis-e1-v2-20261025/truth.json artifacts/thesis-e1-v2-20261025/report.json \
  artifacts/e3-new-prepared
python3 docs/diploma/plot_thesis_calibration.py audit-e3 \
  artifacts/e3-new-prepared artifacts/thesis-e1-v2-20261025/truth.json \
  artifacts/thesis-e1-v2-20261025/report.json build/sv-project artifacts/e3-new-audit.json
```

Output directory должен быть новым. Registry помечен prepared_no_render;
independent_rgb_available=false и native_e3_trials_completed=false сохраняются до
реальных captures/runs. Следующий этап после доступности Blender: выполнить два capture
plans, сверить actual pose/provenance с этим registry, конвертировать RGB один раз на
case/view и подготовить3 profile-specific manifests/reference guards с общими image/
mask hashes. Calibration IDs/model guards различаются, independent RGB truth —общая.
После этого выполнить E3. Не подменять новый RGB историческими картинками другого rig.

PlantUML E3.1 прошёл syntax check; новый mask policy пока только декларация, без построения маски.
