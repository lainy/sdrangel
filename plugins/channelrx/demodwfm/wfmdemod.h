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

#ifndef INCLUDE_WFMDEMOD_H
#define INCLUDE_WFMDEMOD_H

#include <QMutex>
#include <vector>

#include "dsp/basebandsamplesink.h"
#include "channel/channelsinkapi.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/lowpass.h"
#include "util/movingaverage.h"
#include "dsp/fftfilt.h"
#include "dsp/phasediscri.h"
#include "audio/audiofifo.h"
#include "util/message.h"

#include "wfmdemodsettings.h"

#define rfFilterFftLength 1024

class ThreadedBasebandSampleSink;
class DownChannelizer;
class DeviceSourceAPI;

class WFMDemod : public BasebandSampleSink, public ChannelSinkAPI {
public:
    class MsgConfigureWFMDemod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const WFMDemodSettings& getSettings() const { return m_settings; }
        bool getForce() const { return m_force; }

        static MsgConfigureWFMDemod* create(const WFMDemodSettings& settings, bool force)
        {
            return new MsgConfigureWFMDemod(settings, force);
        }

    private:
        WFMDemodSettings m_settings;
        bool m_force;

        MsgConfigureWFMDemod(const WFMDemodSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_force(force)
        { }
    };

    class MsgConfigureChannelizer : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getSampleRate() const { return m_sampleRate; }
        int getCenterFrequency() const { return m_centerFrequency; }

        static MsgConfigureChannelizer* create(int sampleRate, int centerFrequency)
        {
            return new MsgConfigureChannelizer(sampleRate, centerFrequency);
        }

    private:
        int m_sampleRate;
        int m_centerFrequency;

        MsgConfigureChannelizer(int sampleRate, int centerFrequency) :
            Message(),
            m_sampleRate(sampleRate),
            m_centerFrequency(centerFrequency)
        { }
    };

	WFMDemod(DeviceSourceAPI *deviceAPI);
	virtual ~WFMDemod();
	virtual void destroy() { delete this; }

	virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool po);
	virtual void start();
	virtual void stop();
	virtual bool handleMessage(const Message& cmd);

    virtual void getIdentifier(QString& id) { id = objectName(); }
    virtual void getTitle(QString& title) { title = m_settings.m_title; }
    virtual qint64 getCenterFrequency() const { return m_settings.m_inputFrequencyOffset; }

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray& data);

	double getMagSq() const { return m_movingAverage.asDouble(); }
    bool getSquelchOpen() const { return m_squelchOpen; }

    void getMagSqLevels(double& avg, double& peak, int& nbSamples)
    {
        if (m_magsqCount > 0)
        {
            m_magsq = m_magsqSum / m_magsqCount;
            m_magSqLevelStore.m_magsq = m_magsq;
            m_magSqLevelStore.m_magsqPeak = m_magsqPeak;
        }

        avg = m_magSqLevelStore.m_magsq;
        peak = m_magSqLevelStore.m_magsqPeak;
        nbSamples = m_magsqCount == 0 ? 1 : m_magsqCount;

        m_magsqSum = 0.0f;
        m_magsqPeak = 0.0f;
        m_magsqCount = 0;
    }

    virtual int webapiSettingsGet(
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    virtual int webapiSettingsPutPatch(
            bool force,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    virtual int webapiReportGet(
            SWGSDRangel::SWGChannelReport& response,
            QString& errorMessage);

    static int requiredBW(int rfBW)
    {
        if (rfBW <= 48000) {
            return 48000;
        } else {
            return (3*rfBW)/2;
        }
    }

    static const QString m_channelIdURI;
    static const QString m_channelId;

private:
    struct MagSqLevelsStore
    {
        MagSqLevelsStore() :
            m_magsq(1e-12),
            m_magsqPeak(1e-12)
        {}
        double m_magsq;
        double m_magsqPeak;
    };

	enum RateState {
		RSInitialFill,
		RSRunning
	};

    DeviceSourceAPI* m_deviceAPI;
    ThreadedBasebandSampleSink* m_threadedChannelizer;
    DownChannelizer* m_channelizer;

    int m_inputSampleRate;
    int m_inputFrequencyOffset;
    WFMDemodSettings m_settings;
    quint32 m_audioSampleRate;

	NCO m_nco;
	Interpolator m_interpolator; //!< Interpolator between sample rate sent from DSP engine and requested RF bandwidth (rational)
	Real m_interpolatorDistance;
	Real m_interpolatorDistanceRemain;
	fftfilt* m_rfFilter;

	Real m_squelchLevel;
	int m_squelchState;
    bool m_squelchOpen;
    double m_magsq; //!< displayed averaged value
    double m_magsqSum;
    double m_magsqPeak;
    int  m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;

	MovingAverageUtil<Real, double, 16> m_movingAverage;
	Real m_fmExcursion;

	AudioVector m_audioBuffer;
	uint m_audioBufferFill;

	AudioFifo m_audioFifo;
	SampleVector m_sampleBuffer;
	QMutex m_settingsMutex;

	PhaseDiscriminators m_phaseDiscri;

    static const int m_udpBlockSize;

    void applyAudioSampleRate(int sampleRate);
    void applyChannelSettings(int inputSampleRate, int inputFrequencyOffset, bool force = false);
    void applySettings(const WFMDemodSettings& settings, bool force = false);

    void webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const WFMDemodSettings& settings);
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response);
};

#endif // INCLUDE_WFMDEMOD_H
