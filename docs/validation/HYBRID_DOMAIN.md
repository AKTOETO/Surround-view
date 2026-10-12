# Новые hybrid proxy coordinates: независимая scene truth

Дата:12.10.2026. Продолжение [[validation/HYBRID_CORRESPONDENCE]], после footprint controls [[validation/SOURCE_FOOTPRINT]]/[[validation/CUBE_FOOTPRINT]]. Диплом §4.70. План: `configs/research/hybrid-domain-plan.json`, baseline: `docs/validation/baselines/hybrid_domain_v1.json`, raw root: `artifacts/hybrid-domain-v1-final`. Frozen requests: `artifacts/hybrid-domain-requests-v1.json`.

## Выборка и фиксация

Для каждого из60 прежних native hybrid conditions берётся actual magenta proxy, исключаются прежний edge proxy и expected target domain. Threshold прежний: min(R,B)−G>0.15 в RGB8/255, без component filtering. Получено1227 condition pixels, объединённых в547 уникальных frame/carrier coordinates. Пересечения между algorithms дедуплицированы внутри каждого frame/carrier, но carriers/frames не выдаются за независимые сцены. Нет новых rendered outputs или изменения конфигурации native server.

Requests независимо регенерируются при каждом audit из archived actual RGBA, indices и прежнего baseline. Проверяются hashes/bytes, совпадение config/input, actual source UV/status/carrier points на выбранной области каждого варианта. Все60 rows сохранены, включая пустые domains. Captured raw arrays SHA256 связаны scene truth hash в baseline. Geometry helper и первоначальный план зафиксированы `517387d`; поправка archive replay и окончательный план — `5d1c235`, до успешного capture.

## Проверка повторения архива и неудачный первый запуск

Новые source rays используют independent double equidistant inverse. Первые32 прежних displaced pixels каждого frame/carrier используются отдельно как контроль сцены. Первый запуск не прошёл strict archive hit tolerance1e−5m: double inverse на проверенном frame0/plane anchor12/camera0 дал1.033758325e−5m относительно исторической float32 operation order. Диагностический повтор той же архивной последовательностью дал0m. Failed output `artifacts/hybrid-domain-v1/FAILED.json` сохранён; complete truth этого запуска отсутствует.

Для архивных anchors сохранена **именно историческая** последовательность float32 inverse; новые source rays остались double. Допуск не увеличен, capture повторён в новом каталоге. Все640 допустимых anchor pairs повторили kinds и finite world hits точно: max error0m. No-hit sentinel проверяется отдельно. Это воспроизводимость архива, не новая гарантия micrometre accuracy Blender, hardware или carrier geometry.

## Новая независимая разметка

Blender5.2.2LTS восстанавливает прежние recipe, flat road, target/ego, poses и rig с проверкой generator hashes. Для547 уникальных координат записаны:

- ближайший opaque hit виртуального луча с прежними clipping/fov;
- центральный луч каждой допустимой source camera на actual UV;
- четыре clamped source texel centers, их positive bilinear weights и независимые surface kinds.

Kinds:0 no_hit,1 target,2 road,3 ego,4 other. Labels выводятся из scene ray casts, не RGB или местоположения proxy. Virtual/source world hits и native carrier coordinates сохраняются раздельно. Signed camera contributions и local fusion weights читаются из прежнего capture; RGB/pyramid parity уже проверена §4.67 и не заменяется новым анализом labels.

Ненулевой вклад определяется max(abs(linear RGB channels))>1e−6. Locally-invalid camera — отсутствует native local source validity; zero-local-weight camera — local blend weight≤1e−6. Эти категории различны. Positive bilinear texel support требует valid source и texel weight>0, но **не требует positive local fusion weight**. Поэтому наличие target texel в невыбранной graph-cut camera не означает actual causal RGB contribution.

## Измерения

| Метод / boundary | Новые pixels | Target в source texel centers | Нет target center/texel | Locally-invalid camera contributes | Zero-local-weight camera contributes |
|---|---:|---:|---:|---:|---:|
| seam_distance_feather / zero | 131 | 102 | 29 | 0 | 0 |
| graph_cut_seam / zero | 369 | 234 | 135 | 0 | 0 |
| multi_band / zero | 16 | 10 | 6 | 6 | 6 |
| graph_cut_multi_band / zero | 264 | 171 | 93 | 0 | 132 |
| multi_band / normalized | 183 | 10 | 173 | 173 | 173 |
| graph_cut_multi_band / normalized | 264 | 171 | 93 | 0 | 132 |

Все1227 condition pixels имеют virtual kind2 (road); local source central-ray target support равен0 во всех60 rows. У698 имеется target среди положительно взвешенных source texel centers, у529 нет ни central target ray, ни такого texel-center target. Это расширение поддержки sampling, не новая independent ghost segmentation.

У179 condition pixels имеется ненулевой signed contribution от locally-invalid camera:6 для multi_band/zero и173 для multi_band/normalized. Max magnitude соответственно0.134707406 и0.192117289 linear RGB. У cut+MB отсутствует locally-invalid contribution на этой выборке, но в132 pixels каждой boundary camera с нулевым local fusion weight вносит ненулевой reconstructed contribution. Это показывает, почему два diagnostics нельзя объединять.

Одинаковые агрегированные counts173 у normalized no-texel support и locally-invalid contribution не используются как доказательство равенства pixel sets или причинного источника chroma. Полное пространственное происхождение signed pyramid bands этим опытом не восстанавливается. Source/virtual same-road matching при2cm встречается у1840 пар; это может сосуществовать с цветом соседнего объекта в фильтрованном sample. Same-kind/point control не превращается в quality metric.

## Что завершено и что осталось

Независимые virtual/source/texel labels теперь существуют на **всех новых hybrid proxy coordinates этой60-condition серии**; старые labels не экстраполируются. Raw expected target-color overlap0 (§4.67) этим опытом не изменён. Перенос цвета через соседние texel/pyramid отличается от восстановления правильного положения/видимости объекта.

Для529 pixels без local center/texel target нельзя автоматически заявить false positives или приписать все pyramid: нужны spatial dependency trace bands и upstream pixel footprints для причинного разбора. Это отдельный diagnostic depth, а не условие для признания negative height result. Следующий главный приоритет — server-side mitigation height/occlusion с independent truth и unchanged camera RGB. Возможные oracle-depth controls должны быть явно отделены от алгоритма, доступного реальным RGB cameras; нельзя использовать Blender truth в production незаметно.

Остаются natural-object/holdout scenes, optical/mount/height варианты, physical/Aurora и target budgets. Из нынешних output-conditioned correlated samples нельзя вывести universal ranking.

## Воспроизведение

```sh
python3 tests/test_source_correspondence.py
ctest --test-dir build -R '^source_correspondence$' --output-on-failure
MPLCONFIGDIR=/tmp/sv-matplotlib python3 docs/diploma/plot_hybrid_domain.py \
  --output-report artifacts/hybrid-domain-v1-final/reaudit.json
cmp docs/validation/baselines/hybrid_domain_v1.json artifacts/hybrid-domain-v1-final/reaudit.json
```

15 known-answer controls и CTest1/1 прошли; повторный audit побитно совпал. Новые tests проверяют исключение обоих prior domains, shape mismatch, zero-weight texels/invalid cameras, signed nonlocal contributions и empty selection. NumPy/SciPy/Pillow/Matplotlib нужны system audit/plots; Blender geometry helper не импортирует Pillow/SciPy. Product renderer не менялся, performance tests не переоцениваются.

Для повторного Blender capture скопировать план с новым несуществующим output_root; frozen requests можно использовать после проверки hashes. В Blender MCP:

```python
import sys, importlib
from pathlib import Path
root=Path('/home/bogdan/prog/MAI/surround-view') # локальный путь
sys.path.insert(0,str(root/'tools/research'))
sys.path.insert(0,str(root/'tools/blender'))
import hybrid_domain
importlib.reload(hybrid_domain)
print(hybrid_domain.capture(root,root/'artifacts/hybrid-domain-repeat-plan.json'))
```

Затем plot/audit с `--plan`, `--output-report`, `--figures`, направленными на новые artifacts. Исторический baseline не заменять автоматически.
