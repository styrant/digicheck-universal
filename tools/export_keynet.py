# Export the OpenKeyScan KeyNet checkpoint (styrant/openkeyscan-analyzer, MIT, checkpoints/openkeyscan3.pt)
# into the compact blob the app embeds: batch norm folded into each convolution, weights as float16.
#
#   python tools/export_keynet.py <openkeyscan3.pt> Resources/keynet.bin [tests/data/keynet_ref.bin]
#
# Blob layout (little endian): "KNET", u32 version=1, u32 layers, then per layer:
#   u32 cout, u32 cin, u32 k, float16 weights[cout*cin*k*k] (OIHW), float32 bias[cout]
import struct, sys
import numpy as np, torch

sd = torch.load(sys.argv[1], map_location="cpu")
names = [f"conv{i}" for i in range(1, 10)]
out = bytearray(b"KNET") + struct.pack("<II", 1, len(names))
folded = []
for n in names:
    w = sd[f"{n}.conv.weight"].double().numpy()
    g, b = sd[f"{n}.bn.weight"].double().numpy(), sd[f"{n}.bn.bias"].double().numpy()
    m, v = sd[f"{n}.bn.running_mean"].double().numpy(), sd[f"{n}.bn.running_var"].double().numpy()
    s = g / np.sqrt(v + 1e-5)
    wf = (w * s[:, None, None, None]).astype(np.float16)
    bf = (b - m * s).astype(np.float32)
    co, ci, k, _ = w.shape
    out += struct.pack("<III", co, ci, k) + wf.tobytes() + bf.tobytes()
    folded.append((wf.astype(np.float32), bf))
open(sys.argv[2], "wb").write(out)
print("wrote", sys.argv[2], len(out), "bytes")

if len(sys.argv) > 3:
    # reference: a fixed pseudo-random log-CQT-like input and the PyTorch logits, for the C++ unit test
    sys.path.insert(0, ".")
    from model import KeyNet
    net = KeyNet(24, 1, 64, 0.3); net.load_state_dict(sd); net.eval()
    rng = np.random.default_rng(7)
    T = 60
    x = (np.abs(rng.standard_normal((105, T))) * 0.6 + np.linspace(0, 1, 105)[:, None] * 0.4).astype(np.float32)
    with torch.no_grad():
        y = net(torch.tensor(x)[None, None])[0].numpy().astype(np.float32)
    open(sys.argv[3], "wb").write(struct.pack("<II", 105, T) + x.tobytes() + y.tobytes())
    # float16-folded numpy forward, to report the quantisation error
    import torch.nn.functional as F
    h = torch.tensor(x)[None, None]
    for i, (wf, bf) in enumerate(folded):
        h = F.elu(F.conv2d(h, torch.tensor(wf), torch.tensor(bf), padding="same"))
        if i in (1, 3, 5): h = F.max_pool2d(h, 2)
    y16 = h.mean(dim=(2, 3))[0].numpy()
    print("logits max |fp16 - torch|:", np.abs(y16 - y).max(), " argmax", y.argmax(), y16.argmax())
