import os, numpy as np
os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
import cv2

EXR = os.path.expandvars(r"%TEMP%\starmap\hiptyc_2020_16k.exr")
img = cv2.imread(EXR, cv2.IMREAD_UNCHANGED | cv2.IMREAD_ANYDEPTH)
lum = img[..., 0]*0.114 + img[..., 1]*0.587 + img[..., 2]*0.299
H, W = lum.shape

stars = {
    "Sirius":    (101.287, -16.716),
    "Canopus":   ( 95.988, -52.696),
    "Arcturus":  (213.915,  19.182),
    "Vega":      (279.234,  38.784),
    "Rigel":     ( 78.634,  -8.202),
    "Betelgeuse":( 88.793,   7.407),
    "Capella":   ( 79.172,  45.998),
    "Procyon":   (114.825,   5.225),
    "Aldebaran": ( 68.980,  16.509),
    "Spica":     (201.298, -11.161),
    "Antares":   (247.352, -26.432),
    "Pollux":    (116.329,  28.026),
    "Fomalhaut": (344.413, -29.622),
    "Deneb":     (310.358,  45.280),
    "Regulus":   (152.093,  11.967),
    "Altair":    (297.696,   8.868),
}

def predict(ra, dec, ra_origin, ra_dir, dec_up):
    if ra_dir > 0:
        u = ((ra - ra_origin) % 360) / 360.0
    else:
        u = ((ra_origin - ra) % 360) / 360.0
    v = (90.0 - dec)/180.0 if dec_up else (dec + 90.0)/180.0
    return u, v

def window_max(u, v, rad=4):
    c = int(u*W) % W
    r = min(max(int(v*H), 0), H-1)
    r0, r1 = max(0, r-rad), min(H, r+rad+1)
    # handle horizontal wrap
    cs = [(c+dc) % W for dc in range(-rad, rad+1)]
    sub = lum[r0:r1][:, cs]
    return float(sub.max())

print(f"{'convention':32s}  hits(>0.3)/N   mean_max")
best = None
for ra_origin in (0.0, 180.0):
    for ra_dir in (+1, -1):
        for dec_up in (True, False):
            maxes = []
            for ra, dec in stars.values():
                u, v = predict(ra, dec, ra_origin, ra_dir, dec_up)
                maxes.append(window_max(u, v))
            maxes = np.array(maxes)
            hits = int((maxes > 0.3).sum())
            tag = f"origin={ra_origin:5.0f} dir={ra_dir:+d} dec_up={int(dec_up)}"
            print(f"{tag:32s}  {hits:2d}/{len(stars)}       {maxes.mean():.3f}")
            score = (hits, maxes.mean())
            if best is None or score > best[0]:
                best = (score, tag)
print("\nBEST:", best[1], "->", best[0])
