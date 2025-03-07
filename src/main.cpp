#include <iostream>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QDateTime>
#include <QtGui/QScreen>
#include <QtGui/QColor>
#include <QtCore/QTimer>
#include <QtCore/QPoint>
#include <QtNetwork/QUdpSocket>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <atomic>

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <pthread.h>
#include <sched.h>
#endif

class UdpSender : public QObject {
    Q_OBJECT
public:
    UdpSender() {
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket.bind(QHostAddress::Any, 0, QUdpSocket::ShareAddress);
    }

    // Sends colour data to WiZ light via UDP
    void sendColour(const QString &ip, int port, const QColor &colour, int brightness) {
        char buffer[128];
        int len = snprintf(buffer, sizeof(buffer), 
            "{\"id\":1,\"method\":\"setPilot\",\"params\":{\"r\":%d,\"g\":%d,\"b\":%d,\"dimming\":%d}}", 
            colour.red(), colour.green(), colour.blue(), brightness);
        
        if (m_cachedIp != ip) {
            m_cachedIp = ip;
            m_cachedAddress = QHostAddress(ip);
        }
        
        m_socket.writeDatagram(buffer, len, m_cachedAddress, port);
    }

private:
    QUdpSocket m_socket;
    QString m_cachedIp;
    QHostAddress m_cachedAddress;
};

// High-priority thread for screen capture
class ScreenCaptureThread : public QThread {
    Q_OBJECT
public:
    ScreenCaptureThread(QObject *parent = nullptr) : QThread(parent) {
        m_active = false;
        m_x = 0;
        m_y = 0;
        m_size = 10;
        m_updateThreshold = 1;
    }
    
    void setParameters(int x, int y, int size, int threshold) {
        QMutexLocker locker(&m_mutex);
        m_x = x;
        m_y = y;
        m_size = size;
        m_updateThreshold = threshold;
    }
    
    void startCapture() {
        if (!m_active) {
            m_active = true;
            if (!isRunning()) {
                start(QThread::HighPriority);
            }
        }
    }
    
    void stopCapture() {
        m_active = false;
        wait();
    }

signals:
    void colourCaptured(const QColor &colour);

protected:
    void run() override {
        // Set maximum thread priority
        #ifdef Q_OS_WIN
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        #elif defined(Q_OS_LINUX)
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_RR);
        pthread_setschedparam(pthread_self(), SCHED_RR, &param);
        #endif
        
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        
        QColor lastColour;
        QColor currentColour;
        
        const int targetFPS = 60;
        const int sleepTime = 1000 / targetFPS;
        
        QRect captureRect;
        QRect screenRect = screen->geometry();
        
        QElapsedTimer timer;
        timer.start();
        
        while (m_active) {
            {
                QMutexLocker locker(&m_mutex);
                captureRect.setRect(m_x - m_size/2, m_y - m_size/2, m_size, m_size);
            }
            
            if (captureRect.width() <= 0 || captureRect.height() <= 0) {
                captureRect.setWidth(1);
                captureRect.setHeight(1);
            }
            
            captureRect = captureRect.intersected(screenRect);
            
            if (captureRect.isEmpty()) {
                QThread::msleep(sleepTime);
                continue;
            }
            
            QPixmap pixmap = screen->grabWindow(0, 
                captureRect.x(), captureRect.y(), 
                captureRect.width(), captureRect.height());
            
            if (pixmap.isNull()) {
                QThread::msleep(sleepTime);
                continue;
            }
            
            if (captureRect.width() == 1 && captureRect.height() == 1) {
                currentColour = pixmap.toImage().pixelColor(0, 0);
            } else {
                QImage image = pixmap.toImage();
                
                if (image.isNull()) {
                    QThread::msleep(sleepTime);
                    continue;
                }
                
                quint64 rTotal = 0, gTotal = 0, bTotal = 0;
                int pixelCount = 0;
                
                for (int y = 0; y < image.height(); ++y) {
                    const QRgb *line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                    for (int x = 0; x < image.width(); ++x) {
                        QRgb pixel = line[x];
                        rTotal += qRed(pixel);
                        gTotal += qGreen(pixel);
                        bTotal += qBlue(pixel);
                        pixelCount++;
                    }
                }

                if (pixelCount > 0) {
                    currentColour = QColor(
                        rTotal / pixelCount,
                        gTotal / pixelCount,
                        bTotal / pixelCount
                    );
                } else {
                    currentColour = QColor(0, 0, 0);
                }
            }
            
            if (!currentColour.isValid()) {
                currentColour = QColor(0, 0, 0);
            }
            
            // Only emit if colour changed significantly
            if (!lastColour.isValid() || 
                qAbs(currentColour.red() - lastColour.red()) +
                qAbs(currentColour.green() - lastColour.green()) +
                qAbs(currentColour.blue() - lastColour.blue()) > m_updateThreshold) {
                
                lastColour = currentColour;
                emit colourCaptured(currentColour);
            }
            
            int elapsed = timer.elapsed();
            timer.restart();
            if (elapsed < sleepTime) {
                QThread::msleep(sleepTime - elapsed);
            }
        }
    }

private:
    QMutex m_mutex;
    std::atomic<bool> m_active;
    int m_x, m_y, m_size;
    int m_updateThreshold;
};

// Overlay for mouse-based position selection
class EyedropperOverlay : public QWidget {
    Q_OBJECT
public:
    EyedropperOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        QRect geometry;
        for (QScreen *screen : QGuiApplication::screens()) {
            geometry = geometry.united(screen->geometry());
        }
        setGeometry(geometry);
        
        #ifdef Q_OS_WIN
        showFullScreen();
        #endif
    }

signals:
    void positionSelected(const QPoint &pos);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(m_currentPos.x() - 15, m_currentPos.y(), m_currentPos.x() + 15, m_currentPos.y());
        painter.drawLine(m_currentPos.x(), m_currentPos.y() - 15, m_currentPos.x(), m_currentPos.y() + 15);
        
        painter.setPen(QPen(Qt::black, 4));
        painter.drawLine(m_currentPos.x() - 15, m_currentPos.y(), m_currentPos.x() + 15, m_currentPos.y());
        painter.drawLine(m_currentPos.x(), m_currentPos.y() - 15, m_currentPos.x(), m_currentPos.y() + 15);
        
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(m_currentPos.x() - 15, m_currentPos.y(), m_currentPos.x() + 15, m_currentPos.y());
        painter.drawLine(m_currentPos.x(), m_currentPos.y() - 15, m_currentPos.x(), m_currentPos.y() + 15);
        
        painter.fillRect(m_currentPos.x() + 20, m_currentPos.y() + 20, 80, 20, QColor(0, 0, 0, 180));
        painter.setPen(Qt::white);
        painter.drawText(m_currentPos.x() + 25, m_currentPos.y() + 35, 
                        QString("(%1, %2)").arg(m_screenPos.x()).arg(m_screenPos.y()));
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        m_currentPos = event->pos();
        #ifdef Q_OS_WIN
        m_screenPos = mapToGlobal(m_currentPos);
        #else
        m_screenPos = m_currentPos;
        #endif
        
        update();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            #ifdef Q_OS_WIN
            QPoint globalPos = mapToGlobal(event->pos());
            emit positionSelected(globalPos);
            #else
            emit positionSelected(event->globalPos());
            #endif
            close();
        } else if (event->button() == Qt::RightButton) {
            close();
        }
    }
    
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
        }
    }

private:
    QPoint m_currentPos;
    QPoint m_screenPos;
};

class WizLedController : public QMainWindow {
    Q_OBJECT
public:
    WizLedController(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Wiz LED Colour Controller (Ultra Low Latency)");
        resize(400, 400);

        m_captureActive = false;
        m_captureX = QGuiApplication::primaryScreen()->geometry().width() / 2;
        m_captureY = QGuiApplication::primaryScreen()->geometry().height() / 2;
        m_captureSize = 10;
        m_wizIp = "192.168.50.110";
        m_wizPort = 38899;
        m_brightness = 100;
        m_updateThreshold = 3;
        m_gamma = 0.6;
        m_saturation = 1.8;
        m_redFactor = 1.2;
        m_greenFactor = 1.0;
        m_blueFactor = 1.2;

        QWidget *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        
        // Colour preview section
        QGroupBox *colourGroup = new QGroupBox("Colour Preview");
        QVBoxLayout *colourLayout = new QVBoxLayout;
        
        m_colourPreview = new QLabel;
        m_colourPreview->setFixedSize(100, 100);
        m_colourPreview->setAutoFillBackground(true);
        QPalette pal = m_colourPreview->palette();
        pal.setColor(QPalette::Window, Qt::black);
        m_colourPreview->setPalette(pal);
        colourLayout->addWidget(m_colourPreview, 0, Qt::AlignCenter);
        
        m_rgbLabel = new QLabel("RGB: 0, 0, 0");
        colourLayout->addWidget(m_rgbLabel);
        colourGroup->setLayout(colourLayout);
        mainLayout->addWidget(colourGroup);
        
        // Capture settings section
        QGroupBox *captureGroup = new QGroupBox("Capture Settings");
        QVBoxLayout *captureLayout = new QVBoxLayout;
        
        QHBoxLayout *posLayout = new QHBoxLayout;
        posLayout->addWidget(new QLabel("X:"));
        m_xSpinBox = new QSpinBox;
        m_xSpinBox->setRange(0, 5000);
        m_xSpinBox->setValue(m_captureX);
        posLayout->addWidget(m_xSpinBox);
        
        posLayout->addWidget(new QLabel("Y:"));
        m_ySpinBox = new QSpinBox;
        m_ySpinBox->setRange(0, 5000);
        m_ySpinBox->setValue(m_captureY);
        posLayout->addWidget(m_ySpinBox);
        
        posLayout->addWidget(new QLabel("Size:"));
        m_sizeSpinBox = new QSpinBox;
        m_sizeSpinBox->setRange(1, 50);
        m_sizeSpinBox->setValue(m_captureSize);
        posLayout->addWidget(m_sizeSpinBox);

        QPushButton *eyedropperButton = new QPushButton("Pick Position");
        connect(eyedropperButton, &QPushButton::clicked, this, &WizLedController::startEyedropperMode);
        posLayout->addWidget(eyedropperButton);
        
        captureLayout->addLayout(posLayout);
        
        connect(m_xSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
                this, &WizLedController::onCapturePositionChanged);
        connect(m_ySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
                this, &WizLedController::onCapturePositionChanged);
        connect(m_sizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
                this, [this](int value) { m_captureSize = value; updateCaptureParameters(); });
        
        QPushButton *testColourButton = new QPushButton("Test: Send Red Colour");
        connect(testColourButton, &QPushButton::clicked, this, [this]() {
            sendColour(QColor(255, 0, 0));
            m_statusLabel->setText("Sent test colour (Red)");
        });
        captureLayout->addWidget(testColourButton);
        
        m_captureButton = new QPushButton("Start Capture");
        connect(m_captureButton, &QPushButton::clicked, this, &WizLedController::toggleCapture);
        captureLayout->addWidget(m_captureButton);
        
        captureGroup->setLayout(captureLayout);
        mainLayout->addWidget(captureGroup);
        
        // Wiz LED settings section
        QGroupBox *wizGroup = new QGroupBox("Wiz LED Settings");
        QVBoxLayout *wizLayout = new QVBoxLayout;
        
        QHBoxLayout *ipLayout = new QHBoxLayout;
        ipLayout->addWidget(new QLabel("IP Address:"));
        m_ipEdit = new QLineEdit(m_wizIp);
        ipLayout->addWidget(m_ipEdit);
        
        ipLayout->addWidget(new QLabel("Brightness:"));
        m_brightnessSpinBox = new QSpinBox;
        m_brightnessSpinBox->setRange(1, 100);
        m_brightnessSpinBox->setValue(m_brightness);
        ipLayout->addWidget(m_brightnessSpinBox);
        
        wizLayout->addLayout(ipLayout);
        
        // Colour correction controls
        QGroupBox *correctionGroup = new QGroupBox("Colour Correction");
        QVBoxLayout *correctionLayout = new QVBoxLayout;
        
        QHBoxLayout *gammaLayout = new QHBoxLayout;
        gammaLayout->addWidget(new QLabel("Gamma:"));
        m_gammaSpinBox = new QDoubleSpinBox;
        m_gammaSpinBox->setRange(0.5, 3.0);
        m_gammaSpinBox->setSingleStep(0.1);
        m_gammaSpinBox->setValue(m_gamma);
        gammaLayout->addWidget(m_gammaSpinBox);
        correctionLayout->addLayout(gammaLayout);
        
        QHBoxLayout *satLayout = new QHBoxLayout;
        satLayout->addWidget(new QLabel("Saturation:"));
        m_saturationSpinBox = new QDoubleSpinBox;
        m_saturationSpinBox->setRange(0.5, 2.5);
        m_saturationSpinBox->setSingleStep(0.1);
        m_saturationSpinBox->setValue(m_saturation);
        satLayout->addWidget(m_saturationSpinBox);
        correctionLayout->addLayout(satLayout);
        
        QHBoxLayout *wbLayout = new QHBoxLayout;
        wbLayout->addWidget(new QLabel("R:"));
        m_redFactorSpinBox = new QDoubleSpinBox;
        m_redFactorSpinBox->setRange(0.5, 2.0);
        m_redFactorSpinBox->setSingleStep(0.05);
        m_redFactorSpinBox->setValue(m_redFactor);
        wbLayout->addWidget(m_redFactorSpinBox);
        
        wbLayout->addWidget(new QLabel("G:"));
        m_greenFactorSpinBox = new QDoubleSpinBox;
        m_greenFactorSpinBox->setRange(0.5, 2.0);
        m_greenFactorSpinBox->setSingleStep(0.05);
        m_greenFactorSpinBox->setValue(m_greenFactor);
        wbLayout->addWidget(m_greenFactorSpinBox);
        
        wbLayout->addWidget(new QLabel("B:"));
        m_blueFactorSpinBox = new QDoubleSpinBox;
        m_blueFactorSpinBox->setRange(0.5, 2.0);
        m_blueFactorSpinBox->setSingleStep(0.05);
        m_blueFactorSpinBox->setValue(m_blueFactor);
        wbLayout->addWidget(m_blueFactorSpinBox);
        
        correctionLayout->addLayout(wbLayout);
        correctionGroup->setLayout(correctionLayout);
        wizLayout->addWidget(correctionGroup);
        
        QHBoxLayout *fpsLayout = new QHBoxLayout;
        fpsLayout->addWidget(new QLabel("FPS Limit:"));
        m_fpsSpinBox = new QSpinBox;
        m_fpsSpinBox->setRange(30, 200);
        m_fpsSpinBox->setValue(60);
        fpsLayout->addWidget(m_fpsSpinBox);
        
        fpsLayout->addWidget(new QLabel("FPS:"));
        m_fpsLabel = new QLabel("0");
        fpsLayout->addWidget(m_fpsLabel);
        wizLayout->addLayout(fpsLayout);
        
        QPushButton *applyButton = new QPushButton("Apply Settings");
        connect(applyButton, &QPushButton::clicked, this, &WizLedController::applyWizSettings);
        wizLayout->addWidget(applyButton);
        
        wizGroup->setLayout(wizLayout);
        mainLayout->addWidget(wizGroup);
        
        m_statusLabel = new QLabel("Ready");
        mainLayout->addWidget(m_statusLabel);
        
        m_udpSender = new UdpSender();
        
        m_captureThread = new ScreenCaptureThread(this);
        connect(m_captureThread, &ScreenCaptureThread::colourCaptured, 
                this, &WizLedController::onColourCaptured);
        
        m_fpsTimer = new QTimer(this);
        connect(m_fpsTimer, &QTimer::timeout, this, &WizLedController::updateFPS);
        m_fpsTimer->start(1000);
        
        m_frameCount = 0;
        m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();
    }
    
    ~WizLedController() {
        if (m_captureActive) {
            m_captureThread->stopCapture();
        }
        delete m_udpSender;
    }

private:
    // Applies colour corrections including gamma, saturation and RGB balance
    QColor processColour(const QColor &original) {
        if (!original.isValid()) {
            return QColor(0, 0, 0);
        }
        
        if ((original.red() < 5 && original.green() < 5 && original.blue() < 5) || 
            (original.red() > 250 && original.green() > 250 && original.blue() > 250)) {
            return original;
        }
        
        float gamma = m_gammaSpinBox->value();
        float r = pow(original.redF(), 1.0f / gamma) * m_redFactorSpinBox->value();
        float g = pow(original.greenF(), 1.0f / gamma) * m_greenFactorSpinBox->value();
        float b = pow(original.blueF(), 1.0f / gamma) * m_blueFactorSpinBox->value();
        
        r = qBound<float>(0.0f, r, 1.0f);
        g = qBound<float>(0.0f, g, 1.0f);
        b = qBound<float>(0.0f, b, 1.0f);
        
        QColor hsl = QColor::fromRgbF(r, g, b).toHsl();
        
        float saturation = m_saturationSpinBox->value();
        float s = qBound<float>(0.0f, hsl.hslSaturationF() * saturation, 1.0f);
        hsl.setHslF(hsl.hslHueF(), s, hsl.lightnessF());
        
        return hsl.toRgb();
    }

private slots:
    void updateFPS() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 elapsed = now - m_lastFrameTime;
        
        if (elapsed > 0) {
            double fps = m_frameCount * 1000.0 / elapsed;
            m_fpsLabel->setText(QString::number(fps, 'f', 1));
        }
        
        m_frameCount = 0;
        m_lastFrameTime = now;
    }
    
    void startEyedropperMode() {
        bool wasActive = m_captureActive;
        if (m_captureActive) {
            m_captureActive = false;
            m_captureThread->stopCapture();
        }

        EyedropperOverlay *overlay = new EyedropperOverlay();
        connect(overlay, &EyedropperOverlay::positionSelected, this, [this, wasActive](const QPoint &pos) {
            m_xSpinBox->setValue(pos.x());
            m_ySpinBox->setValue(pos.y());
            onCapturePositionChanged();
            m_statusLabel->setText(QString("Position set to (%1, %2)").arg(pos.x()).arg(pos.y()));

            if (wasActive && !m_captureActive) {
                toggleCapture();
            }
        });
        
        connect(overlay, &EyedropperOverlay::destroyed, this, [this, wasActive]() {
            if (wasActive && !m_captureActive) {
                toggleCapture();
            }
        });
        
        overlay->show();
        
        #ifdef Q_OS_WIN
        overlay->activateWindow();
        overlay->raise();
        #endif
    }

    void toggleCapture() {
        if (m_captureActive) {
            m_captureActive = false;
            m_captureButton->setText("Start Capture");
            m_captureThread->stopCapture();
            m_statusLabel->setText("Capture stopped");
        } else {
            m_captureActive = true;
            m_captureButton->setText("Stop Capture");
            
            updateCaptureParameters();
            
            m_captureThread->startCapture();
            m_statusLabel->setText(QString("Capturing at (%1, %2)").arg(m_captureX).arg(m_captureY));
        }
    }
    
    void onCapturePositionChanged() {
        m_captureX = m_xSpinBox->value();
        m_captureY = m_ySpinBox->value();
        
        updateCaptureParameters();
        
        if (m_captureActive) {
            m_statusLabel->setText(QString("Capturing at (%1, %2)").arg(m_captureX).arg(m_captureY));
        }
    }
    
    void updateCaptureParameters() {
        m_captureSize = m_sizeSpinBox->value();
        m_captureThread->setParameters(m_captureX, m_captureY, m_captureSize, m_updateThreshold);
    }
    
    void applyWizSettings() {
        m_wizIp = m_ipEdit->text();
        m_brightness = m_brightnessSpinBox->value();
        m_gamma = m_gammaSpinBox->value();
        m_saturation = m_saturationSpinBox->value();
        m_redFactor = m_redFactorSpinBox->value();
        m_greenFactor = m_greenFactorSpinBox->value();
        m_blueFactor = m_blueFactorSpinBox->value();
        
        m_statusLabel->setText(QString("Settings updated: IP=%1, Brightness=%2").arg(m_wizIp).arg(m_brightness));
        
        if (m_lastSentColour.isValid()) {
            QColor tempColour = m_lastSentColour;
            m_lastSentColour = QColor();
            sendColour(tempColour);
        }
    }
    
    void onColourCaptured(const QColor &newColour) {
        QMetaObject::invokeMethod(this, "updateUIColour", Qt::QueuedConnection, 
                                 Q_ARG(QColor, newColour));
        
        m_frameCount++;
    }
    
    Q_INVOKABLE void updateUIColour(const QColor &colour) {
        QPalette pal = m_colourPreview->palette();
        pal.setColor(QPalette::Window, colour);
        m_colourPreview->setPalette(pal);
        
        sendColour(colour);
    }
    
    void sendColour(const QColor &colour) {
        QColor processedColour = processColour(colour);
        
        m_rgbLabel->setText(QString("Original: %1,%2,%3  LED: %4,%5,%6")
                           .arg(colour.red()).arg(colour.green()).arg(colour.blue())
                           .arg(processedColour.red()).arg(processedColour.green()).arg(processedColour.blue()));
        
        m_udpSender->sendColour(m_wizIp, m_wizPort, processedColour, m_brightness);
        m_lastSentColour = colour;
    }

private:
    QLabel *m_colourPreview;
    QLabel *m_rgbLabel;
    QLabel *m_statusLabel;
    QPushButton *m_captureButton;
    QSpinBox *m_xSpinBox;
    QSpinBox *m_ySpinBox;
    QSpinBox *m_sizeSpinBox;
    QSpinBox *m_brightnessSpinBox;
    QSpinBox *m_fpsSpinBox;
    QLabel *m_fpsLabel;
    QLineEdit *m_ipEdit;
    QDoubleSpinBox *m_gammaSpinBox;
    QDoubleSpinBox *m_saturationSpinBox;
    QDoubleSpinBox *m_redFactorSpinBox;
    QDoubleSpinBox *m_greenFactorSpinBox;
    QDoubleSpinBox *m_blueFactorSpinBox;
    
    bool m_captureActive;
    int m_captureX;
    int m_captureY;
    int m_captureSize;
    QString m_wizIp;
    int m_wizPort;
    int m_brightness;
    QColor m_lastSentColour;
    int m_updateThreshold;
    int m_frameCount;
    qint64 m_lastFrameTime;
    float m_gamma;
    float m_saturation;
    float m_redFactor;
    float m_greenFactor;
    float m_blueFactor;
    
    ScreenCaptureThread *m_captureThread;
    UdpSender *m_udpSender;
    QTimer *m_fpsTimer;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    #ifdef Q_OS_WIN
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    #endif
    
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    
    app.setAttribute(Qt::AA_DisableWindowContextHelpButton);
    
    WizLedController controller;
    controller.show();
    return app.exec();
}

#include "main.moc"