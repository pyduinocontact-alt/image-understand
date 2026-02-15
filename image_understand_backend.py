#!/usr/bin/env python3
"""
Image Understanding Backend for C++ GUI
This script is called by the C++ application and sends progress updates
"""

import sys
import os
import json
import argparse
from pathlib import Path
import subprocess
import importlib
import importlib.util
import inspect
import shutil
# Heavy imports are deferred to provide progress updates during startup.

# Ensure stdout/stderr are safe for Windows consoles
try:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


# -------- Model Cache Manager --------
class ModelCache:
    """Manages cached model paths"""
    
    def __init__(self, model_name):
        self.cache_file = Path.home() / ".image_understand_cache.json"
        self.default_cache_dir = Path.home() / ".cache" / "image_understand_models"
        self.model_name = model_name
        
    def get_cache_info(self):
        """Load cache information from file"""
        if self.cache_file.exists():
            try:
                with open(self.cache_file, 'r') as f:
                    return json.load(f)
            except:
                return {}
        return {}
    
    def save_cache_info(self, model_path):
        """Save cache information to file"""
        cache_info = {
            "model_path": str(model_path),
            "model_name": self.model_name,
            "cached_at": str(Path(model_path).stat().st_mtime)
        }
        
        self.cache_file.parent.mkdir(parents=True, exist_ok=True)
        with open(self.cache_file, 'w') as f:
            json.dump(cache_info, f, indent=2)
    
    def get_cached_model_path(self):
        """Get the cached model path if it exists"""
        cache_info = self.get_cache_info()
        if cache_info and "model_path" in cache_info and cache_info.get("model_name") == self.model_name:
            model_path = Path(cache_info["model_path"])
            if model_path.exists():
                return model_path
        return None
    
    def find_downloaded_model(self):
        """Try to find already downloaded model in common locations"""
        try:
            from huggingface_hub.constants import HF_HUB_CACHE
            repo_dir = self.model_name.replace("/", "--")
            cache_dir = Path(HF_HUB_CACHE) / f"models--{repo_dir}"
            if cache_dir.exists():
                return cache_dir
        except:
            pass
        
        if self.default_cache_dir.exists():
            return self.default_cache_dir
        
        return None


def send_progress(message, percentage, gui_mode=False, step=None, total_steps=None):
    """Send progress update to GUI and CLI"""
    if gui_mode:
        # Format for C++ parsing: PROGRESS:message:percentage
        print(f"PROGRESS:{message}:{percentage}", flush=True)
    # Always print to CLI with step context if provided
    if step is not None and total_steps is not None:
        print(f"[{percentage}%] [{step}/{total_steps}] {message}")
    else:
        print(f"[{percentage}%] {message}")
    sys.stdout.flush()


def human_bytes(num):
    try:
        num = float(num)
    except Exception:
        return str(num)
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if num < 1024.0:
            return f"{num:0.1f}{unit}"
        num /= 1024.0
    return f"{num:0.1f}PB"


def run_pip(args, gui_mode=False):
    send_progress(f"Installing: {' '.join(args)}", 9, gui_mode)
    cmd = [sys.executable, "-m", "pip"] + args
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
        if proc.stdout:
            print(proc.stdout.strip())
        if proc.stderr:
            print(proc.stderr.strip())
        return True
    except subprocess.CalledProcessError as e:
        if e.stdout:
            print(e.stdout.strip())
        if e.stderr:
            print(e.stderr.strip(), file=sys.stderr)
        return False


def ensure_import(module_name, package_name=None, gui_mode=False, upgrade=False):
    try:
        return importlib.import_module(module_name)
    except Exception:
        pkg = package_name or module_name
        args = ["install", pkg]
        if upgrade:
            args = ["install", "-U", pkg]
        ok = run_pip(args, gui_mode=gui_mode)
        if not ok:
            raise
        return importlib.import_module(module_name)


def safe_getattr(module, name):
    try:
        return getattr(module, name)
    except Exception:
        return None


def create_processor(cls, model_path, local_files_only=False):
    try:
        sig = inspect.signature(cls.from_pretrained)
        if "use_fast" in sig.parameters:
            return cls.from_pretrained(model_path, local_files_only=local_files_only, use_fast=False)
    except Exception:
        pass
    return cls.from_pretrained(model_path, local_files_only=local_files_only)


def save_embedding(tensor, path):
    ext = str(Path(path).suffix).lower()
    if ext == ".npy":
        import numpy as np
        np.save(path, tensor.detach().cpu().numpy())
    else:
        # Default to torch save
        import torch
        torch.save(tensor.detach().cpu(), path)


def _detect_model_type(model_id, transformers, local_only=False):
    model_id_l = str(model_id).lower()
    if "qwen2-vl" in model_id_l or "qwen2_vl" in model_id_l:
        return "qwen2_vl"

    # Try local config.json
    try:
        cfg_path = Path(model_id) / "config.json"
        if cfg_path.exists():
            with open(cfg_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            mt = str(data.get("model_type", "")).lower()
            if mt:
                return mt
    except Exception:
        pass

    # Try AutoConfig for remote or local
    try:
        auto_cfg = safe_getattr(transformers, "AutoConfig")
        if auto_cfg is not None:
            cfg = auto_cfg.from_pretrained(model_id, local_files_only=local_only)
            mt = str(getattr(cfg, "model_type", "")).lower()
            if mt:
                return mt
    except Exception:
        pass

    return ""


def _from_pretrained_with_optional_trust(cls, model_path, local_only=False, extra_kwargs=None):
    extra_kwargs = extra_kwargs or {}
    try:
        sig = inspect.signature(cls.from_pretrained)
        kwargs = {"local_files_only": local_only}
        if "trust_remote_code" in sig.parameters:
            kwargs["trust_remote_code"] = True
        for key, value in extra_kwargs.items():
            if key in sig.parameters:
                kwargs[key] = value
        return cls.from_pretrained(model_path, **kwargs)
    except Exception:
        pass
    return cls.from_pretrained(model_path, local_files_only=local_only)


def _build_effective_prompt(context=False, prompt=None, system_prompt=None):
    effective_prompt = None
    if prompt:
        effective_prompt = prompt
    elif context:
        effective_prompt = "a detailed description of the image, including objects, actions, environment, colors, and relationships:"
    if effective_prompt and system_prompt:
        effective_prompt = f"{system_prompt}\n{effective_prompt}"
    return effective_prompt


def generate_caption_qwen2_vl(image, model_path, gui_mode=False, local_only=False, context=False, prompt=None, system_prompt=None, max_tokens=None):
    transformers = ensure_import("transformers", "transformers", gui_mode=gui_mode)
    processor_cls = safe_getattr(transformers, "AutoProcessor")
    qwen_cls = safe_getattr(transformers, "Qwen2VLForConditionalGeneration")
    auto_v2s = safe_getattr(transformers, "AutoModelForVision2Seq")
    auto_causal = safe_getattr(transformers, "AutoModelForCausalLM")

    if processor_cls is None:
        raise RuntimeError("AutoProcessor not available in transformers. Please upgrade transformers.")

    # Prefer slow processor if supported to avoid fast-processor warnings.
    try:
        sig = inspect.signature(processor_cls.from_pretrained)
        if "use_fast" in sig.parameters:
            processor = processor_cls.from_pretrained(model_path, local_files_only=local_only, use_fast=False)
        else:
            processor = processor_cls.from_pretrained(model_path, local_files_only=local_only)
    except Exception:
        processor = processor_cls.from_pretrained(model_path, local_files_only=local_only)
    model_cls = qwen_cls or auto_v2s or auto_causal
    if model_cls is None:
        raise RuntimeError("Qwen2-VL model class not available in transformers. Please upgrade transformers.")

    extra_kwargs = {"device_map": "cpu", "low_cpu_mem_usage": True}
    try:
        torch = ensure_import("torch", "torch", gui_mode=gui_mode)
        extra_kwargs["torch_dtype"] = torch.float32
    except Exception:
        pass
    model = _from_pretrained_with_optional_trust(model_cls, model_path, local_only=local_only, extra_kwargs=extra_kwargs)

    if max_tokens:
        try:
            max_tokens = int(max_tokens)
        except Exception:
            max_tokens = None

    user_text = prompt or (context and "Describe the image in detail.") or "Describe the image."

    if hasattr(processor, "apply_chat_template"):
        messages = []
        if system_prompt:
            messages.append({"role": "system", "content": system_prompt})
        messages.append({
            "role": "user",
            "content": [
                {"type": "image"},
                {"type": "text", "text": user_text}
            ],
        })
        text = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
        inputs = processor(text=[text], images=[image], return_tensors="pt")
    else:
        effective_prompt = _build_effective_prompt(context=context, prompt=user_text, system_prompt=system_prompt)
        inputs = processor(images=image, text=effective_prompt or user_text, return_tensors="pt")

    out = model.generate(**inputs, max_new_tokens=max_tokens or 128)
    if hasattr(processor, "batch_decode"):
        caption = processor.batch_decode(out, skip_special_tokens=True)[0]
    else:
        caption = processor.decode(out[0], skip_special_tokens=True)
    return caption.strip()


def generate_caption_git(image, model_path, gui_mode=False, local_only=False, context=False, prompt=None, system_prompt=None, max_tokens=None):
    transformers = ensure_import("transformers", "transformers", gui_mode=gui_mode)
    processor_cls = safe_getattr(transformers, "GitProcessor")
    model_cls = safe_getattr(transformers, "GitForCausalLM")
    if processor_cls is None or model_cls is None:
        raise RuntimeError("GIT classes not available in transformers. Please upgrade transformers.")

    processor = create_processor(processor_cls, model_path, local_files_only=local_only)
    model = _from_pretrained_with_optional_trust(model_cls, model_path, local_only=local_only)

    if max_tokens:
        try:
            max_tokens = int(max_tokens)
        except Exception:
            max_tokens = None

    # GIT primarily uses image inputs; optional prompt can guide captioning.
    effective_prompt = _build_effective_prompt(context=context, prompt=prompt, system_prompt=system_prompt)
    if effective_prompt:
        inputs = processor(images=image, text=effective_prompt, return_tensors="pt")
    else:
        inputs = processor(images=image, return_tensors="pt")
    out = model.generate(**inputs, max_new_tokens=max_tokens or 64)
    caption = processor.batch_decode(out, skip_special_tokens=True)[0]
    return caption.strip()


def generate_caption(image, gui_mode=False, model_id="Salesforce/blip-image-captioning-base", save_when_downloaded=None, hf_token=None, max_workers=None, context=False, prompt=None, system_prompt=None, max_tokens=None):
    send_progress("Loading caption model...", 85, gui_mode)
    transformers = ensure_import("transformers", "transformers", gui_mode=gui_mode)

    local_only = os.path.exists(model_id)
    model_path = model_id
    if not local_only:
        try:
            snapshot_download = ensure_import("huggingface_hub", "huggingface_hub", gui_mode=gui_mode).snapshot_download
            kwargs = {"repo_id": model_id}
            if hf_token:
                kwargs["token"] = hf_token
            if max_workers:
                try:
                    kwargs["max_workers"] = int(max_workers)
                except Exception:
                    pass
            model_path = snapshot_download(**kwargs)
            if save_when_downloaded:
                try:
                    saved = _copy_or_move_path(model_path, save_when_downloaded, move=False)
                    print(f"Downloaded caption model copied to: {saved}")
                except Exception as e:
                    print(f"WARNING: Failed to copy caption model: {e}", file=sys.stderr)
        except Exception:
            model_path = model_id

    model_type = _detect_model_type(model_path, transformers, local_only=local_only)
    if model_type == "qwen2_vl":
        return generate_caption_qwen2_vl(
            image=image,
            model_path=model_path,
            gui_mode=gui_mode,
            local_only=local_only,
            context=context,
            prompt=prompt,
            system_prompt=system_prompt,
            max_tokens=max_tokens
        )
    if model_type == "git":
        return generate_caption_git(
            image=image,
            model_path=model_path,
            gui_mode=gui_mode,
            local_only=local_only,
            context=context,
            prompt=prompt,
            system_prompt=system_prompt,
            max_tokens=max_tokens
        )

    processor_cls = safe_getattr(transformers, "BlipProcessor")
    model_cls = safe_getattr(transformers, "BlipForConditionalGeneration")
    if processor_cls is None:
        processor_cls = safe_getattr(transformers, "AutoProcessor")
    if processor_cls is None or model_cls is None:
        raise RuntimeError("BLIP classes not available in transformers. Please upgrade transformers.")
    # Prefer slow processor for compatibility
    try:
        sig = inspect.signature(processor_cls.from_pretrained)
        if "use_fast" in sig.parameters:
            processor = processor_cls.from_pretrained(model_path, local_files_only=local_only, use_fast=False)
        else:
            processor = processor_cls.from_pretrained(model_path, local_files_only=local_only)
    except Exception:
        processor = processor_cls.from_pretrained(model_path, local_files_only=local_only)
    model = model_cls.from_pretrained(model_path, local_files_only=local_only)
    effective_prompt = _build_effective_prompt(context=context, prompt=prompt, system_prompt=system_prompt)
    if max_tokens:
        try:
            max_tokens = int(max_tokens)
        except Exception:
            max_tokens = None

    if effective_prompt:
        inputs = processor(images=image, text=effective_prompt, return_tensors="pt")
        out = model.generate(**inputs, max_new_tokens=max_tokens or 96)
    else:
        inputs = processor(images=image, return_tensors="pt")
        out = model.generate(**inputs, max_new_tokens=max_tokens or 32)
    caption = processor.decode(out[0], skip_special_tokens=True)
    return caption


def save_text(text, path):
    _ensure_dir(Path(path).parent)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text.strip() + "\n")


def run_ocr(image, gui_mode=False, lang=None):
    try:
        pytesseract = ensure_import("pytesseract", "pytesseract", gui_mode=gui_mode)
    except Exception as e:
        raise RuntimeError(f"OCR requires pytesseract. Install it first. {e}")
    try:
        if lang:
            text = pytesseract.image_to_string(image, lang=lang)
        else:
            text = pytesseract.image_to_string(image)
    except Exception as e:
        raise RuntimeError(f"OCR failed. Ensure Tesseract is installed and in PATH. {e}")
    return text.strip()


def download_model_only(model_id, download_to=None, resume=False, gui_mode=False, hf_token=None, max_workers=None, ignore_unnecessary=False):
    send_progress(f"Downloading model: {model_id}", 10, gui_mode)
    snapshot_download = ensure_import("huggingface_hub", "huggingface_hub", gui_mode=gui_mode).snapshot_download
    tqdm = ensure_import("tqdm.auto", "tqdm", gui_mode=gui_mode).tqdm

    class GuiTqdm(tqdm):
        def update(self, n=1):
            res = super().update(n)
            if self.total:
                pct = int(self.n * 100 / self.total)
                send_progress(f"Download progress: {pct}% ({human_bytes(self.n)}/{human_bytes(self.total)})", 10 + int(pct * 0.8), gui_mode)
            return res

    kwargs = {"repo_id": model_id, "resume_download": resume}
    if download_to:
        _ensure_dir(download_to)
        kwargs.update({"local_dir": download_to, "local_dir_use_symlinks": False})
    if hf_token:
        kwargs.update({"token": hf_token})
    if max_workers:
        try:
            kwargs.update({"max_workers": int(max_workers)})
        except Exception:
            pass
    ignore_patterns = None
    if ignore_unnecessary:
        ignore_patterns = [
            "*.h5", "*.ot", "*.msgpack", "*.safetensors.index.json",
            "tf_model.h5", "model.ckpt*", "flax_model.msgpack",
            "*.tflite", "*.onnx", "*.pb",
            "*.bin"  # prefer safetensors first
        ]
        kwargs["ignore_patterns"] = ignore_patterns

    # In GUI mode, keep the default HF progress bars so stdout/stderr look complete.
    if not gui_mode:
        kwargs["tqdm_class"] = GuiTqdm

    path = snapshot_download(**kwargs)

    # If we skipped .bin and no .safetensors were downloaded, retry allowing .bin.
    if ignore_unnecessary and download_to:
        try:
            dest = Path(download_to)
            has_safetensors = any(p.suffix == ".safetensors" for p in dest.rglob("*.safetensors"))
            if not has_safetensors:
                print("No .safetensors found. Re-downloading .bin weights...", file=sys.stderr)
                kwargs.pop("ignore_patterns", None)
                kwargs["ignore_patterns"] = [
                    "*.h5", "*.ot", "*.msgpack", "*.safetensors.index.json",
                    "tf_model.h5", "model.ckpt*", "flax_model.msgpack",
                    "*.tflite", "*.onnx", "*.pb"
                ]
                path = snapshot_download(**kwargs)
        except Exception as e:
            print(f"WARNING: Safetensors check failed: {e}", file=sys.stderr)
    print(f"Downloaded to: {path}")
    send_progress("Download complete", 100, gui_mode)
    return path


def _ensure_dir(path):
    Path(path).mkdir(parents=True, exist_ok=True)


def _copy_or_move_path(src_path, dest_root, move=False):
    src = Path(src_path)
    if not src.exists():
        raise FileNotFoundError(f"Source not found: {src}")
    _ensure_dir(dest_root)
    dest_root = Path(dest_root)
    dest = dest_root / src.name
    if dest.exists():
        # Avoid overwriting; append a suffix
        i = 1
        while (dest_root / f"{src.name}-{i}").exists():
            i += 1
        dest = dest_root / f"{src.name}-{i}"
    if src.is_dir():
        if move:
            return shutil.move(str(src), str(dest))
        shutil.copytree(src, dest)
        return str(dest)
    if move:
        return shutil.move(str(src), str(dest))
    shutil.copy2(src, dest)
    return str(dest)


def disable_torchao_imports():
    # Prevent transformers from importing torchao (can be incompatible with installed torch)
    if getattr(importlib.util, "_image_understand_orig_find_spec", None):
        return False
    orig = importlib.util.find_spec

    def find_spec(name, package=None):
        if name == "torchao" or name.startswith("torchao."):
            return None
        return orig(name, package)

    importlib.util._image_understand_orig_find_spec = orig
    importlib.util.find_spec = find_spec
    return True


def import_symbol(module_path, symbol_name):
    try:
        module = importlib.import_module(module_path)
        return safe_getattr(module, symbol_name)
    except Exception:
        return None


class ProgressTracker:
    def __init__(self, gui_mode=False):
        self.gui_mode = gui_mode
        self.steps = []
        self.index = 0

    def set_steps(self, steps):
        self.steps = list(steps)
        self.index = 0

    def tick(self, message, percentage=None):
        if self.steps:
            self.index = min(self.index + 1, len(self.steps))
            step = self.index
            total = len(self.steps)
        else:
            step = None
            total = None

        if percentage is None:
            if total:
                percentage = int((step / total) * 100)
            else:
                percentage = 0

        send_progress(message, percentage, self.gui_mode, step, total)


# -------- Vision Encoder --------
class VisionEncoder:
    def __init__(self, use_cache=False, cache_path=None, gui_mode=False, model_id="openai/clip-vit-base-patch32", resume_download=False, save_when_downloaded=None, save_from_cache=None, hf_token=None, max_workers=None):
        self.model = None
        self.processor = None
        self.use_cache = use_cache
        self.cache_path = cache_path
        self.model_id = model_id
        self.resume_download = resume_download
        self.save_when_downloaded = save_when_downloaded
        self.save_from_cache = save_from_cache
        self.hf_token = hf_token
        self.max_workers = max_workers
        self.cache_manager = ModelCache(model_id)
        self.gui_mode = gui_mode
        self.progress = ProgressTracker(gui_mode=gui_mode)
        self._torch = None
        self._Image = None
        self._CLIPProcessor = None
        self._CLIPModel = None

    def _load_dependencies(self):
        self.progress.tick("Importing libraries...", 8)
        torch = ensure_import("torch", "torch", gui_mode=self.gui_mode)
        pil_module = ensure_import("PIL", "pillow", gui_mode=self.gui_mode)
        if importlib.util.find_spec("torchao") is not None:
            if disable_torchao_imports():
                self.progress.tick("Disabling torchao (compat).", 9)
        ensure_import("transformers", "transformers", gui_mode=self.gui_mode)

        def resolve_clip_classes():
            # Prefer direct submodule imports to avoid lazy loader errors.
            clip_model = import_symbol("transformers.models.clip.modeling_clip", "CLIPModel")
            clip_processor = import_symbol("transformers.models.clip.processing_clip", "CLIPProcessor")
            if clip_processor and clip_model:
                return clip_processor, clip_model

            clip_image = import_symbol("transformers.models.clip.image_processing_clip", "CLIPImageProcessor")
            if clip_image and clip_model:
                self.progress.tick("Using CLIPImageProcessor (image-only).", 11)
                return clip_image, clip_model

            auto_processor = import_symbol("transformers", "AutoProcessor")
            if auto_processor and clip_model:
                self.progress.tick("Using AutoProcessor fallback.", 11)
                return auto_processor, clip_model

            auto_image = import_symbol("transformers", "AutoImageProcessor")
            if auto_image and clip_model:
                self.progress.tick("Using AutoImageProcessor (image-only).", 11)
                return auto_image, clip_model

            # As a last resort, try AutoModel with processor
            auto_model = import_symbol("transformers", "AutoModel")
            if auto_processor and auto_model:
                self.progress.tick("Using AutoModel fallback.", 11)
                return auto_processor, auto_model

            return None, None

        clip_proc, clip_model = resolve_clip_classes()
        if clip_proc is None or clip_model is None:
            # Try installing tokenizers/sentencepiece and retry
            try:
                ensure_import("tokenizers", "tokenizers", gui_mode=self.gui_mode)
                ensure_import("sentencepiece", "sentencepiece", gui_mode=self.gui_mode)
            except Exception:
                pass
            # Upgrade transformers if missing
            transformers = ensure_import("transformers", "transformers", gui_mode=self.gui_mode, upgrade=True)
            clip_proc, clip_model = resolve_clip_classes()

        if clip_proc is None or clip_model is None:
            raise ImportError(
                "transformers is missing CLIPProcessor/AutoProcessor/AutoImageProcessor/CLIPModel. "
                "Please upgrade transformers (e.g., >= 4.26)."
            )

        self._CLIPProcessor = clip_proc
        self._CLIPModel = clip_model

        self._torch = torch
        self._Image = pil_module.Image
        self.progress.tick("Libraries ready.", 12)

    def _download_model_with_progress(self, repo_id):
        self.progress.tick("Preparing model download...", 15)
        send_progress(f"Downloading model: {repo_id}", 15, self.gui_mode)
        try:
            snapshot_download = ensure_import("huggingface_hub", "huggingface_hub", gui_mode=self.gui_mode).snapshot_download
            tqdm = ensure_import("tqdm.auto", "tqdm", gui_mode=self.gui_mode).tqdm
        except Exception:
            # Fallback to normal download without detailed progress.
            return None

        def make_tqdm_class(progress_cb):
            class GuiTqdm(tqdm):
                def __init__(self, *args, **kwargs):
                    self._progress_cb = progress_cb
                    self._last_percent = -1
                    if "file" not in kwargs:
                        kwargs["file"] = sys.stdout
                    super().__init__(*args, **kwargs)

                def update(self, n=1):
                    res = super().update(n)
                    if self.total and self._progress_cb:
                        percent = int(self.n * 100 / self.total)
                        if percent != self._last_percent:
                            self._last_percent = percent
                            desc = getattr(self, "desc", "") or "Downloading model files"
                            rate = None
                            unit = None
                            try:
                                fmt = self.format_dict
                                rate = fmt.get("rate")
                                unit = fmt.get("unit")
                            except Exception:
                                pass
                            self._progress_cb(percent, desc, self.n, self.total, rate, unit)
                    return res

            return GuiTqdm

        def progress_cb(pct, desc, current, total, rate, unit):
            # Map download progress into 15..35
            overall = 15 + int((pct * 20) / 100)
            detail = f"{desc}: {pct}% ({human_bytes(current)}/{human_bytes(total)})"
            if rate:
                unit_str = unit or "B"
                detail += f" @ {rate:0.2f} {unit_str}/s"
            send_progress(detail, overall, self.gui_mode)

        tqdm_class = make_tqdm_class(progress_cb)
        try:
            kwargs = {"repo_id": repo_id, "tqdm_class": tqdm_class, "resume_download": self.resume_download}
            if self.hf_token:
                kwargs["token"] = self.hf_token
            if self.max_workers:
                try:
                    kwargs["max_workers"] = int(self.max_workers)
                except Exception:
                    pass
            return snapshot_download(**kwargs)
        except Exception:
            return None
        
    def load_model(self):
        """Load model with progress updates and caching support"""
        if self._torch is None:
            self._load_dependencies()

        self.progress.tick("Resolving model cache...", 13)
        model_path = None
        
        # If model_id is a local path, prefer it
        if self.model_id and os.path.exists(self.model_id):
            model_path = Path(self.model_id)
            self.progress.tick(f"Loading from: {model_path.name}...", 15)
        
        # Determine model path based on parameters
        if self.cache_path:
            model_path = Path(self.cache_path)
            if not model_path.exists():
                raise FileNotFoundError(f"Specified model path does not exist: {self.cache_path}")
            self.progress.tick(f"Loading from: {model_path.name}...", 15)
        
        elif self.use_cache:
            model_path = self.cache_manager.get_cached_model_path()
            
            if not model_path:
                model_path = self.cache_manager.find_downloaded_model()
            
            if model_path:
                self.progress.tick("Loading cached model...", 15)
            else:
                print("\nWARNING: No cached model found. Downloading...")
                self.progress.tick("No cache found, downloading...", 15)
        
        # Load the model
        self.progress.tick("Loading CLIP model...", 20)
        
        if model_path and model_path.exists():
            try:
                self.model = self._CLIPModel.from_pretrained(str(model_path), local_files_only=True)
                print(f"Loaded model from cache: {model_path}")
                if self.save_from_cache:
                    try:
                        saved = _copy_or_move_path(model_path, self.save_from_cache, move=True)
                        print(f"Cache moved to: {saved}")
                    except Exception as e:
                        print(f"WARNING: Failed to move cache: {e}", file=sys.stderr)
            except Exception as e:
                print(f"WARNING: Failed to load from cache: {e}")
                print("   Downloading fresh copy...")
                downloaded = self._download_model_with_progress(self.model_id)
                if downloaded:
                    self.model = self._CLIPModel.from_pretrained(downloaded, local_files_only=True)
                    self.cache_manager.save_cache_info(Path(downloaded))
                    if self.save_when_downloaded:
                        try:
                            saved = _copy_or_move_path(downloaded, self.save_when_downloaded, move=False)
                            print(f"Downloaded model copied to: {saved}")
                        except Exception as e:
                            print(f"WARNING: Failed to copy download: {e}", file=sys.stderr)
                else:
                    self.model = self._CLIPModel.from_pretrained(self.model_id)
                    cache_dir = Path(self.model.config._name_or_path)
                    if cache_dir.exists():
                        self.cache_manager.save_cache_info(cache_dir)
        else:
            downloaded = self._download_model_with_progress(self.model_id)
            if downloaded:
                self.model = self._CLIPModel.from_pretrained(downloaded, local_files_only=True)
                print("Model downloaded successfully!")
                if self.save_when_downloaded:
                    try:
                        saved = _copy_or_move_path(downloaded, self.save_when_downloaded, move=False)
                        print(f"Downloaded model copied to: {saved}")
                    except Exception as e:
                        print(f"WARNING: Failed to copy download: {e}", file=sys.stderr)
            else:
                self.model = self._CLIPModel.from_pretrained(self.model_id)
                print("Model downloaded successfully!")
            
            try:
                from huggingface_hub.constants import HF_HUB_CACHE
                repo_dir = self.model_id.replace("/", "--")
                cache_dir = Path(HF_HUB_CACHE) / f"models--{repo_dir}"
                if cache_dir.exists():
                    self.cache_manager.save_cache_info(cache_dir)
                    print(f"Model cached at: {cache_dir}")
                    print("\nTip: Next time, use: --auto-load-downloaded to skip re-downloading")
            except Exception as e:
                print(f"WARNING: Could not save cache info: {e}")
        
        self.progress.tick("Loading CLIP processor...", 40)
        
        if model_path and model_path.exists():
            try:
                self.processor = create_processor(self._CLIPProcessor, str(model_path), local_files_only=True)
            except Exception:
                self.processor = create_processor(self._CLIPProcessor, self.model_id, local_files_only=False)
        else:
            self.processor = create_processor(self._CLIPProcessor, self.model_id, local_files_only=False)
        
        self.model.eval()
        
    def encode(self, image_path):
        self.progress.tick("Opening image...", 50)
        image = self._Image.open(image_path).convert("RGB")
        
        self.progress.tick("Processing image...", 60)
        inputs = self.processor(images=image, return_tensors="pt")
        
        self.progress.tick("Encoding image features...", 70)
        with self._torch.no_grad():
            if hasattr(self.model, "get_image_features"):
                features = self.model.get_image_features(**inputs)
                if not isinstance(features, self._torch.Tensor):
                    # Some models return a structured output; extract a tensor.
                    if hasattr(features, "image_embeds") and features.image_embeds is not None:
                        features = features.image_embeds
                    elif hasattr(features, "pooler_output") and features.pooler_output is not None:
                        features = features.pooler_output
                    elif hasattr(features, "last_hidden_state") and features.last_hidden_state is not None:
                        features = features.last_hidden_state[:, 0]
                    elif isinstance(features, (list, tuple)) and features and isinstance(features[0], self._torch.Tensor):
                        features = features[0]
                    else:
                        raise RuntimeError("Model get_image_features did not return a tensor.")
            else:
                outputs = self.model(**inputs)
                if hasattr(outputs, "image_embeds") and outputs.image_embeds is not None:
                    features = outputs.image_embeds
                elif hasattr(outputs, "pooler_output") and outputs.pooler_output is not None:
                    features = outputs.pooler_output
                elif hasattr(outputs, "last_hidden_state") and outputs.last_hidden_state is not None:
                    features = outputs.last_hidden_state[:, 0]
                else:
                    raise RuntimeError("Model output did not contain image features.")
        
        return features


# -------- Projector --------
class Projector:
    def __init__(self, in_dim=512, out_dim=4096):
        import torch
        self.linear = torch.nn.Linear(in_dim, out_dim)
    
    def forward(self, x):
        return self.linear(x)

    def __call__(self, x):
        return self.forward(x)


# -------- Multimodal Tool --------
class VisionToLLMTool:
    def __init__(self, use_cache=False, cache_path=None, gui_mode=False, model_id="openai/clip-vit-base-patch32", resume_download=False, save_when_downloaded=None, save_from_cache=None, hf_token=None, max_workers=None):
        self.encoder = VisionEncoder(use_cache=use_cache, cache_path=cache_path, gui_mode=gui_mode, model_id=model_id, resume_download=resume_download, save_when_downloaded=save_when_downloaded, save_from_cache=save_from_cache, hf_token=hf_token, max_workers=max_workers)
        self.projector = Projector()
        self.gui_mode = gui_mode
        self.progress = ProgressTracker(gui_mode=gui_mode)
        
    def process(self, image_path, save_embedding_path=None, do_caption=False, caption_model=None, do_vision=True, do_non_vision=False, return_vision=False, return_non_vision=False, save_non_vision_path=None, context_generate=False, prompt=None, system_prompt=None, max_tokens=None, do_ocr=False, save_ocr_path=None, ocr_lang=None):
        """Process image with progress updates"""
        steps = [
            "Starting backend...",
            "Image file found.",
            "Initializing tool...",
            "Importing libraries...",
            "Libraries ready.",
            "Resolving model cache...",
            "Loading CLIP model...",
            "Loading CLIP processor...",
            "Opening image...",
            "Processing image...",
            "Encoding image features...",
            "Projecting to LLM space...",
            "Finalizing...",
            "Complete!"
        ]
        self.progress.set_steps(steps)
        self.progress.tick("Initializing tool...", 5)
        
        caption = None
        projected_tokens = None

        if do_vision:
            # Load model
            self.encoder.load_model()
            
            # Encode image
            img_embed = self.encoder.encode(image_path)
            
            self.progress.tick("Projecting to LLM space...", 80)
            
            # Project to LLM token space
            projected_tokens = self.projector(img_embed)
            
            self.progress.tick("Finalizing...", 90)

            if return_vision:
                print("\nVISION_EMBEDDING:")
                print(projected_tokens)
            
            if save_embedding_path:
                try:
                    save_embedding(projected_tokens, save_embedding_path)
                    print(f"Embedding saved to: {save_embedding_path}")
                except Exception as e:
                    print(f"WARNING: Failed to save embedding: {e}", file=sys.stderr)

        if do_non_vision:
            try:
                from PIL import Image
                image = Image.open(image_path).convert("RGB")
                ocr_text = None
                if do_ocr or save_ocr_path:
                    ocr_text = run_ocr(image, gui_mode=self.gui_mode, lang=ocr_lang)
                    if save_ocr_path:
                        save_text(ocr_text, save_ocr_path)
                        print(f"OCR text saved to: {save_ocr_path}")
                caption = generate_caption(
                    image,
                    gui_mode=self.gui_mode,
                    model_id=caption_model or "Salesforce/blip-image-captioning-base",
                    save_when_downloaded=self.encoder.save_when_downloaded,
                    hf_token=getattr(self.encoder, "hf_token", None),
                    max_workers=getattr(self.encoder, "max_workers", None),
                    context=context_generate,
                    prompt=prompt,
                    system_prompt=system_prompt,
                    max_tokens=max_tokens
                )
                if ocr_text:
                    caption = f"{caption}\n\nOCR_TEXT:\n{ocr_text}"
                if return_non_vision:
                    print("\nNON_VISION_TEXT:")
                    print(caption)
                if save_non_vision_path:
                    save_text(caption, save_non_vision_path)
                    print(f"Non-vision text saved to: {save_non_vision_path}")
            except Exception as e:
                print(f"WARNING: Caption generation failed: {e}", file=sys.stderr)
        
        return projected_tokens


# -------- Main Entry Point --------
def main():
    parser = argparse.ArgumentParser(description='Image Understanding Backend')
    parser.add_argument('image_path', type=str, help='Path to the image file')
    parser.add_argument('--auto-load-downloaded', action='store_true',
                       help='Automatically load previously downloaded model from cache')
    parser.add_argument('--load-downloaded', type=str, metavar='PATH',
                       help='Load model from specific directory path')
    parser.add_argument('--gui-mode', action='store_true',
                       help='Enable GUI mode with progress updates')
    parser.add_argument('--model', type=str, default="openai/clip-vit-base-patch32",
                       help='Hugging Face model id or local path')
    parser.add_argument('--resume-download', action='store_true',
                       help='Resume interrupted downloads (Hugging Face)')
    parser.add_argument('--resume-not-default-model-downloadings', type=str, default="",
                       help='Resume download for a specific model id or local path')
    parser.add_argument('--save-embedding', type=str, default="",
                       help='Save embedding to file (.pt or .npy)')
    parser.add_argument('--caption', action='store_true',
                       help='Generate image caption (BLIP)')
    parser.add_argument('--context-generate', action='store_true',
                       help='Generate a more detailed caption/context (longer prompt)')
    parser.add_argument('--prompt', type=str, default="",
                       help='Custom prompt for captioning')
    parser.add_argument('--system-prompt', type=str, default="",
                       help='System prompt prefix for captioning')
    parser.add_argument('--max-tokens', type=str, default="",
                       help='Max new tokens for caption output')
    parser.add_argument('--caption-model', type=str, default="Salesforce/blip-image-captioning-base",
                       help='Caption model id')
    parser.add_argument('--ocr', action='store_true',
                       help='Run OCR and append text to caption')
    parser.add_argument('--ocr-save', type=str, default="",
                       help='Save OCR text to file')
    parser.add_argument('--ocr-lang', type=str, default="",
                       help='OCR languages (e.g. eng, eng+fra)')
    parser.add_argument('--save-when-downloaded', type=str, default="",
                       help='Copy downloaded model to folder')
    parser.add_argument('--save-from-cache', type=str, default="",
                       help='Move cached model to folder')
    parser.add_argument('--vision', action='store_true',
                       help='Run vision embedding pipeline')
    parser.add_argument('--non-vision', action='store_true',
                       help='Run non-vision pipeline (caption to text)')
    parser.add_argument('--return-vision', action='store_true',
                       help='Print vision embedding result to stdout')
    parser.add_argument('--return-non-vision', action='store_true',
                       help='Print non-vision result to stdout')
    parser.add_argument('--save-non-vision', type=str, default="",
                       help='Save non-vision result (caption) to file')
    parser.add_argument('--download-model', type=str, default="",
                       help='Download a model only (no inference)')
    parser.add_argument('--download-to', type=str, default="",
                       help='Folder to download model into')
    parser.add_argument('--hf-token', type=str, default="",
                       help='Hugging Face access token')
    parser.add_argument('--max-workers', type=str, default="",
                       help='Max workers for downloads')
    parser.add_argument('--ignore-unnecessary', action='store_true',
                       help='Skip non-essential files when downloading (TF/Flax, duplicates)')
    
    args = parser.parse_args()
    
    image_path = args.image_path
    use_cache = args.auto_load_downloaded
    cache_path = args.load_downloaded
    gui_mode = args.gui_mode
    model_id = args.model
    resume_download = args.resume_download
    resume_model = args.resume_not_default_model_downloadings
    save_embedding_path = args.save_embedding
    do_caption = args.caption
    caption_model = args.caption_model
    context_generate = args.context_generate
    prompt = args.prompt
    system_prompt = args.system_prompt
    max_tokens = args.max_tokens
    do_ocr = args.ocr
    save_ocr_path = args.ocr_save
    ocr_lang = args.ocr_lang
    save_when_downloaded = args.save_when_downloaded
    save_from_cache = args.save_from_cache
    do_vision = args.vision
    do_non_vision = args.non_vision
    return_vision = args.return_vision
    return_non_vision = args.return_non_vision
    save_non_vision_path = args.save_non_vision
    download_model = args.download_model
    download_to = args.download_to
    hf_token = args.hf_token
    max_workers = args.max_workers
    ignore_unnecessary = args.ignore_unnecessary
    if not do_vision and not do_non_vision:
        do_vision = True
    if resume_model:
        model_id = resume_model
        resume_download = True
    
    tracker = ProgressTracker(gui_mode=gui_mode)
    tracker.tick("Starting backend...", 2)
    
    # Download-only mode
    if download_model:
        download_model_only(download_model, download_to=download_to, resume=resume_download, gui_mode=gui_mode, hf_token=hf_token, max_workers=max_workers, ignore_unnecessary=ignore_unnecessary)
        sys.exit(0)

    # Check if image exists
    if not os.path.exists(image_path):
        print(f"Error: Image file '{image_path}' not found!", file=sys.stderr)
        sys.exit(1)
    
    tracker.tick("Image file found.", 4)
    
    try:
        # Process image
        tool = VisionToLLMTool(
            use_cache=use_cache,
            cache_path=cache_path,
            gui_mode=gui_mode,
            model_id=model_id,
            resume_download=resume_download,
            save_when_downloaded=save_when_downloaded,
            save_from_cache=save_from_cache,
            hf_token=hf_token,
            max_workers=max_workers
        )
        tokens = tool.process(
            image_path,
            save_embedding_path=save_embedding_path,
            do_caption=do_caption or do_non_vision,
            caption_model=caption_model,
            context_generate=context_generate,
            prompt=prompt,
            system_prompt=system_prompt,
            max_tokens=max_tokens,
            do_vision=do_vision,
            do_non_vision=do_non_vision,
            return_vision=return_vision,
            return_non_vision=return_non_vision,
            save_non_vision_path=save_non_vision_path,
            do_ocr=do_ocr,
            save_ocr_path=save_ocr_path,
            ocr_lang=ocr_lang
        )
        
        tracker.tick("Complete!", 100)
        
        # Print results
        print("\n" + "="*60)
        print("IMAGE UNDERSTANDING COMPLETE")
        print("="*60)
        print(f"Image: {image_path}")
        if tokens is not None:
            print(f"Projected tokens shape: {tokens.shape}")
            print(f"Token tensor:\n{tokens}")
        print("="*60 + "\n")
        
        sys.exit(0)
        
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
