// Deploys core Bridgeplay components into the copy of CrossOver that Bridgeplay deploys to the Steam location

import Foundation

enum InstallPayload {
    static let step = String(localized: "Find Bridgeplay's components")

    struct Located: Sendable {
        let dylib: URL
        let overlayShim: URL
        let iconmaker: URL
        let appinfo: URL
        let run: URL
        let builtAt: Int64

        let signatures: [URL]
        let fontsRoot: URL
        let fonts: [String]
        // payload/gamefixes/*.conf, installed into SupportPaths.gamefixesDefault.
        var gamefixes: [URL] = []
    }

    static func root(in bundle: Bundle = AppResources.bundle) throws -> URL {
        guard let url = bundle.url(forResource: "payload", withExtension: nil) else {
            throw StepFailure(
                step: step,
                detail: String(localized: "This build of Bridgeplay carries no components at all.")
            )
        }
        return url
    }

    static func locate(in bundle: Bundle = AppResources.bundle) throws -> Located {
        try locate(root: try root(in: bundle))
    }

    // Reject the empty Intel slice shipped by earlier Apple Silicon builds.
    static func hasIntelComponents(root: URL) -> Bool {
        guard let bytes = try? Data(contentsOf: root.appending(path: SupportPaths.dylibName)),
              let slices = MachOBuild.unsignedSlices(bytes),
              slices.contains(where: { slice in
                  slice.count > 100_000 && slice.withUnsafeBytes {
                      $0.loadUnaligned(fromByteOffset: 4, as: UInt32.self).littleEndian == 0x0100_0007
                  }
              }) else { return false }
        let folder = root.appending(path: "signatures/macos.x86_64")
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        return files.contains { file in
            guard file.pathExtension == "json", let data = try? Data(contentsOf: file),
                  let profile = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  profile["arch"] as? String == "x86_64", profile["platform"] as? String == "macos",
                  let signatures = profile["signatures"] as? [[String: Any]], !signatures.isEmpty,
                  let modules = profile["module_uuids"] as? [String: String],
                  UUID(uuidString: modules["steamclient.dylib"] ?? "") != nil,
                  UUID(uuidString: modules["steamui.dylib"] ?? "") != nil else { return false }
            return true
        }
    }

    static func locateGamefixes(in root: URL) throws -> [URL] {
        ((try? FileManager.default.contentsOfDirectory(
            at: root.appending(path: "gamefixes"), includingPropertiesForKeys: nil
        )) ?? []).filter { $0.pathExtension == "conf" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    static func locate(root: URL) throws -> Located {
        let files = FileManager.default

        let dylib = root.appending(path: SupportPaths.dylibName)
        let shim = root.appending(path: "overlay-shim.dylib")
        let iconmaker = root.appending(path: "iconmaker")
        let appinfo = root.appending(path: "appinfo")
        let run = root.appending(path: "run")
        let stamp = try? String(contentsOf: root.appending(path: "build-time"), encoding: .utf8)
        let builtAt = stamp.flatMap { Int64($0.trimmingCharacters(in: .whitespacesAndNewlines)) }

        var missing: [String] = []
        if !files.fileExists(atPath: dylib.path(percentEncoded: false)) {
            missing.append(SupportPaths.dylibName)
        }
        if !files.fileExists(atPath: shim.path(percentEncoded: false)) {
            missing.append("overlay-shim.dylib")
        }
        if !files.fileExists(atPath: iconmaker.path(percentEncoded: false)) {
            missing.append("iconmaker")
        }
        if !files.fileExists(atPath: appinfo.path(percentEncoded: false)) {
            missing.append("appinfo")
        }
        if !files.fileExists(atPath: run.path(percentEncoded: false)) { missing.append("run") }
        if (builtAt ?? 0) <= 0 { missing.append("build-time") }

        let signatureDir = root.appending(path: SupportPaths.signatureSubpath)
        let signatures = ((try? files.contentsOfDirectory(
            at: signatureDir, includingPropertiesForKeys: nil
        )) ?? []).filter { $0.pathExtension == "json" }.sorted { $0.lastPathComponent < $1.lastPathComponent }

        if signatures.isEmpty { missing.append("\(SupportPaths.signatureSubpath)/*.json") }

        let fontsRoot = root.appending(path: "fonts")
        let fonts = (files.subpaths(atPath: fontsRoot.path(percentEncoded: false)) ?? [])
            .filter { $0.hasSuffix(".ttf") || $0.hasSuffix(".ttc") }.sorted()
        if fonts.isEmpty { missing.append("fonts") }

        guard missing.isEmpty, let builtAt, builtAt > 0 else {
            throw StepFailure(
                step: step,
                detail: String(localized: "These components are missing: \(missing.joined(separator: ", ")). Run make app-payload and build the app again.")
            )
        }

        let gamefixes = try locateGamefixes(in: root)

        return Located(
            dylib: dylib, overlayShim: shim, iconmaker: iconmaker,
            appinfo: appinfo, run: run, builtAt: builtAt, signatures: signatures,
            fontsRoot: fontsRoot, fonts: fonts,
            gamefixes: gamefixes
        )
    }
}
