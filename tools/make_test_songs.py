# Generates two "genre" folders of short WAV files for the TargetBuilder smoke test in CI
# (standard library only): GenreA has much more low end than GenreB.
import math, os, random, struct, sys, wave

out = sys.argv[1] if len(sys.argv) > 1 else "tb_lib"
for genre, low in (("GenreA", 0.9), ("GenreB", 0.2)):
    os.makedirs(os.path.join(out, genre), exist_ok=True)
    for i in range(3):
        rng = random.Random(i)
        lp = 0.0
        frames = bytearray()
        for n in range(48000 * 8):
            w = rng.uniform(-1, 1)
            lp += 0.02 * (w - lp)
            v = max(-1.0, min(1.0, 0.3 * (0.2 * w + low * 6 * lp)))
            s = int(v * 32767)
            frames += struct.pack("<hh", s, s)
        with wave.open(os.path.join(out, genre, f"song{i}.wav"), "wb") as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(48000); f.writeframes(bytes(frames))
