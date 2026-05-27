import Foundation
import Vision
import AppKit

struct OCRLine: CustomStringConvertible {
    let text: String
    let y: CGFloat
    let x: CGFloat

    var description: String { text }
}

func loadImage(at path: String) -> CGImage? {
    guard let image = NSImage(contentsOfFile: path) else { return nil }
    var rect = CGRect(origin: .zero, size: image.size)
    return image.cgImage(forProposedRect: &rect, context: nil, hints: nil)
}

func recognizeText(in image: CGImage, languages: [String]) throws -> [OCRLine] {
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.usesLanguageCorrection = false
    request.recognitionLanguages = languages

    let handler = VNImageRequestHandler(cgImage: image, options: [:])
    try handler.perform([request])

    guard let observations = request.results else { return [] }

    return observations.compactMap { observation in
        guard let candidate = observation.topCandidates(1).first else { return nil }
        let box = observation.boundingBox
        return OCRLine(
            text: candidate.string,
            y: box.origin.y,
            x: box.origin.x
        )
    }
    .sorted {
        if abs($0.y - $1.y) > 0.008 { return $0.y > $1.y }
        return $0.x < $1.x
    }
}

func groupLines(_ lines: [OCRLine], tolerance: CGFloat = 0.012) -> [[OCRLine]] {
    var groups: [[OCRLine]] = []
    for line in lines {
        if let index = groups.firstIndex(where: { abs($0[0].y - line.y) <= tolerance }) {
            groups[index].append(line)
        } else {
            groups.append([line])
        }
    }
    return groups.map { group in group.sorted { $0.x < $1.x } }
}

let args = CommandLine.arguments
guard args.count >= 2 else {
    fputs("Usage: macos-ocr.swift image.png [image2.png ...]\n", stderr)
    exit(1)
}

let languages = ["ru-RU", "en-US"]
for path in args.dropFirst() {
    guard let image = loadImage(at: path) else {
        fputs("Failed to load image: \(path)\n", stderr)
        exit(2)
    }
    do {
        let lines = try recognizeText(in: image, languages: languages)
        print("# \(path)")
        for group in groupLines(lines) {
            print(group.map(\.text).joined(separator: "\t"))
        }
        print()
    } catch {
        fputs("OCR failed for \(path): \(error)\n", stderr)
        exit(3)
    }
}
