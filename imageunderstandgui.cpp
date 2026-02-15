#include "imageunderstandgui.h"
#include <QVBoxLayout>
#include <QWidget>
#include <QFont>
#include <QFileInfo>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QScreen>
#include <QMessageBox>
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
#include <QTextCursor>
#include <QPixmap>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QElapsedTimer>
#include <QStringView>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QCloseEvent>

ImageUnderstandGUI *ImageUnderstandGUI::s_activeLogger = nullptr;

LogWorker::LogWorker(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_flushTimer(new QTimer(this))
{
    m_flushTimer->setInterval(300);
    connect(m_flushTimer, &QTimer::timeout, this, &LogWorker::flushBuffers);
}

void LogWorker::configure(const QString &pythonPath, const QStringList &args, const QString &workDir)
{
    m_pythonPath = pythonPath;
    m_args = args;
    m_workDir = workDir;
}

void LogWorker::start()
{
    if (m_process) {
        return;
    }
    m_process = new QProcess(this);
    if (!m_workDir.isEmpty()) {
        m_process->setWorkingDirectory(m_workDir);
    }

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromUtf8(m_process->readAllStandardOutput());
        if (!out.isEmpty()) {
            fprintf(stdout, "%s", out.toUtf8().constData());
            fflush(stdout);
            m_pendingOut += out;
        }
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QString err = QString::fromUtf8(m_process->readAllStandardError());
        if (!err.isEmpty()) {
            fprintf(stderr, "%s", err.toUtf8().constData());
            fflush(stderr);
            m_pendingErr += err;
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                flushBuffers();
                m_flushTimer->stop();
                emit finished(code, status);
            });

    const QString program = m_pythonPath.isEmpty() ? "python" : m_pythonPath;
    m_process->start(program, m_args);
    if (!m_process->waitForStarted(5000)) {
        const QString err = "Failed to start Python process. Check Python path.\n";
        fprintf(stderr, "%s", err.toUtf8().constData());
        fflush(stderr);
        emit logChunk(err);
        emit progressChunk(err);
        emit finished(-1, QProcess::CrashExit);
        return;
    }
    m_flushTimer->start();
}

void LogWorker::stop()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "stop", Qt::BlockingQueuedConnection);
        return;
    }
    if (!m_process) {
        return;
    }
    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        if (!m_process->waitForFinished(1200)) {
            m_process->kill();
            m_process->waitForFinished(3000);
        }
    }
    m_flushTimer->stop();
}

void LogWorker::flushBuffers()
{
    auto emitProgressLine = [this](const QString &text) {
        if (text.isEmpty()) {
            return;
        }
        // Only forward the most recent progress-related line to keep UI responsive.
        QStringView view(text);
        const int maxScan = 40000;
        if (view.size() > maxScan) {
            view = view.right(maxScan);
        }

        const QStringList markers = {
            "PROGRESS:",
            "Fetching",
            "Loading weights",
            "Download progress",
            "Downloading"
        };

        int best = -1;
        QString marker;
        for (const QString &m : markers) {
            const int idx = view.lastIndexOf(m);
            if (idx > best) {
                best = idx;
                marker = m;
            }
        }
        if (best < 0) {
            return;
        }

        int lineStart = view.lastIndexOf('\n', best);
        if (lineStart < 0) {
            lineStart = 0;
        } else {
            lineStart += 1;
        }
        int lineEnd = view.indexOf('\n', best);
        if (lineEnd < 0) {
            lineEnd = view.size();
        }

        const QString line = view.mid(lineStart, lineEnd - lineStart).toString();
        if (!line.trimmed().isEmpty()) {
            emit progressChunk(line + "\n");
        }
    };

    if (!m_pendingOut.isEmpty()) {
        if (m_pendingOut.size() > 200000) {
            m_pendingOut = m_pendingOut.right(150000);
        }
        const bool heavyWeights =
            m_pendingOut.contains("Loading weights", Qt::CaseInsensitive) ||
            m_pendingOut.contains("Materializing param", Qt::CaseInsensitive);
        if (!heavyWeights) {
            emit logChunk(m_pendingOut);
        }
        emitProgressLine(m_pendingOut);
        m_pendingOut.clear();
    }
    if (!m_pendingErr.isEmpty()) {
        if (m_pendingErr.size() > 200000) {
            m_pendingErr = m_pendingErr.right(150000);
        }
        const bool heavyWeights =
            m_pendingErr.contains("Loading weights", Qt::CaseInsensitive) ||
            m_pendingErr.contains("Materializing param", Qt::CaseInsensitive);
        if (!heavyWeights) {
            emit logChunk(m_pendingErr);
        }
        emitProgressLine(m_pendingErr);
        m_pendingErr.clear();
    }
}

static QString resolveImagePath(const QString &inputPath)
{
    QFileInfo direct(inputPath);
    if (direct.exists()) {
        return direct.absoluteFilePath();
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QFileInfo candidate(dir.filePath(inputPath));
        if (candidate.exists()) {
            return candidate.absoluteFilePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return direct.absoluteFilePath();
}

static QString findProjectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo(dir.filePath("imageunderstand.pro")).exists()) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

static QString resolveBackendScriptPath()
{
    const QString root = findProjectRoot();
    if (!root.isEmpty()) {
        QFileInfo script(QDir(root).filePath("image_understand_backend.py"));
        if (script.exists()) {
            return script.absoluteFilePath();
        }
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QFileInfo candidate(dir.filePath("image_understand_backend.py"));
        if (candidate.exists()) {
            return candidate.absoluteFilePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return QString("image_understand_backend.py");
}

static QString resolveAssetPath(const QString &fileName)
{
    const QString root = findProjectRoot();
    if (!root.isEmpty()) {
        QFileInfo asset(QDir(root).filePath(fileName));
        if (asset.exists()) {
            return asset.absoluteFilePath();
        }
    }
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        QFileInfo candidate(dir.filePath(fileName));
        if (candidate.exists()) {
            return candidate.absoluteFilePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

static QStringList loadSliderPaths()
{
    QStringList paths;
    QFile sliderFile(":/slider.json");
    if (sliderFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(sliderFile.readAll());
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr) {
                if (!val.isString()) {
                    continue;
                }
                const QString name = val.toString().trimmed();
                if (name.isEmpty()) {
                    continue;
                }
                if (name.startsWith(":/")) {
                    paths << name;
                } else if (QFile::exists(":/"+name)) {
                    paths << (":/" + name);
                } else {
                    const QString resolved = resolveAssetPath(name);
                    if (!resolved.isEmpty()) {
                        paths << resolved;
                    }
                }
            }
        }
    }
    return paths;
}

static QStringList loadHumorLines()
{
    QStringList lines;
    QFile linesFile(":/lines.json");
    if (linesFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(linesFile.readAll());
        if (doc.isArray()) {
            const QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr) {
                if (!val.isString()) {
                    continue;
                }
                const QString text = val.toString().trimmed();
                if (!text.isEmpty()) {
                    lines << text;
                }
            }
        }
    }
    return lines;
}

ImageUnderstandGUI::ImageUnderstandGUI(const QString &imagePath,
                                       bool useCache,
                                       const QString &cachePath,
                                       const QString &pythonPath,
                                       const QString &modelId,
                                       bool resumeDownload,
                                       const QString &resumeModel,
                                       const QString &saveEmbeddingPath,
                                       bool doCaption,
                                       bool contextGenerate,
                                       const QString &prompt,
                                       const QString &systemPrompt,
                                       const QString &maxTokens,
                                       bool doOcr,
                                       const QString &ocrSave,
                                       const QString &ocrLang,
                                       const QString &captionModel,
                                       const QString &saveWhenDownloaded,
                                       const QString &saveFromCache,
                                       bool doVision,
                                       bool doNonVision,
                                       bool returnVision,
                                       bool returnNonVision,
                                       const QString &saveNonVisionPath,
                                       const QString &downloadModel,
                                       const QString &downloadTo,
                                       const QString &hfToken,
                                       const QString &maxWorkers,
                                       bool ignoreUnnecessary,
                                       QWidget *parent)
    : QMainWindow(parent)
    , m_imagePath(resolveImagePath(imagePath))
    , m_useCache(useCache)
    , m_cachePath(cachePath)
    , m_pythonPath(pythonPath)
    , m_modelId(modelId)
    , m_resumeDownload(resumeDownload)
    , m_resumeModel(resumeModel)
    , m_saveEmbeddingPath(saveEmbeddingPath)
    , m_doCaption(doCaption)
    , m_contextGenerate(contextGenerate)
    , m_prompt(prompt)
    , m_systemPrompt(systemPrompt)
    , m_maxTokens(maxTokens)
    , m_doOcr(doOcr)
    , m_ocrSave(ocrSave)
    , m_ocrLang(ocrLang)
    , m_captionModel(captionModel)
    , m_saveWhenDownloaded(saveWhenDownloaded)
    , m_saveFromCache(saveFromCache)
    , m_doVision(doVision)
    , m_doNonVision(doNonVision)
    , m_returnVision(returnVision)
    , m_returnNonVision(returnNonVision)
    , m_saveNonVisionPath(saveNonVisionPath)
    , m_downloadModel(downloadModel)
    , m_downloadTo(downloadTo)
    , m_hfToken(hfToken)
    , m_maxWorkers(maxWorkers)
    , m_ignoreUnnecessary(ignoreUnnecessary)
    , m_progressTimer(nullptr)
    , m_logTimer(nullptr)
    , m_progressThrottle(nullptr)
    , m_slideLabel(nullptr)
    , m_humorLabel(nullptr)
    , m_detailsWidget(nullptr)
    , m_detailsSplitter(nullptr)
    , m_slideOpacity(nullptr)
    , m_humorOpacity(nullptr)
    , m_slideAnim(nullptr)
    , m_humorAnim(nullptr)
    , m_slideTimer(nullptr)
    , m_humorTimer(nullptr)
    , m_slideIndex(0)
    , m_humorIndex(0)
    , m_currentProgress(0)
    , m_ioThread(nullptr)
    , m_ioWorker(nullptr)
{
    s_activeLogger = this;
    qInstallMessageHandler(&ImageUnderstandGUI::messageHandler);
    setupUI();
    m_logTimer = new QTimer(this);
    m_logTimer->setInterval(200);
    connect(m_logTimer, &QTimer::timeout, this, &ImageUnderstandGUI::flushPendingLog);
    m_logTimer->start();
    startPythonProcess();
}

ImageUnderstandGUI::~ImageUnderstandGUI()
{
    if (s_activeLogger == this) {
        s_activeLogger = nullptr;
        qInstallMessageHandler(nullptr);
    }
    if (m_ioWorker) {
        stopWorkerAndWait(2000);
    }
    if (m_ioThread) {
        m_ioThread->quit();
        m_ioThread->wait(3000);
        delete m_ioThread;
        m_ioThread = nullptr;
    }
    m_ioWorker = nullptr;
    
    if (m_progressTimer) {
        delete m_progressTimer;
    }
    if (m_logTimer) {
        delete m_logTimer;
    }
    if (m_progressThrottle) {
        delete m_progressThrottle;
    }
    if (m_slideTimer) {
        delete m_slideTimer;
    }
    if (m_humorTimer) {
        delete m_humorTimer;
    }
}

void ImageUnderstandGUI::closeEvent(QCloseEvent *event)
{
    stopWorkerAndWait(2000);
    event->accept();
}

void ImageUnderstandGUI::setupUI()
{
    // Set window title
    QString title = "Image Understanding - Processing";
    if (m_useCache) {
        title += " (Using Cache)";
    } else if (!m_cachePath.isEmpty()) {
        title += " (Custom Path)";
    }
    setWindowTitle(title);
    
    // Allow resizing
    setMinimumSize(520, 320);
    
    // Center window on screen
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int x = available.x() + (available.width() - width()) / 2;
        const int y = available.y() + (available.height() - height()) / 2;
        move(x, y);
    }
    
    // Create central widget and layout
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    centralWidget->setObjectName("central");
    
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setSpacing(20);
    layout->setContentsMargins(30, 30, 30, 30);
    
    // Title label
    m_titleLabel = new QLabel("Processing Image", this);
    m_titleLabel->setObjectName("titleLabel");
    QFont titleFont;
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_titleLabel);
    
    // Image path label
    QFileInfo fileInfo(m_imagePath);
    QString imageName = fileInfo.fileName();
    m_imageLabel = new QLabel(QString("Image: %1").arg(imageName), this);
    m_imageLabel->setObjectName("imageLabel");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_imageLabel);
    
    // Status label
    QString statusText = "Initializing...";
    if (m_useCache) {
        statusText = "Loading from cache...";
    } else if (!m_cachePath.isEmpty()) {
        QFileInfo cacheInfo(m_cachePath);
        statusText = QString("Loading from: %1...").arg(cacheInfo.fileName());
    }
    
    m_statusLabel = new QLabel(statusText, this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName("progressBar");
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    layout->addWidget(m_progressBar);

    // Toggle details button
    m_toggleDetailsButton = new QPushButton("SHOW LESS DETAILS", this);
    m_toggleDetailsButton->setObjectName("toggleButton");
    connect(m_toggleDetailsButton, &QPushButton::clicked, this, [this]() {
        const bool show = m_logView->isHidden();
        m_logView->setVisible(show);
        m_toggleDetailsButton->setText(show ? "SHOW LESS DETAILS" : "SHOW MORE DETAILS");
        if (m_detailsSplitter) {
            m_detailsSplitter->setVisible(true);
            if (show) {
                m_detailsSplitter->setSizes({2, 1});
            } else {
                m_detailsSplitter->setSizes({0, 1});
            }
        }
        if (show) {
            flushPendingLog();
        }
    });
    layout->addWidget(m_toggleDetailsButton);

    // Log view (raw stdout/stderr from Python/libs)
    m_logView = new QPlainTextEdit(this);
    m_logView->setObjectName("logView");
    m_logView->setReadOnly(true);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setMinimumHeight(120);
    m_logView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_logView->setVisible(true);

    // Details area (slides + humor) shown below toggle or below log when open
    m_detailsWidget = new QWidget(this);
    m_detailsWidget->setObjectName("detailsWidget");
    QVBoxLayout *detailsLayout = new QVBoxLayout(m_detailsWidget);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(10);
    m_detailsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_slideLabel = new QLabel(this);
    m_slideLabel->setObjectName("slideLabel");
    m_slideLabel->setAlignment(Qt::AlignCenter);
    m_slideLabel->setMinimumHeight(90);
    m_slideLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    detailsLayout->addWidget(m_slideLabel);

    m_humorLabel = new QLabel(this);
    m_humorLabel->setObjectName("humorLabel");
    m_humorLabel->setAlignment(Qt::AlignCenter);
    m_humorLabel->setWordWrap(true);
    QFont humorFont = m_humorLabel->font();
    humorFont.setPointSize(9);
    m_humorLabel->setFont(humorFont);
    detailsLayout->addWidget(m_humorLabel);

    m_detailsSplitter = new QSplitter(Qt::Vertical, this);
    m_detailsSplitter->addWidget(m_logView);
    m_detailsSplitter->addWidget(m_detailsWidget);
    m_detailsSplitter->setStretchFactor(0, 1);
    m_detailsSplitter->setStretchFactor(1, 1);
    m_detailsSplitter->setChildrenCollapsible(false);
    m_detailsSplitter->setVisible(true);
    m_detailsSplitter->setSizes({2, 1});
    layout->addWidget(m_detailsSplitter);

    // Setup slides and humor lines
    m_slidePaths = loadSliderPaths();
    if (m_slidePaths.isEmpty()) {
        const QString logoPath = resolveAssetPath("logo.png");
        const QString icoPath = resolveAssetPath("ico.png");
        if (!logoPath.isEmpty()) {
            m_slidePaths << logoPath;
        }
        if (!icoPath.isEmpty()) {
            m_slidePaths << icoPath;
        }
    }
    if (m_slidePaths.isEmpty()) {
        m_slideLabel->setText("Loading visuals...");
    }

    m_humorLines = loadHumorLines();
    if (m_humorLines.isEmpty()) {
        m_humorLines = {
            "PyDuino is analyzing the image...",
            "We value your time. Pushing your system to full power.",
            "Crunching pixels so you don't have to.",
            "Calibrating tiny virtual eyeballs...",
            "Extracting meaning from chaos. Hold tight.",
            "PyDuino goal #1: make coding easy.",
            "PyDuino goal #2: teach coding more effectively.",
            "PyDuino goal #3: code in the easiest way possible.",
            "If your fan spins up, that's just ambition.",
            "Pixels in, clarity out. That's the plan.",
            "Giving your machine a tiny workout. You're welcome.",
            "Reading the room. It's full of pixels.",
            "Hang tight — we're turning images into insight.",
            "We asked the GPU nicely. It agreed."
        };
    }

    // Opacity effects and animations
    m_slideOpacity = new QGraphicsOpacityEffect(this);
    m_slideLabel->setGraphicsEffect(m_slideOpacity);
    m_slideAnim = new QPropertyAnimation(m_slideOpacity, "opacity", this);
    m_slideAnim->setDuration(1200);
    m_slideAnim->setKeyValueAt(0.0, 0.0);
    m_slideAnim->setKeyValueAt(0.2, 1.0);
    m_slideAnim->setKeyValueAt(0.8, 1.0);
    m_slideAnim->setKeyValueAt(1.0, 0.0);

    m_humorOpacity = new QGraphicsOpacityEffect(this);
    m_humorLabel->setGraphicsEffect(m_humorOpacity);
    m_humorAnim = new QPropertyAnimation(m_humorOpacity, "opacity", this);
    m_humorAnim->setDuration(1400);
    m_humorAnim->setKeyValueAt(0.0, 0.0);
    m_humorAnim->setKeyValueAt(0.2, 1.0);
    m_humorAnim->setKeyValueAt(0.8, 1.0);
    m_humorAnim->setKeyValueAt(1.0, 0.0);

    // Timers
    m_slideTimer = new QTimer(this);
    m_slideTimer->setInterval(4000);
    connect(m_slideTimer, &QTimer::timeout, this, &ImageUnderstandGUI::advanceSlide);
    m_slideTimer->start();

    m_humorTimer = new QTimer(this);
    m_humorTimer->setInterval(3500);
    connect(m_humorTimer, &QTimer::timeout, this, &ImageUnderstandGUI::advanceHumor);
    m_humorTimer->start();

    advanceSlide();
    advanceHumor();
    
    // Cancel button (initially hidden)
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setVisible(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &ImageUnderstandGUI::onCancelClicked);
    layout->addWidget(m_cancelButton);
    
    // Let the splitter take remaining space so content scales with the window.

    // Theme (PyDuino: green + white with crisp cards)
    QFont baseFont("Segoe UI");
    baseFont.setPointSize(10);
    setFont(baseFont);

    const QString theme = R"(
        QWidget#central {
            background: qlineargradient(x1:0, y1:0, x2:0.9, y2:1,
                        stop:0 #f7fff9, stop:0.45 #ffffff, stop:1 #eefbf3);
        }
        QLabel#titleLabel {
            color: #0c2a16;
            font-size: 18px;
            font-weight: 700;
            letter-spacing: 0.5px;
        }
        QLabel#imageLabel {
            color: #245b39;
            font-size: 12px;
        }
        QLabel#statusLabel {
            color: #2c6f45;
            font-size: 12px;
        }
        QPushButton#toggleButton, QPushButton#cancelButton {
            background: #e9f7ee;
            color: #1f5a37;
            border: 1px solid #bfe6cd;
            border-radius: 8px;
            padding: 8px 14px;
            font-weight: 600;
        }
        QPushButton#toggleButton:hover, QPushButton#cancelButton:hover {
            background: #dff2e6;
            border-color: #9ed9b5;
        }
        QProgressBar#progressBar {
            border: 1px solid #cfe7d9;
            background: #ffffff;
            border-radius: 7px;
            height: 12px;
            text-align: center;
            color: #2c6f45;
        }
        QProgressBar#progressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                        stop:0 #1f8f4a, stop:1 #40c463);
            border-radius: 6px;
        }
        QPlainTextEdit#logView {
            background: #fcfffd;
            border: 1px solid #d8efe1;
            border-radius: 10px;
            padding: 8px;
            color: #193d26;
            selection-background-color: #b9f0c8;
            selection-color: #0b2515;
        }
        QWidget#detailsWidget {
            background: #ffffff;
            border: 1px solid #e2f2e8;
            border-radius: 12px;
        }
        QLabel#slideLabel {
            padding-top: 8px;
        }
        QLabel#humorLabel {
            color: #2e6a43;
            font-style: italic;
            padding-bottom: 10px;
        }
    )";
    centralWidget->setStyleSheet(theme);
}

void ImageUnderstandGUI::startPythonProcess()
{
    // Resolve backend script path (prefer project root)
    const QString scriptPath = resolveBackendScriptPath();
    const QString workDir = QFileInfo::exists(scriptPath)
        ? QFileInfo(scriptPath).absolutePath()
        : QString();

    // Build command arguments
    QStringList arguments;
    arguments << scriptPath;
    arguments << m_imagePath;
    
    if (m_useCache) {
        arguments << "--auto-load-downloaded";
    } else if (!m_cachePath.isEmpty()) {
        arguments << "--load-downloaded" << m_cachePath;
    }
    if (!m_modelId.isEmpty()) {
        arguments << "--model" << m_modelId;
    }
    if (m_resumeDownload) {
        arguments << "--resume-download";
    }
    if (!m_resumeModel.isEmpty()) {
        arguments << "--resume-not-default-model-downloadings" << m_resumeModel;
    }
    if (!m_saveEmbeddingPath.isEmpty()) {
        arguments << "--save-embedding" << m_saveEmbeddingPath;
    }
    if (m_doCaption) {
        arguments << "--caption";
    }
    if (m_contextGenerate) {
        arguments << "--context-generate";
    }
    if (!m_prompt.isEmpty()) {
        arguments << "--prompt" << m_prompt;
    }
    if (!m_systemPrompt.isEmpty()) {
        arguments << "--system-prompt" << m_systemPrompt;
    }
    if (!m_maxTokens.isEmpty()) {
        arguments << "--max-tokens" << m_maxTokens;
    }
    if (m_doOcr) {
        arguments << "--ocr";
    }
    if (!m_ocrSave.isEmpty()) {
        arguments << "--ocr-save" << m_ocrSave;
    }
    if (!m_ocrLang.isEmpty()) {
        arguments << "--ocr-lang" << m_ocrLang;
    }
    if (!m_captionModel.isEmpty()) {
        arguments << "--caption-model" << m_captionModel;
    }
    if (!m_saveWhenDownloaded.isEmpty()) {
        arguments << "--save-when-downloaded" << m_saveWhenDownloaded;
    }
    if (!m_saveFromCache.isEmpty()) {
        arguments << "--save-from-cache" << m_saveFromCache;
    }
    if (m_doVision) {
        arguments << "--vision";
    }
    if (m_doNonVision) {
        arguments << "--non-vision";
    }
    if (m_returnVision) {
        arguments << "--return-vision";
    }
    if (m_returnNonVision) {
        arguments << "--return-non-vision";
    }
    if (!m_saveNonVisionPath.isEmpty()) {
        arguments << "--save-non-vision" << m_saveNonVisionPath;
    }
    if (!m_downloadModel.isEmpty()) {
        arguments << "--download-model" << m_downloadModel;
    }
    if (!m_downloadTo.isEmpty()) {
        arguments << "--download-to" << m_downloadTo;
    }
    if (!m_hfToken.isEmpty()) {
        arguments << "--hf-token" << m_hfToken;
    }
    if (!m_maxWorkers.isEmpty()) {
        arguments << "--max-workers" << m_maxWorkers;
    }
    if (m_ignoreUnnecessary) {
        arguments << "--ignore-unnecessary";
    }
    
    // Add GUI mode flag to get progress updates
    arguments << "--gui-mode";
    
    qDebug() << "Starting Python process with arguments:" << arguments;

    m_ioThread = new QThread(this);
    m_ioWorker = new LogWorker();
    m_ioWorker->configure(m_pythonPath, arguments, workDir);
    m_ioWorker->moveToThread(m_ioThread);

    connect(m_ioThread, &QThread::started, m_ioWorker, &LogWorker::start);
    connect(m_ioWorker, &LogWorker::logChunk,
            this, &ImageUnderstandGUI::onWorkerLogChunk, Qt::QueuedConnection);
    connect(m_ioWorker, &LogWorker::progressChunk,
            this, &ImageUnderstandGUI::onWorkerProgressChunk, Qt::QueuedConnection);
    connect(m_ioWorker, &LogWorker::finished,
            this, &ImageUnderstandGUI::onProcessFinished, Qt::QueuedConnection);
    connect(m_ioWorker, &LogWorker::finished, m_ioThread, &QThread::quit);
    connect(m_ioThread, &QThread::finished, m_ioWorker, &QObject::deleteLater);
    connect(m_ioThread, &QThread::finished, this, [this]() { m_ioWorker = nullptr; });

    m_ioThread->start();
    
    // Start progress timer for simulation
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, &ImageUnderstandGUI::checkProgress);
    m_progressTimer->start(100); // Check every 100ms
}

void ImageUnderstandGUI::onWorkerLogChunk(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_outputBuffer += text;
    appendLogText(text);
    
    // Parse throttled in timer to keep UI responsive.
}

void ImageUnderstandGUI::onWorkerProgressChunk(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_pendingProgress += text;
    if (m_pendingProgress.size() > 80000) {
        m_pendingProgress = m_pendingProgress.right(60000);
    }
}

void ImageUnderstandGUI::appendLogText(const QString &text)
{
    m_pendingLog += text;
    if (m_pendingLog.size() > 200000) {
        m_pendingLog = m_pendingLog.right(150000);
    }
}

void ImageUnderstandGUI::advanceSlide()
{
    if (m_slidePaths.isEmpty()) {
        return;
    }
    const QString path = m_slidePaths.at(m_slideIndex % m_slidePaths.size());
    m_slideIndex++;
    QPixmap pix(path);
    if (!pix.isNull()) {
        const int targetW = m_slideLabel->width() > 0 ? m_slideLabel->width() : 360;
        const int targetH = m_slideLabel->height() > 0 ? m_slideLabel->height() : 90;
        m_slideLabel->setPixmap(pix.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_slideLabel->setText(QFileInfo(path).fileName());
    }
    if (m_slideAnim) {
        m_slideAnim->stop();
        m_slideAnim->setStartValue(0.0);
        m_slideAnim->setEndValue(1.0);
        m_slideAnim->start();
    }
}

void ImageUnderstandGUI::advanceHumor()
{
    if (m_humorLines.isEmpty()) {
        return;
    }
    const QString text = m_humorLines.at(m_humorIndex % m_humorLines.size());
    m_humorIndex++;
    m_humorLabel->setText(text);
    if (m_humorAnim) {
        m_humorAnim->stop();
        m_humorAnim->setStartValue(0.0);
        m_humorAnim->setEndValue(1.0);
        m_humorAnim->start();
    }
}

void ImageUnderstandGUI::flushPendingLog()
{
    if (m_logView) {
        if (!m_pendingLog.isEmpty() && m_logView->isVisible()) {
            const int maxChunk = 8000;
            const QString chunk = m_pendingLog.left(maxChunk);
            m_pendingLog.remove(0, chunk.size());
            m_logView->moveCursor(QTextCursor::End);
            m_logView->insertPlainText(chunk);
            m_logView->moveCursor(QTextCursor::End);
        }
    }

    if (m_pendingProgress.isEmpty()) {
        return;
    }
    if (!m_progressThrottle) {
        m_progressThrottle = new QElapsedTimer();
        m_progressThrottle->start();
    } else if (m_progressThrottle->elapsed() < 250) {
        return;
    }
    m_progressThrottle->restart();

    QString progressChunk = m_pendingProgress;
    m_pendingProgress.clear();

    QStringList lines = progressChunk.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith("PROGRESS:")) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3) {
                QString message = parts[1];
                bool ok;
                int percentage = parts[2].toInt(&ok);
                if (ok) {
                    updateProgress(message, percentage);
                }
            }
        } else if (line.startsWith("Fetching")) {
            m_statusLabel->setText(line);
            QRegularExpression re(R"((\d+)%\|)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                bool ok = false;
                const int pct = match.captured(1).toInt(&ok);
                if (ok) {
                    const int mapped = 15 + (pct * 20) / 100;
                    m_progressBar->setValue(mapped);
                    m_currentProgress = mapped;
                }
            }
        } else if (line.contains("Error") || line.contains("Failed") || line.contains("Traceback")) {
            m_statusLabel->setText(QString("Error: %1").arg(line.left(160)));
            m_progressBar->setValue(0);
            m_cancelButton->setText("Close");
            m_cancelButton->setVisible(true);
        }
    }
}

void ImageUnderstandGUI::messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const QString line = msg + "\n";

    // Mirror Qt logs to console.
    FILE *out = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    fprintf(out, "%s", line.toUtf8().constData());
    fflush(out);

    // Also show in GUI log if available.
    if (s_activeLogger) {
        QMetaObject::invokeMethod(s_activeLogger, [line]() {
            if (ImageUnderstandGUI::s_activeLogger) {
                ImageUnderstandGUI::s_activeLogger->appendLogText(line);
            }
        }, Qt::QueuedConnection);
    }
}

void ImageUnderstandGUI::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    if (m_slideTimer) {
        m_slideTimer->stop();
    }
    if (m_humorTimer) {
        m_humorTimer->stop();
    }
    
    qDebug() << "Process finished with exit code:" << exitCode;
    qDebug() << "Output buffer:" << m_outputBuffer;
    
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        m_statusLabel->setText("Processing complete!");
        m_progressBar->setValue(100);
        if (m_humorLabel) {
            m_humorLabel->setText("Done. Thanks for waiting.");
        }
        
        // Show results in console
        qDebug() << "=== RESULTS ===";
        qDebug() << m_outputBuffer;
        
        // Close window after short delay
        QTimer::singleShot(1000, this, &ImageUnderstandGUI::close);
    } else {
        m_statusLabel->setText(QString("Process failed with code: %1").arg(exitCode));
        m_cancelButton->setText("Close");
        m_cancelButton->setVisible(true);
    }
}

void ImageUnderstandGUI::onCancelClicked()
{
    stopWorkerAndWait(2000);
    close();
}

void ImageUnderstandGUI::stopWorkerAndWait(int waitMs)
{
    if (!m_ioWorker || !m_ioThread) {
        return;
    }
    if (m_ioThread->isRunning()) {
        QMetaObject::invokeMethod(m_ioWorker, "stop", Qt::BlockingQueuedConnection);
        m_ioThread->quit();
        m_ioThread->wait(waitMs);
    }
}

void ImageUnderstandGUI::checkProgress()
{
    // Simulate smooth progress updates
    if (m_currentProgress < 95) {
        m_currentProgress += 1;
        m_progressBar->setValue(m_currentProgress);
    }
}

void ImageUnderstandGUI::parseProgressOutput(const QString &output)
{
    // Parse progress messages from Python
    // Format: "PROGRESS:message:percentage"
    
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    
    for (const QString &line : lines) {
        if (line.startsWith("PROGRESS:")) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3) {
                QString message = parts[1];
                bool ok;
                int percentage = parts[2].toInt(&ok);
                
                if (ok) {
                    updateProgress(message, percentage);
                }
            }
        }
    }
}

void ImageUnderstandGUI::updateProgress(const QString &message, int value)
{
    m_statusLabel->setText(message);
    m_progressBar->setValue(value);
    m_currentProgress = value;
}
