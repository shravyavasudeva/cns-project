/*
 * detector.h - Layer 1/2 jamming detector (one per vehicle / RSU).
 *
 * Measurements over a sliding window (default 0.5 s, evaluated every 0.1 s):
 *   busy      : channel-busy ratio (PHY in RX or CCA_BUSY), sampled every 2 ms
 *   ccaBusy   : share of time in CCA_BUSY only (energy that is not a frame being received)
 *   okRate    : frames decoded per second
 *   errRatio  : detected frames that failed to decode / all detected frames
 *   pdr       : beacons received / beacons expected from neighbours predicted in range
 *   noise     : noise+interference level (dBm) seen on decoded frames (signal - SINR)
 *   rssi      : average signal strength of decoded frames
 *
 * Classification (thresholds are command-line flags, see common.h):
 *   JAMMED     energy flood (busy >= edBusyThr and almost nothing decodes), or
 *              non-frame energy (ccaBusy >= ccaJamThr) with elevated noise, or
 *              energy present but nothing decodes, or
 *              low PDR + elevated noise, or low PDR + many failed decodes on a quiet channel
 *   CONGESTED  busy >= congBusyThr but not jammed (frames still decode)
 *   WEAK       low PDR, channel quiet, noise normal  (just far away / weak signal)
 *   NORMAL     everything else
 * Hysteresis: enterCount bad evaluations to raise JAMMED, exitCount good ones to clear.
 *
 * Link level: a neighbour predicted close (< 0.75 * linkRange) whose beacons stopped
 * arriving is marked as a jammed link (used to suspect jamming near *other* vehicles).
 */
#ifndef V2V_DETECTOR_H
#define V2V_DETECTOR_H

#include "common.h"
#include "neighbor-table.h"

#include "ns3/wifi-module.h"

#include <set>

namespace ns3
{
namespace v2v
{

enum DetState : uint8_t
{
    DET_NORMAL = 0,
    DET_CONGESTED = 1,
    DET_WEAK = 2,
    DET_JAMMED = 3
};

const char* DetStateName(uint8_t s);

struct DetMeasures
{
    double busy = 0;      // RX or CCA_BUSY
    double ccaBusy = 0;   // CCA_BUSY only: energy that is NOT a frame we are receiving
    double okRate = 0;
    double errRatio = 0;
    double pdr = -1; // -1 = unknown (no neighbours expected)
    double noiseDbm = -200;
    double rssiDbm = -200;
    bool hasSignal = false; // at least one decoded frame -> noise/rssi valid
    uint32_t linkJam = 0;
};

class Detector
{
  public:
    Detector(uint32_t self, Ptr<WifiPhy> phy, NeighborTable* nt);
    void Start();

    DetState GetState() const { return m_state; }
    bool LocalJammed() const { return m_state == DET_JAMMED; }
    /** Local jamming, or enough jammed links, or a neighbour reporting jamming. */
    bool JamSuspected() const;
    /** Rough position of the jammed area (average of the evidence). */
    bool JamCenter(Vector& out) const;
    bool IsLinkJammed(uint32_t nbr) const { return m_linkJam.count(nbr) > 0; }
    double LastRssi() const { return m_lastRssi; }
    const DetMeasures& Last() const { return m_last; }

    /** Called every 0.1 s by the ground-truth monitor (v2v-jam.cc). */
    void ScoreSample(bool gtJammed);
    /** One CSV row fragment: tp,fn,fp,tn,first_gt_ms,first_detect_ms,n_normal,n_congested,n_weak,n_jammed */
    std::string SummaryCsv() const;

  private:
    struct Bin
    {
        uint32_t busy = 0;
        uint32_t cca = 0;
        uint32_t total = 0;
        uint32_t ok = 0;
        uint32_t err = 0;
        uint32_t sig = 0;
        double rssiSum = 0;
        double noiseSum = 0;
    };

    void SampleChannel();
    void Evaluate();
    DetMeasures Measure();
    DetState Classify(const DetMeasures& m) const;

    void OnRxOk(Ptr<const Packet> p, double snr, WifiMode mode, WifiPreamble preamble);
    void OnRxError(Ptr<const Packet> p, double snr);
    void OnSniff(Ptr<const Packet> p,
                 uint16_t freqMhz,
                 WifiTxVector txv,
                 MpduInfo mpdu,
                 SignalNoiseDbm sn,
                 uint16_t staId);

    uint32_t m_self;
    Ptr<WifiPhy> m_phy;
    NeighborTable* m_nt;

    std::vector<Bin> m_bins;
    size_t m_cur = 0;

    DetState m_state = DET_NORMAL;
    uint32_t m_badStreak = 0;
    uint32_t m_goodStreak = 0;
    DetMeasures m_last;
    std::set<uint32_t> m_linkJam;
    double m_lastRssi = -100;

    // accuracy bookkeeping
    uint64_t m_tp = 0, m_fn = 0, m_fp = 0, m_tn = 0;
    double m_firstGt = -1;
    double m_firstDetect = -1;
    uint64_t m_stateSamples[4] = {0, 0, 0, 0};
};

} // namespace v2v
} // namespace ns3

#endif
