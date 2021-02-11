/*
 * beatutils.cpp
 *
 *  Created on: 30/nov/2011
 *      Author: vittorio
 */

#include <algorithm>
#include <QtDebug>
#include <QString>
#include <QList>
#include <QMap>

#include "track/beatutils.h"
#include "util/math.h"

namespace {

// When ironing the grid for long sequences of const tempo we use
// a 25 ms tolerance because this small of a difference is inaudible
// This is > 2 * 12 ms, the step width of the QM beat detector
constexpr double kMaxSecsPhaseError = 0.025;
// This is set to avoid to use a constant region during an offset shift.
// That happens for instance when the beat instrument changes.
constexpr double kMaxSecsPhaseErrorSum = 0.1;
constexpr int kMaxOutlierCount = 1;
constexpr int kMinRegionBeatCount = 16;

} // namespace

double BeatUtils::calculateBpm(const QVector<double>& beats,
        const mixxx::audio::SampleRate& sampleRate) {
    if (beats.size() < 2) {
        return 0;
    }

    // If we don't have enough beats for our regular approach, just divide the #
    // of beats by the duration in minutes.
    if (beats.size() < kMinRegionBeatCount) {
        return 60.0 * (beats.size() - 1) * sampleRate / (beats.last() - beats.first());
    }

    QVector<BeatUtils::ConstRegion> constantRegions =
            retrieveConstRegions(beats, sampleRate);
    return makeConstBpm(constantRegions, sampleRate, nullptr);
}

QVector<BeatUtils::ConstRegion> BeatUtils::retrieveConstRegions(
        const QVector<double>& coarseBeats,
        const mixxx::audio::SampleRate& sampleRate) {
    // The QM Beat detector has a step size of 512 frames @ 44100 Hz. This means that
    // Single beats have has a jitter of +- 12 ms around the actual position.
    // Expressed in BPM it means we have for instance steps of these BPM value around 120 BPM
    // 117.454 - 120.185 - 123.046 - 126.048
    // A pure electronic 120.000 BPM track will have many 120,185 BPM beats and a few
    // 117,454 BPM beats to adjust the collected offset.
    // This function irons these adjustment beats by adjusting every beat to the average of
    // a likely constant region.

    // Therefore we loop through the coarse beats and calculate the average beat
    // length from the first beat.
    // A inner loop checks for outliers using the momentary average as beat length.
    // once we have found an average with only single outliers, we store the beats using the
    // current average to adjust them by up to +-12 ms.
    // Than we start with the region from the found beat to the end.

    QVector<ConstRegion> constantRegions;
    if (!coarseBeats.size()) {
        // no beats
        return constantRegions;
    }

    double maxPhaseError = kMaxSecsPhaseError * sampleRate;
    double maxPhaseErrorSum = kMaxSecsPhaseErrorSum * sampleRate;
    int leftIndex = 0;
    int rightIndex = coarseBeats.size() - 1;

    while (leftIndex < coarseBeats.size() - 1) {
        DEBUG_ASSERT(rightIndex > leftIndex);
        double meanBeatLength =
                (coarseBeats[rightIndex] - coarseBeats[leftIndex]) /
                (rightIndex - leftIndex);
        int outliersCount = 0;
        double ironedBeat = coarseBeats[leftIndex];
        double phaseErrorSum = 0;
        int i = leftIndex + 1;
        for (; i <= rightIndex; ++i) {
            ironedBeat += meanBeatLength;
            double phaseError = ironedBeat - coarseBeats[i];
            phaseErrorSum += phaseError;
            if (fabs(phaseError) > maxPhaseError) {
                outliersCount++;
                if (outliersCount > kMaxOutlierCount ||
                        i == leftIndex + 1) { // the first beat must not be an outlier.
                    // region is not const.
                    break;
                }
            }
            if (fabs(phaseErrorSum) > maxPhaseErrorSum) {
                // we drift away in one direction, the meanBeatLength is not optimal.
                break;
            }
        }
        if (i > rightIndex) {
            // We have found a constant enough region.
            double firstBeat = coarseBeats[leftIndex];
            // store the regions for the later stages
            constantRegions.append({firstBeat, meanBeatLength});
            // continue with the next region.
            leftIndex = rightIndex;
            rightIndex = coarseBeats.size() - 1;
            continue;
        }
        // Try a by one beat smaller region
        rightIndex--;
    }

    // Add a final region with zero length to mark the end.
    constantRegions.append({coarseBeats[coarseBeats.size() - 1], 0});
    return constantRegions;
}

// static
double BeatUtils::makeConstBpm(
        const QVector<BeatUtils::ConstRegion>& constantRegions,
        const mixxx::audio::SampleRate& sampleRate,
        double* pFirstBeat) {
    // We assume her the track was recorded with an unhear-able static metronome.
    // This metronome is likely at a full BPM.
    // The track may has intros, outros and bridges without detectable beats.
    // In these regions the detected beat might is floating around and is just wrong.
    // The track may also has regions with different Rythm giving Instruments. They
    // have a different shape of onsets and introduce a static beat offset.
    // The track may also have break beats or other issues that makes the detector
    // hook onto a beat that is by an integer fraction off the original metronome.

    // This code aims to find the static metronome and a phase offset.

    // Find the longest region somwher in the middle of the track to start with.
    // At least this region will be have finally correct annotated beats.
    int midRegion = 0;
    double longesRegionLength = 0;
    double longesRegionBeatLenth = 0;
    for (int i = 0; i < constantRegions.size() - 1; ++i) {
        double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        if (length > longesRegionLength) {
            longesRegionLength = length;
            longesRegionBeatLenth = constantRegions[i].beatLength;
            midRegion = i;
        }
        qDebug() << i << length << constantRegions[i].beatLength;
    }

    if (longesRegionLength == 0) {
        // no betas, we default to
        return 128;
    }

    int longestRegionNumberOfBeats = static_cast<int>(
            (longesRegionLength / longesRegionBeatLenth) + 0.5);
    double longestRegionMinRoundSamples = longesRegionBeatLenth -
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
    double longestRegionMaxRoundSamples = longesRegionBeatLenth +
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);

    int startRegion = midRegion;

    // Find a region at the beginning of the track with a similar tempo and phase
    for (int i = 0; i < midRegion; ++i) {
        double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        int numberOfBeats = static_cast<int>((length / constantRegions[i].beatLength) + 0.5);
        if (numberOfBeats < kMinRegionBeatCount) {
            // Request short regions, too unstable.
            continue;
        }
        double minRoundSamples = constantRegions[i].beatLength -
                ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        double maxRoundSamples = constantRegions[i].beatLength +
                ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        // check if the tempo of the longest region is part of the rounding range of this region
        if (longesRegionBeatLenth > minRoundSamples &&
                longesRegionBeatLenth < maxRoundSamples) {
            // Now check if both regions are at the same phase.
            double newLength = constantRegions[midRegion + 1].firstBeat -
                    constantRegions[i].firstBeat;
            int numberOfOldBeats = static_cast<int>((newLength / longesRegionBeatLenth) + 0.5);
            double newBeatLength = newLength / numberOfOldBeats;
            if (newBeatLength > longestRegionMinRoundSamples &&
                    newBeatLength < longestRegionMaxRoundSamples) {
                longesRegionLength = newLength;
                longesRegionBeatLenth = newBeatLength;
                longestRegionNumberOfBeats = numberOfOldBeats;
                startRegion = i;
                break;
            }
        }
    }

    longestRegionMinRoundSamples = longesRegionBeatLenth -
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
    longestRegionMaxRoundSamples = longesRegionBeatLenth +
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);

    int endRegion = midRegion;

    // Find a region at the end of the track with similar tempo and phase
    for (int i = constantRegions.size() - 2; i > midRegion; --i) {
        double length = constantRegions[i + 1].firstBeat - constantRegions[i].firstBeat;
        int numberOfBeats = static_cast<int>((length / constantRegions[i].beatLength) + 0.5);
        if (numberOfBeats < kMinRegionBeatCount) {
            continue;
        }
        double minRoundSamples = constantRegions[i].beatLength -
                ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        double maxRoundSamples = constantRegions[i].beatLength +
                ((kMaxSecsPhaseError * sampleRate) / numberOfBeats);
        if (longesRegionLength > minRoundSamples &&
                longesRegionLength < maxRoundSamples) {
            // Now check if both regions are at the same phase.
            double newLength = constantRegions[i + 1].firstBeat -
                    constantRegions[startRegion].firstBeat;
            int numberOfOldBeats = static_cast<int>((newLength / longesRegionBeatLenth) + 0.5);
            double newBeatLength = newLength / numberOfOldBeats;
            if (newBeatLength > longestRegionMinRoundSamples &&
                    newBeatLength < longestRegionMaxRoundSamples) {
                longesRegionLength = newLength;
                longesRegionBeatLenth = newBeatLength;
                longestRegionNumberOfBeats = numberOfOldBeats;
                endRegion = i;
                break;
            }
        }
    }

    longestRegionMinRoundSamples = longesRegionBeatLenth -
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);
    longestRegionMaxRoundSamples = longesRegionBeatLenth +
            ((kMaxSecsPhaseError * sampleRate) / longestRegionNumberOfBeats);

    qDebug() << startRegion << midRegion << endRegion << constantRegions.size()
             << longesRegionLength << "<<<<<<<<<<<<<<<<<<<<<<<<<";

    qDebug() << "First beat" << constantRegions[startRegion].firstBeat;
    qDebug() << "Last beat" << constantRegions[endRegion + 1].firstBeat;
    qDebug() << longesRegionLength << longestRegionNumberOfBeats;

    // Create a const region region form the first beat of the first region to the last beat of the last region.

    double minRoundBpm = 60 * sampleRate / longestRegionMaxRoundSamples;
    double maxRoundBpm = 60 * sampleRate / longestRegionMinRoundSamples;
    double centerBpm = 60 * sampleRate / longesRegionBeatLenth;

    qDebug() << "minRoundBpm" << minRoundBpm;
    qDebug() << "maxRoundBpm" << maxRoundBpm;
    double roundBpm = roundBpmWithinRange(minRoundBpm, centerBpm, maxRoundBpm);

    if (pFirstBeat) {
        *pFirstBeat = constantRegions[startRegion].firstBeat;
    }
    return roundBpm;
}

// static
double BeatUtils::roundBpmWithinRange(double minBpm, double centerBpm, double maxBpm) {
    // First try to snap to a full integer BPM
    double snapBpm = round(centerBpm);
    if (snapBpm > minBpm && snapBpm < maxBpm) {
        // Success
        return snapBpm;
    }

    // Probe the reasonable multipliers for 0.5
    double roundBpmWidth = maxBpm - minBpm;
    if (roundBpmWidth > 1.0 / 2) {
        // 0.5 BPM are only reasonable if the double value is not insane
        // or the 2/3 value is not too small.
        if (centerBpm < 85) {
            // this cane be actually up to 175 BPM
            // allow halve BPM values
            return round(centerBpm * 2) / 2;
        } else if (centerBpm > 127) {
            // optimize for 2/3 going down to 85
            return round(centerBpm / 3 * 2) * 3 / 2;
        }
    }

    if (roundBpmWidth > 1.0 / 12) {
        // this covers all sorts of 1/2 2/3 and 3/4 multiplier
        return round(centerBpm * 12) / 12;
    } else {
        // We are here if we have more that ~75 beats and ~30 s
        // try to snap to a 1/12 Bpm
        double snapBpm = round(centerBpm * 12) / 12;
        if (snapBpm > minBpm && snapBpm < maxBpm) {
            // Success
            return snapBpm;
        }
        // else give up and use the original BPM value.
    }

    return centerBpm;
}

// static
QVector<double> BeatUtils::getBeats(const QVector<BeatUtils::ConstRegion>& constantRegions) {
    QVector<double> beats;
    for (int i = 0; i < constantRegions.size() - 1; ++i) {
        double beat = constantRegions[i].firstBeat;
        constexpr double epsilon = 100; // Protection against tiny beats due rounding
        while (beat < constantRegions[i + 1].firstBeat - epsilon) {
            beats.append(beat);
            beat += constantRegions[i].beatLength;
        }
    }
    return beats;
}

// static
double BeatUtils::adjustPhase(
        double firstBeat,
        double bpm,
        const mixxx::audio::SampleRate& sampleRate,
        const QVector<double>& beats) {
    double beatLength = 60 * sampleRate / bpm;
    double startOffset = fmod(firstBeat, beatLength);
    double offsetAdjust = 0;
    double offsetAdjustCount = 0;
    for (const auto& beat : beats) {
        double offset = fmod(beat, beatLength) - startOffset;
        if (abs(offset) < (kMaxSecsPhaseError * sampleRate)) {
            offsetAdjust += offset;
            offsetAdjustCount++;
        }
    }
    offsetAdjust /= offsetAdjustCount;
    qDebug() << "adjusting phase by" << offsetAdjust;
    DEBUG_ASSERT(abs(offsetAdjust) < (kMaxSecsPhaseError * sampleRate));

    return firstBeat - offsetAdjust;
}
