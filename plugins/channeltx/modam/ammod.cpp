///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
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

#include "ammod.h"

#include <QTime>
#include <QDebug>
#include <QMutexLocker>

#include <stdio.h>
#include <complex.h>

#include "SWGChannelSettings.h"
#include "SWGChannelReport.h"
#include "SWGAMModReport.h"

#include "dsp/upchannelizer.h"
#include "dsp/dspengine.h"
#include "dsp/threadedbasebandsamplesource.h"
#include "dsp/dspcommands.h"
#include "device/devicesinkapi.h"
#include "util/db.h"

MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureAMMod, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureChannelizer, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceName, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceSeek, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceStreamTiming, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgReportFileSourceStreamData, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgReportFileSourceStreamTiming, Message)

const QString AMMod::m_channelIdURI = "sdrangel.channeltx.modam";
const QString AMMod::m_channelId ="AMMod";
const int AMMod::m_levelNbSamples = 480; // every 10ms

AMMod::AMMod(DeviceSinkAPI *deviceAPI) :
    ChannelSourceAPI(m_channelIdURI),
    m_deviceAPI(deviceAPI),
    m_basebandSampleRate(48000),
    m_outputSampleRate(48000),
    m_inputFrequencyOffset(0),
    m_audioFifo(4800),
	m_settingsMutex(QMutex::Recursive),
	m_fileSize(0),
	m_recordLength(0),
	m_sampleRate(48000),
	m_levelCalcCount(0),
	m_peakLevel(0.0f),
	m_levelSum(0.0f)
{
	setObjectName(m_channelId);

	m_audioBuffer.resize(1<<14);
	m_audioBufferFill = 0;

	m_magsq = 0.0;

	DSPEngine::instance()->getAudioDeviceManager()->addAudioSource(&m_audioFifo, getInputMessageQueue());
	m_audioSampleRate = DSPEngine::instance()->getAudioDeviceManager()->getInputSampleRate();
	m_toneNco.setFreq(1000.0, m_audioSampleRate);

	// CW keyer
	m_cwKeyer.setSampleRate(m_audioSampleRate);
	m_cwKeyer.setWPM(13);
	m_cwKeyer.setMode(CWKeyerSettings::CWNone);

    applyChannelSettings(m_basebandSampleRate, m_outputSampleRate, m_inputFrequencyOffset, true);
    applySettings(m_settings, true);

    m_channelizer = new UpChannelizer(this);
    m_threadedChannelizer = new ThreadedBasebandSampleSource(m_channelizer, this);
    m_deviceAPI->addThreadedSource(m_threadedChannelizer);
    m_deviceAPI->addChannelAPI(this);
}

AMMod::~AMMod()
{
    m_deviceAPI->removeChannelAPI(this);
    m_deviceAPI->removeThreadedSource(m_threadedChannelizer);
    delete m_threadedChannelizer;
    delete m_channelizer;
    DSPEngine::instance()->getAudioDeviceManager()->removeAudioSource(&m_audioFifo);
}

void AMMod::pull(Sample& sample)
{
	if (m_settings.m_channelMute)
	{
		sample.m_real = 0.0f;
		sample.m_imag = 0.0f;
		return;
	}

	Complex ci;

	m_settingsMutex.lock();

    if (m_interpolatorDistance > 1.0f) // decimate
    {
    	modulateSample();

        while (!m_interpolator.decimate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
        	modulateSample();
        }
    }
    else
    {
        if (m_interpolator.interpolate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
        	modulateSample();
        }
    }

    m_interpolatorDistanceRemain += m_interpolatorDistance;

    ci *= m_carrierNco.nextIQ(); // shift to carrier frequency

    m_settingsMutex.unlock();

    double magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
	magsq /= (SDR_TX_SCALED*SDR_TX_SCALED);
	m_movingAverage(magsq);
	m_magsq = m_movingAverage.asDouble();

	sample.m_real = (FixReal) ci.real();
	sample.m_imag = (FixReal) ci.imag();
}

void AMMod::pullAudio(int nbSamples)
{
//    qDebug("AMMod::pullAudio: %d", nbSamples);
    unsigned int nbAudioSamples = nbSamples * ((Real) m_audioSampleRate / (Real) m_basebandSampleRate);

    if (nbAudioSamples > m_audioBuffer.size())
    {
        m_audioBuffer.resize(nbAudioSamples);
    }

    m_audioFifo.read(reinterpret_cast<quint8*>(&m_audioBuffer[0]), nbAudioSamples, 10);
    m_audioBufferFill = 0;
}

void AMMod::modulateSample()
{
	Real t;

    pullAF(t);
    calculateLevel(t);
    m_audioBufferFill++;

    m_modSample.real((t*m_settings.m_modFactor + 1.0f) * 16384.0f); // modulate and scale zero frequency carrier
    m_modSample.imag(0.0f);
}

void AMMod::pullAF(Real& sample)
{
    switch (m_settings.m_modAFInput)
    {
    case AMModSettings::AMModInputTone:
        sample = m_toneNco.next();
        break;
    case AMModSettings::AMModInputFile:
        // sox f4exb_call.wav --encoding float --endian little f4exb_call.raw
        // ffplay -f f32le -ar 48k -ac 1 f4exb_call.raw
        if (m_ifstream.is_open())
        {
            if (m_ifstream.eof())
            {
            	if (m_settings.m_playLoop)
            	{
                    m_ifstream.clear();
                    m_ifstream.seekg(0, std::ios::beg);
            	}
            }

            if (m_ifstream.eof())
            {
            	sample = 0.0f;
            }
            else
            {
            	m_ifstream.read(reinterpret_cast<char*>(&sample), sizeof(Real));
            	sample *= m_settings.m_volumeFactor;
            }
        }
        else
        {
            sample = 0.0f;
        }
        break;
    case AMModSettings::AMModInputAudio:
        sample = ((m_audioBuffer[m_audioBufferFill].l + m_audioBuffer[m_audioBufferFill].r) / 65536.0f) * m_settings.m_volumeFactor;
        break;
    case AMModSettings::AMModInputCWTone:
        Real fadeFactor;

        if (m_cwKeyer.getSample())
        {
            m_cwKeyer.getCWSmoother().getFadeSample(true, fadeFactor);
            sample = m_toneNco.next() * fadeFactor;
        }
        else
        {
            if (m_cwKeyer.getCWSmoother().getFadeSample(false, fadeFactor))
            {
                sample = m_toneNco.next() * fadeFactor;
            }
            else
            {
                sample = 0.0f;
                m_toneNco.setPhase(0);
            }
        }
        break;
    case AMModSettings::AMModInputNone:
    default:
        sample = 0.0f;
        break;
    }
}

void AMMod::calculateLevel(Real& sample)
{
    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), sample);
        m_levelSum += sample * sample;
        m_levelCalcCount++;
    }
    else
    {
        qreal rmsLevel = sqrt(m_levelSum / m_levelNbSamples);
        //qDebug("NFMMod::calculateLevel: %f %f", rmsLevel, m_peakLevel);
        emit levelChanged(rmsLevel, m_peakLevel, m_levelNbSamples);
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

void AMMod::start()
{
	qDebug() << "AMMod::start: m_outputSampleRate: " << m_outputSampleRate
			<< " m_inputFrequencyOffset: " << m_settings.m_inputFrequencyOffset;

	m_audioFifo.clear();
	applyChannelSettings(m_basebandSampleRate, m_outputSampleRate, m_inputFrequencyOffset, true);
}

void AMMod::stop()
{
}

bool AMMod::handleMessage(const Message& cmd)
{
	if (UpChannelizer::MsgChannelizerNotification::match(cmd))
	{
		UpChannelizer::MsgChannelizerNotification& notif = (UpChannelizer::MsgChannelizerNotification&) cmd;
		qDebug() << "AMMod::handleMessage: MsgChannelizerNotification:"
				<< " basebandSampleRate: " << notif.getBasebandSampleRate()
                << " outputSampleRate: " << notif.getSampleRate()
				<< " inputFrequencyOffset: " << notif.getFrequencyOffset();

		applyChannelSettings(notif.getBasebandSampleRate(), notif.getSampleRate(), notif.getFrequencyOffset());

		return true;
	}
    else if (MsgConfigureChannelizer::match(cmd))
    {
        MsgConfigureChannelizer& cfg = (MsgConfigureChannelizer&) cmd;
        qDebug() << "AMMod::handleMessage: MsgConfigureChannelizer:"
                << " getSampleRate: " << cfg.getSampleRate()
                << " getCenterFrequency: " << cfg.getCenterFrequency();

        m_channelizer->configure(m_channelizer->getInputMessageQueue(),
            cfg.getSampleRate(),
            cfg.getCenterFrequency());

        return true;
    }
    else if (MsgConfigureAMMod::match(cmd))
    {
        MsgConfigureAMMod& cfg = (MsgConfigureAMMod&) cmd;
        qDebug() << "AMMod::handleMessage: MsgConfigureAMMod";

        applySettings(cfg.getSettings(), cfg.getForce());

        return true;
    }
	else if (MsgConfigureFileSourceName::match(cmd))
    {
        MsgConfigureFileSourceName& conf = (MsgConfigureFileSourceName&) cmd;
        m_fileName = conf.getFileName();
        openFileStream();
        return true;
    }
    else if (MsgConfigureFileSourceSeek::match(cmd))
    {
        MsgConfigureFileSourceSeek& conf = (MsgConfigureFileSourceSeek&) cmd;
        int seekPercentage = conf.getPercentage();
        seekFileStream(seekPercentage);

        return true;
    }
    else if (MsgConfigureFileSourceStreamTiming::match(cmd))
    {
    	std::size_t samplesCount;

    	if (m_ifstream.eof()) {
    		samplesCount = m_fileSize / sizeof(Real);
    	} else {
    		samplesCount = m_ifstream.tellg() / sizeof(Real);
    	}

    	MsgReportFileSourceStreamTiming *report;
        report = MsgReportFileSourceStreamTiming::create(samplesCount);
        getMessageQueueToGUI()->push(report);

        return true;
    }
    else if (DSPConfigureAudio::match(cmd))
    {
        DSPConfigureAudio& cfg = (DSPConfigureAudio&) cmd;
        uint32_t sampleRate = cfg.getSampleRate();

        qDebug() << "AMMod::handleMessage: DSPConfigureAudio:"
                << " sampleRate: " << sampleRate;

        if (sampleRate != m_audioSampleRate) {
            applyAudioSampleRate(sampleRate);
        }

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        return true;
    }
	else
	{
		return false;
	}
}

void AMMod::openFileStream()
{
    if (m_ifstream.is_open()) {
        m_ifstream.close();
    }

    m_ifstream.open(m_fileName.toStdString().c_str(), std::ios::binary | std::ios::ate);
    m_fileSize = m_ifstream.tellg();
    m_ifstream.seekg(0,std::ios_base::beg);

    m_sampleRate = 48000; // fixed rate
    m_recordLength = m_fileSize / (sizeof(Real) * m_sampleRate);

    qDebug() << "AMMod::openFileStream: " << m_fileName.toStdString().c_str()
            << " fileSize: " << m_fileSize << "bytes"
            << " length: " << m_recordLength << " seconds";

    MsgReportFileSourceStreamData *report;
    report = MsgReportFileSourceStreamData::create(m_sampleRate, m_recordLength);
    getMessageQueueToGUI()->push(report);
}

void AMMod::seekFileStream(int seekPercentage)
{
    QMutexLocker mutexLocker(&m_settingsMutex);

    if (m_ifstream.is_open())
    {
        int seekPoint = ((m_recordLength * seekPercentage) / 100) * m_sampleRate;
        seekPoint *= sizeof(Real);
        m_ifstream.clear();
        m_ifstream.seekg(seekPoint, std::ios::beg);
    }
}

void AMMod::applyAudioSampleRate(int sampleRate)
{
    qDebug("AMMod::applyAudioSampleRate: %d", sampleRate);

    MsgConfigureChannelizer* channelConfigMsg = MsgConfigureChannelizer::create(
            sampleRate, m_settings.m_inputFrequencyOffset);
    m_inputMessageQueue.push(channelConfigMsg);

    m_settingsMutex.lock();

    m_interpolatorDistanceRemain = 0;
    m_interpolatorConsumed = false;
    m_interpolatorDistance = (Real) sampleRate / (Real) m_outputSampleRate;
    m_interpolator.create(48, sampleRate, m_settings.m_rfBandwidth / 2.2, 3.0);
    m_toneNco.setFreq(m_settings.m_toneFrequency, sampleRate);
    m_cwKeyer.setSampleRate(sampleRate);

    m_settingsMutex.unlock();

    m_audioSampleRate = sampleRate;
}

void AMMod::applyChannelSettings(int basebandSampleRate, int outputSampleRate, int inputFrequencyOffset, bool force)
{
    qDebug() << "AMMod::applyChannelSettings:"
            << " basebandSampleRate: " << basebandSampleRate
            << " outputSampleRate: " << outputSampleRate
            << " inputFrequencyOffset: " << inputFrequencyOffset;

    if ((inputFrequencyOffset != m_inputFrequencyOffset) ||
        (outputSampleRate != m_outputSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_carrierNco.setFreq(inputFrequencyOffset, outputSampleRate);
        m_settingsMutex.unlock();
    }

    if ((outputSampleRate != m_outputSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_interpolatorDistanceRemain = 0;
        m_interpolatorConsumed = false;
        m_interpolatorDistance = (Real) m_audioSampleRate / (Real) outputSampleRate;
        m_interpolator.create(48, m_audioSampleRate, m_settings.m_rfBandwidth / 2.2, 3.0);
        m_settingsMutex.unlock();
    }

    m_basebandSampleRate = basebandSampleRate;
    m_outputSampleRate = outputSampleRate;
    m_inputFrequencyOffset = inputFrequencyOffset;
}

void AMMod::applySettings(const AMModSettings& settings, bool force)
{
    qDebug() << "AMMod::applySettings:"
            << " m_inputFrequencyOffset: " << settings.m_inputFrequencyOffset
            << " m_rfBandwidth: " << settings.m_rfBandwidth
            << " m_modFactor: " << settings.m_modFactor
            << " m_toneFrequency: " << settings.m_toneFrequency
            << " m_volumeFactor: " << settings.m_volumeFactor
            << " m_audioMute: " << settings.m_channelMute
            << " m_playLoop: " << settings.m_playLoop
            << " m_modAFInput " << settings.m_modAFInput
            << " m_audioDeviceName: " << settings.m_audioDeviceName
            << " force: " << force;

    if((settings.m_rfBandwidth != m_settings.m_rfBandwidth) || force)
    {
        m_settingsMutex.lock();
        m_interpolatorDistanceRemain = 0;
        m_interpolatorConsumed = false;
        m_interpolatorDistance = (Real) m_audioSampleRate / (Real) m_outputSampleRate;
        m_interpolator.create(48, m_audioSampleRate, settings.m_rfBandwidth / 2.2, 3.0);
        m_settingsMutex.unlock();
    }

    if ((settings.m_toneFrequency != m_settings.m_toneFrequency) || force)
    {
        m_settingsMutex.lock();
        m_toneNco.setFreq(settings.m_toneFrequency, m_audioSampleRate);
        m_settingsMutex.unlock();
    }

    if ((settings.m_audioDeviceName != m_settings.m_audioDeviceName) || force)
    {
        AudioDeviceManager *audioDeviceManager = DSPEngine::instance()->getAudioDeviceManager();
        int audioDeviceIndex = audioDeviceManager->getInputDeviceIndex(settings.m_audioDeviceName);
        audioDeviceManager->addAudioSource(&m_audioFifo, getInputMessageQueue(), audioDeviceIndex);
        uint32_t audioSampleRate = audioDeviceManager->getInputSampleRate(audioDeviceIndex);

        if (m_audioSampleRate != audioSampleRate) {
            applyAudioSampleRate(audioSampleRate);
        }
    }

    m_settings = settings;
}

QByteArray AMMod::serialize() const
{
    return m_settings.serialize();
}

bool AMMod::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        MsgConfigureAMMod *msg = MsgConfigureAMMod::create(m_settings, true);
        m_inputMessageQueue.push(msg);
        return true;
    }
    else
    {
        m_settings.resetToDefaults();
        MsgConfigureAMMod *msg = MsgConfigureAMMod::create(m_settings, true);
        m_inputMessageQueue.push(msg);
        return false;
    }
}

int AMMod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage __attribute__((unused)))
{
    response.setAmModSettings(new SWGSDRangel::SWGAMModSettings());
    response.getAmModSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int AMMod::webapiSettingsPutPatch(
                bool force,
                const QStringList& channelSettingsKeys,
                SWGSDRangel::SWGChannelSettings& response,
                QString& errorMessage __attribute__((unused)))
{
    AMModSettings settings = m_settings;
    bool frequencyOffsetChanged = false;

    if (channelSettingsKeys.contains("channelMute")) {
        settings.m_channelMute = response.getAmModSettings()->getChannelMute() != 0;
    }
    if (channelSettingsKeys.contains("inputFrequencyOffset"))
    {
        settings.m_inputFrequencyOffset = response.getAmModSettings()->getInputFrequencyOffset();
        frequencyOffsetChanged = true;
    }
    if (channelSettingsKeys.contains("modAFInput")) {
        settings.m_modAFInput = (AMModSettings::AMModInputAF) response.getAmModSettings()->getModAfInput();
    }
    if (channelSettingsKeys.contains("audioDeviceName")) {
        settings.m_audioDeviceName = *response.getAmModSettings()->getAudioDeviceName();
    }
    if (channelSettingsKeys.contains("playLoop")) {
        settings.m_playLoop = response.getAmModSettings()->getPlayLoop() != 0;
    }
    if (channelSettingsKeys.contains("rfBandwidth")) {
        settings.m_rfBandwidth = response.getAmModSettings()->getRfBandwidth();
    }
    if (channelSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = response.getAmModSettings()->getRgbColor();
    }
    if (channelSettingsKeys.contains("title")) {
        settings.m_title = *response.getAmModSettings()->getTitle();
    }
    if (channelSettingsKeys.contains("toneFrequency")) {
        settings.m_toneFrequency = response.getAmModSettings()->getToneFrequency();
    }
    if (channelSettingsKeys.contains("volumeFactor")) {
        settings.m_volumeFactor = response.getAmModSettings()->getVolumeFactor();
    }
    if (channelSettingsKeys.contains("modFactor")) {
        settings.m_modFactor = response.getAmModSettings()->getModFactor();
    }

    if (channelSettingsKeys.contains("cwKeyer"))
    {
        SWGSDRangel::SWGCWKeyerSettings *apiCwKeyerSettings = response.getAmModSettings()->getCwKeyer();
        CWKeyerSettings cwKeyerSettings = m_cwKeyer.getSettings();

        if (channelSettingsKeys.contains("cwKeyer.loop")) {
            cwKeyerSettings.m_loop = apiCwKeyerSettings->getLoop() != 0;
        }
        if (channelSettingsKeys.contains("cwKeyer.mode")) {
            cwKeyerSettings.m_mode = (CWKeyerSettings::CWMode) apiCwKeyerSettings->getMode();
        }
        if (channelSettingsKeys.contains("cwKeyer.text")) {
            cwKeyerSettings.m_text = *apiCwKeyerSettings->getText();
        }
        if (channelSettingsKeys.contains("cwKeyer.sampleRate")) {
            cwKeyerSettings.m_sampleRate = apiCwKeyerSettings->getSampleRate();
        }
        if (channelSettingsKeys.contains("cwKeyer.wpm")) {
            cwKeyerSettings.m_wpm = apiCwKeyerSettings->getWpm();
        }

        m_cwKeyer.setLoop(cwKeyerSettings.m_loop);
        m_cwKeyer.setMode(cwKeyerSettings.m_mode);
        m_cwKeyer.setSampleRate(cwKeyerSettings.m_sampleRate);
        m_cwKeyer.setText(cwKeyerSettings.m_text);
        m_cwKeyer.setWPM(cwKeyerSettings.m_wpm);

        if (m_guiMessageQueue) // forward to GUI if any
        {
            CWKeyer::MsgConfigureCWKeyer *msgCwKeyer = CWKeyer::MsgConfigureCWKeyer::create(cwKeyerSettings, force);
            m_guiMessageQueue->push(msgCwKeyer);
        }
    }

    if (frequencyOffsetChanged)
    {
        AMMod::MsgConfigureChannelizer *msgChan = AMMod::MsgConfigureChannelizer::create(
                m_audioSampleRate, settings.m_inputFrequencyOffset);
        m_inputMessageQueue.push(msgChan);
    }

    MsgConfigureAMMod *msg = MsgConfigureAMMod::create(settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureAMMod *msgToGUI = MsgConfigureAMMod::create(settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

int AMMod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage __attribute__((unused)))
{
    response.setAmModReport(new SWGSDRangel::SWGAMModReport());
    response.getAmModReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void AMMod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const AMModSettings& settings)
{
    response.getAmModSettings()->setChannelMute(settings.m_channelMute ? 1 : 0);
    response.getAmModSettings()->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    response.getAmModSettings()->setModAfInput((int) settings.m_modAFInput);
    response.getAmModSettings()->setPlayLoop(settings.m_playLoop ? 1 : 0);
    response.getAmModSettings()->setRfBandwidth(settings.m_rfBandwidth);
    response.getAmModSettings()->setModFactor(settings.m_modFactor);
    response.getAmModSettings()->setRgbColor(settings.m_rgbColor);

    if (response.getAmModSettings()->getTitle()) {
        *response.getAmModSettings()->getTitle() = settings.m_title;
    } else {
        response.getAmModSettings()->setTitle(new QString(settings.m_title));
    }

    response.getAmModSettings()->setToneFrequency(settings.m_toneFrequency);
    response.getAmModSettings()->setVolumeFactor(settings.m_volumeFactor);

    if (!response.getAmModSettings()->getCwKeyer()) {
        response.getAmModSettings()->setCwKeyer(new SWGSDRangel::SWGCWKeyerSettings);
    }

    SWGSDRangel::SWGCWKeyerSettings *apiCwKeyerSettings = response.getAmModSettings()->getCwKeyer();
    const CWKeyerSettings& cwKeyerSettings = m_cwKeyer.getSettings();
    apiCwKeyerSettings->setLoop(cwKeyerSettings.m_loop ? 1 : 0);
    apiCwKeyerSettings->setMode((int) cwKeyerSettings.m_mode);
    apiCwKeyerSettings->setSampleRate(cwKeyerSettings.m_sampleRate);

    if (apiCwKeyerSettings->getText()) {
        *apiCwKeyerSettings->getText() = cwKeyerSettings.m_text;
    } else {
        apiCwKeyerSettings->setText(new QString(cwKeyerSettings.m_text));
    }

    apiCwKeyerSettings->setWpm(cwKeyerSettings.m_wpm);

    if (response.getAmModSettings()->getAudioDeviceName()) {
        *response.getAmModSettings()->getAudioDeviceName() = settings.m_audioDeviceName;
    } else {
        response.getAmModSettings()->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }
}

void AMMod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    response.getAmModReport()->setChannelPowerDb(CalcDb::dbPower(getMagSq()));
    response.getAmModReport()->setAudioSampleRate(m_audioSampleRate);
    response.getAmModReport()->setChannelSampleRate(m_outputSampleRate);
}
