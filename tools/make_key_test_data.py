# Reference data for tests/key_test.cpp: librosa CQT and OpenKeyScan predictions for a deterministic
# test signal that the C++ test regenerates sample-exactly (see testSignal() there).
#   python tools/make_key_test_data.py <openkeyscan-analyzer dir> tests/data/key_cqt_ref.bin
import struct, sys
import numpy as np, librosa, torch
sys.path.insert(0, sys.argv[1])
from model import KeyNet

def test_signal(sr, root, minor, secs=24.0):
    # I-vi-IV-V (major) or i-iv-v-i-VI-iv-V-i (harmonic minor) with 6 harmonics, 2 s per chord, fixed phases
    t = np.arange(int(sr * secs)) / sr
    scale = [0, 2, 3, 5, 7, 8, 11] if minor else [0, 2, 4, 5, 7, 9, 11]
    degs = [0, 3, 4, 0, 5, 3, 4, 0] if minor else [0, 5, 3, 4]
    x = np.zeros_like(t)
    for i in range(int(secs / 2)):
        d = degs[i % len(degs)]
        seg = (t >= 2 * i) & (t < 2 * i + 2)
        tt = t[seg] - 2 * i
        env = np.exp(-tt * 0.7)
        notes = [scale[(d + j) % 7] + 12 * ((d + j) // 7) for j in (0, 2, 4)]
        y = np.zeros_like(tt)
        for n, gain in [(notes[0] - 12, 0.25)] + [(m, 0.15) for m in notes]:
            f = 220.0 * 2 ** ((root + n - 9) / 12)
            for h in range(6):
                y += gain * 0.6 ** h * np.sin(2 * np.pi * f * (h + 1) * tt + 0.7 * h)
        x[seg] = y * env
    return (0.25 * x).astype(np.float32)

net = KeyNet(24, 1, 64, 0.3)
net.load_state_dict(torch.load(sys.argv[1] + "/checkpoints/openkeyscan3.pt", map_location="cpu")); net.eval()
cases = [(2, 0), (9, 1), (7, 0), (4, 1)]          # D major, A minor, G major, E minor
out = bytearray(struct.pack("<I", len(cases)))
for root, minor in cases:
    x = test_signal(44100, root, minor)
    C = np.log1p(np.abs(librosa.cqt(x, sr=44100, hop_length=8820, n_bins=105, bins_per_octave=24, fmin=65))).astype(np.float32)
    with torch.no_grad():
        cls = int(net(torch.tensor(C[:, :-2])[None, None]).argmax())
    print(root, minor, C.shape, "->", cls)
    out += struct.pack("<iiiI", root, minor, cls, C.shape[1]) + C.tobytes()
open(sys.argv[2], "wb").write(out)
