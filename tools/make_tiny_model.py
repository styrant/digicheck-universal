"""Write a tiny, randomly initialised MuScriptor checkpoint (GGUF) for smoke tests.

Same file layout, metadata keys and audio front end as the real models that NeuralNote v2 downloads
(muscriptor-small/medium/large-f16.gguf), but a 1-layer, 64-wide transformer with random weights:
about 1 MB instead of 210 MB+. It loads and runs through the whole engine (STFT, mel, transformer,
decoding, note assembly), so CI can test the transcription path without downloading the real model.
Its notes are meaningless.

    python tools/make_tiny_model.py out/tiny.gguf          (needs: pip install gguf numpy)
"""

import sys

import numpy as np
import gguf

ARCH = "muscriptor"
DIM, HEADS, FFN, LAYERS = 64, 4, 128, 1
VOCAB, INSTRUMENTS, DATASETS = 1393, 1001, 5
N_FFT, N_MELS = 2048, 512


def main(path: str) -> None:
    rng = np.random.default_rng(1234)

    def w(*shape, dtype=np.float16, scale=0.05):
        return (rng.standard_normal(shape) * scale).astype(dtype)

    t = {
        "cond.dataset_name.weight": w(DATASETS, DIM, dtype=np.float32),
        "cond.instrument_group.weight": w(INSTRUMENTS, DIM, dtype=np.float32),
        "cond.mel_fb.weight": np.abs(w(N_MELS, N_FFT // 2 + 1, dtype=np.float32, scale=0.01)),
        "cond.stft_window": np.hanning(N_FFT).astype(np.float32),
        "cond.proj.bias": np.zeros(DIM, np.float32),
        "cond.proj.weight": w(DIM, N_MELS, dtype=np.float32),
        "token_embd.weight": w(VOCAB + 1, DIM),
        "output.weight": w(VOCAB, DIM),
        "output_norm.bias": np.zeros(DIM, np.float32),
        "output_norm.weight": np.ones(DIM, np.float32),
    }
    for i in range(LAYERS):
        t |= {
            f"blk.{i}.attn_norm.weight": np.ones(DIM, np.float32),
            f"blk.{i}.attn_norm.bias": np.zeros(DIM, np.float32),
            f"blk.{i}.ffn_norm.weight": np.ones(DIM, np.float32),
            f"blk.{i}.ffn_norm.bias": np.zeros(DIM, np.float32),
            f"blk.{i}.attn_qkv.weight": w(3 * DIM, DIM),
            f"blk.{i}.attn_out.weight": w(DIM, DIM),
            f"blk.{i}.ffn_up.weight": w(FFN, DIM),
            f"blk.{i}.ffn_down.weight": w(DIM, FFN),
        }

    writer = gguf.GGUFWriter(path, ARCH)
    writer.add_type("model")
    writer.add_name("MuScriptor tiny (random weights, for tests)")
    writer.add_file_type(int(gguf.LlamaFileType.MOSTLY_F16))
    writer.add_int32(f"{ARCH}.format_version", 1)
    writer.add_string(f"{ARCH}.license.notice", "Random weights for testing only.")
    for key, value in {
        "embedding_length": DIM, "block_count": LAYERS, "attention.head_count": HEADS,
        "attention.head_dim": DIM // HEADS, "feed_forward_length": FFN, "vocab_size": VOCAB,
        "initial_token_id": VOCAB, "logit_mask_start": VOCAB,
        "audio.sample_rate": 16000, "audio.n_fft": N_FFT, "audio.hop_length": 160,
        "audio.frame_rate": 100, "audio.n_mels": N_MELS,
    }.items():
        writer.add_int32(f"{ARCH}.{key}", value)
    writer.add_float32(f"{ARCH}.attention.layer_norm_epsilon", 1e-5)
    writer.add_float32(f"{ARCH}.position_embedding.max_period", 10000.0)
    writer.add_float32(f"{ARCH}.audio.log_eps", 1e-6)
    writer.add_string(f"{ARCH}.source.revision", "tiny-test")
    writer.add_string(f"{ARCH}.source.sha256", "0" * 64)
    writer.add_string(f"{ARCH}.source.weight_dtype", "f16")
    for name, array in t.items():
        writer.add_tensor(name, np.ascontiguousarray(array))
    writer.write_header_to_file()
    writer.write_kv_data_to_file()
    writer.write_tensors_to_file()
    writer.close()
    print(f"wrote {path}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "tiny.gguf")
