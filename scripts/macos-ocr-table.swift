import Foundation
import Vision
import AppKit

struct OCRToken {
    let text: String
    let x: CGFloat
    let y: CGFloat
    let width: CGFloat
    let height: CGFloat
}

func loadImage(at path: String) -> CGImage? {
    guard let image = NSImage(contentsOfFile: path) else { return nil }
    var rect = CGRect(origin: .zero, size: image.size)
    return image.cgImage(forProposedRect: &rect, context: nil, hints: nil)
}

func recognizeTokens(in image: CGImage) throws -> [OCRToken] {
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.usesLanguageCorrection = false
    request.recognitionLanguages = ["ru-RU", "en-US"]

    let handler = VNImageRequestHandler(cgImage: image, options: [:])
    try handler.perform([request])

    return (request.results ?? []).compactMap { observation in
        guard let candidate = observation.topCandidates(1).first else { return nil }
        let box = observation.boundingBox
        return OCRToken(
            text: candidate.string,
            x: box.origin.x,
            y: box.origin.y,
            width: box.size.width,
            height: box.size.height
        )
    }
}

func cropImage(_ image: CGImage, rect: CGRect) -> CGImage? {
    let width = CGFloat(image.width)
    let height = CGFloat(image.height)
    let crop = CGRect(
        x: rect.origin.x * width,
        y: rect.origin.y * height,
        width: rect.size.width * width,
        height: rect.size.height * height
    ).integral
    return image.cropping(to: crop)
}

let args = CommandLine.arguments
guard args.count >= 2 else {
    fputs("Usage: macos-ocr-table.swift image.png\n", stderr)
    exit(1)
}

guard let image = loadImage(at: args[1]) else {
    fputs("Failed to load image\n", stderr)
    exit(2)
}

// Program table region on TM 1987-07 page 2 (normalized coordinates).
let tableRect = CGRect(x: 0.02, y: 0.34, width: 0.96, height: 0.28)
guard let cropped = cropImage(image, rect: tableRect) else {
    fputs("Failed to crop image\n", stderr)
    exit(3)
}

let tokens = try recognizeTokens(in: cropped)
let rowTolerance: CGFloat = 0.018
var rows: [[OCRToken]] = []
for token in tokens.sorted(by: { $0.y == $1.y ? $0.x < $1.x : $0.y > $1.y }) {
    if let index = rows.firstIndex(where: { abs($0[0].y - token.y) <= rowTolerance }) {
        rows[index].append(token)
    } else {
        rows.append([token])
    }
}

for row in rows {
    let line = row.sorted { $0.x < $1.x }.map(\.text).joined(separator: "\t")
    print(line)
}
