#!/usr/bin/env python3
"""Conceptual wireframes for the surface-carrier research note (not measurements)."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


OUT = Path(__file__).parent / "figures" / "projection_models.png"


def axes3d(ax, title):
    ax.set_title(title, fontsize=10, pad=1)
    ax.set_box_aspect((1, 1, 0.8))
    ax.view_init(elev=22, azim=-48)
    ax.set_xlim(-1.35, 1.35)
    ax.set_ylim(-1.35, 1.35)
    ax.set_zlim(-0.05, 1.9)
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_zticks([])
    ax.set_axis_off()


def plot_bowl(ax):
    x = np.linspace(-1, 1, 25)
    y = np.linspace(-1, 1, 25)
    X, Y = np.meshgrid(x, y)
    dx = np.maximum(np.abs(X) - 0.42, 0) / 0.58
    dy = np.maximum(np.abs(Y) - 0.42, 0) / 0.58
    Z = 1.0 * (dx**2 + dy**2) / 2
    ax.plot_wireframe(X, Y, Z, rstride=3, cstride=3, color="#2878a8", linewidth=0.7)


def plot_dome_floor(ax):
    phi = np.linspace(0, 2 * np.pi, 33)
    lat = np.linspace(0, np.pi / 2, 13)
    P, L = np.meshgrid(phi, lat)
    ax.plot_wireframe(np.cos(L) * np.cos(P), np.cos(L) * np.sin(P),
                      np.sin(L), rstride=2, cstride=4,
                      color="#2878a8", linewidth=0.65)
    r = np.linspace(0, 1, 5)
    A, R = np.meshgrid(phi, r)
    ax.plot_wireframe(R * np.cos(A), R * np.sin(A), np.zeros_like(R),
                      rstride=1, cstride=4, color="#b16a25", linewidth=0.65)


def plot_cylinder_floor(ax):
    phi = np.linspace(0, 2 * np.pi, 33)
    z = np.linspace(0, 1.8, 13)
    P, Z = np.meshgrid(phi, z)
    ax.plot_wireframe(np.cos(P), np.sin(P), Z, rstride=3, cstride=4,
                      color="#2878a8", linewidth=0.65)
    r = np.linspace(0, 1, 5)
    A, R = np.meshgrid(phi, r)
    ax.plot_wireframe(R * np.cos(A), R * np.sin(A), np.zeros_like(R),
                      rstride=1, cstride=4, color="#b16a25", linewidth=0.65)


def plot_cube(ax):
    t = np.linspace(-1, 1, 9)
    T, H = np.meshgrid(t, t)
    faces = [
        (np.ones_like(T), T, H + 0.9), (-np.ones_like(T), T, H + 0.9),
        (T, np.ones_like(T), H + 0.9), (T, -np.ones_like(T), H + 0.9),
        (T, H, np.ones_like(T) + 0.9), (T, H, -np.ones_like(T) + 0.9),
    ]
    for X, Y, Z in faces:
        ax.plot_wireframe(X, Y, Z, rstride=2, cstride=2,
                          color="#2878a8", linewidth=0.65)


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig = plt.figure(figsize=(12.0, 7.0), layout="constrained")
    panels = [
        ("Плоскость", lambda ax: ax.plot_surface(*np.meshgrid(np.linspace(-1, 1, 12),
                                                           np.linspace(-1, 1, 12)),
                                                np.zeros((12, 12)), color="#76a9c4",
                                                alpha=0.75, edgecolor="#2878a8",
                                                linewidth=0.35)),
        ("Прямоугольная чаша", plot_bowl),
        ("Купол + пол", plot_dome_floor),
        ("Цилиндр + пол", plot_cylinder_floor),
        ("Кубическая оболочка", plot_cube),
    ]
    for i, (title, draw) in enumerate(panels, start=1):
        ax = fig.add_subplot(2, 3, i, projection="3d")
        axes3d(ax, title)
        draw(ax)
        if title in ("Купол + пол", "Цилиндр + пол"):
            ax.scatter([0], [0], [0.35], s=20, color="#bd3f36", depthshade=False)
    fig.suptitle("Варианты поверхности-носителя для одних и тех же камер", fontsize=14)
    fig.text(0.5, 0.01,
             "Концептуальные сетки: масштаб условный; качество текстур и производительность не измерялись.",
             ha="center", fontsize=9)
    fig.savefig(OUT, dpi=180)


if __name__ == "__main__":
    main()
