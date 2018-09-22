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

#include "SWGDeviceSettings.h"
#include "SWGDeviceState.h"

#include "util/simpleserializer.h"
#include "dsp/dspcommands.h"
#include "dsp/dspengine.h"
#include "device/devicesinkapi.h"
#include "device/devicesourceapi.h"
#include "bladerf/devicebladerfshared.h"

#include "bladerfoutput.h"
#include "bladerfoutputthread.h"

MESSAGE_CLASS_DEFINITION(BladerfOutput::MsgConfigureBladerf, Message)
MESSAGE_CLASS_DEFINITION(BladerfOutput::MsgStartStop, Message)
MESSAGE_CLASS_DEFINITION(BladerfOutput::MsgReportBladerf, Message)

BladerfOutput::BladerfOutput(DeviceSinkAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
	m_settings(),
	m_dev(0),
	m_bladerfThread(0),
	m_deviceDescription("BladeRFOutput"),
	m_running(false)
{
    m_sampleSourceFifo.resize(16*BLADERFOUTPUT_BLOCKSIZE);
    openDevice();
    m_deviceAPI->setBuddySharedPtr(&m_sharedParams);
}

BladerfOutput::~BladerfOutput()
{
    if (m_running) stop();
    closeDevice();
    m_deviceAPI->setBuddySharedPtr(0);
}

void BladerfOutput::destroy()
{
    delete this;
}

bool BladerfOutput::openDevice()
{
    if (m_dev != 0)
    {
        closeDevice();
    }

    int res;

    m_sampleSourceFifo.resize(m_settings.m_devSampleRate/(1<<(m_settings.m_log2Interp <= 4 ? m_settings.m_log2Interp : 4)));

    if (m_deviceAPI->getSourceBuddies().size() > 0)
    {
        DeviceSourceAPI *sourceBuddy = m_deviceAPI->getSourceBuddies()[0];
        DeviceBladeRFParams *buddySharedParams = (DeviceBladeRFParams *) sourceBuddy->getBuddySharedPtr();

        if (buddySharedParams == 0)
        {
            qCritical("BladerfOutput::start: could not get shared parameters from buddy");
            return false;
        }

        if (buddySharedParams->m_dev == 0) // device is not opened by buddy
        {
            qCritical("BladerfOutput::start: could not get BladeRF handle from buddy");
            return false;
        }

        m_sharedParams = *(buddySharedParams); // copy parameters from buddy
        m_dev = m_sharedParams.m_dev;          // get BladeRF handle
    }
    else
    {
        if (!DeviceBladeRF::open_bladerf(&m_dev, qPrintable(m_deviceAPI->getSampleSinkSerial())))
        {
            qCritical("BladerfOutput::start: could not open BladeRF %s", qPrintable(m_deviceAPI->getSampleSinkSerial()));
            return false;
        }

        m_sharedParams.m_dev = m_dev;
    }

    // TODO: adjust USB transfer data according to sample rate
    if ((res = bladerf_sync_config(m_dev, BLADERF_TX_X1, BLADERF_FORMAT_SC16_Q11, 64, 8192, 32, 10000)) < 0)
    {
        qCritical("BladerfOutput::start: bladerf_sync_config with return code %d", res);
        return false;
    }

    if ((res = bladerf_enable_module(m_dev, BLADERF_MODULE_TX, true)) < 0)
    {
        qCritical("BladerfOutput::start: bladerf_enable_module with return code %d", res);
        return false;
    }

    return true;
}

void BladerfOutput::init()
{
    applySettings(m_settings, true);
}

bool BladerfOutput::start()
{
//	QMutexLocker mutexLocker(&m_mutex);

    if (!m_dev) {
        return false;
    }

    if (m_running) stop();

    m_bladerfThread = new BladerfOutputThread(m_dev, &m_sampleSourceFifo);

//	mutexLocker.unlock();
	applySettings(m_settings, true);

	m_bladerfThread->setLog2Interpolation(m_settings.m_log2Interp);

    m_bladerfThread->startWork();

	qDebug("BladerfOutput::start: started");
    m_running = true;

    return true;
}

void BladerfOutput::closeDevice()
{
    int res;

    if (m_dev == 0) { // was never open
        return;
    }

    if ((res = bladerf_enable_module(m_dev, BLADERF_MODULE_TX, false)) < 0)
    {
        qCritical("BladerfOutput::closeDevice: bladerf_enable_module with return code %d", res);
    }

    if (m_deviceAPI->getSourceBuddies().size() == 0)
    {
        qDebug("BladerfOutput::closeDevice: closing device since Rx side is not open");

        if (m_dev != 0) // close BladeRF
        {
            bladerf_close(m_dev);
        }
    }

    m_sharedParams.m_dev = 0;
    m_dev = 0;
}

void BladerfOutput::stop()
{
//	QMutexLocker mutexLocker(&m_mutex);
    if (m_bladerfThread != 0)
	{
		m_bladerfThread->stopWork();
		delete m_bladerfThread;
		m_bladerfThread = 0;
	}

    m_running = false;
}

QByteArray BladerfOutput::serialize() const
{
    return m_settings.serialize();
}

bool BladerfOutput::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureBladerf* message = MsgConfigureBladerf::create(m_settings, true);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureBladerf* messageToGUI = MsgConfigureBladerf::create(m_settings, true);
        m_guiMessageQueue->push(messageToGUI);
    }

    return success;
}

const QString& BladerfOutput::getDeviceDescription() const
{
	return m_deviceDescription;
}

int BladerfOutput::getSampleRate() const
{
	int rate = m_settings.m_devSampleRate;
	return (rate / (1<<m_settings.m_log2Interp));
}

quint64 BladerfOutput::getCenterFrequency() const
{
	return m_settings.m_centerFrequency;
}

void BladerfOutput::setCenterFrequency(qint64 centerFrequency)
{
    BladeRFOutputSettings settings = m_settings;
    settings.m_centerFrequency = centerFrequency;

    MsgConfigureBladerf* message = MsgConfigureBladerf::create(settings, false);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureBladerf* messageToGUI = MsgConfigureBladerf::create(settings, false);
        m_guiMessageQueue->push(messageToGUI);
    }
}

bool BladerfOutput::handleMessage(const Message& message)
{
	if (MsgConfigureBladerf::match(message))
	{
		MsgConfigureBladerf& conf = (MsgConfigureBladerf&) message;
		qDebug() << "BladerfOutput::handleMessage: MsgConfigureBladerf";

		if (!applySettings(conf.getSettings(), conf.getForce()))
		{
			qDebug("BladeRF config error");
		}

		return true;
	}
    else if (MsgStartStop::match(message))
    {
        MsgStartStop& cmd = (MsgStartStop&) message;
        qDebug() << "BladerfOutput::handleMessage: MsgStartStop: " << (cmd.getStartStop() ? "start" : "stop");

        if (cmd.getStartStop())
        {
            if (m_deviceAPI->initGeneration())
            {
                m_deviceAPI->startGeneration();
            }
        }
        else
        {
            m_deviceAPI->stopGeneration();
        }

        return true;
    }
	else
	{
		return false;
	}
}

bool BladerfOutput::applySettings(const BladeRFOutputSettings& settings, bool force)
{
	bool forwardChange    = false;
    bool suspendOwnThread = false;
    bool threadWasRunning = false;
//	QMutexLocker mutexLocker(&m_mutex);

	qDebug() << "BladerfOutput::applySettings: m_dev: " << m_dev;

    if ((m_settings.m_devSampleRate != settings.m_devSampleRate) ||
        (m_settings.m_log2Interp != settings.m_log2Interp) || force)
    {
        suspendOwnThread = true;
    }

    if (suspendOwnThread)
    {
        if (m_bladerfThread)
        {
            if (m_bladerfThread->isRunning())
            {
                m_bladerfThread->stopWork();
                threadWasRunning = true;
            }
        }
    }

	if ((m_settings.m_devSampleRate != settings.m_devSampleRate) || (m_settings.m_log2Interp != settings.m_log2Interp) || force)
	{
	    int fifoSize;

	    if (settings.m_log2Interp >= 5)
	    {
	        fifoSize = DeviceBladeRFShared::m_sampleFifoMinSize32;
	    }
	    else
	    {
	        fifoSize = std::max(
	            (int) ((settings.m_devSampleRate/(1<<settings.m_log2Interp)) * DeviceBladeRFShared::m_sampleFifoLengthInSeconds),
	            DeviceBladeRFShared::m_sampleFifoMinSize);
	    }

        m_sampleSourceFifo.resize(fifoSize);
	}

    if ((m_settings.m_devSampleRate != settings.m_devSampleRate) || force)
    {
        forwardChange = true;

        if (m_dev != 0)
        {
            unsigned int actualSamplerate;

            if (bladerf_set_sample_rate(m_dev, BLADERF_MODULE_TX, settings.m_devSampleRate, &actualSamplerate) < 0)
            {
                qCritical("BladerfOutput::applySettings: could not set sample rate: %d", settings.m_devSampleRate);
            }
            else
            {
                qDebug() << "BladerfOutput::applySettings: bladerf_set_sample_rate(BLADERF_MODULE_TX) actual sample rate is " << actualSamplerate;
            }
        }
    }

    if ((m_settings.m_log2Interp != settings.m_log2Interp) || force)
    {
        forwardChange = true;

        if (m_bladerfThread != 0)
        {
            m_bladerfThread->setLog2Interpolation(settings.m_log2Interp);
            qDebug() << "BladerfOutput::applySettings: set interpolation to " << (1<<settings.m_log2Interp);
        }
    }

	if ((m_settings.m_vga1 != settings.m_vga1) || force)
	{
		if (m_dev != 0)
		{
			if(bladerf_set_txvga1(m_dev, settings.m_vga1) != 0)
			{
				qDebug("BladerfOutput::applySettings: bladerf_set_txvga1() failed");
			}
			else
			{
				qDebug() << "BladerfOutput::applySettings: VGA1 gain set to " << settings.m_vga1;
			}
		}
	}

	if ((m_settings.m_vga2 != settings.m_vga2) || force)
	{
		if(m_dev != 0)
		{
			if(bladerf_set_txvga2(m_dev, settings.m_vga2) != 0)
			{
				qDebug("BladerfOutput::applySettings:bladerf_set_rxvga2() failed");
			}
			else
			{
				qDebug() << "BladerfOutput::applySettings: VGA2 gain set to " << settings.m_vga2;
			}
		}
	}

	if ((m_settings.m_xb200 != settings.m_xb200) || force)
	{
		if (m_dev != 0)
		{
            bool changeSettings;

            if (m_deviceAPI->getSourceBuddies().size() > 0)
            {
                DeviceSourceAPI *buddy = m_deviceAPI->getSourceBuddies()[0];

                if (buddy->getDeviceSourceEngine()->state() == DSPDeviceSourceEngine::StRunning) // Tx side running
                {
                    changeSettings = false;
                }
                else
                {
                    changeSettings = true;
                }
            }
            else // No Rx open
            {
                changeSettings = true;
            }

            if (changeSettings)
            {
                if (settings.m_xb200)
                {
                    if (bladerf_expansion_attach(m_dev, BLADERF_XB_200) != 0)
                    {
                        qDebug("BladerfOutput::applySettings: bladerf_expansion_attach(xb200) failed");
                    }
                    else
                    {
                        qDebug() << "BladerfOutput::applySettings: Attach XB200";
                    }
                }
                else
                {
                    if (bladerf_expansion_attach(m_dev, BLADERF_XB_NONE) != 0)
                    {
                        qDebug("BladerfOutput::applySettings: bladerf_expansion_attach(none) failed");
                    }
                    else
                    {
                        qDebug() << "BladerfOutput::applySettings: Detach XB200";
                    }
                }

                m_sharedParams.m_xb200Attached = settings.m_xb200;
            }
        }
	}

	if ((m_settings.m_xb200Path != settings.m_xb200Path) || force)
	{
		if (m_dev != 0)
		{
			if(bladerf_xb200_set_path(m_dev, BLADERF_MODULE_TX, settings.m_xb200Path) != 0)
			{
				qDebug("BladerfOutput::applySettings: bladerf_xb200_set_path(BLADERF_MODULE_TX) failed");
			}
			else
			{
				qDebug() << "BladerfOutput::applySettings: set xb200 path to " << settings.m_xb200Path;
			}
		}
	}

	if ((m_settings.m_xb200Filter != settings.m_xb200Filter) || force)
	{
		if (m_dev != 0)
		{
			if(bladerf_xb200_set_filterbank(m_dev, BLADERF_MODULE_TX, settings.m_xb200Filter) != 0)
			{
				qDebug("BladerfOutput::applySettings: bladerf_xb200_set_filterbank(BLADERF_MODULE_TX) failed");
			}
			else
			{
				qDebug() << "BladerfOutput::applySettings: set xb200 filter to " << settings.m_xb200Filter;
			}
		}
	}

	if ((m_settings.m_bandwidth != settings.m_bandwidth) || force)
	{
		if(m_dev != 0)
		{
			unsigned int actualBandwidth;

			if( bladerf_set_bandwidth(m_dev, BLADERF_MODULE_TX, settings.m_bandwidth, &actualBandwidth) < 0)
			{
				qCritical("BladerfOutput::applySettings: could not set bandwidth: %d", settings.m_bandwidth);
			}
			else
			{
				qDebug() << "BladerfOutput::applySettings: bladerf_set_bandwidth(BLADERF_MODULE_TX) actual bandwidth is " << actualBandwidth;
			}
		}
	}

	if (m_settings.m_centerFrequency != settings.m_centerFrequency)
	{
		forwardChange = true;
	}

	if (m_dev != NULL)
	{
		if (bladerf_set_frequency( m_dev, BLADERF_MODULE_TX, settings.m_centerFrequency ) != 0)
		{
			qDebug("BladerfOutput::applySettings: bladerf_set_frequency(%lld) failed", settings.m_centerFrequency);
		}
	}

    if (threadWasRunning)
    {
        m_bladerfThread->startWork();
    }

    m_settings.m_centerFrequency = settings.m_centerFrequency;
    m_settings.m_bandwidth = settings.m_bandwidth;
    m_settings.m_xb200Filter = settings.m_xb200Filter;
    m_settings.m_xb200 = settings.m_xb200;
    m_settings.m_xb200Path = settings.m_xb200Path;
    m_settings.m_vga2 = settings.m_vga2;
    m_settings.m_vga1 = settings.m_vga1;
    m_settings.m_devSampleRate = settings.m_devSampleRate;
    m_settings.m_log2Interp = settings.m_log2Interp;

	if (forwardChange)
	{
		int sampleRate = m_settings.m_devSampleRate/(1<<m_settings.m_log2Interp);
		DSPSignalNotification *notif = new DSPSignalNotification(sampleRate, m_settings.m_centerFrequency);
		m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
	}

	qDebug() << "BladerfOutput::applySettings: center freq: " << m_settings.m_centerFrequency << " Hz"
			<< " device sample rate: " << m_settings.m_devSampleRate << "S/s"
			<< " baseband sample rate: " << m_settings.m_devSampleRate/(1<<m_settings.m_log2Interp) << "S/s"
			<< " BW: " << m_settings.m_bandwidth << "Hz";

	return true;
}

int BladerfOutput::webapiSettingsGet(
                SWGSDRangel::SWGDeviceSettings& response,
                QString& errorMessage __attribute__((unused)))
{
    response.setBladeRfOutputSettings(new SWGSDRangel::SWGBladeRFOutputSettings());
    response.getBladeRfOutputSettings()->init();
    webapiFormatDeviceSettings(response, m_settings);
    return 200;
}

void BladerfOutput::webapiFormatDeviceSettings(SWGSDRangel::SWGDeviceSettings& response, const BladeRFOutputSettings& settings)
{
    response.getBladeRfOutputSettings()->setCenterFrequency(settings.m_centerFrequency);
    response.getBladeRfOutputSettings()->setDevSampleRate(settings.m_devSampleRate);
    response.getBladeRfOutputSettings()->setVga1(settings.m_vga1);
    response.getBladeRfOutputSettings()->setVga2(settings.m_vga2);
    response.getBladeRfOutputSettings()->setBandwidth(settings.m_bandwidth);
    response.getBladeRfOutputSettings()->setLog2Interp(settings.m_log2Interp);
    response.getBladeRfOutputSettings()->setXb200(settings.m_xb200 ? 1 : 0);
    response.getBladeRfOutputSettings()->setXb200Path((int) settings.m_xb200Path);
    response.getBladeRfOutputSettings()->setXb200Filter((int) settings.m_xb200Filter);
}

int BladerfOutput::webapiSettingsPutPatch(
                bool force,
                const QStringList& deviceSettingsKeys,
                SWGSDRangel::SWGDeviceSettings& response, // query + response
                QString& errorMessage __attribute__((unused)))
{
    BladeRFOutputSettings settings = m_settings;

    if (deviceSettingsKeys.contains("centerFrequency")) {
        settings.m_centerFrequency = response.getBladeRfOutputSettings()->getCenterFrequency();
    }
    if (deviceSettingsKeys.contains("devSampleRate")) {
        settings.m_devSampleRate = response.getBladeRfOutputSettings()->getDevSampleRate();
    }
    if (deviceSettingsKeys.contains("vga1")) {
        settings.m_vga1 = response.getBladeRfOutputSettings()->getVga1();
    }
    if (deviceSettingsKeys.contains("vga2")) {
        settings.m_vga2 = response.getBladeRfOutputSettings()->getVga2();
    }
    if (deviceSettingsKeys.contains("bandwidth")) {
        settings.m_bandwidth = response.getBladeRfOutputSettings()->getBandwidth();
    }
    if (deviceSettingsKeys.contains("log2Interp")) {
        settings.m_log2Interp = response.getBladeRfOutputSettings()->getLog2Interp();
    }
    if (deviceSettingsKeys.contains("xb200")) {
        settings.m_xb200 = response.getBladeRfOutputSettings()->getXb200() == 0 ? 0 : 1;
    }
    if (deviceSettingsKeys.contains("xb200Path")) {
        settings.m_xb200Path = static_cast<bladerf_xb200_path>(response.getBladeRfOutputSettings()->getXb200Path());
    }
    if (deviceSettingsKeys.contains("xb200Filter")) {
        settings.m_xb200Filter = static_cast<bladerf_xb200_filter>(response.getBladeRfOutputSettings()->getXb200Filter());
    }

    MsgConfigureBladerf *msg = MsgConfigureBladerf::create(settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureBladerf *msgToGUI = MsgConfigureBladerf::create(settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatDeviceSettings(response, settings);
    return 200;
}

int BladerfOutput::webapiRunGet(
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage __attribute__((unused)))
{
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    return 200;
}

int BladerfOutput::webapiRun(
        bool run,
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage __attribute__((unused)))
{
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    MsgStartStop *message = MsgStartStop::create(run);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgStartStop *messagetoGui = MsgStartStop::create(run);
        m_guiMessageQueue->push(messagetoGui);
    }

    return 200;
}

