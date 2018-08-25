/**
 * SDRangel
 * This is the web REST/JSON API of SDRangel SDR software. SDRangel is an Open Source Qt5/OpenGL 3.0+ (4.3+ in Windows) GUI and server Software Defined Radio and signal analyzer in software. It supports Airspy, BladeRF, HackRF, LimeSDR, PlutoSDR, RTL-SDR, SDRplay RSP1 and FunCube     ---   Limitations and specifcities:       * In SDRangel GUI the first Rx device set cannot be deleted. Conversely the server starts with no device sets and its number of device sets can be reduced to zero by as many calls as necessary to /sdrangel/deviceset with DELETE method.   * Preset import and export from/to file is a server only feature.   * Device set focus is a GUI only feature.   * The following channels are not implemented (status 501 is returned): ATV and DATV demodulators, Channel Analyzer NG, LoRa demodulator   * The device settings and report structures contains only the sub-structure corresponding to the device type. The DeviceSettings and DeviceReport structures documented here shows all of them but only one will be or should be present at a time   * The channel settings and report structures contains only the sub-structure corresponding to the channel type. The ChannelSettings and ChannelReport structures documented here shows all of them but only one will be or should be present at a time    --- 
 *
 * OpenAPI spec version: 4.0.6
 * Contact: f4exb06@gmail.com
 *
 * NOTE: This class is auto generated by the swagger code generator program.
 * https://github.com/swagger-api/swagger-codegen.git
 * Do not edit the class manually.
 */


#include "SWGHackRFInputSettings.h"

#include "SWGHelpers.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QObject>
#include <QDebug>

namespace SWGSDRangel {

SWGHackRFInputSettings::SWGHackRFInputSettings(QString* json) {
    init();
    this->fromJson(*json);
}

SWGHackRFInputSettings::SWGHackRFInputSettings() {
    center_frequency = 0L;
    m_center_frequency_isSet = false;
    l_oppm_tenths = 0;
    m_l_oppm_tenths_isSet = false;
    bandwidth = 0;
    m_bandwidth_isSet = false;
    lna_gain = 0;
    m_lna_gain_isSet = false;
    vga_gain = 0;
    m_vga_gain_isSet = false;
    log2_decim = 0;
    m_log2_decim_isSet = false;
    fc_pos = 0;
    m_fc_pos_isSet = false;
    dev_sample_rate = 0;
    m_dev_sample_rate_isSet = false;
    bias_t = 0;
    m_bias_t_isSet = false;
    lna_ext = 0;
    m_lna_ext_isSet = false;
    dc_block = 0;
    m_dc_block_isSet = false;
    iq_correction = 0;
    m_iq_correction_isSet = false;
    link_tx_frequency = 0;
    m_link_tx_frequency_isSet = false;
    file_record_name = nullptr;
    m_file_record_name_isSet = false;
}

SWGHackRFInputSettings::~SWGHackRFInputSettings() {
    this->cleanup();
}

void
SWGHackRFInputSettings::init() {
    center_frequency = 0L;
    m_center_frequency_isSet = false;
    l_oppm_tenths = 0;
    m_l_oppm_tenths_isSet = false;
    bandwidth = 0;
    m_bandwidth_isSet = false;
    lna_gain = 0;
    m_lna_gain_isSet = false;
    vga_gain = 0;
    m_vga_gain_isSet = false;
    log2_decim = 0;
    m_log2_decim_isSet = false;
    fc_pos = 0;
    m_fc_pos_isSet = false;
    dev_sample_rate = 0;
    m_dev_sample_rate_isSet = false;
    bias_t = 0;
    m_bias_t_isSet = false;
    lna_ext = 0;
    m_lna_ext_isSet = false;
    dc_block = 0;
    m_dc_block_isSet = false;
    iq_correction = 0;
    m_iq_correction_isSet = false;
    link_tx_frequency = 0;
    m_link_tx_frequency_isSet = false;
    file_record_name = new QString("");
    m_file_record_name_isSet = false;
}

void
SWGHackRFInputSettings::cleanup() {













    if(file_record_name != nullptr) { 
        delete file_record_name;
    }
}

SWGHackRFInputSettings*
SWGHackRFInputSettings::fromJson(QString &json) {
    QByteArray array (json.toStdString().c_str());
    QJsonDocument doc = QJsonDocument::fromJson(array);
    QJsonObject jsonObject = doc.object();
    this->fromJsonObject(jsonObject);
    return this;
}

void
SWGHackRFInputSettings::fromJsonObject(QJsonObject &pJson) {
    ::SWGSDRangel::setValue(&center_frequency, pJson["centerFrequency"], "qint64", "");
    
    ::SWGSDRangel::setValue(&l_oppm_tenths, pJson["LOppmTenths"], "qint32", "");
    
    ::SWGSDRangel::setValue(&bandwidth, pJson["bandwidth"], "qint32", "");
    
    ::SWGSDRangel::setValue(&lna_gain, pJson["lnaGain"], "qint32", "");
    
    ::SWGSDRangel::setValue(&vga_gain, pJson["vgaGain"], "qint32", "");
    
    ::SWGSDRangel::setValue(&log2_decim, pJson["log2Decim"], "qint32", "");
    
    ::SWGSDRangel::setValue(&fc_pos, pJson["fcPos"], "qint32", "");
    
    ::SWGSDRangel::setValue(&dev_sample_rate, pJson["devSampleRate"], "qint32", "");
    
    ::SWGSDRangel::setValue(&bias_t, pJson["biasT"], "qint32", "");
    
    ::SWGSDRangel::setValue(&lna_ext, pJson["lnaExt"], "qint32", "");
    
    ::SWGSDRangel::setValue(&dc_block, pJson["dcBlock"], "qint32", "");
    
    ::SWGSDRangel::setValue(&iq_correction, pJson["iqCorrection"], "qint32", "");
    
    ::SWGSDRangel::setValue(&link_tx_frequency, pJson["linkTxFrequency"], "qint32", "");
    
    ::SWGSDRangel::setValue(&file_record_name, pJson["fileRecordName"], "QString", "QString");
    
}

QString
SWGHackRFInputSettings::asJson ()
{
    QJsonObject* obj = this->asJsonObject();

    QJsonDocument doc(*obj);
    QByteArray bytes = doc.toJson();
    delete obj;
    return QString(bytes);
}

QJsonObject*
SWGHackRFInputSettings::asJsonObject() {
    QJsonObject* obj = new QJsonObject();
    if(m_center_frequency_isSet){
        obj->insert("centerFrequency", QJsonValue(center_frequency));
    }
    if(m_l_oppm_tenths_isSet){
        obj->insert("LOppmTenths", QJsonValue(l_oppm_tenths));
    }
    if(m_bandwidth_isSet){
        obj->insert("bandwidth", QJsonValue(bandwidth));
    }
    if(m_lna_gain_isSet){
        obj->insert("lnaGain", QJsonValue(lna_gain));
    }
    if(m_vga_gain_isSet){
        obj->insert("vgaGain", QJsonValue(vga_gain));
    }
    if(m_log2_decim_isSet){
        obj->insert("log2Decim", QJsonValue(log2_decim));
    }
    if(m_fc_pos_isSet){
        obj->insert("fcPos", QJsonValue(fc_pos));
    }
    if(m_dev_sample_rate_isSet){
        obj->insert("devSampleRate", QJsonValue(dev_sample_rate));
    }
    if(m_bias_t_isSet){
        obj->insert("biasT", QJsonValue(bias_t));
    }
    if(m_lna_ext_isSet){
        obj->insert("lnaExt", QJsonValue(lna_ext));
    }
    if(m_dc_block_isSet){
        obj->insert("dcBlock", QJsonValue(dc_block));
    }
    if(m_iq_correction_isSet){
        obj->insert("iqCorrection", QJsonValue(iq_correction));
    }
    if(m_link_tx_frequency_isSet){
        obj->insert("linkTxFrequency", QJsonValue(link_tx_frequency));
    }
    if(file_record_name != nullptr && *file_record_name != QString("")){
        toJsonValue(QString("fileRecordName"), file_record_name, obj, QString("QString"));
    }

    return obj;
}

qint64
SWGHackRFInputSettings::getCenterFrequency() {
    return center_frequency;
}
void
SWGHackRFInputSettings::setCenterFrequency(qint64 center_frequency) {
    this->center_frequency = center_frequency;
    this->m_center_frequency_isSet = true;
}

qint32
SWGHackRFInputSettings::getLOppmTenths() {
    return l_oppm_tenths;
}
void
SWGHackRFInputSettings::setLOppmTenths(qint32 l_oppm_tenths) {
    this->l_oppm_tenths = l_oppm_tenths;
    this->m_l_oppm_tenths_isSet = true;
}

qint32
SWGHackRFInputSettings::getBandwidth() {
    return bandwidth;
}
void
SWGHackRFInputSettings::setBandwidth(qint32 bandwidth) {
    this->bandwidth = bandwidth;
    this->m_bandwidth_isSet = true;
}

qint32
SWGHackRFInputSettings::getLnaGain() {
    return lna_gain;
}
void
SWGHackRFInputSettings::setLnaGain(qint32 lna_gain) {
    this->lna_gain = lna_gain;
    this->m_lna_gain_isSet = true;
}

qint32
SWGHackRFInputSettings::getVgaGain() {
    return vga_gain;
}
void
SWGHackRFInputSettings::setVgaGain(qint32 vga_gain) {
    this->vga_gain = vga_gain;
    this->m_vga_gain_isSet = true;
}

qint32
SWGHackRFInputSettings::getLog2Decim() {
    return log2_decim;
}
void
SWGHackRFInputSettings::setLog2Decim(qint32 log2_decim) {
    this->log2_decim = log2_decim;
    this->m_log2_decim_isSet = true;
}

qint32
SWGHackRFInputSettings::getFcPos() {
    return fc_pos;
}
void
SWGHackRFInputSettings::setFcPos(qint32 fc_pos) {
    this->fc_pos = fc_pos;
    this->m_fc_pos_isSet = true;
}

qint32
SWGHackRFInputSettings::getDevSampleRate() {
    return dev_sample_rate;
}
void
SWGHackRFInputSettings::setDevSampleRate(qint32 dev_sample_rate) {
    this->dev_sample_rate = dev_sample_rate;
    this->m_dev_sample_rate_isSet = true;
}

qint32
SWGHackRFInputSettings::getBiasT() {
    return bias_t;
}
void
SWGHackRFInputSettings::setBiasT(qint32 bias_t) {
    this->bias_t = bias_t;
    this->m_bias_t_isSet = true;
}

qint32
SWGHackRFInputSettings::getLnaExt() {
    return lna_ext;
}
void
SWGHackRFInputSettings::setLnaExt(qint32 lna_ext) {
    this->lna_ext = lna_ext;
    this->m_lna_ext_isSet = true;
}

qint32
SWGHackRFInputSettings::getDcBlock() {
    return dc_block;
}
void
SWGHackRFInputSettings::setDcBlock(qint32 dc_block) {
    this->dc_block = dc_block;
    this->m_dc_block_isSet = true;
}

qint32
SWGHackRFInputSettings::getIqCorrection() {
    return iq_correction;
}
void
SWGHackRFInputSettings::setIqCorrection(qint32 iq_correction) {
    this->iq_correction = iq_correction;
    this->m_iq_correction_isSet = true;
}

qint32
SWGHackRFInputSettings::getLinkTxFrequency() {
    return link_tx_frequency;
}
void
SWGHackRFInputSettings::setLinkTxFrequency(qint32 link_tx_frequency) {
    this->link_tx_frequency = link_tx_frequency;
    this->m_link_tx_frequency_isSet = true;
}

QString*
SWGHackRFInputSettings::getFileRecordName() {
    return file_record_name;
}
void
SWGHackRFInputSettings::setFileRecordName(QString* file_record_name) {
    this->file_record_name = file_record_name;
    this->m_file_record_name_isSet = true;
}


bool
SWGHackRFInputSettings::isSet(){
    bool isObjectUpdated = false;
    do{
        if(m_center_frequency_isSet){ isObjectUpdated = true; break;}
        if(m_l_oppm_tenths_isSet){ isObjectUpdated = true; break;}
        if(m_bandwidth_isSet){ isObjectUpdated = true; break;}
        if(m_lna_gain_isSet){ isObjectUpdated = true; break;}
        if(m_vga_gain_isSet){ isObjectUpdated = true; break;}
        if(m_log2_decim_isSet){ isObjectUpdated = true; break;}
        if(m_fc_pos_isSet){ isObjectUpdated = true; break;}
        if(m_dev_sample_rate_isSet){ isObjectUpdated = true; break;}
        if(m_bias_t_isSet){ isObjectUpdated = true; break;}
        if(m_lna_ext_isSet){ isObjectUpdated = true; break;}
        if(m_dc_block_isSet){ isObjectUpdated = true; break;}
        if(m_iq_correction_isSet){ isObjectUpdated = true; break;}
        if(m_link_tx_frequency_isSet){ isObjectUpdated = true; break;}
        if(file_record_name != nullptr && *file_record_name != QString("")){ isObjectUpdated = true; break;}
    }while(false);
    return isObjectUpdated;
}
}

