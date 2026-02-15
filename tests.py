# download_blip_fast.py
from huggingface_hub import snapshot_download
from pathlib import Path

# Model to download
model_id = "Salesforce/blip-image-captioning-large"

# Destination folder
dest = Path(r"C:\Users\bismi\Downloads\image-understand\models\blip-image-captioning-large")

# Fast download with aggressive but safe threading
path = snapshot_download(
    repo_id=model_id,
    local_dir=dest,
    local_dir_use_symlinks=False,  # harmless on Windows
    max_workers=32                 # aggressive but safe for Ryzen 5
)

print("✅ Download complete! Model saved to:", path)

