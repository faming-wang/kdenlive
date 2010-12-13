/***************************************************************************
 *   Copyright (C) 2010 by Simon Andreas Eugster (simon.eu@gmail.com)      *
 *   This file is part of kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <QPainter>
#include <QMenu>

#include "spectrogram.h"

// Defines the number of FFT samples to store.
// Around 4 kB for a window size of 2000. Should be at least as large as the
// highest vertical screen resolution available for complete reconstruction.
// Can be less as a pre-rendered image is kept in space.
#define SPECTROGRAM_HISTORY_SIZE 1000

// Uncomment for debugging
//#define DEBUG_SPECTROGRAM

#ifdef DEBUG_SPECTROGRAM
#include <QDebug>
#endif

#define MIN_DB_VALUE -120
#define MAX_FREQ_VALUE 96000
#define MIN_FREQ_VALUE 1000

Spectrogram::Spectrogram(QWidget *parent) :
        AbstractAudioScopeWidget(true, parent),
        m_fftTools(),
        m_fftHistory(),
        m_fftHistoryImg(),
        m_parameterChanged(false)
{
    ui = new Ui::Spectrogram_UI;
    ui->setupUi(this);


    m_aResetHz = new QAction(i18n("Reset maximum frequency to sampling rate"), this);
    m_aGrid = new QAction(i18n("Draw grid"), this);
    m_aGrid->setCheckable(true);
    m_aTrackMouse = new QAction(i18n("Track mouse"), this);
    m_aTrackMouse->setCheckable(true);


    m_menu->addSeparator();
    m_menu->addAction(m_aResetHz);
    m_menu->addAction(m_aTrackMouse);
    m_menu->addAction(m_aGrid);
    m_menu->removeAction(m_aRealtime);


    ui->windowSize->addItem("256", QVariant(256));
    ui->windowSize->addItem("512", QVariant(512));
    ui->windowSize->addItem("1024", QVariant(1024));
    ui->windowSize->addItem("2048", QVariant(2048));

    ui->windowFunction->addItem(i18n("Rectangular window"), FFTTools::Window_Rect);
    ui->windowFunction->addItem(i18n("Triangular window"), FFTTools::Window_Triangle);
    ui->windowFunction->addItem(i18n("Hamming window"), FFTTools::Window_Hamming);

    // Note: These strings are used in both Spectogram and AudioSpectrum. Ideally change both (if necessary) to reduce workload on translators
    ui->labelFFTSize->setToolTip(i18n("The maximum window size is limited by the number of samples per frame."));
    ui->windowSize->setToolTip(i18n("A bigger window improves the accuracy at the cost of computational power."));
    ui->windowFunction->setToolTip(i18n("The rectangular window function is good for signals with equal signal strength (narrow peak), but creates more smearing. See Window function on Wikipedia."));

    bool b = true;
    b &= connect(m_aResetHz, SIGNAL(triggered()), this, SLOT(slotResetMaxFreq()));
    b &= connect(ui->windowFunction, SIGNAL(currentIndexChanged(int)), this, SLOT(forceUpdate()));
    b &= connect(this, SIGNAL(signalMousePositionChanged()), this, SLOT(forceUpdateHUD()));
    Q_ASSERT(b);

    AbstractScopeWidget::init();
}

Spectrogram::~Spectrogram()
{
    writeConfig();

    delete m_aResetHz;
    delete m_aTrackMouse;
    delete m_aGrid;
}

void Spectrogram::readConfig()
{
    AbstractScopeWidget::readConfig();

    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup scopeConfig(config, AbstractScopeWidget::configName());

    ui->windowSize->setCurrentIndex(scopeConfig.readEntry("windowSize", 0));
    ui->windowFunction->setCurrentIndex(scopeConfig.readEntry("windowFunction", 0));
    m_aTrackMouse->setChecked(scopeConfig.readEntry("trackMouse", true));
    m_aGrid->setChecked(scopeConfig.readEntry("drawGrid", true));
    m_dBmax = scopeConfig.readEntry("dBmax", 0);
    m_dBmin = scopeConfig.readEntry("dBmin", -70);
    m_freqMax = scopeConfig.readEntry("freqMax", 0);

    if (m_freqMax == 0) {
        m_customFreq = false;
        m_freqMax = 10000;
    } else {
        m_customFreq = true;
    }
}
void Spectrogram::writeConfig()
{
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup scopeConfig(config, AbstractScopeWidget::configName());

    scopeConfig.writeEntry("windowSize", ui->windowSize->currentIndex());
    scopeConfig.writeEntry("windowFunction", ui->windowFunction->currentIndex());
    scopeConfig.writeEntry("trackMouse", m_aTrackMouse->isChecked());
    scopeConfig.writeEntry("drawGrid", m_aGrid->isChecked());
    scopeConfig.writeEntry("dBmax", m_dBmax);
    scopeConfig.writeEntry("dBmin", m_dBmin);

    if (m_customFreq) {
        scopeConfig.writeEntry("freqMax", m_freqMax);
    } else {
        scopeConfig.writeEntry("freqMax", 0);
    }

    scopeConfig.sync();
}

QString Spectrogram::widgetName() const { return QString("Spectrogram"); }

QRect Spectrogram::scopeRect()
{
    m_scopeRect = QRect(
            QPoint(
                    10,                                     // Left
                    ui->verticalSpacer->geometry().top()+6  // Top
            ),
            AbstractAudioScopeWidget::rect().bottomRight()
    );
    m_innerScopeRect = QRect(
            QPoint(
                    m_scopeRect.left()+66,                  // Left
                    m_scopeRect.top()+6                     // Top
            ), QPoint(
                    ui->verticalSpacer->geometry().right()-70,
                    ui->verticalSpacer->geometry().bottom()-40
            )
    );
    return m_scopeRect;
}

QImage Spectrogram::renderHUD(uint)
{

    QTime start = QTime::currentTime();

    int x, y;
    const uint minDistY = 30; // Minimum distance between two lines
    const uint minDistX = 40;
    const uint textDistX = 10;
    const uint textDistY = 25;
    const uint topDist = m_innerScopeRect.top() - m_scopeRect.top();
    const uint leftDist = m_innerScopeRect.left() - m_scopeRect.left();
    const int mouseX = m_mousePos.x() - m_innerScopeRect.left();
    const int mouseY = m_mousePos.y() - m_innerScopeRect.top();
    bool hideText;

    QImage hud(m_scopeRect.size(), QImage::Format_ARGB32);
    hud.fill(qRgba(0,0,0,0));

    QPainter davinci(&hud);
    davinci.setPen(AbstractScopeWidget::penLight);


    // Frame display
    if (m_aGrid->isChecked()) {
        for (int frameNumber = 0; frameNumber < m_innerScopeRect.height(); frameNumber += minDistY) {
            y = topDist + m_innerScopeRect.height()-1 - frameNumber;
            hideText = m_aTrackMouse->isChecked() && m_mouseWithinWidget && abs(y - mouseY) < (int)textDistY && mouseY < m_innerScopeRect.height()
                    && mouseX < m_innerScopeRect.width() && mouseX >= 0;
    
            davinci.drawLine(leftDist, y, leftDist + m_innerScopeRect.width()-1, y);
            if (!hideText) {
                davinci.drawText(leftDist + m_innerScopeRect.width() + textDistX, y + 6, QVariant(frameNumber).toString());
            }
        }
    }
    // Draw a line through the mouse position with the correct Frame number
    if (m_aTrackMouse->isChecked() && m_mouseWithinWidget && mouseY < m_innerScopeRect.height()
            && mouseX < m_innerScopeRect.width() && mouseX >= 0) {
        davinci.setPen(AbstractScopeWidget::penLighter);

        x = leftDist + mouseX;
        y = topDist + mouseY - 20;
        if (y < 0) {
            y = 0;
        }
        if (y > (int)topDist + m_innerScopeRect.height()-1 - 30) {
            y = topDist + m_innerScopeRect.height()-1 - 30;
        }
        davinci.drawLine(x, topDist + mouseY, leftDist + m_innerScopeRect.width()-1, topDist + mouseY);
        davinci.drawText(leftDist + m_innerScopeRect.width() + textDistX,
                         y,
                         m_scopeRect.right()-m_innerScopeRect.right()-textDistX,
                         40,
                         Qt::AlignLeft,
                         i18n("Frame\n%1", m_innerScopeRect.height()-1-mouseY));
    }

    // Frequency grid
    const uint hzDiff = ceil( ((float)minDistX)/m_innerScopeRect.width() * m_freqMax / 1000 ) * 1000;
    const int rightBorder = leftDist + m_innerScopeRect.width()-1;
    x = 0;
    y = topDist + m_innerScopeRect.height() + textDistY;
    if (m_aGrid->isChecked()) {
        for (uint hz = 0; x <= rightBorder; hz += hzDiff) {
            davinci.setPen(AbstractScopeWidget::penLight);
            x = leftDist + (m_innerScopeRect.width()-1) * ((float)hz)/m_freqMax;

            // Hide text if it would overlap with the text drawn at the mouse position
            hideText = m_aTrackMouse->isChecked() && m_mouseWithinWidget && abs(x-(leftDist + mouseX + 20)) < (int) minDistX + 16
                    && mouseX < m_innerScopeRect.width() && mouseX >= 0;

            if (x <= rightBorder) {
                davinci.drawLine(x, topDist, x, topDist + m_innerScopeRect.height()+6);
            }
            if (x+textDistY < leftDist + m_innerScopeRect.width()) {
                // Only draw the text label if there is still enough room for the final one at the right.
                if (!hideText) {
                    davinci.drawText(x-4, y, QVariant(hz/1000).toString());
                }
            }


            if (hz > 0) {
                // Draw finer lines between the main lines
                davinci.setPen(AbstractScopeWidget::penLightDots);
                for (uint dHz = 3; dHz > 0; dHz--) {
                    x = leftDist + m_innerScopeRect.width() * ((float)hz - dHz * hzDiff/4.0f)/m_freqMax;
                    if (x > rightBorder) {
                        break;
                    }
                    davinci.drawLine(x, topDist, x, topDist + m_innerScopeRect.height()-1);
                }
            }
        }
        // Draw the line at the very right (maximum frequency)
        x = leftDist + m_innerScopeRect.width()-1;
        hideText = m_aTrackMouse->isChecked() && m_mouseWithinWidget && abs(x-(leftDist + mouseX + 30)) < (int) minDistX
                && mouseX < m_innerScopeRect.width() && mouseX >= 0;
        davinci.drawLine(x, topDist, x, topDist + m_innerScopeRect.height()+6);
        if (!hideText) {
            davinci.drawText(x-10, y, i18n("%1 kHz").arg((double)m_freqMax/1000, 0, 'f', 1));
        }
    }

    // Draw a line through the mouse position with the correct frequency label
    if (m_aTrackMouse->isChecked() && m_mouseWithinWidget && mouseX < m_innerScopeRect.width() && mouseX >= 0) {
        davinci.setPen(AbstractScopeWidget::penThin);
        x = leftDist + mouseX;
        davinci.drawLine(x, topDist, x, topDist + m_innerScopeRect.height()+6);
        davinci.drawText(x-10, y, i18n("%1 kHz")
                         .arg((double)(m_mousePos.x()-m_innerScopeRect.left())/m_innerScopeRect.width() * m_freqMax/1000, 0, 'f', 2));
    }

    // Draw the dB brightness scale
    float val;
    davinci.setPen(AbstractScopeWidget::penLighter);
    for (y = topDist; y < (int)topDist + m_innerScopeRect.height(); y++) {
        val = 1-((float)y-topDist)/(m_innerScopeRect.height()-1);
        int col = qRgba(255, 255, 255, 255.0 * val);
        for (x = leftDist-6; x >= (int)leftDist-13; x--) {
            hud.setPixel(x, y, col);
        }
    }
    const int rectWidth = leftDist-m_scopeRect.left()-22;
    const int rectHeight = 50;
    davinci.setFont(QFont(QFont().defaultFamily(), 10));
    davinci.drawText(m_scopeRect.left(), topDist, rectWidth, rectHeight, Qt::AlignRight, i18n("%1\ndB", m_dBmax));
    davinci.drawText(m_scopeRect.left(), topDist + m_innerScopeRect.height()-20, rectWidth, rectHeight, Qt::AlignRight, i18n("%1\ndB", m_dBmin));


    emit signalHUDRenderingFinished(start.elapsed(), 1);
    return hud;
}
QImage Spectrogram::renderAudioScope(uint, const QVector<int16_t> audioFrame, const int freq,
                                     const int num_channels, const int num_samples, const int newData) {
    if (audioFrame.size() > 63) {
        if (!m_customFreq) {
            m_freqMax = freq / 2;
        }
        bool newDataAvailable = newData > 0;

#ifdef DEBUG_SPECTROGRAM
        qDebug() << "New data for " << widgetName() << ": " << newDataAvailable << " (" << newData << " units)";
#endif

        QTime start = QTime::currentTime();

        int fftWindow = ui->windowSize->itemData(ui->windowSize->currentIndex()).toInt();
        if (fftWindow > num_samples) {
            fftWindow = num_samples;
        }
        if ((fftWindow & 1) == 1) {
            fftWindow--;
        }

        // Show the window size used, for information
        ui->labelFFTSizeNumber->setText(QVariant(fftWindow).toString());

        if (newDataAvailable) {

            float freqSpectrum[fftWindow/2];

            // Get the spectral power distribution of the input samples,
            // using the given window size and function
            FFTTools::WindowType windowType = (FFTTools::WindowType) ui->windowFunction->itemData(ui->windowFunction->currentIndex()).toInt();
            m_fftTools.fftNormalized(audioFrame, 0, num_channels, freqSpectrum, windowType, fftWindow, 0);

            // This methid might be called also when a simple refresh is required.
            // In this case there is no data to append to the history. Only append new data.
            QVector<float> spectrumVector(fftWindow/2);
            memcpy(spectrumVector.data(), &freqSpectrum[0], fftWindow/2 * sizeof(float));
            m_fftHistory.prepend(spectrumVector);
        }
#ifdef DEBUG_SPECTROGRAM
        else {
            qDebug() << widgetName() << ": Has no new data to Fourier-transform";
        }
#endif

        // Limit the maximum history size to avoid wasting space
        while (m_fftHistory.size() > SPECTROGRAM_HISTORY_SIZE) {
            m_fftHistory.removeLast();
        }

        // Draw the spectrum
        QImage spectrum(m_scopeRect.size(), QImage::Format_ARGB32);
        spectrum.fill(qRgba(0,0,0,0));
        QPainter davinci(&spectrum);
        const uint w = m_innerScopeRect.width();
        const uint h = m_innerScopeRect.height();
        const uint leftDist = m_innerScopeRect.left() - m_scopeRect.left();
        const uint topDist = m_innerScopeRect.top() - m_scopeRect.top();
        float f;
        float x;
        float x_prev = 0;
        float val;
        uint windowSize;
        uint xi;
        uint y;
        bool completeRedraw = true;

        if (m_fftHistoryImg.size() == m_scopeRect.size() && !m_parameterChanged) {
            // The size of the widget and the parameters (like min/max dB) have not changed since last time,
            // so we can re-use it, shift it by one pixel, and render the single remaining line. Usually about
            // 10 times faster for a widget height of around 400 px.
            if (newDataAvailable) {
                davinci.drawImage(0, -1, m_fftHistoryImg);
            } else {
                // spectrum = m_fftHistoryImg does NOT work, leads to segfaults (anyone knows why, please tell me)
                davinci.drawImage(0, 0, m_fftHistoryImg);
            }
            completeRedraw = false;
        }

        y = 0;
        if (newData || m_parameterChanged) {
            m_parameterChanged = false;

            for (QList<QVector<float> >::iterator it = m_fftHistory.begin(); it != m_fftHistory.end(); it++) {

                windowSize = (*it).size();

                for (uint i = 0; i < w; i++) {

                    // i:   Pixel coordinate
                    // f:   Target frequency
                    // x:   Frequency array index (float!) corresponding to the pixel
                    // xi:  floor(x)
                    // val: dB value at position x (Range: [-inf,0])

                    f = i/((float) w-1.0) * m_freqMax;
                    x = 2*f/freq * (windowSize - 1);
                    xi = (int) floor(x);

                    if (x >= windowSize) {
                        break;
                    }

                    // Use linear interpolation in order to get smoother display
                    if (i == 0 || xi == windowSize-1) {
                        // ... except if we are at the left or right border of the display or the spectrum
                        val = (*it)[xi];
                    } else {

                        if ((*it)[xi] > (*it)[xi+1]
                            && x_prev < xi) {
                            // This is a hack to preserve peaks.
                            // Consider f = {0, 100, 0}
                            //          x = {0.5,  1.5}
                            // Then x is 50 both times, and the 100 peak is lost.
                            // Get it back here for the first x after the peak.
                            val = (*it)[xi];
                        } else {
                            val =   (xi+1 - x) * (*it)[xi]
                                  + (x - xi)   * (*it)[xi+1];
                        }
                    }

                    // Normalize to [0 1], 1 corresponding to 0 dB and 0 to dbMin dB
                    val = (val-m_dBmax)/(m_dBmax-m_dBmin) + 1;
                    if (val < 0) {
                        val = 0;
                    } else if (val > 1) {
                        val = 1;
                    }

                    spectrum.setPixel(leftDist + i, topDist + h-1 - y, qRgba(255, 255, 255, val * 255));

                    x_prev = x;
                }

                y++;
                if (y >= topDist + m_innerScopeRect.height()) {
                    break;
                }
                if (!completeRedraw) {
                    break;
                }
            }
        }

#ifdef DEBUG_SPECTROGRAM
        qDebug() << "Rendered " << y-topDist << "lines from " << m_fftHistory.size() << " available samples in " << start.elapsed() << " ms"
                << (completeRedraw ? "" : " (re-used old image)");
        uint storedBytes = 0;
        for (QList< QVector<float> >::iterator it = m_fftHistory.begin(); it != m_fftHistory.end(); it++) {
            storedBytes += (*it).size() * sizeof((*it)[0]);
        }
        qDebug() << QString("Total storage used: %1 kB").arg((double)storedBytes/1000, 0, 'f', 2);
#endif

        m_fftHistoryImg = spectrum;

        emit signalScopeRenderingFinished(start.elapsed(), 1);
        return spectrum;
    } else {
        emit signalScopeRenderingFinished(0, 1);
        return QImage();
    }
}
QImage Spectrogram::renderBackground(uint) { return QImage(); }

bool Spectrogram::isHUDDependingOnInput() const { return false; }
bool Spectrogram::isScopeDependingOnInput() const { return true; }
bool Spectrogram::isBackgroundDependingOnInput() const { return false; }

void Spectrogram::handleMouseDrag(const QPoint movement, const RescaleDirection rescaleDirection, const Qt::KeyboardModifiers rescaleModifiers)
{
    if (rescaleDirection == North) {
        // Nort-South direction: Adjust the dB scale

        if ((rescaleModifiers & Qt::ShiftModifier) == 0) {

            // By default adjust the min dB value
            m_dBmin += movement.y();

        } else {

            // Adjust max dB value if Shift is pressed.
            m_dBmax += movement.y();

        }

        // Ensure the dB values lie in [-100, 0] (or rather [MIN_DB_VALUE, 0])
        // 0 is the upper bound, everything below -70 dB is most likely noise
        if (m_dBmax > 0) {
            m_dBmax = 0;
        }
        if (m_dBmin < MIN_DB_VALUE) {
            m_dBmin = MIN_DB_VALUE;
        }
        // Ensure there is at least 6 dB between the minimum and the maximum value;
        // lower values hardly make sense
        if (m_dBmax - m_dBmin < 6) {
            if ((rescaleModifiers & Qt::ShiftModifier) == 0) {
                // min was adjusted; Try to adjust the max value to maintain the
                // minimum dB difference of 6 dB
                m_dBmax = m_dBmin + 6;
                if (m_dBmax > 0) {
                    m_dBmax = 0;
                    m_dBmin = -6;
                }
            } else {
                // max was adjusted, adjust min
                m_dBmin = m_dBmax - 6;
                if (m_dBmin < MIN_DB_VALUE) {
                    m_dBmin = MIN_DB_VALUE;
                    m_dBmax = MIN_DB_VALUE+6;
                }
            }
        }

        m_parameterChanged = true;
        forceUpdateHUD();
        forceUpdateScope();

    } else if (rescaleDirection == East) {
        // East-West direction: Adjust the maximum frequency
        m_freqMax -= 100*movement.x();
        if (m_freqMax < MIN_FREQ_VALUE) {
            m_freqMax = MIN_FREQ_VALUE;
        }
        if (m_freqMax > MAX_FREQ_VALUE) {
            m_freqMax = MAX_FREQ_VALUE;
        }
        m_customFreq = true;

        m_parameterChanged = true;
        forceUpdateHUD();
        forceUpdateScope();
    }
}



void Spectrogram::slotResetMaxFreq()
{
    m_customFreq = false;
    m_parameterChanged = true;
    forceUpdateHUD();
    forceUpdateScope();
}

void Spectrogram::resizeEvent(QResizeEvent *event)
{
    m_parameterChanged = true;
    AbstractAudioScopeWidget::resizeEvent(event);
}

#undef SPECTROGRAM_HISTORY_SIZE
#ifdef DEBUG_SPECTROGRAM
#undef DEBUG_SPECTROGRAM
#endif
