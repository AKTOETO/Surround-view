# Linux-прототип: сборка и запуск

Версия 0.3.0. Эта заметка — навигация; подробные команды хранятся один раз в `engineering/`. Что проверено и что осталось: [[prototype/STATUS]]. Реализация: [[diploma/03_PROTOTYPE_IMPLEMENTATION]]. Исследование способов слияния текстур и surface carriers не входит в текущую реализацию; методика сравнения находится в [[research/PROJECTION_AND_STITCHING]].

## Зависимости и структура

[[engineering/BUILD|Полный список библиотек, CMake targets и install layout]]. Реальные OpenCV, GLM, Boost и OpenSSL подключаются из packages; FetchContent отсутствует.

## Сборка и тесты

[[engineering/BUILD|Linux GPU/CPU, Qt 5/6, CTest и ASan/UBSan]]. [[engineering/AURORA|Offline SDK, Source0, spec, сборка и установка RPM]]. Целевые SDK/устройство пока не проверены.

## Данные и первый кадр

[[engineering/USAGE|Первый запуск и все CLI]]. [[engineering/SCENE|Фотографическая улица, CC0, SHA-256 и ограничения виртуального rig]]. [[engineering/BLENDER|Метрическая 3D-улица, разнесённые камеры, движение и replay export]]. Аналитическая сцена сохранена для численных проверок.

## Сервер и интерактивный клиент

[[engineering/USAGE|Replay, сервер/клиент, управление, capture и формат manifest]]. Прямого live producer в сервере нет; OpenCV recorder готовит запись.

## Калибровка и диагностика

[[engineering/USAGE|Детектор доски и intrinsics через OpenCV; known-XYZ NumPy и сервисная диагностика]]. Поза доски и матрица установки относительно автомобиля — разные результаты.

## Повторение опытов и построение графиков

[[engineering/PLATFORM_TEST|Native suite на ПК/устройстве и строгое сравнение]]. Исторические данные 0.1.0 — [[prototype/MEASUREMENTS]]; новые первичные baselines — [[validation/baselines/PC_RTX]] и [[validation/baselines/PC_MESA]]. Скрипты и команды рисунков — [[diploma/README]].
