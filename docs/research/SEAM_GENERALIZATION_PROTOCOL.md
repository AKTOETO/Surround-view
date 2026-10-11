# Новые procedural instances для seam follow-up

Зафиксировано 11.10.2026 до получения новых изображений. Recipe: `assets/scenarios/seam-generalization-v1.json`; воспроизведение существующими `diagnostic.build_diagnostic` и `paired_truth.capture_paired`. Seed101/102/103 не использовались для выбора текущего solver. Это новые экземпляры **одного** procedural street generator, не три независимых типа городской среды и не реальные фотографии.

## Неизменяемый план

| Seed | Вариант | Buildings spacing/height, m | Coded target center/size, m |
|---|---|---|---|
| 101 | low-block | 8 / 4…7 | (2.4,2.3,0.35) / (0.4,0.4,0.7) |
| 102 | pole | 11 / 7…12 | (2.4,−2.3,1.3) / (0.15,0.15,2.6) |
| 103 | raised-box | 15 / 4…12 | (−2.6,2,0.7) / (0.8,0.6,1.4) |

Во всех случаях: seeded ±3° yaw/pitch и ±0.1m along-body mount offsets, near-obstacle jitter≤0.4m. Rendering использует истинные perturbed calibration parameters; опыт проверяет geometry/scene variation при известной калибровке, **не** accuracy calibration solver. Capture: две позы, frame_step6, cube faces256px, output320×180, source IDs/independent ray visibility. Одна запись pose между кадрами не оценивает длинный temporal clip.

Native scenario `configs/research/multilabel-sequence.json`: четыре неизменных profiles, boundary zero, λ0.1, levels4, seed20261013, два warmup/семь repeats. Генератор, recipe и native source fingerprints сохраняются до анализа; field split=validation — организационная метка, не доказательство независимости. Не менять solver/threshold/target placement по увиденному output. Если coded target невидим/слишком мал, сохранить случай и обозначить ограничение IoU, не заменять более удобной позой.

## Проверки и границы

Проверить capture/manifest/config/pose hashes перед запуском; raw report/captures проверять read-only audit `--comparison seam_solver`. Все три planned cases должны быть представлены, чтобы назвать серию полной. Отчёт, остановившийся на одной новой сцене, остаётся промежуточным. Primary metrics/restore/тайминговые ограничения те же, что [[research/MULTILABEL_SEAM_PROTOCOL]]. Никакого tuning на новых cases; при изменении алгоритма их считать просмотренными и выделять новые seeds.

Blender MCP доступен: Blender5.2.2 LTS/addon1.8/protocol13. Builder создаёт отдельную сцену; исходную active scene вернуть после capture, не удалять её объекты. Мир полностью воспроизводим из recipe и checked-in Python asset generator; .blend/LFS не требуются. Captured scientific inputs/results находятся в artifacts, а recipe в assets; это не downloaded artistic asset.
