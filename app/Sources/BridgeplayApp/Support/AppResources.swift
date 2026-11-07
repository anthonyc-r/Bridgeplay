import Foundation

enum AppResources {
    static let bundle: Bundle = {
        if let resources = Bundle.main.resourceURL,
           let packaged = Bundle(url: resources.appending(path: "BridgeplayApp_BridgeplayApp.bundle")) {
            return packaged
        }
        return .module
    }()
}
