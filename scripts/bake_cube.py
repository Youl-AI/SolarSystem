"""
Bake an equatorial (RA/Dec) equirectangular star map into a Vulkan cube map,
using a convention we fully control so the constellation catalog aligns by
construction (kAlign = identity).

Cube texel direction D  ->  (RA,Dec) = raDecToDir^-1(D)  ->  equirect(u,v):
    u = ((180 - RA) mod 360) / 360      (verified 16/16 bright stars)
    v = (90 - Dec) / 180

raDecToDir:  x=cos(dec)cos(ra), y=sin(dec), z=cos(dec)sin(ra)
  => dec = asin(y),  ra = atan2(z, x)

Cube face convention (Vulkan/GL/D3D shared 'Cube Map Face Selection'):
  s = 2*(col+0.5)/N - 1 ,  t = 2*(row+0.5)/N - 1   (row 0 = top)
  +X: (  1, -t, -s)   -X: ( -1, -t,  s)
  +Y: (  s,  1,  t)   -Y: (  s, -1, -t)
  +Z: (  s, -t,  1)   -Z: ( -s, -t, -1)
Face order written to DDS array layers 0..5: +X,-X,+Y,-Y,+Z,-Z
"""
import os, sys, struct, numpy as np
os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
import cv2

def face_dirs(face, N):
    j = (np.arange(N, dtype=np.float64) + 0.5)
    s = 2.0 * j / N - 1.0            # varies along columns
    t = 2.0 * j / N - 1.0            # varies along rows
    S, T = np.meshgrid(s, t)         # S[row,col]=s(col), T[row,col]=t(row)
    one = np.ones_like(S)
    if   face == 0: d = ( one, -T, -S)   # +X
    elif face == 1: d = (-one, -T,  S)   # -X
    elif face == 2: d = (   S, one,  T)  # +Y
    elif face == 3: d = (   S, -one, -T) # -Y
    elif face == 4: d = (   S, -T, one)  # +Z
    elif face == 5: d = (  -S, -T, -one) # -Z
    x, y, z = d
    n = np.sqrt(x*x + y*y + z*z)
    return x/n, y/n, z/n

def sample_equirect(rgb, ue, ve):
    H, W, _ = rgb.shape
    fx = ue * W - 0.5
    fy = ve * H - 0.5
    x0 = np.floor(fx).astype(np.int64); y0 = np.floor(fy).astype(np.int64)
    dx = (fx - x0)[..., None]; dy = (fy - y0)[..., None]
    x0m = x0 % W; x1m = (x0 + 1) % W
    y0c = np.clip(y0, 0, H-1); y1c = np.clip(y0 + 1, 0, H-1)
    c00 = rgb[y0c, x0m]; c10 = rgb[y0c, x1m]
    c01 = rgb[y1c, x0m]; c11 = rgb[y1c, x1m]
    top = c00*(1-dx) + c10*dx
    bot = c01*(1-dx) + c11*dx
    return top*(1-dy) + bot*dy

def bake_face(rgb, face, N):
    x, y, z = face_dirs(face, N)
    dec = np.arcsin(np.clip(y, -1, 1))          # radians
    ra = np.arctan2(z, x)                        # radians, (-pi,pi]
    ra_deg = np.degrees(ra) % 360.0
    dec_deg = np.degrees(dec)
    ue = ((180.0 - ra_deg) % 360.0) / 360.0
    ve = (90.0 - dec_deg) / 180.0
    return sample_equirect(rgb, ue, ve).astype(np.float32)  # (N,N,3) RGB

def write_cube_dds_fp32(path, faces, N):
    # DDS + DX10 header, DXGI_FORMAT_R32G32B32A32_FLOAT, cubemap, 1 mip
    DDSD = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000   # caps,h,w,pixfmt,linearsize
    CAPS = 0x1000 | 0x8                          # texture|complex
    CAPS2 = 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000  # cubemap+6
    pitch = N * 16
    hdr = struct.pack('<I', 0x20534444)          # 'DDS '
    hdr += struct.pack('<7I', 124, DDSD, N, N, pitch, 0, 1)  # size,flags,h,w,pitch,depth,mips
    hdr += struct.pack('<11I', *([0]*11))        # reserved
    # pixelformat (32 bytes): size,flags=FOURCC,fourCC='DX10',...
    hdr += struct.pack('<2I', 32, 0x4)
    hdr += b'DX10'
    hdr += struct.pack('<5I', 0, 0, 0, 0, 0)
    hdr += struct.pack('<4I', CAPS, CAPS2, 0, 0)
    hdr += struct.pack('<I', 0)                   # reserved2
    # DX10 header: dxgiFormat=2, dim=3(TEX2D), misc=0x4(CUBE), arraySize=1, misc2=0
    hdr += struct.pack('<5I', 2, 3, 0x4, 1, 0)
    assert len(hdr) == 148, len(hdr)
    with open(path, 'wb') as f:
        f.write(hdr)
        for face in faces:  # order +X,-X,+Y,-Y,+Z,-Z
            rgba = np.empty((N, N, 4), np.float32)
            rgba[..., :3] = face
            rgba[..., 3] = 1.0
            f.write(rgba.tobytes())

def main():
    src = sys.argv[1]
    out = sys.argv[2]
    N = int(sys.argv[3])
    print(f"loading {src} ...", flush=True)
    img = cv2.imread(src, cv2.IMREAD_UNCHANGED | cv2.IMREAD_ANYDEPTH)
    if img is None:
        raise SystemExit("failed to read EXR")
    rgb = img[..., ::-1].astype(np.float32).copy()   # BGR->RGB
    print(f"  equirect {rgb.shape}, baking {N}x{N} x6 ...", flush=True)
    faces = []
    for face in range(6):
        faces.append(bake_face(rgb, face, N))
        print(f"  face {face} done", flush=True)
    write_cube_dds_fp32(out, faces, N)
    print(f"wrote {out} ({os.path.getsize(out)/1e6:.1f} MB)")

if __name__ == "__main__":
    main()
