# 08 — Capture and OCR (`sniplyngo_capture`, `sniplyngo_ocr`)

| | |
| --- | --- |
| Milestone | M3 |
| Status | **Outline only** — deliberately not designed yet |
| Depends on (planned) | `01 core`, `05 coordinator` (as a new `Origin`) |
| Used by | 07 (explicit capture request only) |

## 1. Why this is an outline

Region OCR is a fallback for text that cannot be selected, and it is the part of the product with the largest
unverified platform surface: permission prompts, portal versions, temporary-file ownership, per-monitor geometry
and OCR cost. Writing a detailed design now would produce a document that the first real test invalidates.

The top-level design therefore defers it (`../DESIGN.md` §1.2, §5) and lists the preconditions below. Nothing here
is a commitment for M1.

## 2. Preconditions before this document becomes a real design

Each item must be answered with evidence from a real desktop, not from documentation:

1. Which capture backend is available on each target (Screenshot Portal and its version, `target` support,
   ScreenCast, native X11), and what exactly does the user see (system dialog, own overlay, nothing)?
2. Temporary files: does the backend return a URI backed by a file the application owns, and what happens on
   failure? If ownership cannot be established, the path is disabled rather than deleting an arbitrary URI.
3. Geometry: per-output scale, rotation, origin and crop mapping for 100/125/150/200 % and mixed-DPI setups. One
   global DPR is not sufficient (`../linux-integration.md` §7).
4. Cost: OCR latency and memory for a representative 1200×300 region in English and mixed Chinese/English, on a
   low-end laptop.
5. Permission lifecycle: how often the prompt appears, and whether it can be granted once per session.

## 3. Provisional shape (subject to change)

```cpp
// capture: returns pixels, owns nothing outside the request
struct CaptureRequest { QRect logicalRegion; int screenIndex; quint64 generation; };
struct CapturedImage { QImage image; QRect physicalRect; qreal scaleFactor; QString backendId; };

class CaptureBackend {
public:
    virtual ~CaptureBackend() = default;
    virtual CaptureProbe probe() = 0;                                  // no user-visible side effect
    virtual void capture(const CaptureRequest &, CaptureCallback) = 0; // async; deadline enforced
    virtual void cancel(quint64 generation) = 0;
};

// ocr: text plus per-line confidence, never persisted
struct OcrLine { QString text; QRectF boxPhysical; float confidence; };
struct OcrResult { QVector<OcrLine> lines; float meanConfidence; QString engineId; };

class OcrEngine {
public:
    virtual ~OcrEngine() = default;
    virtual OcrResult recognize(const QImage &, const OcrOptions &) = 0;   // runs on a worker thread
};
```

Integration points, decided now so M3 does not reshape M1 code:

- The coordinator gains `Origin::RegionCapture`. It reuses the same generation, the same consent gate and the same
  result window; capture must not introduce a second pipeline.
- OCR runs on a worker thread; cancellation is cooperative and a late result is discarded by generation.
- Images and recognized text stay in memory; no image is written by the application, and no recognized text is
  logged or persisted.
- If a backend cannot guarantee "no application-created temporary file", the UI must say so before the user
  captures, or the backend is not offered.

## 4. Explicit non-decisions

- No warm ScreenCast stream, no cursor tracking for anchoring, no continuous capture, no automatic OCR.
- No own selection overlay until a backend that supports region capture is verified; a system chooser is
  acceptable and must be described as such.
- No second OCR engine until one has a measured reason to exist.

## 5. Open questions that belong to M3

1. Is `target=Area` (Screenshot v3) usable on the target desktops, and is its selection UI acceptable?
2. Does a full-screen capture plus our own crop produce correct results at fractional scaling, or does the
   backend deliver buffers at a different scale than the compositor's logical geometry?
3. Which OCR configuration (page-segmentation mode, upscaling) performs best on UI text and mixed CJK/Latin?
4. What is the minimum honest disclosure for a system permission dialog the user will see on every capture?

## 6. Change log

| Date | Change |
| --- | --- |
| 2026-11-16 | Outline created; deliberately deferred to M3 with explicit preconditions. |
