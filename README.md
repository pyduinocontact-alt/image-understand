# PyDuino Image Understand

A fast, local-first image understanding tool with a **Qt GUI + Python backend**.  
Generate captions, OCR text, and embeddings with simple CLI flags or the GUI.

---

## Features
- **GUI + CLI**: run from the app or the terminal.
- **Captions**: BLIP-based image captioning.
- **OCR**: extract on-image text with language support.
- **Embeddings**: CLIP embeddings for search or analysis.
- **Model downloads**: download once, reuse locally.
- **Progress logs**: detailed logs in GUI and console without freezing.

---

## Quick Start (GUI)
1. Build the Qt app.
2. Run the executable:
```
image-understand.exe
```
3. Select an image and configure options.

---

## Quick Start (CLI)

### Basic caption
```
image-understand test1.png --caption
```

### Use your Python 3.10 (with torch)
```
image-understand test1.png --use-python C:\Users\bismi\AppData\Local\Programs\Python\Python310\python.exe --caption
```

### OCR + caption (save result)
```
image-understand test1.png --non-vision --caption-model C:\Users\bismi\Downloads\image-understand\models\blip-image-captioning-large --ocr --ocr-lang eng --return-non-vision --save-non-vision C:\Users\bismi\Downloads\image-understand\caption.txt
```

---

## Model Download (CLI)

### Download only
```
image-understand --download-model Salesforce/blip-image-captioning-base --download-to D:\models
```

### Faster download + token (if needed)
```
image-understand --download-model Qwen/Qwen2-VL-2B-Instruct --download-to D:\models --max-workers 32 --hf-token YOUR_TOKEN
```

---

## OCR Notes
- OCR uses **Tesseract** via `pytesseract`.
- Install Tesseract and ensure it’s in your PATH.
- Example languages: `eng`, `eng+fra`, `eng+ara`

---

## GUI Assets
GUI uses embedded resources for slider images and humor lines:
- `slider.json` (embedded in Qt resource)
- `lines.json` (embedded in Qt resource)

To edit, update the JSON files and rebuild.

---

## Build
This is a Qt (qmake) project.

```
qmake
mingw32-make -j12
```

The output is in:
```
build\
```

---

## Installer
Inno Setup script is in:
```
image-understand.iss
```

It installs:
- executable
- backend python
- Qt runtime
- optional PATH entry

---

## Troubleshooting

**Error: `No module named 'torch'`**
- Use the same Python you installed torch with:
```
image-understand test1.png --use-python C:\Path\To\Python310\python.exe ...
```

**GUI shows OCR lines like “Processing Image”**
- OCR is reading the GUI window. Run OCR from CLI or hide the GUI when OCR is required.

---

## License
MIT (add your own if different)

