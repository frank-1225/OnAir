// Missing transfers concatenate non-adjacent pieces of audio. Unlike an RF fade (silence with samples retained), they change line phase.
// Assert recovery against the known transmitted chart, while checking that old good rows and normal clock correction stay unchanged.
#include "../data/marine/fax/fax_util.h"
using namespace faxt;

struct Decoded { FaxImage image; FaxStatus status; };

static Decoded decode(const std::vector<float>& audio, size_t chunk = 4096) {
    FaxDecoder d;
    d.configure(12000);
    d.setLpm(120);
    feedAll(d, audio, chunk);
    Decoded out;
    uint64_t seq = 0;
    d.latestImage(out.image, seq);
    out.status = d.status();
    return out;
}

// The missing samples can also remove a whole row. Choose the nearest reference row, then measure the remaining *horizontal* error
// across a full circular line, not only near zero (the original decoder stays displaced by hundreds of pixels).
static double phaseError(const FaxImage& im, const FaxImage& ref, int rowDelta) {
    if (im.width != ref.width || im.height < 201) return 1e9;
    const int W = im.width;
    double error = 0;
    int count = 0;
    for (int r = 100; r < 200; r += 3) {
        double bestCorr = -1, bestShift = 1e9;
        for (int dr = -2; dr <= 2; dr++) {
            const int y = r + rowDelta + dr;
            if (y < 0 || y >= ref.height) continue;
            const auto* a = &im.pix[static_cast<size_t>(r + 1) * W]; // initial white row
            const auto* b = &ref.pix[static_cast<size_t>(y) * W];
            int best = 0;
            double score = -1e99;
            for (int shift = -W / 2; shift <= W / 2; shift += 8) {
                double s = 0;
                for (int x = 0; x < W; x += 4) s += (a[(x + shift + W) % W] - 220.) * (b[x] - 220.);
                if (s > score) { score = s; best = shift; }
            }
            std::vector<uint8_t> rolled(W);
            for (int x = 0; x < W; x++) rolled[x] = a[(x + best + W) % W];
            double shift, corr;
            rowShift(rolled.data(), b, W, 12, shift, corr);
            if (corr > bestCorr) { bestCorr = corr; bestShift = best + shift; }
        }
        error += std::fabs(bestShift);
        count++;
    }
    return count ? error / count : 1e9;
}

int main() {
    Opts o;
    o.lines = 240;
    const auto clean = makeAudio(o);
    const auto ref = faxTestChart(faxImageWidth(576), o.lines, o.seed);
    const auto a = decode(clean, 997), b = decode(clean, 32768);
    FCHECK(a.image.pix == b.image.pix, "callback chunk size changes decoded pixels");
    FCHECK(a.status.syncRecoveries == 0 && b.status.syncRecoveries == 0, "normal chart caused a false recovery");
    const auto q = compare(a.image, ref, 1);
    FCHECK(q.meanCorr > .97 && std::fabs(q.drift) < 1, "clean reception degraded");

    const size_t at = static_cast<size_t>((35.5 + 80 * .5) * o.rate);
    for (int missing : {60, 120, 300, 600, 1644, 3000, 4600, 9600}) {
        auto audio = clean;
        audio.erase(audio.begin() + at, audio.begin() + at + missing);
        const auto d = decode(audio);
        const double error = phaseError(d.image, ref, missing / 6000);
        printf("missing %d samples (%.1f ms): phase error %.2f px, %llu recoveries\n", missing, missing / 12., error,
               (unsigned long long)d.status.syncRecoveries);
        FCHECK(error < 2 && d.status.syncRecoveries > 0, "did not recover after %d missing samples: %.2f px", missing, error);
        const size_t prefix = static_cast<size_t>(70) * ref.width;
        FCHECK(d.image.pix.size() >= prefix && std::equal(a.image.pix.begin(), a.image.pix.begin() + prefix, d.image.pix.begin()),
               "recovery changed good rows preceding the loss");
    }
    { // A whole missing line changes the row count, but not horizontal phase.
        auto audio = clean;
        audio.erase(audio.begin() + at, audio.begin() + at + 6000);
        const auto d = decode(audio);
        FCHECK(phaseError(d.image, ref, 1) < 2 && d.status.syncRecoveries == 0, "whole-line loss changed line phase");
    }
    { // Repeated samples move phase in the opposite direction.
        auto audio = clean;
        audio.insert(audio.begin() + at, clean.begin() + at - 1200, clean.begin() + at);
        const auto d = decode(audio);
        const double error = phaseError(d.image, ref, 0);
        FCHECK(error < 2 && d.status.syncRecoveries > 0, "did not recover repeated samples: %.2f px", error);
    }
    { // A fade retains the timeline. It must not result in a persistent correction.
        auto audio = clean;
        std::fill(audio.begin() + at, audio.begin() + at + 1644, 0.f);
        const auto d = decode(audio);
        FCHECK(phaseError(d.image, ref, 0) < 2 && d.status.syncRecoveries == 0, "fade moved subsequent rows");
    }
    if (fails) return 1;
    printf("OK\n");
    return 0;
}
