import Foundation

enum CheckComponentsCommand {
    static let flag = "--check-components"

    static func run() -> Never {
        do {
            let payload = try InstallPayload.locate()
            let bridge = try BridgePayload.locate()
            #if arch(x86_64)
            guard InstallPayload.hasIntelComponents(root: try InstallPayload.root()) else {
                throw StepFailure(step: InstallPayload.step, detail: "The native Intel Steam components are incomplete.")
            }
            #endif
            let resources = AppResources.bundle
            let languages = ["it", "ru", "zh-Hans"]
            guard languages.allSatisfy({ resources.url(forResource: "Localizable", withExtension: "strings", subdirectory: nil, localization: $0) != nil }) else {
                throw StepFailure(step: InstallPayload.step, detail: "Compiled localizations are missing.")
            }
            let result: [String: Any] = [
                "resources": resources.bundleURL.path,
                "platform": SupportPaths.signaturePlatform,
                "signatures": payload.signatures.count,
                "fonts": payload.fonts.count,
                "bridgeResources": bridge.sources.count,
                "localizations": ["en"] + languages,
            ]
            let data = try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys])
            FileHandle.standardOutput.write(data + Data("\n".utf8))
            exit(0)
        } catch {
            FileHandle.standardError.write(Data("\(error.localizedDescription)\n".utf8))
            exit(1)
        }
    }
}
