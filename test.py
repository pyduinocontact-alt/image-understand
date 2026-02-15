# download_blip_fast.py
from huggingface_hub import snapshot_download
from pathlib import Path
import os

# Model to download
model_id = "microsoft/git-base-textcaps"


# Destination folder
dest = Path(r"C:\Users\bismi\Downloads\image-understand\models\microsoft\git-base-textcaps")

# Optional token (for gated/private models)
hf_token = os.getenv("HF_TOKEN") or ""

# Skip non-essential files to keep download smaller
ignore_patterns = [
    "*.h5", "*.ot", "*.msgpack", "*.safetensors.index.json",
    "tf_model.h5", "model.ckpt*", "flax_model.msgpack",
    "*.tflite", "*.onnx", "*.pb"
]

# Prefer safetensors to avoid duplicate weights when available
prefer_safetensors = True
weight_ignore = []
if prefer_safetensors:
    weight_ignore = ["*.bin"]

# Fast download with aggressive but safe threading
path = snapshot_download(
    repo_id=model_id,
    local_dir=dest,
    local_dir_use_symlinks=False,  # harmless on Windows
    max_workers=32,                # aggressive but safe for Ryzen 5
    token=hf_token or None,
    ignore_patterns=ignore_patterns + weight_ignore,
)

# If no safetensors were downloaded (repo doesn't have them), retry with .bin
if prefer_safetensors:
    has_safetensors = any(p.suffix == ".safetensors" for p in dest.rglob("*.safetensors"))
    if not has_safetensors:
        print("No .safetensors found. Re-downloading .bin weights...")
        path = snapshot_download(
            repo_id=model_id,
            local_dir=dest,
            local_dir_use_symlinks=False,
            max_workers=32,
            token=hf_token or None,
            ignore_patterns=ignore_patterns,  # allow .bin
        )

print("✅ Download complete! Model saved to:", path)
