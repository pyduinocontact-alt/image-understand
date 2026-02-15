#ifndef IMAGEUNDERSTANDGUI_H
#define IMAGEUNDERSTANDGUI_H

#include <QMainWindow>
#include <QProcess>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QPlainTextEdit>
#include <QTimer>
class QThread;
class QElapsedTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QSplitter;
class QCloseEvent;

class LogWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogWorker(QObject *parent = nullptr);
    void configure(const QString &pythonPath, const QStringList &args, const QString &workDir);

public slots:
    void start();
    void stop();

signals:
    void logChunk(const QString &text);
    void progressChunk(const QString &text);
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void flushBuffers();

private:
    QProcess *m_process;
    QTimer *m_flushTimer;
    QString m_pythonPath;
    QStringList m_args;
    QString m_workDir;
    QString m_pendingOut;
    QString m_pendingErr;
};

class ImageUnderstandGUI : public QMainWindow
{
    Q_OBJECT

public:
    explicit ImageUnderstandGUI(const QString &imagePath, 
                                bool useCache = false, 
                                const QString &cachePath = QString(),
                                const QString &pythonPath = QString(),
                                const QString &modelId = QString(),
                                bool resumeDownload = false,
                                const QString &resumeModel = QString(),
                                const QString &saveEmbeddingPath = QString(),
                                bool doCaption = false,
                                bool contextGenerate = false,
                                const QString &prompt = QString(),
                                const QString &systemPrompt = QString(),
                                const QString &maxTokens = QString(),
                                bool doOcr = false,
                                const QString &ocrSave = QString(),
                                const QString &ocrLang = QString(),
                                const QString &captionModel = QString(),
                                const QString &saveWhenDownloaded = QString(),
                                const QString &saveFromCache = QString(),
                                bool doVision = true,
                                bool doNonVision = false,
                                bool returnVision = false,
                                bool returnNonVision = false,
                                const QString &saveNonVisionPath = QString(),
                                const QString &downloadModel = QString(),
                                const QString &downloadTo = QString(),
                                const QString &hfToken = QString(),
                                const QString &maxWorkers = QString(),
                                bool ignoreUnnecessary = false,
                                QWidget *parent = nullptr);
    ~ImageUnderstandGUI();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onWorkerLogChunk(const QString &text);
    void onWorkerProgressChunk(const QString &text);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onCancelClicked();
    void checkProgress();

private:
    void setupUI();
    void startPythonProcess();
    void stopWorkerAndWait(int waitMs);
    void updateProgress(const QString &message, int value);
    void parseProgressOutput(const QString &output);
    void appendLogText(const QString &text);
    void flushPendingLog();
    void advanceSlide();
    void advanceHumor();
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    
    QString m_imagePath;
    bool m_useCache;
    QString m_cachePath;
    QString m_pythonPath;
    QString m_modelId;
    bool m_resumeDownload;
    QString m_resumeModel;
    QString m_saveEmbeddingPath;
    bool m_doCaption;
    bool m_contextGenerate;
    QString m_prompt;
    QString m_systemPrompt;
    QString m_maxTokens;
    bool m_doOcr;
    QString m_ocrSave;
    QString m_ocrLang;
    QString m_captionModel;
    QString m_saveWhenDownloaded;
    QString m_saveFromCache;
    bool m_doVision;
    bool m_doNonVision;
    bool m_returnVision;
    bool m_returnNonVision;
    QString m_saveNonVisionPath;
    QString m_downloadModel;
    QString m_downloadTo;
    QString m_hfToken;
    QString m_maxWorkers;
    bool m_ignoreUnnecessary;
    
    QLabel *m_titleLabel;
    QLabel *m_imageLabel;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_cancelButton;
    QPushButton *m_toggleDetailsButton;
    QPlainTextEdit *m_logView;
    QLabel *m_slideLabel;
    QLabel *m_humorLabel;
    QWidget *m_detailsWidget;
    QSplitter *m_detailsSplitter;
    QGraphicsOpacityEffect *m_slideOpacity;
    QGraphicsOpacityEffect *m_humorOpacity;
    QPropertyAnimation *m_slideAnim;
    QPropertyAnimation *m_humorAnim;
    QTimer *m_slideTimer;
    QTimer *m_humorTimer;
    QStringList m_slidePaths;
    QStringList m_humorLines;
    int m_slideIndex;
    int m_humorIndex;

    static ImageUnderstandGUI *s_activeLogger;
    
    QTimer *m_progressTimer;
    QTimer *m_logTimer;
    QElapsedTimer *m_progressThrottle;
    QString m_outputBuffer;
    QString m_pendingLog;
    QString m_pendingProgress;
    int m_currentProgress;

    QThread *m_ioThread;
    LogWorker *m_ioWorker;
};

#endif // IMAGEUNDERSTANDGUI_H
