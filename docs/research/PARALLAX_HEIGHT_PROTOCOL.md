# Управляемая высота и расстояние объекта

E-STITCH-parallax-height-01, 11.10.2026. До capture фиксируем master recipe `assets/scenarios/parallax-height-v1.json` и runtime matrix `configs/research/parallax-height-plan.json`. Основание: [[validation/CARRIER_LATERAL]] выявила сильные деформации поднятых объектов, но street scene смешивала высоту, текстуру, форму и окклюзии.

## Управляемые факторы

Один world/mount seed101; тот же perturbed rig и независимая direct camera, что в lateral опыте. Из isolated street builder сохраняются ego с camera locators, road и coded object. Остальные mesh objects удаляются только из новой исследовательской сцены; road расширяется до100×100m с верхней плоскостью z=0. Lights/world сохраняются. Provenance явно указывает world override и фактически retained mesh inventory; legacy recipe описывает исходный builder, а не оставшиеся здания.

Magenta emission box фиксированного размера0.4×0.4×0.4m, без изменения материала/поворота. Факторы: center z=0.2/0.8/1.4m и XY=(2.4,2.3)/(4.8,4.6)m. На нижнем уровне box стоит на дороге; два верхних являются идеализированным переносом того же box вверх без опоры. Это **высота центра**, не размер объекта. Верхний box целиком выше всех optical centers rig. Шесть cases отличаются только target center. Для каждого —два одинаковых сценарных poses при frames0/6; vehicle движется0.4m между кадрами, но каждый фактор сравнивается на совпадающем frame index. Два кадра одного фактора не выдаются за независимые seeds.

Camera cubefaces256px, fisheye400px, direct output320×180; source IDs отключены в этой серии. Экспортируются direct RGB/object-ID/any-camera visibility, target evaluated position, poses/timestamps, hashes. Source-ID correspondence/полноценная ghost segmentation здесь не измеряется.

## Native matrix и критерии

Пять прежних medium carriers, те же mesh ceilings, четыре profiles multi_band/graph_cut_multi_band × zero/normalized, warmup2/repeats7, seed20261011.30 fresh servers,240 scene/frame/carrier/profile quality conditions; вычисления выполняет production C++, управление —sv-client-lib через C++ svctl research. Geometry probe отдельно от timing samples. Входы всех factors имеют один rig, одинаковые poses, camera settings и мир кроме target translation; evaluated target position записывается в direct truth; legacy static camera capture не имеет такого поля. Единство генератора проверяется hashes, но отдельная per-camera evaluated-position запись отсутствует.

Первичные критерии —coded target IoU, precision/recall, predicted/truth pixels и false-positive/missing pixels на полном кадре по прежней component policy. Дополнительно linear RGB MAE на independently visible interior ROI; он может быть мал из-за однородной дороги и не заменяет object metric. Публикуются все heights/ranges/profiles, включая disappearance и нулевой IoU. Сравнения height/range условны на seed/pose/carrier/profile; не предполагается монотонность и не подбирается лучший case после просмотра.

Геометрический контроль центра: для optical center C и scene point P пересечение camera ray с ground plane задаётся Q=C+t(P−C), t=Cz/(Cz−Pz). При Pz=0 получается Q=P; при t≤0 нет пересечения в направлении наблюдаемого point. Этот контроль не предсказывает RGB IoU box: есть площадь, occlusion, clipping, finite meshes и fusion. Проверяемая гипотеза —повышение Pz изменяет совместимость source rays с единой поверхностью, а blending сам не восстанавливает общую 3D-геометрию.

RGB truth независим от carrier и native fusion. Source-camera validity/visibility не строятся из output. Отсутствие textured output, ошибочная позиция и число connected components не смешиваются в одну «ghost» оценку. Native ray-ground контроль не является оценкой всех носителей; dome/cylinder/cube имеют другие intersections.

Это initial controlled synthetic screen: один rig/seed, две зависимые poses, один материал/размер и camera resolution. Нет holdout/natural correspondence/real cameras/Aurora/FPS вывода. Fixed carrier order и отсутствие thermal control запрещают speed ranking. Для расширения нужны другие baselines/mounts, высоты/дистанции, shapes/разрешения и физические фотографии.

## Проверки наблюдаемости и интерпретации

Audit сохраняет chroma pixel count каждого исходного fisheye и число direct target-ID pixels с any-camera visibility. Full-frame target IoU включает также части, не видимые source cameras; это screen совпадения изображений, не чистая ошибка fusion на общем наблюдаемом support. Source chroma counts не являются correspondence. Центроид —дополнительный proxy: симметричные ложные копии могут дать нулевое смещение при нулевой IoU.

После просмотра всех заранее заданных факторов зафиксировано: far-z020 касается нижней границы direct frame; fixed view меняет projected size и clipping при переносе box. Поэтому range comparison не является чистой зависимостью от расстояния и не используется как causal speed/quality ranking. Для следующего протокола нужны целиком попадающие в кадр targets и одинаково определённый source-visible object support. Существующие inputs/results сохранены без подбора нового view после получения метрик.
