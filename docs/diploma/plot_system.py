#!/usr/bin/env python3
"""Captioned thesis diagrams and optional actual native-render street views."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess

os.environ.setdefault("MPLCONFIGDIR", "/tmp/sv-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BLUE, GREEN, AMBER = "#dceaf5", "#dceee7", "#f9e8c9"
plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "svg.fonttype": "none"})


def canvas(title, size=(11, 5)):
    figure, axis = plt.subplots(figsize=size, layout="constrained")
    axis.set(xlim=(0, 1), ylim=(0, 1))
    axis.axis("off")
    axis.set_title(title, pad=12, fontsize=13)
    return figure, axis


def box(axis, x, y, width, height, label, color=BLUE, fontsize=10):
    axis.add_patch(FancyBboxPatch((x, y), width, height, boxstyle="round,pad=0.008,rounding_size=0.012",
                                 facecolor=color, edgecolor="#40576b", linewidth=1.1))
    axis.text(x + width / 2, y + height / 2, label, ha="center", va="center", fontsize=fontsize)


def arrow(axis, start, end, label="", offset=(0, .025)):
    axis.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=13,
                                  linewidth=1.2, color="#40576b", shrinkA=2, shrinkB=2))
    if label:
        axis.text((start[0] + end[0]) / 2 + offset[0], (start[1] + end[1]) / 2 + offset[1], label,
                  ha="center", va="bottom", fontsize=9, bbox={"facecolor": "white", "alpha": .85, "edgecolor": "none"})


def save(figure, directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    figure.savefig(directory / name, dpi=160)
    plt.close(figure)


def theory(output):
    folder = output / "theory"
    figure, axis = plt.subplots(figsize=(9, 5), layout="constrained")
    axis.add_patch(Rectangle((-2.3, -.9), 4.6, 1.8, facecolor=BLUE, edgecolor="#40576b"))
    axis.text(0, 0, "ТС\nX вперёд, Y влево, Z вверх", ha="center", va="center")
    for name, x, y, dx, dy in [("Передняя", 2.3, 0, 1, 0), ("Правая", 0, -.9, 0, -1),
                               ("Задняя", -2.3, 0, -1, 0), ("Левая", 0, .9, 0, 1)]:
        axis.plot(x, y, "o", color="#287b8e", ms=8)
        axis.arrow(x, y, dx * 1.7, dy * 1.7, width=.02, head_width=.2, length_includes_head=True, color="#287b8e")
        axis.text(x + dx * 2, y + dy * 2, name, ha="center", va="center")
    axis.annotate("Xᵥ", (1, 2), (0, 2), arrowprops={"arrowstyle": "->"})
    axis.annotate("Yᵥ", (0, 3), (0, 2), arrowprops={"arrowstyle": "->"})
    axis.set(xlim=(-5, 5), ylim=(-3.5, 3.5), aspect="equal", xlabel="Xᵥ, м", ylabel="Yᵥ, м",
             title="Расположение четырёх камер: схема направления оптических осей в плане")
    axis.grid(alpha=.2)
    save(figure, folder, "01_rig.svg")

    theta = np.linspace(0, 1.45, 500)
    figure, axis = plt.subplots(figsize=(9, 4.5), layout="constrained")
    curves = [(np.tan(theta), "Перспектива: tan θ"), (theta, "Эквидистантная: θ"),
              (2 * np.sin(theta / 2), "Равновеликая: 2 sin(θ/2)"),
              (theta * (1 + .03 * theta**2 - .005 * theta**4 + .001 * theta**6), "Fisheye: пример ненулевых k")]
    for values, label in curves:
        axis.plot(np.degrees(theta), values, label=label, linewidth=2)
    axis.set(xlabel="Угол к оптической оси, град.", ylabel="Нормированный радиус r/f", ylim=(0, 3),
             title="Угловые проекции: расчётные кривые, не измерение объектива")
    axis.legend(fontsize=9)
    axis.grid(alpha=.25)
    save(figure, folder, "01_projection.svg")

    figure = plt.figure(figsize=(11, 4.6), layout="constrained")
    x, y = np.meshgrid(np.linspace(-6, 6, 65), np.linspace(-4.5, 4.5, 51))
    for index, height in enumerate([0, 1.5], 1):
        axis = figure.add_subplot(1, 2, index, projection="3d")
        z = height / 2 * ((np.maximum(abs(x) - 2.6, 0) / 3.4)**2 +
                          (np.maximum(abs(y) - 1.2, 0) / 3.3)**2)
        axis.plot_surface(x, y, z, cmap="Blues", edgecolor="#7997ae", linewidth=.15, alpha=.92)
        axis.set(xlabel="X, м", ylabel="Y, м", zlabel="Z, м", zlim=(0, 1.6), title=f"Высота угла H={height:g} м")
        axis.view_init(elev=28, azim=-55)
    save(figure, folder, "01_surfaces.svg")

    figure, axis = canvas("Два независимых механизма дефекта изображения", (10, 4))
    box(axis, .05, .6, .32, .22, "Ошибка геометрии\nK, k, T камеры", AMBER)
    box(axis, .05, .15, .32, .22, "Ошибка времени\nskew, возраст, очередь", BLUE)
    box(axis, .63, .38, .3, .23, "Несовпадение контуров\nи неверная свежесть")
    arrow(axis, (.38, .7), (.62, .54), "UV-остаток")
    arrow(axis, (.38, .26), (.62, .44), "Движение между кадрами", offset=(0, -.12))
    save(figure, folder, "01_errors.svg")

    weights = np.linspace(0, 1, 400)
    linear = np.where(weights <= .0031308, 12.92 * weights,
                      1.055 * weights**(1 / 2.4) - .055)
    figure, axis = plt.subplots(figsize=(8.5, 4), layout="constrained")
    axis.plot(weights, weights, label="Смешение уже encoded sRGB", linewidth=2)
    axis.plot(weights, linear, label="Смешение linear-light → encode", linewidth=2)
    axis.plot([.5], [.735356983], "o", color="#287b8e")
    axis.annotate("При w=0.5: 0.735 вместо 0.5", (.5, .735356983), (.13, .88),
                  arrowprops={"arrowstyle": "->"}, fontsize=10)
    axis.set(xlabel="Вес белого w при смешении чёрного и белого", ylabel="Выходное sRGB значение",
             title="Фотометрическое смешение: аналитический пример")
    axis.legend(loc="lower right", fontsize=9)
    axis.grid(alpha=.2)
    save(figure, folder, "01_linear_blend.svg")


def design(output):
    folder = output / "design"
    figure, axis = canvas("Компоненты прототипа 0.3.0 и владельцы данных", (12, 5.8))
    box(axis, .02, .65, .21, .23, "OpenCV capture /\nanalytic simulator /\nHDR panorama", GREEN)
    box(axis, .32, .65, .23, .23, "RGB8 + manifest\ncalibration IDs\nSHA-256")
    box(axis, .67, .65, .29, .23, "sv-server\nAsio: sockets\nmain: EGL owner")
    box(axis, .68, .15, .28, .23, "sv-client\nQt Quick / QImage\norbit, zoom, presets")
    box(axis, .32, .15, .23, .23, "sv-core / sv-vision\nK, k, T\nconfiguration + sync")
    box(axis, .02, .15, .21, .23, "sv-platform-test\nJSON + Markdown\nCPU/GPU criteria", GREEN)
    arrow(axis, (.24, .76), (.31, .76))
    arrow(axis, (.56, .76), (.66, .76))
    arrow(axis, (.81, .64), (.81, .39), "RGBA8 / copied IPC", offset=(.02, 0))
    arrow(axis, (.69, .39), (.69, .64), "Команды", offset=(-.055, 0))
    arrow(axis, (.55, .3), (.67, .65), "Общие контракты", offset=(-.16, -.06))
    arrow(axis, (.31, .26), (.24, .26))
    save(figure, folder, "02_components.svg")

    figure, axis = canvas("Разделение данных до оценивания параметров", (11, 4.2))
    labels = [(0.02, "Снимки доски /\nизвестные XYZ"), (.27, "Train\nоптимизация"),
              (.53, "Фиксированная\nмодель"), (.79, "Validation\nпорог + отчёт")]
    for x, label in labels:
        box(axis, x, .43, .19, .3, label, GREEN if x == .79 else BLUE)
    for a, b in [(0.21, .27), (.46, .53), (.72, .79)]:
        arrow(axis, (a, .58), (b, .58))
    axis.text(.5, .17, "Проверочные файлы/XYZ не входят в fitting. Для снимков поза validation-доски оценивается отдельно.",
              ha="center", fontsize=9)
    save(figure, folder, "02_validation_split.svg")

    figure, axis = plt.subplots(figsize=(10, 4.2), layout="constrained")
    times = [[3, 11, 18], [6, 16, 21], [9, 20, 23], [8, 17, 24]]
    selected = [18, 16, 20, 17]
    axis.axvspan(8, 28, color=BLUE, alpha=.6, label="Поиск около anchor=18, окно ±10 мс")
    for index, (values, chosen) in enumerate(zip(times, selected)):
        y = 3 - index
        axis.plot([0, 35], [y, y], color="#9bb0c0", linewidth=1)
        axis.scatter(values, [y] * 3, color="#8e969e", s=45)
        axis.scatter([chosen], [y], color="#287b8e", s=100, zorder=3)
        axis.text(chosen + .7, y + .15, str(chosen), fontsize=9)
    axis.axvline(30, color="#ce9154", linestyle="--", label="now=30 мс")
    axis.set(xlim=(0, 35), ylim=(-.5, 3.8), yticks=[3, 2, 1, 0], yticklabels=["Камера 0", "Камера 1", "Камера 2", "Камера 3"],
             xlabel="Время доставки кадра, мс (условный пример)", title="Отбор ближайших кадров: итоговый skew = 20−16 = 4 мс")
    axis.legend(loc="upper left", fontsize=8)
    save(figure, folder, "02_sync.svg")


def implementation(output):
    folder = output / "implementation"
    figure, axis = canvas("Последовательность: команда, кадр, собственная копия, release", (12, 6.5))
    positions = [.07, .29, .51, .73, .93]
    for x, label in zip(positions, ["UI / QML", "Bridge / GUI", "Asio runner", "Render / EGL", "ImageProvider"]):
        axis.text(x, .95, label, ha="center", weight="bold", fontsize=10)
        axis.plot([x, x], [.06, .91], "--", color="#9bb0c0", linewidth=1)
    events = [(0, 1, .85, "orbit"), (1, 2, .75, "COMMAND + ID"), (2, 3, .65, "bounded command queue"),
              (3, 2, .52, "render → readback → RGBA8"), (2, 1, .4, "FRAME + session/token"),
              (1, 4, .29, "deep QImage copy"), (1, 2, .18, "RELEASE: session/frame/token"),
              (4, 0, .07, "image ready → present submit")]
    for left, right, y, label in events:
        arrow(axis, (positions[left], y), (positions[right], y), label, offset=(0, .012))
    save(figure, folder, "03_sequence.svg")

    figure, axis = canvas("OpenCV-калибровка: разные пространства и виды проверки", (12, 5))
    box(axis, .03, .62, .21, .25, "Фото доски\nfindChessboardCornersSB\nUV ↔ XYZ доски", GREEN, 9)
    box(axis, .37, .62, .24, .25, "fisheye::calibrate\nK, k; train views\nfixed skew")
    box(axis, .72, .62, .24, .25, "Held-out images\nsolvePnP: board pose\np95 residual", AMBER, 9)
    box(axis, .03, .12, .21, .25, "Измеренная площадка\nXYZ автомобиля\nнезависимые UV", GREEN, 9)
    box(axis, .37, .12, .24, .25, "NumPy LM / Huber\nK, k, T\nknown-XYZ solver")
    box(axis, .72, .12, .24, .25, "Known-XYZ validation\nрепроекция / rank\nновый calibration ID", AMBER, 9)
    for y in [.745, .245]:
        arrow(axis, (.25, y), (.36, y))
        arrow(axis, (.62, y), (.71, y))
    save(figure, folder, "03_calibration.svg")

    figure, axis = canvas("Сборка и аппаратная проверка: разные этапы подтверждения", (12, 5.5))
    box(axis, .03, .65, .23, .22, "Git HEAD\nSource0 + revision\nобщий source hash", GREEN)
    box(axis, .38, .65, .24, .22, "Aurora SDK target\npackage dependencies\n%cmake + %ninja", AMBER)
    box(axis, .75, .65, .22, .22, "Целевой RPM\nvalidation / signing\ninstallation", AMBER)
    box(axis, .75, .15, .22, .22, "Устройство Аврора\nnative qualification\nJSON + REPORT.md", AMBER)
    box(axis, .38, .15, .24, .22, "Strict comparison\nsame code + workload\nratio или reasons")
    box(axis, .03, .15, .23, .22, "PC RTX / Mesa\nпервичный baseline\nтот же native suite", GREEN)
    arrow(axis, (.27, .76), (.37, .76))
    arrow(axis, (.63, .76), (.74, .76))
    arrow(axis, (.86, .64), (.86, .38))
    arrow(axis, (.74, .26), (.63, .26))
    arrow(axis, (.27, .26), (.37, .26))
    axis.text(.5, .03, "Зелёное: проверено на Linux. Янтарное: ожидает реальных SDK / устройства.", ha="center", fontsize=9)
    save(figure, folder, "03_rpm_workflow.svg")


def conclusion(output):
    figure, axis = canvas("Исследовательские задачи: имеющиеся свидетельства и следующий этап", (12, 5.5))
    rows = [("R1 — анализ", "Модели / источники", "Профиль конкретного устройства"),
            ("R2 — калибровка", "OpenCV + known XYZ", "Измеренная площадка / реальные камеры"),
            ("R3 — GPU", "Plane / bowl / image tests", "Швы, UV-маски, целевой GPU"),
            ("R4 — дрейф", "Заданный поворот / recovery", "Overlap / ложные тревоги"),
            ("R5 — архитектура", "Linux IPC / native RPM", "Aurora integration / live source"),
            ("R6 — сравнение", "RTX / Mesa native reports", "Устройство / thermal / display latency")]
    for index, (task, proof, pending) in enumerate(rows):
        y = .84 - index * .14
        box(axis, .02, y, .2, .09, task, BLUE, 9)
        box(axis, .32, y, .27, .09, proof, GREEN, 9)
        box(axis, .69, y, .29, .09, pending, AMBER, 9)
        arrow(axis, (.23, y + .045), (.31, y + .045))
        arrow(axis, (.6, y + .045), (.68, y + .045))
    save(figure, output / "conclusion", "05_evidence.svg")


def street(output, build, work, backend):
    if work.exists():
        raise ValueError("street capture directory must be new")
    work.mkdir(parents=True)
    build = build.resolve()
    config = json.loads((ROOT / "configs/street-demo.json").read_text())
    subprocess.run([str(build / "sv-scene"), "--config", str(ROOT / "configs/street-demo.json"),
                    "--panorama", str(ROOT / "assets/demo/urban_street_01_1k.hdr"), "--output", str(work / "fixture")], check=True)
    names = [("oblique", "Общий ракурс", .8, 1.), ("front", "Со стороны передней камеры", 0., .65),
             ("top", "Вид сверху", 0., math.pi / 2)]
    previews = []
    for name, label, azimuth, elevation in names:
        config["virtual_camera"].update(azimuth_rad=azimuth, elevation_rad=elevation)
        filename = work / (name + ".json")
        filename.write_text(json.dumps(config, indent=2) + "\n")
        subprocess.run([str(build / "sv-bench"), "--config", str(filename), "--manifest", str(work / "fixture/manifest.json"),
                        "--output", str(work / name), "--egl-platform", backend, "--iterations", "2", "--warmup", "1"],
                       check=True, stdout=subprocess.DEVNULL)
        preview = plt.imread(work / name / "preview.ppm")
        folder = output / "implementation"
        folder.mkdir(parents=True, exist_ok=True)
        plt.imsave(folder / ("03_street_" + name + ".png"), preview)
        previews.append((label, preview))
    figure, axes = plt.subplots(1, 3, figsize=(15, 4), layout="constrained")
    for axis, (label, preview) in zip(axes, previews):
        axis.imshow(preview)
        axis.set_title(label, fontsize=11)
        axis.axis("off")
    figure.suptitle("Readback GLES: фотоокружение на внутреннем куполе, пол под автомобилем и виртуальная камера внутри", fontsize=11)
    save(figure, work, "street_views.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=HERE / "figures")
    parser.add_argument("--capture-street", action="store_true")
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--work", type=Path, default=ROOT / "artifacts/street-figures")
    parser.add_argument("--egl-platform", choices=["surfaceless", "device", "default"], default="surfaceless")
    args = parser.parse_args()
    theory(args.output)
    design(args.output)
    implementation(args.output)
    conclusion(args.output)
    if args.capture_street:
        street(args.output, args.build, args.work.resolve(), args.egl_platform)
    print(args.output)
