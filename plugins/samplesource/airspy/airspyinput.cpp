///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015 Edouard Griffiths, F4EXB                                   //
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

#include <string.h>
#include <errno.h>
#include <QDebug>
#include <QList>

#include "SWGDeviceSettings.h"
#include "SWGDeviceState.h"
#include "SWGDeviceReport.h"
#include "SWGAirspyReport.h"

#include "airspyinput.h"
#include "airspyplugin.h"

#include <device/devicesourceapi.h>
#include <dsp/filerecord.h>
#include "dsp/dspcommands.h"
#include "dsp/dspengine.h"
#include "airspysettings.h"
#include "airspythread.h"

MESSAGE_CLASS_DEFINITION(AirspyInput::MsgConfigureAirspy, Message)
MESSAGE_CLASS_DEFINITION(AirspyInput::MsgStartStop, Message)
MESSAGE_CLASS_DEFINITION(AirspyInput::MsgFileRecord, Message)

const qint64 AirspyInput::loLowLimitFreq = 24000000L;
const qint64 AirspyInput::loHighLimitFreq = 1900000000L;

AirspyInput::AirspyInput(DeviceSourceAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
	m_settings(),
	m_dev(0),
	m_airspyThread(0),
	m_deviceDescription("Airspy"),
	m_running(false)
{
    openDevice();
    m_fileSink = new FileRecord(QString("test_%1.sdriq").arg(m_deviceAPI->getDeviceUID()));
    m_deviceAPI->addSink(m_fileSink);
}

AirspyInput::~AirspyInput()
{
    if (m_running) stop();
    m_deviceAPI->removeSink(m_fileSink);
    delete m_fileSink;
    closeDevice();
}

void AirspyInput::destroy()
{
    delete this;
}

bool AirspyInput::openDevice()
{
    if (m_dev != 0)
    {
        closeDevice();
    }

    airspy_error rc;

    rc = (airspy_error) airspy_init();

    if (rc != AIRSPY_SUCCESS)
    {
        qCritical("AirspyInput::start: failed to initiate Airspy library %s", airspy_error_name(rc));
    }

    if (!m_sampleFifo.setSize(1<<19))
    {
        qCritical("AirspyInput::start: could not allocate SampleFifo");
        return false;
    }

    int device = m_deviceAPI->getSampleSourceSequence();

    if ((m_dev = open_airspy_from_sequence(device)) == 0)
    {
        qCritical("AirspyInput::start: could not open Airspy #%d", device);
        return false;
    }

#ifdef LIBAIRSPY_DYN_RATES
    uint32_t nbSampleRates;
    uint32_t *sampleRates;

    airspy_get_samplerates(m_dev, &nbSampleRates, 0);

    sampleRates = new uint32_t[nbSampleRates];

    airspy_get_samplerates(m_dev, sampleRates, nbSampleRates);

    if (nbSampleRates == 0)
    {
        qCritical("AirspyInput::start: could not obtain Airspy sample rates");
        return false;
    }
    else
    {
        qDebug("AirspyInput::start: %d sample rates", nbSampleRates);
    }

    m_sampleRates.clear();

    for (unsigned int i=0; i<nbSampleRates; i++)
    {
        m_sampleRates.push_back(sampleRates[i]);
        qDebug("AirspyInput::start: sampleRates[%d] = %u Hz", i, sampleRates[i]);
    }

    delete[] sampleRates;
#else
    qDebug("AirspyInput::start: detault rates");
    m_sampleRates.clear();
    m_sampleRates.push_back(10000000);
    m_sampleRates.push_back(2500000);
#endif

//    MsgReportAirspy *message = MsgReportAirspy::create(m_sampleRates);
//    getOutputMessageQueueToGUI()->push(message);

    rc = (airspy_error) airspy_set_sample_type(m_dev, AIRSPY_SAMPLE_INT16_IQ);

    if (rc != AIRSPY_SUCCESS)
    {
        qCritical("AirspyInput::start: could not set sample type to INT16_IQ");
        return false;
    }

    return true;
}

void AirspyInput::init()
{
    applySettings(m_settings, true);
}

bool AirspyInput::start()
{
	QMutexLocker mutexLocker(&m_mutex);

    if (!m_dev) {
        return false;
    }

    if (m_running) { stop(); }

	m_airspyThread = new AirspyThread(m_dev, &m_sampleFifo);
	m_airspyThread->setSamplerate(m_sampleRates[m_settings.m_devSampleRateIndex]);
	m_airspyThread->setLog2Decimation(m_settings.m_log2Decim);
	m_airspyThread->setFcPos((int) m_settings.m_fcPos);

	m_airspyThread->startWork();

	mutexLocker.unlock();

	applySettings(m_settings, true);

	qDebug("AirspyInput::startInput: started");
	m_running = true;

	return true;
}

void AirspyInput::closeDevice()
{
    if (m_dev != 0)
    {
        airspy_stop_rx(m_dev);
        airspy_close(m_dev);
        m_dev = 0;
    }

    m_deviceDescription.clear();
    airspy_exit();
}

void AirspyInput::stop()
{
	qDebug("AirspyInput::stop");
	QMutexLocker mutexLocker(&m_mutex);

	if (m_airspyThread != 0)
	{
		m_airspyThread->stopWork();
		delete m_airspyThread;
		m_airspyThread = 0;
	}

	m_running = false;
}

QByteArray AirspyInput::serialize() const
{
    return m_settings.serialize();
}

bool AirspyInput::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureAirspy* message = MsgConfigureAirspy::create(m_settings, true);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureAirspy* messageToGUI = MsgConfigureAirspy::create(m_settings, true);
        m_guiMessageQueue->push(messageToGUI);
    }

    return success;
}

const QString& AirspyInput::getDeviceDescription() const
{
	return m_deviceDescription;
}

int AirspyInput::getSampleRate() const
{
	int rate = m_sampleRates[m_settings.m_devSampleRateIndex];
	return (rate / (1<<m_settings.m_log2Decim));
}

quint64 AirspyInput::getCenterFrequency() const
{
	return m_settings.m_centerFrequency;
}

void AirspyInput::setCenterFrequency(qint64 centerFrequency)
{
    AirspySettings settings = m_settings;
    settings.m_centerFrequency = centerFrequency;

    MsgConfigureAirspy* message = MsgConfigureAirspy::create(settings, false);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureAirspy* messageToGUI = MsgConfigureAirspy::create(settings, false);
        m_guiMessageQueue->push(messageToGUI);
    }
}

bool AirspyInput::handleMessage(const Message& message)
{
	if (MsgConfigureAirspy::match(message))
	{
		MsgConfigureAirspy& conf = (MsgConfigureAirspy&) message;
		qDebug() << "AirspyInput::handleMessage: MsgConfigureAirspy";

		bool success = applySettings(conf.getSettings(), conf.getForce());

		if (!success)
		{
			qDebug("AirspyInput::handleMessage: Airspy config error");
		}

		return true;
	}
    else if (MsgStartStop::match(message))
    {
        MsgStartStop& cmd = (MsgStartStop&) message;
        qDebug() << "AirspyInput::handleMessage: MsgStartStop: " << (cmd.getStartStop() ? "start" : "stop");

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
    else if (MsgFileRecord::match(message))
    {
        MsgFileRecord& conf = (MsgFileRecord&) message;
        qDebug() << "AirspyInput::handleMessage: MsgFileRecord: " << conf.getStartStop();

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
	else
	{
		return false;
	}
}

void AirspyInput::setDeviceCenterFrequency(quint64 freq_hz)
{
	qint64 df = ((qint64)freq_hz * m_settings.m_LOppmTenths) / 10000000LL;
	freq_hz += df;

	airspy_error rc = (airspy_error) airspy_set_freq(m_dev, static_cast<uint32_t>(freq_hz));

	if (rc != AIRSPY_SUCCESS)
	{
		qWarning("AirspyInput::setDeviceCenterFrequency: could not frequency to %llu Hz", freq_hz);
	}
	else
	{
		qDebug("AirspyInput::setDeviceCenterFrequency: frequency set to %llu Hz", freq_hz);
	}
}

bool AirspyInput::applySettings(const AirspySettings& settings, bool force)
{
	QMutexLocker mutexLocker(&m_mutex);

	bool forwardChange = false;
	airspy_error rc = AIRSPY_ERROR_OTHER;

	qDebug() << "AirspyInput::applySettings";

    if ((m_settings.m_dcBlock != settings.m_dcBlock) ||
        (m_settings.m_iqCorrection != settings.m_iqCorrection) || force)
    {
		m_settings.m_dcBlock = settings.m_dcBlock;
		m_settings.m_iqCorrection = settings.m_iqCorrection;
		m_deviceAPI->configureCorrections(m_settings.m_dcBlock, m_settings.m_iqCorrection);
	}

	if ((m_settings.m_devSampleRateIndex != settings.m_devSampleRateIndex) || force)
	{
		forwardChange = true;

		if (settings.m_devSampleRateIndex < m_sampleRates.size())
		{
			m_settings.m_devSampleRateIndex = settings.m_devSampleRateIndex;
		}
		else
		{
			m_settings.m_devSampleRateIndex = m_sampleRates.size() - 1;
		}

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_samplerate(m_dev, static_cast<airspy_samplerate_t>(m_settings.m_devSampleRateIndex));

			if (rc != AIRSPY_SUCCESS)
			{
				qCritical("AirspyInput::applySettings: could not set sample rate index %u (%d S/s): %s", m_settings.m_devSampleRateIndex, m_sampleRates[m_settings.m_devSampleRateIndex], airspy_error_name(rc));
			}
			else if (m_airspyThread != 0)
			{
				qDebug("AirspyInput::applySettings: sample rate set to index: %u (%d S/s)", m_settings.m_devSampleRateIndex, m_sampleRates[m_settings.m_devSampleRateIndex]);
				m_airspyThread->setSamplerate(m_sampleRates[m_settings.m_devSampleRateIndex]);
			}
		}
	}

	if ((m_settings.m_log2Decim != settings.m_log2Decim) || force)
	{
		forwardChange = true;

		if (m_airspyThread != 0)
		{
			m_airspyThread->setLog2Decimation(settings.m_log2Decim);
			qDebug() << "AirspyInput: set decimation to " << (1<<settings.m_log2Decim);
		}
	}

	if ((m_settings.m_centerFrequency != settings.m_centerFrequency)
        || (m_settings.m_LOppmTenths != settings.m_LOppmTenths)
        || (m_settings.m_fcPos != settings.m_fcPos)
        || (m_settings.m_log2Decim != settings.m_log2Decim)
        || (m_settings.m_transverterMode != settings.m_transverterMode)
        || (m_settings.m_transverterDeltaFrequency != settings.m_transverterDeltaFrequency) || force)
	{
        qint64 deviceCenterFrequency = DeviceSampleSource::calculateDeviceCenterFrequency(
                settings.m_centerFrequency,
                settings.m_transverterDeltaFrequency,
                settings.m_log2Decim,
                (DeviceSampleSource::fcPos_t) settings.m_fcPos,
                m_sampleRates[m_settings.m_devSampleRateIndex],
                settings.m_transverterMode);

        m_settings.m_centerFrequency = settings.m_centerFrequency;
        m_settings.m_log2Decim = settings.m_log2Decim;
        m_settings.m_transverterMode = settings.m_transverterMode;
        m_settings.m_transverterDeltaFrequency = settings.m_transverterDeltaFrequency;
        m_settings.m_LOppmTenths = settings.m_LOppmTenths;

		if (m_dev != 0) {
			setDeviceCenterFrequency(deviceCenterFrequency);
		}

		forwardChange = true;
	}

	if ((m_settings.m_fcPos != settings.m_fcPos) || force)
	{
		m_settings.m_fcPos = settings.m_fcPos;

		if (m_airspyThread != 0)
		{
			m_airspyThread->setFcPos((int) m_settings.m_fcPos);
			qDebug() << "AirspyInput: set fc pos (enum) to " << (int) m_settings.m_fcPos;
		}
	}

	if ((m_settings.m_lnaGain != settings.m_lnaGain) || force)
	{
		m_settings.m_lnaGain = settings.m_lnaGain;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_lna_gain(m_dev, m_settings.m_lnaGain);

			if(rc != AIRSPY_SUCCESS)
			{
				qDebug("AirspyInput::applySettings: airspy_set_lna_gain failed: %s", airspy_error_name(rc));
			}
			else
			{
				qDebug() << "AirspyInput:applySettings: LNA gain set to " << m_settings.m_lnaGain;
			}
		}
	}

	if ((m_settings.m_lnaAGC != settings.m_lnaAGC) || force)
	{
		m_settings.m_lnaAGC = settings.m_lnaAGC;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_lna_agc(m_dev, (m_settings.m_lnaAGC ? 1 : 0));
		}

		if(rc != AIRSPY_SUCCESS)
		{
			qDebug("AirspyInput::applySettings: airspy_set_lna_agc failed: %s", airspy_error_name(rc));
		}
		else
		{
			qDebug() << "AirspyInput:applySettings: LNA AGC set to " << m_settings.m_lnaAGC;
		}
	}

	if ((m_settings.m_mixerGain != settings.m_mixerGain) || force)
	{
		m_settings.m_mixerGain = settings.m_mixerGain;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_mixer_gain(m_dev, m_settings.m_mixerGain);

			if(rc != AIRSPY_SUCCESS)
			{
				qDebug("AirspyInput::applySettings: airspy_set_mixer_gain failed: %s", airspy_error_name(rc));
			}
			else
			{
				qDebug() << "AirspyInput:applySettings: mixer gain set to " << m_settings.m_mixerGain;
			}
		}
	}

	if ((m_settings.m_mixerAGC != settings.m_mixerAGC) || force)
	{
		m_settings.m_mixerAGC = settings.m_mixerAGC;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_mixer_agc(m_dev, (m_settings.m_mixerAGC ? 1 : 0));
		}

		if(rc != AIRSPY_SUCCESS)
		{
			qDebug("AirspyInput::applySettings: airspy_set_mixer_agc failed: %s", airspy_error_name(rc));
		}
		else
		{
			qDebug() << "AirspyInput:applySettings: Mixer AGC set to " << m_settings.m_mixerAGC;
		}
	}

	if ((m_settings.m_vgaGain != settings.m_vgaGain) || force)
	{
		m_settings.m_vgaGain = settings.m_vgaGain;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_vga_gain(m_dev, m_settings.m_vgaGain);

			if(rc != AIRSPY_SUCCESS)
			{
				qDebug("AirspyInput::applySettings: airspy_set_vga_gain failed: %s", airspy_error_name(rc));
			}
			else
			{
				qDebug() << "AirspyInput:applySettings: VGA gain set to " << m_settings.m_vgaGain;
			}
		}
	}

	if ((m_settings.m_biasT != settings.m_biasT) || force)
	{
		m_settings.m_biasT = settings.m_biasT;

		if (m_dev != 0)
		{
			rc = (airspy_error) airspy_set_rf_bias(m_dev, (m_settings.m_biasT ? 1 : 0));

			if(rc != AIRSPY_SUCCESS)
			{
				qDebug("AirspyInput::applySettings: airspy_set_rf_bias failed: %s", airspy_error_name(rc));
			}
			else
			{
				qDebug() << "AirspyInput:applySettings: bias tee set to " << m_settings.m_biasT;
			}
		}
	}

	if (forwardChange)
	{
		int sampleRate = m_sampleRates[m_settings.m_devSampleRateIndex]/(1<<m_settings.m_log2Decim);
		DSPSignalNotification *notif = new DSPSignalNotification(sampleRate, m_settings.m_centerFrequency);
        m_fileSink->handleMessage(*notif); // forward to file sink
        m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
	}

	return true;
}

struct airspy_device *AirspyInput::open_airspy_from_sequence(int sequence)
{
	struct airspy_device *devinfo;
	airspy_error rc = AIRSPY_ERROR_OTHER;

	for (int i = 0; i < AirspyPlugin::m_maxDevices; i++)
	{
		rc = (airspy_error) airspy_open(&devinfo);

		if (rc == AIRSPY_SUCCESS)
		{
			if (i == sequence) {
				return devinfo;
			} else {
			    airspy_close(devinfo);
			}
		}
		else
		{
			break;
		}
	}

	return 0;
}

int AirspyInput::webapiRunGet(
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage __attribute__((unused)))
{
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    return 200;
}

int AirspyInput::webapiRun(
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

int AirspyInput::webapiSettingsGet(
                SWGSDRangel::SWGDeviceSettings& response,
                QString& errorMessage __attribute__((unused)))
{
    response.setAirspySettings(new SWGSDRangel::SWGAirspySettings());
    response.getAirspySettings()->init();
    webapiFormatDeviceSettings(response, m_settings);
    return 200;
}

int AirspyInput::webapiSettingsPutPatch(
                bool force,
                const QStringList& deviceSettingsKeys,
                SWGSDRangel::SWGDeviceSettings& response, // query + response
                QString& errorMessage __attribute__((unused)))
{
    AirspySettings settings = m_settings;

    if (deviceSettingsKeys.contains("centerFrequency")) {
        settings.m_centerFrequency = response.getAirspySettings()->getCenterFrequency();
    }
    if (deviceSettingsKeys.contains("LOppmTenths")) {
        settings.m_LOppmTenths = response.getAirspySettings()->getLOppmTenths();
    }
    if (deviceSettingsKeys.contains("devSampleRateIndex")) {
        settings.m_devSampleRateIndex = response.getAirspySettings()->getDevSampleRateIndex();
    }
    if (deviceSettingsKeys.contains("lnaGain")) {
        settings.m_lnaGain = response.getAirspySettings()->getLnaGain();
    }
    if (deviceSettingsKeys.contains("mixerGain")) {
        settings.m_mixerGain = response.getAirspySettings()->getMixerGain();
    }
    if (deviceSettingsKeys.contains("vgaGain")) {
        settings.m_vgaGain = response.getAirspySettings()->getVgaGain();
    }
    if (deviceSettingsKeys.contains("lnaAGC")) {
        settings.m_lnaAGC = response.getAirspySettings()->getLnaAgc() != 0;
    }
    if (deviceSettingsKeys.contains("mixerAGC")) {
        settings.m_mixerAGC = response.getAirspySettings()->getMixerAgc() != 0;
    }
    if (deviceSettingsKeys.contains("log2Decim")) {
        settings.m_log2Decim = response.getAirspySettings()->getLog2Decim();
    }
    if (deviceSettingsKeys.contains("fcPos")) {
        int fcPos = response.getAirspySettings()->getFcPos();
        fcPos = fcPos < 0 ? 0 : fcPos > 2 ? 2 : fcPos;
        settings.m_fcPos = (AirspySettings::fcPos_t) fcPos;
    }
    if (deviceSettingsKeys.contains("biasT")) {
        settings.m_biasT = response.getAirspySettings()->getBiasT() != 0;
    }
    if (deviceSettingsKeys.contains("dcBlock")) {
        settings.m_dcBlock = response.getAirspySettings()->getDcBlock() != 0;
    }
    if (deviceSettingsKeys.contains("iqCorrection")) {
        settings.m_iqCorrection = response.getAirspySettings()->getIqCorrection() != 0;
    }
    if (deviceSettingsKeys.contains("transverterDeltaFrequency")) {
        settings.m_transverterDeltaFrequency = response.getAirspySettings()->getTransverterDeltaFrequency();
    }
    if (deviceSettingsKeys.contains("transverterMode")) {
        settings.m_transverterMode = response.getAirspySettings()->getTransverterMode() != 0;
    }
    if (deviceSettingsKeys.contains("fileRecordName")) {
        settings.m_fileRecordName = *response.getAirspySettings()->getFileRecordName();
    }

    MsgConfigureAirspy *msg = MsgConfigureAirspy::create(settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureAirspy *msgToGUI = MsgConfigureAirspy::create(settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatDeviceSettings(response, settings);
    return 200;
}

int AirspyInput::webapiReportGet(
        SWGSDRangel::SWGDeviceReport& response,
        QString& errorMessage __attribute__((unused)))
{
    response.setAirspyReport(new SWGSDRangel::SWGAirspyReport());
    response.getAirspyReport()->init();
    webapiFormatDeviceReport(response);
    return 200;
}

void AirspyInput::webapiFormatDeviceSettings(SWGSDRangel::SWGDeviceSettings& response, const AirspySettings& settings)
{
    response.getAirspySettings()->setCenterFrequency(settings.m_centerFrequency);
    response.getAirspySettings()->setLOppmTenths(settings.m_LOppmTenths);
    response.getAirspySettings()->setDevSampleRateIndex(settings.m_devSampleRateIndex);
    response.getAirspySettings()->setLnaGain(settings.m_lnaGain);
    response.getAirspySettings()->setMixerGain(settings.m_mixerGain);
    response.getAirspySettings()->setVgaGain(settings.m_vgaGain);
    response.getAirspySettings()->setLnaAgc(settings.m_lnaAGC ? 1 : 0);
    response.getAirspySettings()->setMixerAgc(settings.m_mixerAGC ? 1 : 0);
    response.getAirspySettings()->setLog2Decim(settings.m_log2Decim);
    response.getAirspySettings()->setFcPos((int) settings.m_fcPos);
    response.getAirspySettings()->setBiasT(settings.m_biasT ? 1 : 0);
    response.getAirspySettings()->setDcBlock(settings.m_dcBlock ? 1 : 0);
    response.getAirspySettings()->setIqCorrection(settings.m_iqCorrection ? 1 : 0);
    response.getAirspySettings()->setTransverterDeltaFrequency(settings.m_transverterDeltaFrequency);
    response.getAirspySettings()->setTransverterMode(settings.m_transverterMode ? 1 : 0);

    if (response.getAirspySettings()->getFileRecordName()) {
        *response.getAirspySettings()->getFileRecordName() = settings.m_fileRecordName;
    } else {
        response.getAirspySettings()->setFileRecordName(new QString(settings.m_fileRecordName));
    }
}

void AirspyInput::webapiFormatDeviceReport(SWGSDRangel::SWGDeviceReport& response)
{
    response.getAirspyReport()->setSampleRates(new QList<SWGSDRangel::SWGSampleRate*>);

    for (std::vector<uint32_t>::const_iterator it = getSampleRates().begin(); it != getSampleRates().end(); ++it)
    {
        response.getAirspyReport()->getSampleRates()->append(new SWGSDRangel::SWGSampleRate);
        response.getAirspyReport()->getSampleRates()->back()->setRate(*it);
    }
}
