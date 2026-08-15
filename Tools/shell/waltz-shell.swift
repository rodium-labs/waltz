// Streams what macOS is playing to a Waltz board over USB.
//
//   swift Tools/shell/waltz-shell.swift          find the board and stream
//   swift Tools/shell/waltz-shell.swift --once   print one reading and exit
//
// One file, and run as a script rather than compiled, for a reason worth
// knowing: macOS only hands now-playing information to a process it trusts, and
// an unsigned binary built here is not one. Apple's own swift driver is, so the
// script runs and a compiled copy of the same code comes back empty.
//
// The board has no decoder and no card yet, so the only real music in reach is
// whatever the machine is playing. macOS knows what that is - it is what fills
// the Now Playing tile in Control Centre - and MediaRemote is where it keeps it.
// That works whatever the player is: Music, Spotify, a browser tab.
//
// MediaRemote hands over no audio, so the spectrum comes from somewhere else:
// ScreenCaptureKit will give up the system mix without a loopback driver, and
// Spectrum.swift turns that into twelve bars. It needs Screen Recording
// permission - the same gate macOS puts in front of system audio - and if that
// is refused the tool carries on with metadata alone and the board keeps its own
// meter running rather than freezing.

import Accelerate
import AVFoundation
import Foundation
import ScreenCaptureKit

// MARK: - MediaRemote

private let mediaRemote: CFBundle? = CFBundleCreate(
    kCFAllocatorDefault,
    URL(fileURLWithPath: "/System/Library/PrivateFrameworks/MediaRemote.framework") as CFURL)

private typealias GetNowPlayingInfo =
    @convention(c) (DispatchQueue, @escaping ([String: Any]) -> Void) -> Void

private func nowPlaying(_ done: @escaping ([String: Any]?) -> Void) {
    guard let bundle = mediaRemote,
          let ptr = CFBundleGetFunctionPointerForName(
              bundle, "MRMediaRemoteGetNowPlayingInfo" as CFString) else {
        done(nil)
        return
    }
    let fn = unsafeBitCast(ptr, to: GetNowPlayingInfo.self)
    fn(DispatchQueue.global()) { info in done(info) }
}

struct Track: Equatable {
    var title = ""
    var artist = ""
    var duration = 0
    var playing = false
}

/// What the board is told about position, worked out rather than polled for.
///
/// MediaRemote reports where the playhead was at a timestamp, not where it is.
/// Asking again every second would be both wasteful and jittery, so the elapsed
/// figure is carried forward from the last reading at the reported rate.
struct Position {
    var elapsedAt = 0.0
    var stamp = Date.distantPast
    var rate = 0.0

    func now() -> Int {
        let secs = elapsedAt + Date().timeIntervalSince(stamp) * rate
        return max(0, Int(secs.rounded()))
    }
}

// MARK: - The board

/// Any CDC port the board might be on. The serial number comes from the die, so
/// the name is stable per board but not predictable from here.
func findBoard() -> String? {
    let devs = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
    let candidates = devs.filter { $0.hasPrefix("cu.usbmodem") }.sorted()
    return candidates.first.map { "/dev/" + $0 }
}

/// The serial link, with the two things a naive version gets wrong.
///
/// Writes are serialised. Three threads feed this - the audio queue with bars,
/// MediaRemote's callback with metadata, the main loop with the position - and
/// unsynchronised writes interleave mid-line. The board then reads a title with
/// spectrum hex spliced into it, which looks exactly like it is stuck on some
/// garbled track.
///
/// And it reconnects. Resetting the board makes the USB device disappear and
/// come back; a handle opened before that points at nothing, and every write
/// after it succeeds into the void.
final class Board {
    private var handle: FileHandle?
    private let lock = NSLock()
    private var path: String?
    private var lastTry = Date.distantPast

    /// Called after a reconnect so the board is told everything again rather
    /// than left showing whatever it had when the cable went.
    var onReconnect: (() -> Void)?

    private func openIfNeeded() {
        guard handle == nil else { return }
        guard Date().timeIntervalSince(lastTry) > 1 else { return }
        lastTry = Date()

        guard let p = findBoard() else { return }
        guard let h = FileHandle(forWritingAtPath: p) else { return }
        handle = h
        path = p
        FileHandle.standardError.write("connected to \(p)\n".data(using: .utf8)!)
        onReconnect?()
    }

    func send(_ line: String) {
        lock.lock()
        defer { lock.unlock() }

        openIfNeeded()
        guard let h = handle, let d = (line + "\n").data(using: .ascii) else { return }
        do {
            try h.write(contentsOf: d)
        } catch {
            // Gone. Drop it and let the next send find the device again.
            FileHandle.standardError.write("link lost, waiting for the board\n"
                .data(using: .utf8)!)
            try? h.close()
            handle = nil
            path = nil
        }
    }

    func close() {
        lock.lock()
        defer { lock.unlock() }
        try? handle?.close()
        handle = nil
    }
}

/// The panel font covers 32..126, and the board substitutes anything else.
/// Folding accents here keeps names readable rather than a row of question
/// marks - "Beyoncé" should not arrive as "Beyonc?".
func asciiFold(_ s: String) -> String {
    let folded = s.folding(options: [.diacriticInsensitive, .widthInsensitive],
                           locale: Locale(identifier: "en_US"))
    return String(folded.unicodeScalars.map { ch -> Character in
        (ch.value >= 32 && ch.value <= 126) ? Character(ch) : " "
    }).trimmingCharacters(in: .whitespaces)
}

// MARK: - Spectrum

/// Turns a stream of samples into SPECTRUM_BARS levels, 0..100.
final class Analyser {
    /// 1024 at 48 kHz is 21 ms - short enough to feel instant, long enough to
    /// put the bottom band somewhere useful.
    static let n = 1024
    private let log2n = vDSP_Length(10)

    private let setup: FFTSetup
    private var window = [Float](repeating: 0, count: Analyser.n)
    private var ring = [Float](repeating: 0, count: Analyser.n)
    private var filled = 0

    /// Band edges in Hz. Logarithmic, because pitch is and a linear split puts
    /// eleven of the twelve bars in the top two octaves where nothing happens.
    private let edges: [Float] = [40, 80, 160, 250, 400, 630, 1000,
                                  1600, 2500, 4000, 6300, 10000, 16000]

    private(set) var bars: [Float]
    private let barCount: Int

    init(bars barCount: Int) {
        self.barCount = barCount
        self.bars = [Float](repeating: 0, count: barCount)
        setup = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2))!
        vDSP_hann_window(&window, vDSP_Length(Analyser.n), Int32(vDSP_HANN_NORM))
    }

    deinit { vDSP_destroy_fftsetup(setup) }

    /// Push mono samples. Returns true when a fresh frame of bars is ready.
    func feed(_ samples: [Float], sampleRate: Float) -> Bool {
        var produced = false
        var i = 0

        while i < samples.count {
            let take = min(Analyser.n - filled, samples.count - i)
            for k in 0..<take { ring[filled + k] = samples[i + k] }
            filled += take
            i += take

            if filled == Analyser.n {
                transform(sampleRate: sampleRate)
                // Half-overlap: the bars move at twice the frame rate for the
                // same window length, which is what stops them looking steppy.
                for k in 0..<(Analyser.n / 2) { ring[k] = ring[k + Analyser.n / 2] }
                filled = Analyser.n / 2
                produced = true
            }
        }
        return produced
    }

    private func transform(sampleRate: Float) {
        var windowed = [Float](repeating: 0, count: Analyser.n)
        vDSP_vmul(ring, 1, window, 1, &windowed, 1, vDSP_Length(Analyser.n))

        let half = Analyser.n / 2
        var real = [Float](repeating: 0, count: half)
        var imag = [Float](repeating: 0, count: half)
        var mags = [Float](repeating: 0, count: half)

        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: half) { cp in
                        vDSP_ctoz(cp, 2, &split, 1, vDSP_Length(half))
                    }
                }
                vDSP_fft_zrip(setup, &split, 1, log2n, FFTDirection(FFT_FORWARD))
                vDSP_zvabs(&split, 1, &mags, 1, vDSP_Length(half))
            }
        }

        let binHz = sampleRate / Float(Analyser.n)
        for b in 0..<barCount {
            let lo = Int(edges[b] / binHz)
            let hi = min(half - 1, Int(edges[b + 1] / binHz))
            var sum: Float = 0
            var count = 0
            if hi >= lo {
                for k in lo...hi { sum += mags[k]; count += 1 }
            }
            let mean = count > 0 ? sum / Float(count) : 0

            // Loudness is logarithmic, so the bar is too. The floor is chosen so
            // a quiet passage still shows movement rather than flatlining.
            let db = 20 * log10f(max(mean, 1e-7) / Float(Analyser.n) * 4)
            var level = (db + 70) / 70 * 100
            level = min(100, max(0, level))

            // Fast up, slow down: a meter that falls as quickly as it rises
            // reads as noise. The board does the same for its peak markers.
            bars[b] = level > bars[b] ? level : bars[b] * 0.82 + level * 0.18
        }
    }
}

/// Captures the system mix and hands it to an Analyser.
final class SystemAudio: NSObject, SCStreamOutput {
    private var stream: SCStream?
    private let analyser: Analyser
    private let onBars: ([Float]) -> Void
    private var rate: Float = 48000

    init(bars: Int, onBars: @escaping ([Float]) -> Void) {
        self.analyser = Analyser(bars: bars)
        self.onBars = onBars
    }

    /// Throws if the permission is missing, which the caller reports and
    /// carries on without - metadata alone is still worth streaming.
    func start() async throws {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false, onScreenWindowsOnly: false)
        guard let display = content.displays.first else {
            throw NSError(domain: "waltz", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "no display to attach to"])
        }

        let filter = SCContentFilter(display: display, excludingApplications: [],
                                     exceptingWindows: [])
        let cfg = SCStreamConfiguration()
        cfg.capturesAudio = true
        cfg.excludesCurrentProcessAudio = true
        cfg.sampleRate = 48000
        cfg.channelCount = 2
        // Video is not wanted but the stream insists on a size; the smallest
        // legal one at one frame a second costs nothing.
        cfg.width = 2
        cfg.height = 2
        cfg.minimumFrameInterval = CMTime(value: 1, timescale: 1)

        let s = SCStream(filter: filter, configuration: cfg, delegate: nil)
        try s.addStreamOutput(self, type: .audio,
                              sampleHandlerQueue: DispatchQueue(label: "waltz.audio"))
        try await s.startCapture()
        stream = s
    }

    func stream(_ s: SCStream, didOutputSampleBuffer sb: CMSampleBuffer,
                of type: SCStreamOutputType) {
        guard type == .audio else { return }

        var mono: [Float] = []
        try? sb.withAudioBufferList { abl, _ in
            // Channels arrive as separate buffers; the mix of both is what a
            // single meter should show.
            var channels: [[Float]] = []
            for buf in abl {
                guard let d = buf.mData else { continue }
                let n = Int(buf.mDataByteSize) / MemoryLayout<Float>.size
                let p = d.bindMemory(to: Float.self, capacity: n)
                channels.append(Array(UnsafeBufferPointer(start: p, count: n)))
            }
            guard let first = channels.first else { return }
            mono = [Float](repeating: 0, count: first.count)
            for ch in channels {
                for i in 0..<min(ch.count, mono.count) { mono[i] += ch[i] }
            }
            let scale = 1 / Float(max(1, channels.count))
            for i in 0..<mono.count { mono[i] *= scale }
        }

        if !mono.isEmpty, analyser.feed(mono, sampleRate: rate) {
            onBars(analyser.bars)
        }
    }
}

// MARK: - Main

let once = CommandLine.arguments.contains("--once")

let board = Board()
var lastTrack = Track()
var position = Position()
var haveReading = false

board.onReconnect = {
    // Forget what the board was told; every field goes again on the next poll.
    lastTrack = Track()
}

func poll() {
    nowPlaying { info in
        guard let info = info else { return }

        var t = Track()
        t.title = asciiFold(info["kMRMediaRemoteNowPlayingInfoTitle"] as? String ?? "")
        t.artist = asciiFold(info["kMRMediaRemoteNowPlayingInfoArtist"] as? String ?? "")
        t.duration = Int((info["kMRMediaRemoteNowPlayingInfoDuration"] as? Double ?? 0).rounded())
        let rate = info["kMRMediaRemoteNowPlayingInfoPlaybackRate"] as? Double ?? 0
        t.playing = rate > 0

        position.elapsedAt = info["kMRMediaRemoteNowPlayingInfoElapsedTime"] as? Double ?? 0
        position.stamp = info["kMRMediaRemoteNowPlayingInfoTimestamp"] as? Date ?? Date()
        position.rate = rate
        haveReading = true

        if once {
            print("title    \(t.title)")
            print("artist   \(t.artist)")
            print("duration \(t.duration)")
            print("elapsed  \(position.now())")
            print("playing  \(t.playing)")
            if let art = info["kMRMediaRemoteNowPlayingInfoArtworkData"] as? Data {
                print("artwork  \(art.count) bytes")
            }
            exit(0)
        }

        // Only the fields that changed: the board keeps what it was told, and a
        // title resent every tick would restart its marquee.
        if t.title != lastTrack.title { board.send("T" + t.title) }
        if t.artist != lastTrack.artist { board.send("A" + t.artist) }
        if t.duration != lastTrack.duration { board.send("D\(t.duration)") }
        if t.playing != lastTrack.playing { board.send("P" + (t.playing ? "1" : "0")) }
        lastTrack = t
    }
}

if once {
    poll()
    RunLoop.current.run(until: Date().addingTimeInterval(3))
    FileHandle.standardError.write("no reading from MediaRemote\n".data(using: .utf8)!)
    exit(1)
}

// The spectrum, if macOS will part with it.
let bars = 12
var lastBarSend = Date.distantPast
let audio = SystemAudio(bars: bars) { levels in
    // Twenty a second is plenty: the panel repaints its meter at fifty and the
    // eye cannot follow faster than this anyway.
    guard Date().timeIntervalSince(lastBarSend) > 0.05 else { return }
    lastBarSend = Date()
    let hex = levels.map { String(format: "%02x", Int(max(0, min(100, $0)))) }.joined()
    board.send("B" + hex)
}

let audioReady = DispatchSemaphore(value: 0)
Task {
    do {
        try await audio.start()
        FileHandle.standardError.write("spectrum: capturing system audio\n"
            .data(using: .utf8)!)
    } catch {
        FileHandle.standardError.write(
            ("spectrum: off (\(error.localizedDescription))\n" +
             "  grant Screen Recording to this terminal in System Settings >\n" +
             "  Privacy & Security, then run this again. Metadata still streams.\n")
                .data(using: .utf8)!)
    }
    audioReady.signal()
}
audioReady.wait()

// Metadata changes rarely; position is carried forward and sent often enough
// that the seconds on the panel tick like a clock.
var ticks = 0
while true {
    if ticks % 10 == 0 { poll() }
    if haveReading { board.send("E\(position.now())") }
    ticks += 1
    Thread.sleep(forTimeInterval: 0.1)
}
