///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015 F4EXB                                                      //
// written by Edouard Griffiths                                                  //
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

#ifndef INCLUDE_BFMDEMOD_H
#define INCLUDE_BFMDEMOD_H

#include <QMutex>
#include <vector>

#include "dsp/basebandsamplesink.h"
#include "channel/channelsinkapi.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/lowpass.h"
#include "dsp/movingaverage.h"
#include "dsp/fftfilt.h"
#include "dsp/phaselock.h"
#include "dsp/filterrc.h"
#include "dsp/phasediscri.h"
#include "audio/audiofifo.h"
#include "util/message.h"

#include "rdsparser.h"
#include "rdsdecoder.h"
#include "rdsdemod.h"
#include "bfmdemodsettings.h"

class DeviceSourceAPI;
class ThreadedBasebandSampleSink;
class DownChannelizer;

namespace SWGSDRangel {
    class SWGRDSReport;
}

class BFMDemod : public BasebandSampleSink, public ChannelSinkAPI {
public:
    class MsgConfigureBFMDemod : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const BFMDemodSettings& getSettings() const { return m_settings; }
        bool getForce() const { return m_force; }

        static MsgConfigureBFMDemod* create(const BFMDemodSettings& settings, bool force)
        {
            return new MsgConfigureBFMDemod(settings, force);
        }

    private:
        BFMDemodSettings m_settings;
        bool m_force;

        MsgConfigureBFMDemod(const BFMDemodSettings& settings, bool force) :
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

    class MsgReportChannelSampleRateChanged : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getSampleRate() const { return m_sampleRate; }

        static MsgReportChannelSampleRateChanged* create(int sampleRate)
        {
            return new MsgReportChannelSampleRateChanged(sampleRate);
        }

    private:
        int m_sampleRate;

        MsgReportChannelSampleRateChanged(int sampleRate) :
            Message(),
            m_sampleRate(sampleRate)
        { }
    };

	BFMDemod(DeviceSourceAPI *deviceAPI);
	virtual ~BFMDemod();
	virtual void destroy() { delete this; }
	void setSampleSink(BasebandSampleSink* sampleSink) { m_sampleSink = sampleSink; }

	int getSampleRate() const { return m_inputSampleRate; }
	virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool po);
	virtual void start();
	virtual void stop();
	virtual bool handleMessage(const Message& cmd);

    virtual void getIdentifier(QString& id) { id = objectName(); }
    virtual void getTitle(QString& title) { title = m_settings.m_title; }
    virtual qint64 getCenterFrequency() const { return m_settings.m_inputFrequencyOffset; }

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray& data);

	double getMagSq() const { return m_magsq; }

	bool getPilotLock() const { return m_pilotPLL.locked(); }
	Real getPilotLevel() const { return m_pilotPLL.get_pilot_level(); }

	Real getDecoderQua() const { return m_rdsDecoder.m_qua; }
	bool getDecoderSynced() const { return m_rdsDecoder.synced(); }
	Real getDemodAcc() const { return m_rdsDemod.m_report.acc; }
	Real getDemodQua() const { return m_rdsDemod.m_report.qua; }
	Real getDemodFclk() const { return m_rdsDemod.m_report.fclk; }

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

    RDSParser& getRDSParser() { return m_rdsParser; }

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

	DeviceSourceAPI *m_deviceAPI;
    ThreadedBasebandSampleSink* m_threadedChannelizer;
    DownChannelizer* m_channelizer;

    int m_inputSampleRate;
    int m_inputFrequencyOffset;
    BFMDemodSettings m_settings;
    quint32 m_audioSampleRate;

	NCO m_nco;
	Interpolator m_interpolator; //!< Interpolator between fixed demod bandwidth and audio bandwidth (rational)
	Real m_interpolatorDistance;
	Real m_interpolatorDistanceRemain;

	Interpolator m_interpolatorStereo; //!< Twin Interpolator for stereo subcarrier
	Real m_interpolatorStereoDistance;
	Real m_interpolatorStereoDistanceRemain;

	Interpolator m_interpolatorRDS; //!< Twin Interpolator for stereo subcarrier
	Real m_interpolatorRDSDistance;
	Real m_interpolatorRDSDistanceRemain;

	Lowpass<Real> m_lowpass;
	fftfilt* m_rfFilter;
	static const int filtFftLen = 1024;

	Real m_squelchLevel;
	int m_squelchState;

	Real m_m1Arg; //!> x^-1 real sample

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int    m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;

	AudioVector m_audioBuffer;
	uint m_audioBufferFill;

	BasebandSampleSink* m_sampleSink;
	AudioFifo m_audioFifo;
	SampleVector m_sampleBuffer;
	QMutex m_settingsMutex;

	RDSPhaseLock m_pilotPLL;
	Real m_pilotPLLSamples[4];

	RDSDemod m_rdsDemod;
	RDSDecoder m_rdsDecoder;
	RDSParser m_rdsParser;

	LowPassFilterRC m_deemphasisFilterX;
	LowPassFilterRC m_deemphasisFilterY;
    static const Real default_deemphasis;

	Real m_fmExcursion;
	static const int default_excursion = 750000; // +/- 75 kHz

	PhaseDiscriminators m_phaseDiscri;

    static const int m_udpBlockSize;

	void applyAudioSampleRate(int sampleRate);
    void applyChannelSettings(int inputSampleRate, int inputFrequencyOffset, bool force = false);
	void applySettings(const BFMDemodSettings& settings, bool force = false);

    void webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const BFMDemodSettings& settings);
    void webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response);
    void webapiFormatRDSReport(SWGSDRangel::SWGRDSReport *report);
};

#endif // INCLUDE_BFMDEMOD_H
