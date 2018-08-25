///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
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

#include <QDebug>
#include <string.h>
#include <errno.h>

#include "SWGDeviceSettings.h"
#include "SWGRtlSdrSettings.h"
#include "SWGDeviceState.h"
#include "SWGDeviceReport.h"
#include "SWGRtlSdrReport.h"

#include "rtlsdrinput.h"
#include "device/devicesourceapi.h"
#include "rtlsdrthread.h"
#include "dsp/dspcommands.h"
#include "dsp/dspengine.h"
#include "dsp/filerecord.h"

MESSAGE_CLASS_DEFINITION(RTLSDRInput::MsgConfigureRTLSDR, Message)
MESSAGE_CLASS_DEFINITION(RTLSDRInput::MsgFileRecord, Message)
MESSAGE_CLASS_DEFINITION(RTLSDRInput::MsgStartStop, Message)

const quint64 RTLSDRInput::frequencyLowRangeMin = 0UL;
const quint64 RTLSDRInput::frequencyLowRangeMax = 275000UL;
const quint64 RTLSDRInput::frequencyHighRangeMin = 24000UL;
const quint64 RTLSDRInput::frequencyHighRangeMax = 1900000UL;
const int RTLSDRInput::sampleRateLowRangeMin = 230000U;
const int RTLSDRInput::sampleRateLowRangeMax = 300000U;
const int RTLSDRInput::sampleRateHighRangeMin = 950000U;
const int RTLSDRInput::sampleRateHighRangeMax = 2400000U;

RTLSDRInput::RTLSDRInput(DeviceSourceAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
	m_settings(),
	m_dev(0),
	m_rtlSDRThread(0),
	m_deviceDescription(),
	m_running(false)
{
    openDevice();

    m_fileSink = new FileRecord(QString("test_%1.sdriq").arg(m_deviceAPI->getDeviceUID()));
    m_deviceAPI->addSink(m_fileSink);
}

RTLSDRInput::~RTLSDRInput()
{
    if (m_running) stop();
    m_deviceAPI->removeSink(m_fileSink);
    delete m_fileSink;
    closeDevice();
}

void RTLSDRInput::destroy()
{
    delete this;
}

bool RTLSDRInput::openDevice()
{
    if (m_dev != 0)
    {
        closeDevice();
    }

    char vendor[256];
    char product[256];
    char serial[256];
    int res;
    int numberOfGains;

    if (!m_sampleFifo.setSize(96000 * 4))
    {
        qCritical("RTLSDRInput::openDevice: Could not allocate SampleFifo");
        return false;
    }

    int device;

    if ((device = rtlsdr_get_index_by_serial(qPrintable(m_deviceAPI->getSampleSourceSerial()))) < 0)
    {
        qCritical("RTLSDRInput::openDevice: could not get RTLSDR serial number");
        return false;
    }

    if ((res = rtlsdr_open(&m_dev, device)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: could not open RTLSDR #%d: %s", device, strerror(errno));
        return false;
    }

    vendor[0] = '\0';
    product[0] = '\0';
    serial[0] = '\0';

    if ((res = rtlsdr_get_usb_strings(m_dev, vendor, product, serial)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: error accessing USB device");
        stop();
        return false;
    }

    qInfo("RTLSDRInput::openDevice: open: %s %s, SN: %s", vendor, product, serial);
    m_deviceDescription = QString("%1 (SN %2)").arg(product).arg(serial);

    if ((res = rtlsdr_set_sample_rate(m_dev, 1152000)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: could not set sample rate: 1024k S/s");
        stop();
        return false;
    }

    if ((res = rtlsdr_set_tuner_gain_mode(m_dev, 1)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: error setting tuner gain mode");
        stop();
        return false;
    }

    if ((res = rtlsdr_set_agc_mode(m_dev, 0)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: error setting agc mode");
        stop();
        return false;
    }

    numberOfGains = rtlsdr_get_tuner_gains(m_dev, NULL);

    if (numberOfGains < 0)
    {
        qCritical("RTLSDRInput::openDevice: error getting number of gain values supported");
        stop();
        return false;
    }

    m_gains.resize(numberOfGains);

    if (rtlsdr_get_tuner_gains(m_dev, &m_gains[0]) < 0)
    {
        qCritical("RTLSDRInput::openDevice: error getting gain values");
        stop();
        return false;
    }
    else
    {
        qDebug() << "RTLSDRInput::openDevice: " << m_gains.size() << "gains";
    }

    if ((res = rtlsdr_reset_buffer(m_dev)) < 0)
    {
        qCritical("RTLSDRInput::openDevice: could not reset USB EP buffers: %s", strerror(errno));
        stop();
        return false;
    }

    return true;
}

void RTLSDRInput::init()
{
    applySettings(m_settings, true);
}

bool RTLSDRInput::start()
{
	QMutexLocker mutexLocker(&m_mutex);

	if (!m_dev) {
	    return false;
	}

    if (m_running) stop();

	m_rtlSDRThread = new RTLSDRThread(m_dev, &m_sampleFifo);
	m_rtlSDRThread->setSamplerate(m_settings.m_devSampleRate);
	m_rtlSDRThread->setLog2Decimation(m_settings.m_log2Decim);
	m_rtlSDRThread->setFcPos((int) m_settings.m_fcPos);

	m_rtlSDRThread->startWork();

	mutexLocker.unlock();

	applySettings(m_settings, true);
	m_running = true;

	return true;
}

void RTLSDRInput::closeDevice()
{
    if (m_dev != 0)
    {
        rtlsdr_close(m_dev);
        m_dev = 0;
    }

    m_deviceDescription.clear();
}

void RTLSDRInput::stop()
{
	QMutexLocker mutexLocker(&m_mutex);

	if (m_rtlSDRThread != 0)
	{
		m_rtlSDRThread->stopWork();
		delete m_rtlSDRThread;
		m_rtlSDRThread = 0;
	}

	m_running = false;
}

QByteArray RTLSDRInput::serialize() const
{
    return m_settings.serialize();
}

bool RTLSDRInput::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureRTLSDR* message = MsgConfigureRTLSDR::create(m_settings, true);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureRTLSDR* messageToGUI = MsgConfigureRTLSDR::create(m_settings, true);
        m_guiMessageQueue->push(messageToGUI);
    }

    return success;
}

const QString& RTLSDRInput::getDeviceDescription() const
{
	return m_deviceDescription;
}

int RTLSDRInput::getSampleRate() const
{
	int rate = m_settings.m_devSampleRate;
	return (rate / (1<<m_settings.m_log2Decim));
}

quint64 RTLSDRInput::getCenterFrequency() const
{
	return m_settings.m_centerFrequency;
}

void RTLSDRInput::setCenterFrequency(qint64 centerFrequency)
{
    RTLSDRSettings settings = m_settings;
    settings.m_centerFrequency = centerFrequency;

    MsgConfigureRTLSDR* message = MsgConfigureRTLSDR::create(settings, false);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureRTLSDR* messageToGUI = MsgConfigureRTLSDR::create(settings, false);
        m_guiMessageQueue->push(messageToGUI);
    }
}

bool RTLSDRInput::handleMessage(const Message& message)
{
    if (MsgConfigureRTLSDR::match(message))
    {
        MsgConfigureRTLSDR& conf = (MsgConfigureRTLSDR&) message;
        qDebug() << "RTLSDRInput::handleMessage: MsgConfigureRTLSDR";

        bool success = applySettings(conf.getSettings(), conf.getForce());

        if (!success)
        {
            qDebug("RTLSDRInput::handleMessage: config error");
        }

        return true;
    }
    else if (MsgFileRecord::match(message))
    {
        MsgFileRecord& conf = (MsgFileRecord&) message;
        qDebug() << "RTLSDRInput::handleMessage: MsgFileRecord: " << conf.getStartStop();

        if (conf.getStartStop())
        {
            if (m_settings.m_fileRecordName.size() != 0) {
                m_fileSink->setFileName(m_settings.m_fileRecordName);
            } else {
                m_fileSink->genUniqueFileName(m_deviceAPI->getDeviceUID());
            }

            m_fileSink->startRecording();
        }
        else
        {
            m_fileSink->stopRecording();
        }

        return true;
    }
    else if (MsgStartStop::match(message))
    {
        MsgStartStop& cmd = (MsgStartStop&) message;
        qDebug() << "RTLSDRInput::handleMessage: MsgStartStop: " << (cmd.getStartStop() ? "start" : "stop");

        if (cmd.getStartStop())
        {
            if (m_deviceAPI->initAcquisition())
            {
                m_deviceAPI->startAcquisition();
            }
        }
        else
        {
            m_deviceAPI->stopAcquisition();
        }

        return true;
    }
    else
    {
        return false;
    }
}

bool RTLSDRInput::applySettings(const RTLSDRSettings& settings, bool force)
{
    bool forwardChange = false;

    if ((m_settings.m_agc != settings.m_agc) || force)
    {
        if (rtlsdr_set_agc_mode(m_dev, settings.m_agc ? 1 : 0) < 0)
        {
            qCritical("RTLSDRInput::applySettings: could not set AGC mode %s", settings.m_agc ? "on" : "off");
        }
        else
        {
            qDebug("RTLSDRInput::applySettings: AGC mode %s", settings.m_agc ? "on" : "off");
            m_settings.m_agc = settings.m_agc;
        }
    }

    if ((m_settings.m_gain != settings.m_gain) || force)
    {
        m_settings.m_gain = settings.m_gain;

        if(m_dev != 0)
        {
            if (rtlsdr_set_tuner_gain(m_dev, m_settings.m_gain) != 0)
            {
                qCritical("RTLSDRInput::applySettings: rtlsdr_set_tuner_gain() failed");
            }
            else
            {
                qDebug("RTLSDRInput::applySettings: rtlsdr_set_tuner_gain() to %d", m_settings.m_gain);
            }
        }
    }

    if ((m_settings.m_dcBlock != settings.m_dcBlock) || (m_settings.m_iqImbalance != settings.m_iqImbalance) || force)
    {
        m_settings.m_dcBlock = settings.m_dcBlock;
        m_settings.m_iqImbalance = settings.m_iqImbalance;
        m_deviceAPI->configureCorrections(m_settings.m_dcBlock, m_settings.m_iqImbalance);
        qDebug("RTLSDRInput::applySettings: corrections: DC block: %s IQ imbalance: %s",
                m_settings.m_dcBlock ? "true" : "false",
                m_settings.m_iqImbalance ? "true" : "false");
    }

    if ((m_settings.m_loPpmCorrection != settings.m_loPpmCorrection) || force)
    {
        if (m_dev != 0)
        {
            if (rtlsdr_set_freq_correction(m_dev, settings.m_loPpmCorrection) < 0)
            {
                qCritical("RTLSDRInput::applySettings: could not set LO ppm correction: %d", settings.m_loPpmCorrection);
            }
            else
            {
                m_settings.m_loPpmCorrection = settings.m_loPpmCorrection;
                qDebug("RTLSDRInput::applySettings: LO ppm correction set to: %d", settings.m_loPpmCorrection);
            }
        }
    }

    if ((m_settings.m_devSampleRate != settings.m_devSampleRate) || force)
    {
        m_settings.m_devSampleRate = settings.m_devSampleRate;
        forwardChange = true;

        if(m_dev != 0)
        {
            if( rtlsdr_set_sample_rate(m_dev, settings.m_devSampleRate) < 0)
            {
                qCritical("RTLSDRInput::applySettings: could not set sample rate: %d", settings.m_devSampleRate);
            }
            else
            {
                if (m_rtlSDRThread) m_rtlSDRThread->setSamplerate(settings.m_devSampleRate);
                qDebug("RTLSDRInput::applySettings: sample rate set to %d", m_settings.m_devSampleRate);
            }
        }
    }

    if ((m_settings.m_log2Decim != settings.m_log2Decim) || force)
    {
        forwardChange = true;

        if (m_rtlSDRThread != 0)
        {
            m_rtlSDRThread->setLog2Decimation(settings.m_log2Decim);
        }

        qDebug("RTLSDRInput::applySettings: log2decim set to %d", m_settings.m_log2Decim);
    }

    if ((m_settings.m_centerFrequency != settings.m_centerFrequency)
        || (m_settings.m_fcPos != settings.m_fcPos)
        || (m_settings.m_log2Decim != settings.m_log2Decim)
        || (m_settings.m_devSampleRate != settings.m_devSampleRate)
        || (m_settings.m_transverterMode != settings.m_transverterMode)
        || (m_settings.m_transverterDeltaFrequency != settings.m_transverterDeltaFrequency) || force)
    {
        qint64 deviceCenterFrequency = DeviceSampleSource::calculateDeviceCenterFrequency(
                settings.m_centerFrequency,
                settings.m_transverterDeltaFrequency,
                settings.m_log2Decim,
                (DeviceSampleSource::fcPos_t) settings.m_fcPos,
                settings.m_devSampleRate,
                settings.m_transverterMode);

        m_settings.m_centerFrequency = settings.m_centerFrequency;
        m_settings.m_log2Decim = settings.m_log2Decim;
        m_settings.m_devSampleRate = settings.m_devSampleRate;
        m_settings.m_transverterMode = settings.m_transverterMode;
        m_settings.m_transverterDeltaFrequency = settings.m_transverterDeltaFrequency;

        forwardChange = true;

        if ((m_settings.m_fcPos != settings.m_fcPos) || force)
        {
            m_settings.m_fcPos = settings.m_fcPos;

            if (m_rtlSDRThread != 0) {
                m_rtlSDRThread->setFcPos((int) m_settings.m_fcPos);
            }

            qDebug() << "RTLSDRInput::applySettings: set fc pos (enum) to " << (int) m_settings.m_fcPos;
        }

        if (m_dev != 0)
        {
            if (rtlsdr_set_center_freq( m_dev, deviceCenterFrequency ) != 0) {
                qWarning("RTLSDRInput::applySettings: rtlsdr_set_center_freq(%lld) failed", deviceCenterFrequency);
            } else {
                qDebug("RTLSDRInput::applySettings: rtlsdr_set_center_freq(%lld)", deviceCenterFrequency);
            }
        }
    }

    if ((m_settings.m_noModMode != settings.m_noModMode) || force)
    {
        m_settings.m_noModMode = settings.m_noModMode;
        qDebug() << "RTLSDRInput::applySettings: set noModMode to " << m_settings.m_noModMode;

        // Direct Modes: 0: off, 1: I, 2: Q, 3: NoMod.
        if (m_settings.m_noModMode) {
            set_ds_mode(3);
        } else {
            set_ds_mode(0);
        }
    }

    if ((m_settings.m_lowSampleRate != settings.m_lowSampleRate) || force)
    {
        m_settings.m_lowSampleRate = settings.m_lowSampleRate;
    }

    if ((m_settings.m_rfBandwidth != settings.m_rfBandwidth) || force)
    {
        m_settings.m_rfBandwidth = settings.m_rfBandwidth;

        if (m_dev != 0)
        {
            if (rtlsdr_set_tuner_bandwidth( m_dev, m_settings.m_rfBandwidth) != 0)
            {
                qCritical("RTLSDRInput::applySettings: could not set RF bandwidth to %u", m_settings.m_rfBandwidth);
            }
            else
            {
                qDebug() << "RTLSDRInput::applySettings: set RF bandwidth to " << m_settings.m_rfBandwidth;
            }
        }
    }

    if (forwardChange)
    {
        int sampleRate = m_settings.m_devSampleRate/(1<<m_settings.m_log2Decim);
        DSPSignalNotification *notif = new DSPSignalNotification(sampleRate, m_settings.m_centerFrequency);
        m_fileSink->handleMessage(*notif); // forward to file sink
        m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
    }

    return true;
}

void RTLSDRInput::set_ds_mode(int on)
{
	rtlsdr_set_direct_sampling(m_dev, on);
}

int RTLSDRInput::webapiSettingsGet(
                SWGSDRangel::SWGDeviceSettings& response,
                QString& errorMessage __attribute__((unused)))
{
    response.setRtlSdrSettings(new SWGSDRangel::SWGRtlSdrSettings());
    response.getRtlSdrSettings()->init();
    webapiFormatDeviceSettings(response, m_settings);
    return 200;
}

int RTLSDRInput::webapiSettingsPutPatch(
                bool force,
                const QStringList& deviceSettingsKeys,
                SWGSDRangel::SWGDeviceSettings& response, // query + response
                QString& errorMessage __attribute__((unused)))
{
    RTLSDRSettings settings = m_settings;

    if (deviceSettingsKeys.contains("agc")) {
        settings.m_agc = response.getRtlSdrSettings()->getAgc() != 0;
    }
    if (deviceSettingsKeys.contains("centerFrequency")) {
        settings.m_centerFrequency = response.getRtlSdrSettings()->getCenterFrequency();
    }
    if (deviceSettingsKeys.contains("dcBlock")) {
        settings.m_dcBlock = response.getRtlSdrSettings()->getDcBlock() != 0;
    }
    if (deviceSettingsKeys.contains("devSampleRate")) {
        settings.m_devSampleRate = response.getRtlSdrSettings()->getDevSampleRate();
    }
    if (deviceSettingsKeys.contains("fcPos")) {
        settings.m_fcPos = (RTLSDRSettings::fcPos_t) response.getRtlSdrSettings()->getFcPos();
    }
    if (deviceSettingsKeys.contains("gain")) {
        settings.m_gain = response.getRtlSdrSettings()->getGain();
    }
    if (deviceSettingsKeys.contains("iqImbalance")) {
        settings.m_iqImbalance = response.getRtlSdrSettings()->getIqImbalance() != 0;
    }
    if (deviceSettingsKeys.contains("loPpmCorrection")) {
        settings.m_loPpmCorrection = response.getRtlSdrSettings()->getLoPpmCorrection();
    }
    if (deviceSettingsKeys.contains("log2Decim")) {
        settings.m_log2Decim = response.getRtlSdrSettings()->getLog2Decim();
    }
    if (deviceSettingsKeys.contains("lowSampleRate")) {
        settings.m_lowSampleRate = response.getRtlSdrSettings()->getLowSampleRate() != 0;
    }
    if (deviceSettingsKeys.contains("noModMode")) {
        settings.m_noModMode = response.getRtlSdrSettings()->getNoModMode() != 0;
    }
    if (deviceSettingsKeys.contains("transverterDeltaFrequency")) {
        settings.m_transverterDeltaFrequency = response.getRtlSdrSettings()->getTransverterDeltaFrequency();
    }
    if (deviceSettingsKeys.contains("transverterMode")) {
        settings.m_transverterMode = response.getRtlSdrSettings()->getTransverterMode() != 0;
    }
    if (deviceSettingsKeys.contains("rfBandwidth")) {
        settings.m_rfBandwidth = response.getRtlSdrSettings()->getRfBandwidth();
    }
    if (deviceSettingsKeys.contains("fileRecordName")) {
        settings.m_fileRecordName = *response.getRtlSdrSettings()->getFileRecordName();
    }

    MsgConfigureRTLSDR *msg = MsgConfigureRTLSDR::create(settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRTLSDR *msgToGUI = MsgConfigureRTLSDR::create(settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatDeviceSettings(response, settings);
    return 200;
}

void RTLSDRInput::webapiFormatDeviceSettings(SWGSDRangel::SWGDeviceSettings& response, const RTLSDRSettings& settings)
{
    qDebug("RTLSDRInput::webapiFormatDeviceSettings: m_lowSampleRate: %s", settings.m_lowSampleRate ? "true" : "false");
    response.getRtlSdrSettings()->setAgc(settings.m_agc ? 1 : 0);
    response.getRtlSdrSettings()->setCenterFrequency(settings.m_centerFrequency);
    response.getRtlSdrSettings()->setDcBlock(settings.m_dcBlock ? 1 : 0);
    response.getRtlSdrSettings()->setDevSampleRate(settings.m_devSampleRate);
    response.getRtlSdrSettings()->setFcPos((int) settings.m_fcPos);
    response.getRtlSdrSettings()->setGain(settings.m_gain);
    response.getRtlSdrSettings()->setIqImbalance(settings.m_iqImbalance ? 1 : 0);
    response.getRtlSdrSettings()->setLoPpmCorrection(settings.m_loPpmCorrection);
    response.getRtlSdrSettings()->setLog2Decim(settings.m_log2Decim);
    response.getRtlSdrSettings()->setLowSampleRate(settings.m_lowSampleRate ? 1 : 0);
    response.getRtlSdrSettings()->setNoModMode(settings.m_noModMode ? 1 : 0);
    response.getRtlSdrSettings()->setTransverterDeltaFrequency(settings.m_transverterDeltaFrequency);
    response.getRtlSdrSettings()->setTransverterMode(settings.m_transverterMode ? 1 : 0);
    response.getRtlSdrSettings()->setRfBandwidth(settings.m_rfBandwidth);

    if (response.getRtlSdrSettings()->getFileRecordName()) {
        *response.getRtlSdrSettings()->getFileRecordName() = settings.m_fileRecordName;
    } else {
        response.getRtlSdrSettings()->setFileRecordName(new QString(settings.m_fileRecordName));
    }
}

int RTLSDRInput::webapiRunGet(
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage __attribute__((unused)))
{
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    return 200;
}

int RTLSDRInput::webapiRun(
        bool run,
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage __attribute__((unused)))
{
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    MsgStartStop *message = MsgStartStop::create(run);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgStartStop *msgToGUI = MsgStartStop::create(run);
        m_guiMessageQueue->push(msgToGUI);
    }

    return 200;
}

int RTLSDRInput::webapiReportGet(
        SWGSDRangel::SWGDeviceReport& response,
        QString& errorMessage __attribute__((unused)))
{
    response.setRtlSdrReport(new SWGSDRangel::SWGRtlSdrReport());
    response.getRtlSdrReport()->init();
    webapiFormatDeviceReport(response);
    return 200;
}

void RTLSDRInput::webapiFormatDeviceReport(SWGSDRangel::SWGDeviceReport& response)
{
    response.getRtlSdrReport()->setGains(new QList<SWGSDRangel::SWGGain*>);

    for (std::vector<int>::const_iterator it = getGains().begin(); it != getGains().end(); ++it)
    {
        response.getRtlSdrReport()->getGains()->append(new SWGSDRangel::SWGGain);
        response.getRtlSdrReport()->getGains()->back()->setGainCb(*it);
    }
}


