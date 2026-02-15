#include "imageunderstandgui.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QDebug>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Image Understanding Tool");
    app.setApplicationVersion("1.0");
    
    // Setup command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("Image Understanding Tool - Process images with CLIP vision encoder");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Add positional argument for image path
    parser.addPositionalArgument("image", "Path to the image file");
    
    // Add optional arguments
    QCommandLineOption autoCacheOption("auto-load-downloaded",
                                      "Automatically load previously downloaded model from cache");
    parser.addOption(autoCacheOption);
    
    QCommandLineOption cachePathOption("load-downloaded",
                                      "Load model from specific directory path",
                                      "path");
    parser.addOption(cachePathOption);

    QCommandLineOption pythonPathOption("use-python",
                                      "Use a specific Python executable",
                                      "path");
    parser.addOption(pythonPathOption);

    QCommandLineOption modelOption("model",
                                   "Hugging Face model id or local path",
                                   "model");
    parser.addOption(modelOption);
    
    QCommandLineOption resumeDownloadOption("resume-download",
                                           "Resume interrupted downloads (Hugging Face)");
    parser.addOption(resumeDownloadOption);
    
    QCommandLineOption resumeModelOption("resume-not-default-model-downloadings",
                                         "Resume download for a specific model id or local path",
                                         "model");
    parser.addOption(resumeModelOption);

    QCommandLineOption saveEmbeddingOption("save-embedding",
                                           "Save embedding to file (.pt or .npy)",
                                           "path");
    parser.addOption(saveEmbeddingOption);

    QCommandLineOption captionOption("caption",
                                     "Generate image caption (BLIP)");
    parser.addOption(captionOption);

    QCommandLineOption contextGenerateOption("context-generate",
                                             "Generate a more detailed caption/context (longer prompt)");
    parser.addOption(contextGenerateOption);

    QCommandLineOption promptOption("prompt",
                                    "Custom prompt for captioning",
                                    "text");
    parser.addOption(promptOption);

    QCommandLineOption systemPromptOption("system-prompt",
                                          "System prompt prefix for captioning",
                                          "text");
    parser.addOption(systemPromptOption);

    QCommandLineOption maxTokensOption("max-tokens",
                                       "Max new tokens for caption output",
                                       "n");
    parser.addOption(maxTokensOption);

    QCommandLineOption ocrOption("ocr",
                                 "Run OCR and append text to caption");
    parser.addOption(ocrOption);

    QCommandLineOption ocrSaveOption("ocr-save",
                                     "Save OCR text to file",
                                     "path");
    parser.addOption(ocrSaveOption);

    QCommandLineOption ocrLangOption("ocr-lang",
                                     "OCR languages (e.g. eng, eng+fra)",
                                     "langs");
    parser.addOption(ocrLangOption);

    QCommandLineOption captionModelOption("caption-model",
                                          "Caption model id (default: Salesforce/blip-image-captioning-base)",
                                          "model");
    parser.addOption(captionModelOption);

    QCommandLineOption saveWhenDownloadedOption("save-when-downloaded",
                                                "Copy downloaded model to folder",
                                                "path");
    parser.addOption(saveWhenDownloadedOption);

    QCommandLineOption saveFromCacheOption("save-from-cache",
                                           "Move cached model to folder",
                                           "path");
    parser.addOption(saveFromCacheOption);

    QCommandLineOption downloadModelOption("download-model",
                                           "Download a model only (no inference)",
                                           "model");
    parser.addOption(downloadModelOption);

    QCommandLineOption downloadToOption("download-to",
                                        "Folder to download model into",
                                        "path");
    parser.addOption(downloadToOption);

    QCommandLineOption hfTokenOption("hf-token",
                                     "Hugging Face access token",
                                     "token");
    parser.addOption(hfTokenOption);

    QCommandLineOption maxWorkersOption("max-workers",
                                        "Max workers for downloads",
                                        "n");
    parser.addOption(maxWorkersOption);

    QCommandLineOption ignoreUnnecessaryOption("ignore-unnecessary",
                                              "Skip non-essential files when downloading (e.g., TF/Flax, safetensors or bin duplicates)",
                                              "bool");
    parser.addOption(ignoreUnnecessaryOption);

    QCommandLineOption visionOption("vision",
                                    "Run vision embedding pipeline");
    parser.addOption(visionOption);

    QCommandLineOption nonVisionOption("non-vision",
                                       "Run non-vision pipeline (caption to text)");
    parser.addOption(nonVisionOption);

    QCommandLineOption returnVisionOption("return-vision",
                                          "Print vision embedding result to stdout");
    parser.addOption(returnVisionOption);

    QCommandLineOption returnNonVisionOption("return-non-vision",
                                             "Print non-vision result to stdout");
    parser.addOption(returnNonVisionOption);

    QCommandLineOption saveNonVisionOption("save-non-vision",
                                           "Save non-vision result (caption) to file",
                                           "path");
    parser.addOption(saveNonVisionOption);
    
    // Process command line
    parser.process(app);
    
    // Get positional arguments
    const QStringList args = parser.positionalArguments();

    QString downloadModel = parser.value(downloadModelOption);
    QString downloadTo = parser.value(downloadToOption);
    
    if (args.isEmpty() && downloadModel.isEmpty()) {
        QMessageBox::critical(nullptr, "Error", 
                            "Usage: image-understand <image_path> [--auto-load-downloaded] [--load-downloaded <path>] [--use-python <path>] [--model <model>] [--resume-download] [--resume-not-default-model-downloadings <model>] [--save-embedding <path>] [--caption] [--context-generate] [--prompt <text>] [--system-prompt <text>] [--max-tokens <n>] [--ocr] [--ocr-save <path>] [--ocr-lang <langs>] [--caption-model <model>] [--save-when-downloaded <path>] [--save-from-cache <path>] [--vision] [--non-vision] [--return-vision] [--return-non-vision] [--save-non-vision <path>] [--download-model <model>] [--download-to <path>] [--hf-token <token>] [--max-workers <n>] [--ignore-unnecessary]\n\n"
                            "Examples:\n"
                            "  image-understand photo.png\n"
                            "  image-understand photo.png --auto-load-downloaded\n"
                            "  image-understand photo.png --load-downloaded /path/to/model\n"
                            "  image-understand photo.png --use-python C:\\Python310\\python.exe\n"
                            "  image-understand photo.png --model openai/clip-vit-base-patch32\n"
                            "  image-understand photo.png --resume-download\n"
                            "  image-understand photo.png --resume-not-default-model-downloadings openai/clip-vit-large-patch14\n"
                            "  image-understand photo.png --save-embedding output.pt\n"
                            "  image-understand photo.png --caption --caption-model Salesforce/blip-image-captioning-base\n"
                            "  image-understand photo.png --caption --context-generate\n"
                            "  image-understand photo.png --caption --prompt \"Describe the scene in detail.\" --max-tokens 96\n"
                            "  image-understand photo.png --save-when-downloaded D:\\models\n"
                            "  image-understand photo.png --save-from-cache D:\\models\n"
                            "  image-understand photo.png --non-vision --return-non-vision\n"
                            "  image-understand photo.png --vision --save-embedding out.pt\n"
                            "  image-understand --download-model Salesforce/blip-image-captioning-base --download-to D:\\models\n"
                            "  image-understand --download-model Salesforce/blip-image-captioning-base --download-to D:\\models --max-workers 32\n"
                            "  image-understand --download-model Salesforce/blip-image-captioning-large --download-to D:\\models --ignore-unnecessary");
        return 1;
    }
    
    QString imagePath = args.isEmpty() ? QString() : args.at(0);
    
    QFileInfo imageInfo;
    // Check if image exists (skip in download-only mode)
    if (downloadModel.isEmpty()) {
        imageInfo = QFileInfo(imagePath);
        if (!imageInfo.exists()) {
            QMessageBox::critical(nullptr, "Error", 
                                QString("Image file not found: %1").arg(imagePath));
            return 1;
        }
    
        // Check if it's a valid image format
        QStringList validExtensions = {"png", "jpg", "jpeg", "bmp", "gif", "webp", "tiff"};
        QString extension = imageInfo.suffix().toLower();
        
        if (!validExtensions.contains(extension)) {
            QMessageBox::critical(nullptr, "Error", 
                                QString("Unsupported image format: %1\n"
                                      "Supported formats: %2")
                                .arg(extension)
                                .arg(validExtensions.join(", ")));
            return 1;
        }
    }
    
    // Get cache options
    bool useCache = parser.isSet(autoCacheOption);
    QString cachePath = parser.value(cachePathOption);
    QString pythonPath = parser.value(pythonPathOption);
    QString modelId = parser.value(modelOption);
    bool resumeDownload = parser.isSet(resumeDownloadOption);
    QString resumeModel = parser.value(resumeModelOption);
    QString saveEmbeddingPath = parser.value(saveEmbeddingOption);
    bool doCaption = parser.isSet(captionOption);
    bool contextGenerate = parser.isSet(contextGenerateOption);
    QString prompt = parser.value(promptOption);
    QString systemPrompt = parser.value(systemPromptOption);
    QString maxTokens = parser.value(maxTokensOption);
    bool doOcr = parser.isSet(ocrOption);
    QString ocrSave = parser.value(ocrSaveOption);
    QString ocrLang = parser.value(ocrLangOption);
    QString captionModel = parser.value(captionModelOption);
    QString saveWhenDownloaded = parser.value(saveWhenDownloadedOption);
    QString saveFromCache = parser.value(saveFromCacheOption);
    QString hfToken = parser.value(hfTokenOption);
    QString maxWorkers = parser.value(maxWorkersOption);
    bool ignoreUnnecessary = parser.isSet(ignoreUnnecessaryOption);
    bool doVision = parser.isSet(visionOption);
    bool doNonVision = parser.isSet(nonVisionOption);
    bool returnVision = parser.isSet(returnVisionOption);
    bool returnNonVision = parser.isSet(returnNonVisionOption);
    QString saveNonVisionPath = parser.value(saveNonVisionOption);

    if (!doVision && !doNonVision) {
        doVision = true;
    }
    if (!pythonPath.isEmpty()) {
        QFileInfo pyInfo(pythonPath);
        if (!pyInfo.exists()) {
            QMessageBox::critical(nullptr, "Error",
                                QString("Python executable not found: %1").arg(pythonPath));
            return 1;
        }
    }
    
    // Check if custom cache path exists
    if (!cachePath.isEmpty()) {
        QFileInfo cacheInfo(cachePath);
        if (!cacheInfo.exists()) {
            QMessageBox::critical(nullptr, "Error", 
                                QString("Specified model path does not exist: %1").arg(cachePath));
            return 1;
        }
    }
    
    // Print startup information
    qDebug() << "============================================================";
    qDebug() << "IMAGE UNDERSTANDING TOOL (C++ GUI + Python Backend)";
    qDebug() << "============================================================";
    qDebug() << "Image:" << imagePath;
    
    if (useCache) {
        qDebug() << "Mode: Using auto-detected cached model";
    } else if (!cachePath.isEmpty()) {
        qDebug() << "Mode: Using custom model path";
        qDebug() << "Path:" << cachePath;
    } else {
        qDebug() << "Mode: Standard (may download model)";
    }
    
    qDebug() << "============================================================";
    
    // If download-only mode, allow running without image path validation
    if (!downloadModel.isEmpty()) {
        imagePath = "download-only";
    }

    // Create and show GUI
    ImageUnderstandGUI window(imagePath, useCache, cachePath, pythonPath, modelId, resumeDownload, resumeModel, saveEmbeddingPath, doCaption, contextGenerate, prompt, systemPrompt, maxTokens, doOcr, ocrSave, ocrLang, captionModel, saveWhenDownloaded, saveFromCache, doVision, doNonVision, returnVision, returnNonVision, saveNonVisionPath, downloadModel, downloadTo, hfToken, maxWorkers, ignoreUnnecessary);
    window.show();
    
    return app.exec();
}
