///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2017 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include "udpsourcegui.h"

#include "device/devicesinkapi.h"
#include "device/deviceuiset.h"
#include "dsp/spectrumvis.h"
#include "dsp/dspengine.h"
#include "util/simpleserializer.h"
#include "util/db.h"
#include "gui/basicchannelsettingsdialog.h"
#include "plugin/pluginapi.h"
#include "mainwindow.h"

#include "ui_udpsourcegui.h"

UDPSourceGUI* UDPSourceGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx)
{
    UDPSourceGUI* gui = new UDPSourceGUI(pluginAPI, deviceUISet, channelTx);
    return gui;
}

void UDPSourceGUI::destroy()
{
    delete this;
}

void UDPSourceGUI::setName(const QString& name)
{
    setObjectName(name);
}

QString UDPSourceGUI::getName() const
{
    return objectName();
}

qint64 UDPSourceGUI::getCenterFrequency() const {
    return m_channelMarker.getCenterFrequency();
}

void UDPSourceGUI::setCenterFrequency(qint64 centerFrequency)
{
    m_channelMarker.setCenterFrequency(centerFrequency);
    applySettings();
}

void UDPSourceGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(true);
}

QByteArray UDPSourceGUI::serialize() const
{
    return m_settings.serialize();
}

bool UDPSourceGUI::deserialize(const QByteArray& data)
{
    if(m_settings.deserialize(data))
    {
        displaySettings();
        applySettings(true);
        return true;
    } else {
        resetToDefaults();
        return false;
    }
}

bool UDPSourceGUI::handleMessage(const Message& message)
{
    if (UDPSource::MsgConfigureUDPSource::match(message))
    {
        const UDPSource::MsgConfigureUDPSource& cfg = (UDPSource::MsgConfigureUDPSource&) message;
        m_settings = cfg.getSettings();
        blockApplySettings(true);
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else
    {
        return false;
    }
}

void UDPSourceGUI::handleSourceMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()) != 0)
    {
        if (handleMessage(*message))
        {
            delete message;
        }
    }
}

UDPSourceGUI::UDPSourceGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx, QWidget* parent) :
        RollupWidget(parent),
        ui(new Ui::UDPSourceGUI),
        m_pluginAPI(pluginAPI),
        m_deviceUISet(deviceUISet),
        m_tickCount(0),
        m_channelMarker(this),
        m_rfBandwidthChanged(false),
        m_doApplySettings(true)
{
    ui->setupUi(this);
    connect(this, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));
    setAttribute(Qt::WA_DeleteOnClose, true);

    m_spectrumVis = new SpectrumVis(SDR_TX_SCALEF, ui->glSpectrum);
    m_udpSource = (UDPSource*) channelTx;
    m_udpSource->setSpectrumSink(m_spectrumVis);
    m_udpSource->setMessageQueueToGUI(getInputMessageQueue());

    ui->fmDeviation->setEnabled(false);
    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);

    ui->glSpectrum->setCenterFrequency(0);
    ui->glSpectrum->setSampleRate(ui->sampleRate->text().toInt());
    ui->glSpectrum->setDisplayWaterfall(true);
    ui->glSpectrum->setDisplayMaxHold(true);
    m_spectrumVis->configure(m_spectrumVis->getInputMessageQueue(),
            64, // FFT size
            10, // overlapping %
            0,  // number of averaging samples
            0,  // no averaging
            FFTWindow::BlackmanHarris,
            false); // logarithmic scale

    ui->glSpectrum->connectTimer(MainWindow::getInstance()->getMasterTimer());
    connect(&MainWindow::getInstance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    m_channelMarker.blockSignals(true);
    m_channelMarker.setBandwidth(16000);
    m_channelMarker.setCenterFrequency(0);
    m_channelMarker.setColor(m_settings.m_rgbColor);
    m_channelMarker.setTitle("UDP Sample Sink");
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_deviceUISet->registerTxChannelInstance(UDPSource::m_channelIdURI, this);
    m_deviceUISet->addChannelMarker(&m_channelMarker);
    m_deviceUISet->addRollupWidget(this);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));

    ui->spectrumGUI->setBuddies(m_spectrumVis->getInputMessageQueue(), m_spectrumVis, ui->glSpectrum);

    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleSourceMessages()));
    connect(m_udpSource, SIGNAL(levelChanged(qreal, qreal, int)), ui->volumeMeter, SLOT(levelChanged(qreal, qreal, int)));

    displaySettings();
    applySettings(true);
}

UDPSourceGUI::~UDPSourceGUI()
{
    m_deviceUISet->removeTxChannelInstance(this);
    delete m_udpSource; // TODO: check this: when the GUI closes it has to delete the modulator
    delete m_spectrumVis;
    delete ui;
}

void UDPSourceGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void UDPSourceGUI::applySettings(bool force)
{
    if (m_doApplySettings)
    {
        UDPSource::MsgConfigureChannelizer *msgChan = UDPSource::MsgConfigureChannelizer::create(
                m_settings.m_inputSampleRate,
                m_settings.m_inputFrequencyOffset);
        m_udpSource->getInputMessageQueue()->push(msgChan);

        UDPSource::MsgConfigureUDPSource* message = UDPSource::MsgConfigureUDPSource::create( m_settings, force);
        m_udpSource->getInputMessageQueue()->push(message);

        ui->applyBtn->setEnabled(false);
        ui->applyBtn->setStyleSheet("QPushButton { background:rgb(79,79,79); }");
    }
}

void UDPSourceGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor);

    setTitleColor(m_settings.m_rgbColor);
    this->setWindowTitle(m_channelMarker.getTitle());

    blockApplySettings(true);

    ui->deltaFrequency->setValue(m_settings.m_inputFrequencyOffset);
    ui->sampleRate->setText(QString("%1").arg(roundf(m_settings.m_inputSampleRate), 0));
    ui->glSpectrum->setSampleRate(m_settings.m_inputSampleRate);
    ui->rfBandwidth->setText(QString("%1").arg(roundf(m_settings.m_rfBandwidth), 0));
    ui->fmDeviation->setText(QString("%1").arg(m_settings.m_fmDeviation, 0));
    ui->amModPercent->setText(QString("%1").arg(roundf(m_settings.m_amModFactor * 100.0), 0));

    setSampleFormatIndex(m_settings.m_sampleFormat);

    ui->channelMute->setChecked(m_settings.m_channelMute);
    ui->autoRWBalance->setChecked(m_settings.m_autoRWBalance);
    ui->stereoInput->setChecked(m_settings.m_stereoInput);

    ui->gainInText->setText(tr("%1").arg(m_settings.m_gainIn, 0, 'f', 1));
    ui->gainIn->setValue(roundf(m_settings.m_gainIn * 10.0));

    ui->gainOutText->setText(tr("%1").arg(m_settings.m_gainOut, 0, 'f', 1));
    ui->gainOut->setValue(roundf(m_settings.m_gainOut * 10.0));

    if (m_settings.m_squelchEnabled) {
        ui->squelchText->setText(tr("%1").arg(m_settings.m_squelch, 0, 'f', 0));
    } else {
        ui->squelchText->setText("---");
    }

    ui->squelch->setValue(roundf(m_settings.m_squelch));

    ui->squelchGateText->setText(tr("%1").arg(roundf(m_settings.m_squelchGate * 1000.0), 0, 'f', 0));
    ui->squelchGate->setValue(roundf(m_settings.m_squelchGate * 100.0));

    ui->localUDPAddress->setText(m_settings.m_udpAddress);
    ui->localUDPPort->setText(tr("%1").arg(m_settings.m_udpPort));

    ui->applyBtn->setEnabled(false);
    ui->applyBtn->setStyleSheet("QPushButton { background:rgb(79,79,79); }");

    blockApplySettings(false);
}

void UDPSourceGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings();
}

void UDPSourceGUI::on_deltaFrequency_changed(qint64 value)
{
    m_settings.m_inputFrequencyOffset = value;
    m_channelMarker.setCenterFrequency(value);
    applySettings();
}

void UDPSourceGUI::on_sampleFormat_currentIndexChanged(int index)
{
    if (index == (int) UDPSourceSettings::FormatNFM) {
        ui->fmDeviation->setEnabled(true);
    } else {
        ui->fmDeviation->setEnabled(false);
    }

    if (index == (int) UDPSourceSettings::FormatAM) {
        ui->amModPercent->setEnabled(true);
    } else {
        ui->amModPercent->setEnabled(false);
    }

    setSampleFormat(index);

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_localUDPAddress_editingFinished()
{
    m_settings.m_udpAddress = ui->localUDPAddress->text();
    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_localUDPPort_editingFinished()
{
    bool ok;
    quint16 udpPort = ui->localUDPPort->text().toInt(&ok);

    if((!ok) || (udpPort < 1024)) {
        udpPort = 9998;
    }

    m_settings.m_udpPort = udpPort;
    ui->localUDPPort->setText(tr("%1").arg(m_settings.m_udpPort));

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_sampleRate_textEdited(const QString& arg1 __attribute__((unused)))
{
    bool ok;
    Real inputSampleRate = ui->sampleRate->text().toDouble(&ok);

    if ((!ok) || (inputSampleRate < 1000)) {
        m_settings.m_inputSampleRate = 48000;
        ui->sampleRate->setText(QString("%1").arg(m_settings.m_inputSampleRate, 0));
    } else {
        m_settings.m_inputSampleRate = inputSampleRate;
    }

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_rfBandwidth_textEdited(const QString& arg1 __attribute__((unused)))
{
    bool ok;
    Real rfBandwidth = ui->rfBandwidth->text().toDouble(&ok);

    if ((!ok) || (rfBandwidth > m_settings.m_inputSampleRate))
    {
        m_settings.m_rfBandwidth = m_settings.m_inputSampleRate;
        ui->rfBandwidth->setText(QString("%1").arg(m_settings.m_rfBandwidth, 0));
    }
    else
    {
        m_settings.m_rfBandwidth = rfBandwidth;
    }

    m_rfBandwidthChanged = true;

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_fmDeviation_textEdited(const QString& arg1 __attribute__((unused)))
{
    bool ok;
    int fmDeviation = ui->fmDeviation->text().toInt(&ok);

    if ((!ok) || (fmDeviation < 1)) {
        m_settings.m_fmDeviation = 2500;
        ui->fmDeviation->setText(QString("%1").arg(m_settings.m_fmDeviation));
    } else {
        m_settings.m_fmDeviation = fmDeviation;
    }

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_amModPercent_textEdited(const QString& arg1 __attribute__((unused)))
{
    bool ok;
    int amModPercent = ui->amModPercent->text().toInt(&ok);

    if ((!ok) || (amModPercent < 1) || (amModPercent > 100))
    {
        m_settings.m_amModFactor = 0.95;
        ui->amModPercent->setText(QString("%1").arg(95));
    } else {
        m_settings.m_amModFactor = amModPercent / 100.0;
    }

    ui->applyBtn->setEnabled(true);
    ui->applyBtn->setStyleSheet("QPushButton { background-color : green; }");
}

void UDPSourceGUI::on_gainIn_valueChanged(int value)
{
    m_settings.m_gainIn = value / 10.0;
    ui->gainInText->setText(tr("%1").arg(m_settings.m_gainIn, 0, 'f', 1));
    applySettings();
}

void UDPSourceGUI::on_gainOut_valueChanged(int value)
{
    m_settings.m_gainOut = value / 10.0;
    ui->gainOutText->setText(tr("%1").arg(m_settings.m_gainOut, 0, 'f', 1));
    applySettings();
}

void UDPSourceGUI::on_squelch_valueChanged(int value)
{
    m_settings.m_squelchEnabled = (value != -100);
    m_settings.m_squelch = value * 1.0;

    if (m_settings.m_squelchEnabled) {
        ui->squelchText->setText(tr("%1").arg(m_settings.m_squelch, 0, 'f', 0));
    } else {
        ui->squelchText->setText("---");
    }

    applySettings();
}

void UDPSourceGUI::on_squelchGate_valueChanged(int value)
{
    m_settings.m_squelchGate = value / 100.0;
    ui->squelchGateText->setText(tr("%1").arg(roundf(value * 10.0), 0, 'f', 0));
    applySettings();
}

void UDPSourceGUI::on_channelMute_toggled(bool checked)
{
    m_settings.m_channelMute = checked;
    applySettings();
}

void UDPSourceGUI::on_applyBtn_clicked()
{
    if (m_rfBandwidthChanged)
    {
        m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
        m_rfBandwidthChanged = false;
    }

    ui->glSpectrum->setSampleRate(m_settings.m_inputSampleRate);

    applySettings();
}

void UDPSourceGUI::on_resetUDPReadIndex_clicked()
{
    m_udpSource->resetReadIndex();
}

void UDPSourceGUI::on_autoRWBalance_toggled(bool checked)
{
    m_settings.m_autoRWBalance = checked;
    applySettings();
}

void UDPSourceGUI::on_stereoInput_toggled(bool checked)
{
    m_settings.m_stereoInput = checked;
    applySettings();
}

void UDPSourceGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    if ((widget == ui->spectrumBox) && (m_udpSource != 0))
    {
        m_udpSource->setSpectrum(rollDown);
    }
}

void UDPSourceGUI::onMenuDialogCalled(const QPoint &p)
{
    BasicChannelSettingsDialog dialog(&m_channelMarker, this);
    dialog.move(p);
    dialog.exec();

    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    m_settings.m_rgbColor = m_channelMarker.getColor().rgb();

    setWindowTitle(m_channelMarker.getTitle());
    setTitleColor(m_settings.m_rgbColor);

    applySettings();
}

void UDPSourceGUI::leaveEvent(QEvent*)
{
    m_channelMarker.setHighlighted(false);
}

void UDPSourceGUI::enterEvent(QEvent*)
{
    m_channelMarker.setHighlighted(true);
}

void UDPSourceGUI::tick()
{
    m_channelPowerAvg(m_udpSource->getMagSq());
    m_inPowerAvg(m_udpSource->getInMagSq());

    if (m_tickCount % 4 == 0)
    {
        double powDb = CalcDb::dbPower(m_channelPowerAvg.asDouble());
        ui->channelPower->setText(tr("%1 dB").arg(powDb, 0, 'f', 1));
        double inPowDb = CalcDb::dbPower(m_inPowerAvg.asDouble());
        ui->inputPower->setText(tr("%1").arg(inPowDb, 0, 'f', 1));
    }

    int32_t bufferGauge = m_udpSource->getBufferGauge();
    ui->bufferGaugeNegative->setValue((bufferGauge < 0 ? -bufferGauge : 0));
    ui->bufferGaugePositive->setValue((bufferGauge < 0 ? 0 : bufferGauge));
    QString s = QString::number(bufferGauge, 'f', 0);
    ui->bufferRWBalanceText->setText(tr("%1").arg(s));

    if (m_udpSource->getSquelchOpen()) {
        ui->channelMute->setStyleSheet("QToolButton { background-color : green; }");
    } else {
        ui->channelMute->setStyleSheet("QToolButton { background:rgb(79,79,79); }");
    }

    m_tickCount++;
}

void UDPSourceGUI::setSampleFormatIndex(const UDPSourceSettings::SampleFormat& sampleFormat)
{
    switch(sampleFormat)
    {
        case UDPSourceSettings::FormatSnLE:
            ui->sampleFormat->setCurrentIndex(0);
            ui->fmDeviation->setEnabled(false);
            ui->stereoInput->setChecked(true);
            ui->stereoInput->setEnabled(false);
            break;
        case UDPSourceSettings::FormatNFM:
            ui->sampleFormat->setCurrentIndex(1);
            ui->fmDeviation->setEnabled(true);
            ui->stereoInput->setEnabled(true);
            break;
        case UDPSourceSettings::FormatLSB:
            ui->sampleFormat->setCurrentIndex(2);
            ui->fmDeviation->setEnabled(false);
            ui->stereoInput->setEnabled(true);
            break;
        case UDPSourceSettings::FormatUSB:
            ui->sampleFormat->setCurrentIndex(3);
            ui->fmDeviation->setEnabled(false);
            ui->stereoInput->setEnabled(true);
            break;
        case UDPSourceSettings::FormatAM:
            ui->sampleFormat->setCurrentIndex(4);
            ui->fmDeviation->setEnabled(false);
            ui->stereoInput->setEnabled(true);
            break;
        default:
            ui->sampleFormat->setCurrentIndex(0);
            ui->fmDeviation->setEnabled(false);
            ui->stereoInput->setChecked(true);
            ui->stereoInput->setEnabled(false);
            break;
    }
}

void UDPSourceGUI::setSampleFormat(int index)
{
    switch(index)
    {
    case 0:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatSnLE;
        ui->fmDeviation->setEnabled(false);
        ui->stereoInput->setChecked(true);
        ui->stereoInput->setEnabled(false);
        break;
    case 1:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatNFM;
        ui->fmDeviation->setEnabled(true);
        ui->stereoInput->setEnabled(true);
        break;
    case 2:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatLSB;
        ui->fmDeviation->setEnabled(false);
        ui->stereoInput->setEnabled(true);
        break;
    case 3:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatUSB;
        ui->fmDeviation->setEnabled(false);
        ui->stereoInput->setEnabled(true);
        break;
    case 4:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatAM;
        ui->fmDeviation->setEnabled(false);
        ui->stereoInput->setEnabled(true);
        break;
    default:
        m_settings.m_sampleFormat = UDPSourceSettings::FormatSnLE;
        ui->fmDeviation->setEnabled(false);
        ui->stereoInput->setChecked(true);
        ui->stereoInput->setEnabled(false);
        break;
    }
}

