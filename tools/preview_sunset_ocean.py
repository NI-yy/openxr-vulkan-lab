"""Deterministic, approximate single-eye preview of ocean.frag.

This is a CPU translation for visual iteration, not a Vulkan conformance test.
The camera is fixed at the LOCAL origin, facing -Z with a symmetric 90° FOV.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--size", type=int, default=768, help="Square image size in pixels")
parser.add_argument("--output", type=Path,
                    default=REPO / "docs" / "sunset-ocean-host-preview.png")
args = parser.parse_args()
if args.size < 16:
    parser.error("--size must be at least 16")
W = H = args.size
xs = (np.arange(W, dtype=np.float32) + 0.5) / W
ys = (np.arange(H, dtype=np.float32) + 0.5) / H
xx, yy = np.meshgrid(xs, ys)
ray = np.stack((2 * xx - 1, 1 - 2 * yy, -np.ones_like(xx)), axis=-1)
ray /= np.linalg.norm(ray, axis=-1, keepdims=True)
sun = np.array([0.08, 0.055, -1.0], dtype=np.float32)
sun /= np.linalg.norm(sun)

def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)

def sky(direction, disc):
    height = np.maximum(direction[..., 1], 0)
    sun_angle = np.clip(np.sum(direction * sun, axis=-1), -1, 1)
    haze = np.exp(-height * 8)
    air_mass = 1 / (height + .08)
    transmission = np.exp(-.055 * air_mass)
    rayleigh = .75 * (1 + sun_angle * sun_angle)
    g = .76
    mie_base = 1 + g * g - 2 * g * sun_angle
    mie = (1 - g * g) / (mie_base * np.sqrt(mie_base))
    upper_mix = np.exp(-height * 2.3)
    upper = (np.array([.03, .075, .22]) * (1 - upper_mix)[..., None]
             + np.array([.12, .19, .38]) * upper_mix[..., None])
    warm_mix = np.maximum(sun_angle, 0) ** 2
    distant = (np.array([.25, .14, .29]) * (1 - warm_mix)[..., None]
               + np.array([1.05, .29, .09]) * warm_mix[..., None])
    color = upper * (1 - haze[..., None] * .78) + distant * (haze * .78)[..., None]
    color += np.array([.065, .10, .19]) * (rayleigh * (1 - np.exp(-.12 * air_mass)) * (1 - haze * .5))[..., None]
    color += np.array([1, .42, .12]) * (mie * haze * transmission * .055)[..., None]
    color += np.array([.9, .44, .20]) * (np.maximum(sun_angle, 0) ** 30 * haze * .16)[..., None]
    if disc:
        color += np.array([1, .60, .28]) * (smooth(.99965, .99982, sun_angle) * 3.2)[..., None]
    return color

color = sky(ray, True)
distance = -1.5 / np.minimum(ray[..., 1], -.001)
point = ray[..., [0, 2]] * np.clip(distance, 0, 2000)[..., None]
dpx = np.abs(np.gradient(point, axis=1))
dpy = np.abs(np.gradient(point, axis=0))
foot = np.maximum(dpx, dpy)
ks = [np.array(v) for v in ((.72, .33), (-.41, 1.17), (2.37, -1.36), (-3.93, -4.21))]
bend0 = np.array([-.24, .42])
bend1 = np.array([.39, .18])
b0 = np.sum(point * bend0, axis=-1)
b1 = np.sum(point * bend1, axis=-1)
p0 = np.sum(point * ks[0], axis=-1) + .90 * np.sin(b0)
p1 = np.sum(point * ks[1], axis=-1) + .75 * np.sin(b1)
p2 = np.sum(point * ks[2], axis=-1) + .55 * np.sin(p0)
p3 = np.sum(point * ks[3], axis=-1) + .50 * np.sin(p1)
dp0 = ks[0] + (.90 * np.cos(b0))[..., None] * bend0
dp1 = ks[1] + (.75 * np.cos(b1))[..., None] * bend1
vis = [np.exp(-.20 * np.sum(np.abs(k) * foot, axis=-1) ** 2) for k in ks]
slope = (.080 * vis[0] * np.cos(p0))[..., None] * dp0
slope += (.050 * vis[1] * np.cos(p1))[..., None] * dp1
slope += (.014 * vis[2] * np.cos(p2))[..., None] * (ks[2] + (.55 * np.cos(p0))[..., None] * dp0)
slope += (.005 * vis[3] * np.cos(p3))[..., None] * (ks[3] + (.50 * np.cos(p1))[..., None] * dp1)
slope *= (1 - smooth(65, 550, distance))[..., None]
normal = np.stack((-slope[..., 0], np.ones_like(xx), -slope[..., 1]), axis=-1)
normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
reflected = ray - 2 * np.sum(ray * normal, axis=-1, keepdims=True) * normal
view_cos = np.maximum(np.sum(-ray * normal, axis=-1), 0)
fresnel = .02 + .98 * (1 - view_cos) ** 5
water = np.array([.013, .050, .073]) * (1 - (.10 + .86 * fresnel))[..., None]
water += sky(reflected, False) * (.10 + .86 * fresnel)[..., None]
half = sun - ray
half /= np.linalg.norm(half, axis=-1, keepdims=True)
ndh = np.maximum(np.sum(normal * half, axis=-1), 0)
rough = .19 + (.34 - .19) * np.clip(np.linalg.norm(foot, axis=-1) * .18, 0, 1)
a2 = rough * rough
denom = ndh * ndh * (a2 - 1) + 1
distribution = a2 / (np.pi * denom * denom)
water += np.array([1, .56, .23]) * (distribution * fresnel * .32)[..., None]
horizon_ray = np.stack((ray[..., 0], np.full_like(xx, .003), ray[..., 2]), axis=-1)
horizon_ray /= np.linalg.norm(horizon_ray, axis=-1, keepdims=True)
far_sky = sky(horizon_ray, False)
fade = 1 - smooth(180, 1700, distance)
water = far_sky * (1 - fade[..., None]) + water * fade[..., None]
mask = (ray[..., 1] < -.001) & (distance > 0)
color = np.where(mask[..., None], water, color)
lum = np.sum(color * np.array([.2126, .7152, .0722]), axis=-1)
color = color / (1 + lum[..., None])
color = np.where(color <= .0031308, color * 12.92, 1.055 * color ** (1 / 2.4) - .055)
args.output.parent.mkdir(parents=True, exist_ok=True)
Image.fromarray(np.uint8(np.clip(color, 0, 1) * 255)).save(args.output)
print(args.output)
