# Controlled coded-object stitching fixture

Авторский procedural Blender мир со светящейся magenta мишенью: cuboid 0.35×0.35×1.6 m,
центр (2.7,1.6,0.8) m. Blueprint: `assets/scenarios/object-stitch-v1.json`;
генератор: `tools/blender/diagnostic.py`. Blender 5.2.2 LTS / EEVEE, 10.10.2026.
Один rig/view, два кадра x=0/0.4 m при t=0/0.2 s; cube faces 256,
fisheye RGB/IDs 400×400, virtual RGB/IDs 320×180. Это exploratory fixture,
не untouched holdout. Внешние ассеты не используются.

В дополнение к paired fixture добавлены `source_objects_camera*_*.npy` — восемь
uint16 ID карт на оптических входах с теми же object IDs, что у virtual truth.
Карты получены ray casts непосредственно в equidistant pixel centers, без carrier;
hide-render helper meshes пропущены. Прозрачность/antialiasing/cube resampling не
моделируются, поэтому ID на границах не являются точной RGB segmentation.
Другие optical models/ненулевой skew exporter отклоняет.

- PNG camera inputs: converter output, SHA-256 и calibration IDs в manifest.
- `objects_*.npy`, `visibility_*.npy`, `virtual_*.png`: exact scene-point truth.
- `paired_truth.json`: target identity, source map paths/hashes, script provenance.
- `capture.json`: original cube index; raw cube files и `.blend` snapshot в fixture не входят.
- `ground_truth.json`, `config.json`: pose/calibration/provenance.

Размер около 3.8 MB. Raw captures, пробный захват у границы кадра и snapshots остаются
в `artifacts/`, не входят в Git. Для численного воспроизведения Blender не нужен:
`python3 tools/research/object_stitch.py --output artifacts/object-study-repeat`.
Методика, ограничения, все результаты и Blender reproduction:
`docs/validation/OBJECT_STITCH.md`.
